// Host-side stand-ins for Canvas globals and the install registry.
// The registry is shared_mutex-guarded and not what these repros exercise.
#include "Cipher/Cipher.h"
#include "Cipher/CipherRegistry.h"
#include "Cipher/CipherUtils.h"
#include <Canvas/Canvas.h>

namespace Canvas {
    const char *libName = "libBootloader.so";
    std::uintptr_t libBase = 0;
    bool isLibLoaded(const char *) { return true; }
    std::uintptr_t findLib(const char *) { return 0; }
}

std::uintptr_t CipherUtils::CipherScan(const char *, const char *, const Flags &, const std::uintptr_t &, const char *) { return 0; }

namespace CipherInternal {
    void RecordInstall(CipherBase *, void *, Types, std::uintptr_t, std::size_t, const char *) {}
    void ForgetInstall(CipherBase *) {}
    void NotePatchBytes(CipherBase *, std::size_t) {}
    std::size_t LookupPatchBytes(CipherBase *) { return 4; }
    std::string ResolveOwnerForLog(void *) { return "harness"; }
}

std::vector<CipherInstallInfo> Cipher::DescribePatchesAt(std::uintptr_t) { return {}; }
