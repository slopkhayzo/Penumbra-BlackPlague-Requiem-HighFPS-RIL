#include "target_host.h"

#include <Windows.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using pbpfps::target::Adapter;
using pbpfps::target::ImageView;
using pbpfps::target::Selection;
using pbpfps::target::Signature;

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

// Builds a synthetic .text holding the given signatures, each separated by
// filler that cannot extend a match. Masked bytes are written as zero.
class SyntheticText {
  public:
    void Add(const Signature& signature) {
        bytes_.insert(bytes_.end(), 16, std::uint8_t{0xCC});
        for (std::size_t index = 0; index < signature.size; ++index) {
            bytes_.push_back(signature.mask[index] == 'x'
                                 ? signature.bytes[index]
                                 : std::uint8_t{0});
        }
    }

    void AddAll(std::span<const Signature> signatures,
                const wchar_t* const except = nullptr) {
        for (const Signature& signature : signatures) {
            if (except != nullptr && std::wstring(signature.name) == except) {
                continue;
            }
            Add(signature);
        }
    }

    [[nodiscard]] ImageView View() {
        bytes_.insert(bytes_.end(), 16, std::uint8_t{0xCC});
        ImageView view{};
        view.base = bytes_.data();
        view.imageSize = static_cast<std::uint32_t>(bytes_.size());
        view.text = bytes_.data();
        view.textSize = bytes_.size();
        return view;
    }

  private:
    std::vector<std::uint8_t> bytes_;
};

int TestSelectsEachAdapter() {
    for (const Adapter* const adapter : pbpfps::target::Adapters()) {
        SyntheticText text;
        text.AddAll(pbpfps::target::EngineSignatures());
        text.AddAll(adapter->gameSignatures);
        Selection selection{};
        if (!pbpfps::target::SelectAdapterForImage(text.View(), selection)) {
            std::wcerr << selection.error << L'\n';
            return Fail("a complete adapter image was rejected");
        }
        if (selection.adapter != adapter) {
            return Fail("the wrong adapter was selected");
        }
        if (selection.mapping.gameRun == nullptr ||
            selection.mapping.buttonHandlerUpdate == nullptr ||
            selection.mapping.playerAddYaw == nullptr ||
            selection.mapping.playerHandsUpdate == nullptr ||
            selection.mapping.mouseSdlUpdate == nullptr) {
            return Fail("a selected adapter left a mapped function null");
        }
    }
    return 0;
}

int TestPartialMatchFailsClosed() {
    const Adapter& blackPlague = pbpfps::target::BlackPlagueAdapter();

    // Engine layer alone matches both games and must select neither.
    {
        SyntheticText text;
        text.AddAll(pbpfps::target::EngineSignatures());
        Selection selection{};
        if (pbpfps::target::SelectAdapterForImage(text.View(), selection)) {
            return Fail("an engine-only image selected an adapter");
        }
        if (selection.adapter != nullptr ||
            selection.mapping.gameRun != nullptr || selection.error.empty()) {
            return Fail("an engine-only rejection leaked state or gave no reason");
        }
    }

    // One missing layout witness rejects the adapter, and the other adapter
    // must not pick the image up.
    {
        SyntheticText text;
        text.AddAll(pbpfps::target::EngineSignatures());
        text.AddAll(blackPlague.gameSignatures,
                    L"witness: player active and state");
        Selection selection{};
        if (pbpfps::target::SelectAdapterForImage(text.View(), selection)) {
            return Fail("an image missing a layout witness was accepted");
        }
        if (selection.error.find(L"witness: player active and state") ==
            std::wstring::npos) {
            return Fail("the rejection did not name the missing witness");
        }
    }

    // A duplicated unique signature is ambiguous and must be rejected.
    {
        SyntheticText text;
        text.AddAll(pbpfps::target::EngineSignatures());
        text.AddAll(blackPlague.gameSignatures);
        text.Add(pbpfps::target::EngineSignatures().front());
        Selection selection{};
        if (pbpfps::target::SelectAdapterForImage(text.View(), selection)) {
            return Fail("an image with a duplicated signature was accepted");
        }
    }
    return 0;
}

