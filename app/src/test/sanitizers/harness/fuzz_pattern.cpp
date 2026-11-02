// libFuzzer target: real CipherUtils::patternToBytes, called the way CipherScanPattern calls it.
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "Cipher/CipherUtils.h"
#include "patternToBytes.inc"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char bytes[256] = "";  // CipherUtils.cpp CipherScanPattern* / CipherRegistry.cpp ExplainScanFailure
    std::string mask;
    CipherUtils::patternToBytes(std::string(reinterpret_cast<const char *>(data), size), bytes, mask);
    return 0;
}
