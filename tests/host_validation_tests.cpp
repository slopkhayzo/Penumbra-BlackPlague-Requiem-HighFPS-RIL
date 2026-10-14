#include "host_validation.h"

#include <cstdlib>
#include <iostream>

int main() {
    const pbpfps::host::Report report = pbpfps::host::ValidateMainImage();
    if (report.compatibleForDiagnostics) {
        std::cerr << "FAIL: an ordinary test executable passed the Penumbra host preflight\n";
        return 1;
    }
    if (report.error.empty()) {
        std::cerr << "FAIL: incompatible host rejection had no reason\n";
        return 1;
    }
    std::cout << "host_validation_tests: PASS (";
    std::wcout << report.error << L")\n";
    return 0;
}
