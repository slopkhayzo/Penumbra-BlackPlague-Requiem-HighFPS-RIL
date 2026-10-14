#include "target_adapter.h"
#include "target_game_layout.h"

namespace pbpfps::target {
namespace {

#include "target_requiem_signatures.inc"

// Each value is carried by a generated signature above (a function or
// witness window whose literal bytes contain the displacement), so this
// layout cannot be selected for a binary compiled against other offsets.
// cPlayer is four bytes larger ahead of these members than in Black Plague.
constexpr Layout kLayout = Episode2Layout({
    .death = 0x290,
    .state = 0x2C0,
    .crossHairState = 0x2E8,
    .crossHairPosition = 0x2EC,
});

constexpr Adapter kAdapter = {
    AdapterId::requiem,
    L"Penumbra: Requiem",
    L"Requiem",
    L"Requiem.exe",
    0x48B277A7,
    0x002F9000,
    kGameSignatures,
    &kLayout,
};

} // namespace

const Adapter& RequiemAdapter() noexcept { return kAdapter; }

} // namespace pbpfps::target
