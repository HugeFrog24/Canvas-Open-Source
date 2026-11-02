// Feeds synthetic /proc/self/maps text to the real procutils_read_mprotection
// (link with -Wl,--wrap=fopen). artpatch_apply/restore put whatever this
// returns back on the patched page after writing it.
//   adjacent:    query the first page of a mapping that directly follows another
//   fopen-fails: /proc/self/maps cannot be opened (e.g. fd exhaustion)
// With -DFUZZ it is a libFuzzer target: 8 bytes of address, then maps text.
#include <sys/mman.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" FILE *__real_fopen(const char *, const char *);
extern "C" int procutils_read_mprotection(void *addr);

static const char *g_maps = "";
static size_t g_len = 0;
static bool g_fail = false;

extern "C" FILE *__wrap_fopen(const char *path, const char *mode) {
    if (std::strcmp(path, "/proc/self/maps") != 0)
        return __real_fopen(path, mode);
    if (g_fail)
        return nullptr;
    return fmemopen(const_cast<char *>(g_maps), g_len, "r");
}

static const char *prot(int p) {
    static char s[4];
    s[0] = (p & PROT_READ) ? 'r' : '-';
    s[1] = (p & PROT_WRITE) ? 'w' : '-';
    s[2] = (p & PROT_EXEC) ? 'x' : '-';
    s[3] = 0;
    return s;
}

#ifdef FUZZ
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size <= 8)
        return 0;
    std::uintptr_t addr = 0;
    std::memcpy(&addr, data, 8);
    g_maps = reinterpret_cast<const char *>(data + 8);
    g_len = size - 8;
    procutils_read_mprotection(reinterpret_cast<void *>(addr));
    return 0;
}
#else
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "adjacent";
    if (!std::strcmp(mode, "adjacent")) {
        // A library's read-only segment followed directly by its code segment.
        static const char maps[] =
            "70000000-70010000 r--p 00000000 fd:00 1234 /data/app/lib/arm64/libBootloader.so\n"
            "70010000-70400000 r-xp 00010000 fd:00 1234 /data/app/lib/arm64/libBootloader.so\n";
        g_maps = maps;
        g_len = sizeof(maps) - 1;
        const int got = procutils_read_mprotection(reinterpret_cast<void *>(0x70010000));
        std::printf("first code page 0x70010000: expected r-x, got %s -> %s\n", prot(got),
                    got == (PROT_READ | PROT_EXEC) ? "ok" : "WRONG");
        return got == (PROT_READ | PROT_EXEC) ? 0 : 1;
    }
    if (!std::strcmp(mode, "fopen-fails")) {
        g_fail = true;
        procutils_read_mprotection(reinterpret_cast<void *>(0x70010000));
        std::puts("survived");
    }
    return 0;
}
#endif
