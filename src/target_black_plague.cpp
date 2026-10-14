#include "target_adapter.h"
#include "target_game_layout.h"

namespace pbpfps::target {
namespace {

#include "target_black_plague_signatures.inc"

// Each value is carried by a generated signature above (a function or
// witness window whose literal bytes contain the displacement), so this
// layout cannot be selected for a binary compiled against other offsets.
constexpr Layout kLayout = Episode2Layout({
    .death = 0x28C,
    .state = 0x2BC,
    .crossHairState = 0x2E4,
    .crossHairPosition = 0x2E8,
});

constexpr Adapter kAdapter = {
    AdapterId::blackPlague,
    L"Penumbra: Black Plague",
    L"BlackPlague",
    L"Penumbra.exe",
    0x47C3DC06,
    0x002F6000,
    kGameSignatures,
    &kLayout,
};

} // namespace

const Adapter& BlackPlagueAdapter() noexcept { return kAdapter; }

} // namespace pbpfps::target
