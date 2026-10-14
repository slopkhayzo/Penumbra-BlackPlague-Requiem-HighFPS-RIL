#include "bootstrap.h"
#include "logger.h"

#include <Windows.h>

BOOL WINAPI DllMain(const HINSTANCE instance, const DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(nullptr, 0, pbpfps::BootstrapThread,
                                           instance, 0, nullptr);
        if (thread == nullptr) {
            return FALSE;
        }
        CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        // The loader keeps the ASI resident for the process lifetime. Avoid
        // thread suspension and hook teardown while the Windows loader lock is
        // held; process termination releases all hook state safely.
        pbpfps::logger::Shutdown();
    }
    return TRUE;
}
