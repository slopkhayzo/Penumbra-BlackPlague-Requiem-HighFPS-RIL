#include "particle_runtime.h"

#include "guarded_memory.h"
#include "target_layout.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pbpfps::particle {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }


const std::uint8_t* Offset(const void* const base,
                           const std::ptrdiff_t offset) noexcept {
    return static_cast<const std::uint8_t*>(base) + offset;
}

std::uint8_t* Offset(void* const base,
                     const std::ptrdiff_t offset) noexcept {
    return static_cast<std::uint8_t*>(base) + offset;
}

template <typename T>
bool ReadAt(const void* const base, const std::ptrdiff_t offset,
            T& value) noexcept {
    return memory::TryReadAt(base, offset, value);
}

template <typename Vector>
bool IsFinite(const Vector& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

} // namespace

void Runtime::Configure(const RuntimeFunctions& functions) noexcept {
    Reset();
    functions_ = functions;
}

void Runtime::Reset() noexcept {
    calls_ = {};
    restore_ = {};
    callCount_ = 0;
    collecting_ = false;
    presenting_ = false;
}

void Runtime::BeginCollection() noexcept {
    calls_ = {};
    callCount_ = 0;
    collecting_ = true;
}

void Runtime::EndCollection() noexcept { collecting_ = false; }

ObserveResult Runtime::Observe(void* const emitter, void* const camera,
                               const float frameTime,
                               void* const renderList) noexcept {
    if (!collecting_ || emitter == nullptr || camera == nullptr ||
        !std::isfinite(frameTime)) {
        return ObserveResult::invalid;
    }
    if (!memory::HasVtableEntry(emitter, Fields().updateGraphicsVtableIndex,
                                functions_.updateGraphicsTarget)) {
        return ObserveResult::invalid;
    }
    std::uint32_t drawType = 0;
    std::uint32_t emitterType = 0;
    if (!ReadAt(emitter, Fields().emitterDrawType, drawType) || drawType > 3 ||
        !ReadAt(emitter, Fields().emitterType, emitterType) || emitterType > 1) {
        return ObserveResult::invalid;
    }
    if (emitterType == Fields().emitterBeamType) {
        return ObserveResult::ignoredBeam;
    }
    const bool line = drawType == Fields().emitterLineDrawType;
    for (std::size_t index = 0; index < callCount_; ++index) {
        if (calls_[index].emitter == emitter) {
            return ObserveResult::duplicate;
        }
    }
    if (callCount_ == calls_.size()) {
        return ObserveResult::capacityExhausted;
    }
    calls_[callCount_++] = {emitter, camera, renderList, frameTime, line};
    return line ? ObserveResult::addedLine : ObserveResult::added;
}

bool Runtime::ValidateEmitter(void* const emitter, const bool line,
                              std::uint32_t& activeCount,
                              void**& particles,
                              std::uint8_t*& dirty) const noexcept {
    if (emitter == nullptr || functions_.updateGraphics == nullptr ||
        functions_.updateGraphicsTarget == nullptr) {
        return false;
    }
    void** end = nullptr;
    void** capacity = nullptr;
    std::uint32_t drawType = 0;
    std::uint32_t emitterType = 0;
    std::uint8_t dirtyValue = 0;
    if (!memory::HasVtableEntry(emitter, Fields().updateGraphicsVtableIndex,
                                functions_.updateGraphicsTarget) ||
        !ReadAt(emitter, Fields().emitterParticlesBegin, particles) ||
        !ReadAt(emitter, Fields().emitterParticlesEnd, end) ||
        !ReadAt(emitter, Fields().emitterParticlesCapacity, capacity) ||
        !ReadAt(emitter, Fields().emitterActiveCount, activeCount) ||
        !ReadAt(emitter, Fields().emitterDrawType, drawType) || drawType > 3 ||
        (drawType == Fields().emitterLineDrawType) != line ||
        !ReadAt(emitter, Fields().emitterType, emitterType) ||
        emitterType != 0 ||
        !ReadAt(emitter, Fields().emitterGraphicsDirty, dirtyValue) || dirtyValue > 1) {
        return false;
    }
    dirty = Offset(emitter, Fields().emitterGraphicsDirty);
    if (particles == nullptr) {
        return false;
    }
    const auto beginAddress = reinterpret_cast<std::uintptr_t>(particles);
    const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
    const auto capacityAddress = reinterpret_cast<std::uintptr_t>(capacity);
    if (endAddress < beginAddress || capacityAddress < endAddress ||
        (endAddress - beginAddress) % sizeof(void*) != 0 ||
        (capacityAddress - beginAddress) % sizeof(void*) != 0) {
        return false;
    }
    const auto vectorCount = static_cast<std::size_t>(
        (endAddress - beginAddress) / sizeof(void*));
    const auto vectorCapacity = static_cast<std::size_t>(
        (capacityAddress - beginAddress) / sizeof(void*));
    return activeCount <= vectorCount && vectorCount <= vectorCapacity &&
           activeCount <= kMaximumParticlesPerEmitter;
}

