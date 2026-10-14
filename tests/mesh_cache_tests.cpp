#include "mesh_cache.h"

#include "rigid_transform.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace {

struct FakeMesh {
    void** vtable = nullptr;
    std::array<std::byte, 0x4CC> padding{};
    pbpfps::transform::Matrix model{};
    pbpfps::transform::Matrix inverse{};
    int modelCalls = 0;
    int inverseCalls = 0;
};

static_assert(offsetof(FakeMesh, model) == 0x4D0);

void Require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

pbpfps::transform::Matrix* __fastcall GetModelMatrix(void* self, void*, void*) {
    auto* const mesh = static_cast<FakeMesh*>(self);
    ++mesh->modelCalls;
    return &mesh->model;
}

pbpfps::transform::Matrix* __fastcall GetInvModelMatrix(void* self, void*) {
    auto* const mesh = static_cast<FakeMesh*>(self);
    ++mesh->inverseCalls;
    return &mesh->inverse;
}

void TestCollectionAndRefresh() {
    std::array<void*, 19> vtable{};
    vtable[18] = reinterpret_cast<void*>(&GetModelMatrix);
    FakeMesh mesh{};
    mesh.vtable = vtable.data();

    pbpfps::mesh::Cache cache;
    cache.Configure(reinterpret_cast<void*>(&GetModelMatrix),
                    reinterpret_cast<void*>(&GetModelMatrix),
                    reinterpret_cast<void*>(&GetInvModelMatrix));
    Require(cache.Observe(&mesh) == pbpfps::mesh::ObserveResult::inactive,
            "entity was accepted outside collection");
    cache.BeginCollection();
    Require(cache.Observe(&mesh) == pbpfps::mesh::ObserveResult::added,
            "mesh entity was not collected");
    Require(cache.Observe(&mesh) == pbpfps::mesh::ObserveResult::duplicate,
            "duplicate mesh entity was not detected");
    cache.EndCollection();
    const auto result = cache.Refresh();
    Require(result.refreshed == 1 && result.invalid == 0,
            "valid mesh cache was not refreshed");
    Require(mesh.modelCalls == 1 && mesh.inverseCalls == 1,
            "model and inverse caches were not both refreshed");
}

void TestVtableMutationFailsClosed() {
    std::array<void*, 19> vtable{};
    vtable[18] = reinterpret_cast<void*>(&GetModelMatrix);
    FakeMesh mesh{};
    mesh.vtable = vtable.data();
    pbpfps::mesh::Cache cache;
    cache.Configure(reinterpret_cast<void*>(&GetModelMatrix),
                    reinterpret_cast<void*>(&GetModelMatrix),
                    reinterpret_cast<void*>(&GetInvModelMatrix));
    cache.BeginCollection();
    (void)cache.Observe(&mesh);
    cache.EndCollection();
    vtable[18] = nullptr;
    const auto result = cache.Refresh();
    Require(result.invalid == 1 && result.refreshed == 0,
            "changed mesh identity did not fail closed");
    Require(mesh.modelCalls == 0 && mesh.inverseCalls == 0,
            "invalid mesh entity was called");
}

} // namespace

int main() {
    TestCollectionAndRefresh();
    TestVtableMutationFailsClosed();
    std::cout << "mesh cache tests passed\n";
    return EXIT_SUCCESS;
}
