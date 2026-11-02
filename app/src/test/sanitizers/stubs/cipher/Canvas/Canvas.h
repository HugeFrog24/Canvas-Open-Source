#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace Canvas {
    extern const char *libName;
    extern std::uintptr_t libBase;
    bool isLibLoaded(const char *);
    std::uintptr_t findLib(const char *);
}
