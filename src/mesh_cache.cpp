#include "mesh_cache.h"

#include "rigid_transform.h"

#include "guarded_memory.h"
#include "target_layout.h"

#include <cstddef>
#include <cstdint>

namespace pbpfps::mesh {
namespace {

const target::Layout& Fields() noexcept { return target::ActiveLayout(); }


} // namespace

void Cache::Configure(void* const getModelMatrixTrampoline,
                      void* const getModelMatrixTarget,
                      void* const getInvModelMatrix) noexcept {
    getModelMatrixTrampoline_ = getModelMatrixTrampoline;
    getModelMatrixTarget_ = getModelMatrixTarget;
    getInvModelMatrix_ = getInvModelMatrix;
    Reset();
}

std::size_t Cache::Bucket(const void* const entity) noexcept {
    // Fibonacci hash of the address; heap objects share their low bits.
    const auto address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(entity));
    return static_cast<std::size_t>((address * 2654435761U) >> 20) %
           kBucketCount;
}

void Cache::Reset() noexcept {
    entities_ = {};
    buckets_ = {};
    count_ = 0;
    collecting_ = false;
}

void Cache::BeginCollection() noexcept {
    entities_ = {};
    buckets_ = {};
    count_ = 0;
    collecting_ = true;
}

void Cache::EndCollection() noexcept { collecting_ = false; }

ObserveResult Cache::Observe(void* const entity) noexcept {
    if (!collecting_ || entity == nullptr) {
        return ObserveResult::inactive;
    }
    // The table is four times the entity capacity, so a probe always ends at
    // a free bucket.
    std::size_t bucket = Bucket(entity);
    while (buckets_[bucket] != nullptr) {
        if (buckets_[bucket] == entity) {
            return ObserveResult::duplicate;
        }
        bucket = (bucket + 1U) % kBucketCount;
    }
    if (count_ == entities_.size()) {
        return ObserveResult::capacityExhausted;
    }
    buckets_[bucket] = entity;
    entities_[count_++] = entity;
    return ObserveResult::added;
}

bool Cache::IsExpectedMeshEntity(void* const entity) const noexcept {
    return memory::HasVtableEntry(entity, Fields().meshGetModelMatrixVtableIndex,
                                  getModelMatrixTarget_);
}

RefreshResult Cache::Refresh() noexcept {
    RefreshResult result{};
    if (getModelMatrixTrampoline_ == nullptr ||
        getInvModelMatrix_ == nullptr) {
        result.invalid = static_cast<std::uint32_t>(count_);
        return result;
    }
    using GetModelMatrixFn =
        transform::Matrix*(__thiscall*)(void*, void*);
    using GetInvModelMatrixFn = transform::Matrix*(__thiscall*)(void*);
    const auto getModelMatrix =
        reinterpret_cast<GetModelMatrixFn>(getModelMatrixTrampoline_);
    const auto getInvModelMatrix =
        reinterpret_cast<GetInvModelMatrixFn>(getInvModelMatrix_);

    for (std::size_t index = 0; index < count_; ++index) {
        void* const entity = entities_[index];
        if (!IsExpectedMeshEntity(entity)) {
            entities_[index] = nullptr;
            ++result.invalid;
            continue;
        }
        transform::Matrix* const model = getModelMatrix(entity, nullptr);
        const auto* const expected =
            static_cast<const std::uint8_t*>(entity) +
            Fields().meshModelMatrixCache;
        transform::Matrix* const inverse = getInvModelMatrix(entity);
        if (model != reinterpret_cast<const transform::Matrix*>(expected) ||
            inverse == nullptr) {
            entities_[index] = nullptr;
            ++result.invalid;
            continue;
        }
        ++result.refreshed;
    }
    return result;
}

} // namespace pbpfps::mesh
