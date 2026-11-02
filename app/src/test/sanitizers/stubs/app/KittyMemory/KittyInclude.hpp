#pragma once
// Shape-only stand-in for the KittyMemory types Canvas.h and CipherUtils name.
#include <cstdint>
#include <string>
#include <vector>

namespace KittyMemory {
struct ProcMap {
    std::uintptr_t startAddress = 0;
    std::uintptr_t endAddress = 0;
    bool is_ro = false;
    bool is_rw = false;
    bool is_rx = false;
};
}

namespace KittyScanner {
class ElfScanner {
public:
    static ElfScanner createWithPath(const std::string &) { return {}; }
    bool isValid() const { return false; }
    std::vector<KittyMemory::ProcMap> segments() const { return {}; }
    KittyMemory::ProcMap baseSegment() const { return {}; }
};
}

using KittyMemory::ProcMap;
using KittyScanner::ElfScanner;