int TestAdaptersAreDistinct() {
    const Adapter& blackPlague = pbpfps::target::BlackPlagueAdapter();
    const Adapter& requiem = pbpfps::target::RequiemAdapter();
    if (blackPlague.layout == requiem.layout ||
        blackPlague.layout->playerState == requiem.layout->playerState ||
        blackPlague.layout->playerCrossHairPosition ==
            requiem.layout->playerCrossHairPosition) {
        return Fail("the two adapters do not carry distinct player layouts");
    }
    if (blackPlague.layout->handlerPlayer != requiem.layout->handlerPlayer ||
        blackPlague.layout->initInventory != requiem.layout->initInventory) {
        return Fail("the shared episode-2 fields diverged between adapters");
    }
    return 0;
}

int TestUnrelatedHostFailsClosed() {
    // This test executable is a valid PE32 image but not a supported game.
    Selection selection{};
    if (pbpfps::target::SelectAdapter(GetModuleHandleW(nullptr), selection)) {
        return Fail("the test executable was accepted as a game host");
    }
    if (selection.adapter != nullptr || selection.error.empty()) {
        return Fail("unrelated-host rejection leaked state or gave no reason");
    }
    return 0;
}

// Optional: verify the installed retail executables when their directory is
// passed on the command line. Each file is mapped as an image, not executed.
int TestInstalledExecutable(const std::wstring& path, const Adapter& expected) {
    const HMODULE module = LoadLibraryExW(
        path.c_str(), nullptr,
        LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_AS_DATAFILE);
    if (module == nullptr) {
        std::wcerr << path << L'\n';
        return Fail("could not map an installed executable");
    }
    ImageView image{};
    std::wstring error;
    Selection selection{};
    const bool selected = pbpfps::target::OpenImage(module, image, error) &&
                          pbpfps::target::SelectAdapterForImage(image, selection);
    const Adapter* const adapter = selection.adapter;
    const void* const gameRun = selection.mapping.gameRun;
    const std::uint32_t timestamp = image.timestamp;
    const std::uint32_t imageSize = image.imageSize;
    const std::wstring reason = error.empty() ? selection.error : error;
    FreeLibrary(module);
    if (!selected) {
        std::wcerr << path << L": " << reason << L'\n';
        return Fail("an installed executable was rejected");
    }
    if (adapter != &expected) {
        return Fail("an installed executable selected the wrong adapter");
    }
    if (gameRun == nullptr) {
        return Fail("an installed executable left cGame::Run unmapped");
    }
    std::wcout << L"  " << path << L" -> " << expected.name << L" (timestamp 0x"
               << std::hex << timestamp << L", imageSize 0x" << imageSize
               << std::dec << L", reference build="
               << (timestamp == expected.referenceTimestamp &&
                   imageSize == expected.referenceImageSize)
               << L")\n";
    return 0;
}

} // namespace

int wmain(const int argc, wchar_t** const argv) {
    if (TestSelectsEachAdapter() != 0 || TestPartialMatchFailsClosed() != 0 ||
        TestAdaptersAreDistinct() != 0 || TestUnrelatedHostFailsClosed() != 0) {
        return 1;
    }
    if (argc > 1) {
        const std::wstring directory = argv[1];
        if (TestInstalledExecutable(directory + L"\\Penumbra.exe",
                                    pbpfps::target::BlackPlagueAdapter()) != 0 ||
            TestInstalledExecutable(directory + L"\\Requiem.exe",
                                    pbpfps::target::RequiemAdapter()) != 0) {
            return 1;
        }
    }
    std::cout << "target_host_tests: PASS\n";
    return 0;
}
