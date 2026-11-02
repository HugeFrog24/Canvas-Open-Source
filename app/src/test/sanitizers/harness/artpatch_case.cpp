// ChatFlagFixInstall.cpp:114-120 hex formatting, fed to the real artpatch.c.
#include "ChatFlagFix/ChatFlagFix.h"
#include "artpatch/artpatch.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv) {
    const std::uint32_t reg = argc > 1 ? std::atoi(argv[1]) : 10;
    const std::uint32_t replacement = ChatFlagFix::Detail::kAndKeepBits0To1 | (reg << 5) | reg;  // ChatFlagFix.h:161
    char hex[9] = {};
    const auto *bytes = reinterpret_cast<const unsigned char *>(&replacement);
    std::snprintf(hex, sizeof(hex), "%02x%02x%02x%02x", bytes[0], bytes[1], bytes[2], bytes[3]);  // verbatim

    auto *code = static_cast<std::uint32_t *>(
        mmap(nullptr, getpagesize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    code[0] = 0x121b7108u;  // the original AND at the site
    patch_t patch = artpatch_new();
    artpatch_set_hex(patch, hex);
    artpatch_set_addr(patch, code);
    artpatch_apply(patch);
    std::printf("w%-2u hex=\"%s\" expected=%08x written=%08x -> %s\n", reg, hex, replacement, code[0],
                code[0] == replacement ? "ok" : "GARBAGE WRITTEN TO CODE");
    return code[0] == replacement ? 0 : 1;
}
