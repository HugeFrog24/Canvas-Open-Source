// Host-side stand-ins for the Canvas globals the real CipherRegistry.cpp,
// CipherBase.cpp and CipherPatch.cpp name, compiled against the real
// Core/Canvas/Canvas.h (KittyMemory comes from stubs/app). Unlike
// cipher_env.cpp, the install registry here is Canvas's own code.
#include <cstdint>
#include <string>

#include "Cipher/Cipher.h"
#include "Cipher/CipherUtils.h"
#include <Canvas/Canvas.h>

namespace Canvas {
    const char *libName = "libBootloader.so";
    std::uintptr_t libBase = 0;
    KittyScanner::ElfScanner libElfScanner;  // invalid: ExplainScanFailure returns {}
    bool isLibLoaded(const char *) { return true; }
    std::uintptr_t findLib(const char *) { return 0; }
}

std::uintptr_t CipherUtils::CipherScan(const char *, const char *, const Flags &, const std::uintptr_t &, const char *) { return 0; }

#include "patternToBytes.inc"  // verbatim run.sh extraction (ExplainScanFailure's string overload calls it)