// One guarded pass over the emitter's particles: no page queries and no
// per-particle calls. Nothing is written to the game here.
bool Runtime::CaptureParticles(void** const particles,
                               const std::uint32_t count,
                               const float alpha) noexcept {
    __try {
        for (std::uint32_t index = 0; index < count; ++index) {
            void* const particle = particles[index];
            if (particle == nullptr) {
                return false;
            }
            RestoreEntry& entry = restore_[index];
            Vector3& previous = entry.lastPosition;
            std::memcpy(&entry.position, Offset(particle, Fields().particlePosition),
                        sizeof(Vector3));
            std::memcpy(&previous, Offset(particle, Fields().particleLastPosition),
                        sizeof(Vector3));
            entry.particle = particle;
            entry.interpolated = {
                previous.x + (entry.position.x - previous.x) * alpha,
                previous.y + (entry.position.y - previous.y) * alpha,
                previous.z + (entry.position.z - previous.z) * alpha,
            };
            // The same one-tick displacement, carried back from the
            // interpolated head, keeps a line streak at its stock length.
            entry.interpolatedLast = {
                entry.interpolated.x - (entry.position.x - previous.x),
                entry.interpolated.y - (entry.position.y - previous.y),
                entry.interpolated.z - (entry.position.z - previous.z),
            };
            if (!IsFinite(entry.position) || !IsFinite(previous) ||
                !IsFinite(entry.interpolated) ||
                !IsFinite(entry.interpolatedLast)) {
                return false;
            }
        }
        return true;
    } __except (memory::FaultFilter(GetExceptionCode())) {
        return false;
    }
}

bool Runtime::WriteParticles(const std::uint32_t count,
                             const bool interpolated,
                             const bool line) noexcept {
    bool written = true;
    for (std::uint32_t index = 0; index < count; ++index) {
        const RestoreEntry& entry = restore_[index];
        // Guarded per particle so one bad entry cannot strand the rest in
        // their temporary positions.
        written = memory::TryWrite(
                      Offset(entry.particle, Fields().particlePosition),
                      interpolated ? entry.interpolated : entry.position) &&
                  written;
        if (line) {
            written = memory::TryWrite(
                          Offset(entry.particle,
                                 Fields().particleLastPosition),
                          interpolated ? entry.interpolatedLast
                                       : entry.lastPosition) &&
                      written;
        }
    }
    return written;
}

bool Runtime::PresentOne(const Call& call, const double alpha,
                         BatchResult& result) noexcept {
    std::uint32_t activeCount = 0;
    void** particles = nullptr;
    std::uint8_t* dirty = nullptr;
    if (!ValidateEmitter(call.emitter, call.line, activeCount, particles,
                         dirty)) {
        return false;
    }
    if (activeCount == 0) {
        ++result.stationarySkipped;
        return true;
    }

    // Regenerated even when no particle moved: the engine builds the quads
    // in the view space of the camera it was handed, so a turning camera
    // needs fresh vertices for stationary particles too.
    if (!CaptureParticles(particles, activeCount, static_cast<float>(alpha))) {
        return false;
    }

    std::uint8_t savedDirty = 0;
    if (!memory::TryRead(dirty, savedDirty)) {
        return false;
    }
    if (!WriteParticles(activeCount, true, call.line) ||
        !memory::TryWrite(dirty, std::uint8_t{1})) {
        if (!WriteParticles(activeCount, false, call.line) ||
            !memory::TryWrite(dirty, savedDirty)) {
            ++result.restoreFailed;
        }
        return false;
    }

    using UpdateGraphicsFn =
        void(__thiscall*)(void*, void*, float, void*);
    const auto update =
        reinterpret_cast<UpdateGraphicsFn>(functions_.updateGraphics);
    update(call.emitter, call.camera, call.frameTime, call.renderList);
    ++result.regenerated;

    const bool particlesRestored =
        WriteParticles(activeCount, false, call.line);
    if (!memory::TryWrite(dirty, savedDirty) || !particlesRestored) {
        ++result.restoreFailed;
        return false;
    }
    result.particles += activeCount;
    ++result.applied;
    return true;
}

BatchResult Runtime::Present(const clock::Sample& clockSample) noexcept {
    BatchResult result{};
    if (presenting_) {
        ++result.invalid;
        return result;
    }
    presenting_ = true;
    for (std::size_t index = 0; index < callCount_; ++index) {
        ++result.targets;
        if (clockSample.kind != clock::SampleKind::interpolated ||
            !std::isfinite(clockSample.alpha) || clockSample.alpha < 0.0 ||
            clockSample.alpha > 1.0) {
            ++result.clockWait;
            continue;
        }
        if (!PresentOne(calls_[index], clockSample.alpha, result)) {
            ++result.invalid;
        }
    }
    presenting_ = false;
    return result;
}

} // namespace pbpfps::particle
