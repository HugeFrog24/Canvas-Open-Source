// libFuzzer differential target: the real artpatch.c and procutils.c driven by
// a sequence of set_hex / set_addr / apply / restore operations on code pages,
// checked after every operation against a byte model of the pages and their
// protection. The sequence follows CipherPatch's use: set_Opcode -> set_hex,
// the first Fire() -> set_addr + apply, later Fire() -> apply, Restore() and
// the destructor -> restore / die.
//
// Known classes are kept out so new behaviour surfaces: only upper-case hex
// (F2: lower case), no set_hex while applied (F11), no empty hex (L5), and the
// first page of the mapping is never patched (F12). With ARTPATCH_DIFF_NO_CROSS
// set, patches also stay inside one page (the page-crossing case,
// artpatch_cross.c).
#include <fuzzer/FuzzedDataProvider.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "artpatch/artpatch.h"
}

namespace {

constexpr int kPages = 5;      // page 0: first page of the mapping, never patched; 1-3: targets; 4: tail
constexpr int kMaxBytes = 12;  // up to three instructions
size_t g_page = 0;
unsigned char *g_region = nullptr;
std::vector<unsigned char> g_model;
bool g_noCross = false;
std::string g_trace;

unsigned char pattern(size_t i) { return static_cast<unsigned char>(i * 7 + 3); }

void reset() {
    mprotect(g_region, kPages * g_page, PROT_READ | PROT_WRITE);
    for (size_t i = 0; i < kPages * g_page; ++i) g_region[i] = pattern(i);
    mprotect(g_region, kPages * g_page, PROT_READ | PROT_EXEC);
    g_model.assign(g_region, g_region + kPages * g_page);
}

bool perms_rx(uintptr_t addr) {
    FILE *f = std::fopen("/proc/self/maps", "r");
    char line[512];
    bool ok = false;
    while (f && std::fgets(line, sizeof(line), f)) {
        unsigned long lo = 0, hi = 0;
        char p[5] = "";
        if (std::sscanf(line, "%lx-%lx %4s", &lo, &hi, p) == 3 && addr >= lo && addr < hi) {
            ok = !std::strncmp(p, "r-x", 3);
            break;
        }
    }
    if (f) std::fclose(f);
    return ok;
}

[[noreturn]] void mismatch(const char *what, size_t at) {
    std::fprintf(stderr, "MISMATCH %s at region+0x%zx (page %zu, offset 0x%zx)\n  ops: %s\n", what, at, at / g_page,
                 at % g_page, g_trace.c_str());
    __builtin_trap();
}

void check(const char *after) {
    for (size_t i = 0; i < g_model.size(); ++i)
        if (g_region[i] != g_model[i]) mismatch(after, i);
    for (int p = 0; p < kPages; ++p)
        if (!perms_rx(reinterpret_cast<uintptr_t>(g_region) + p * g_page)) mismatch("page protection", p * g_page);
}

}  // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    g_page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    g_region = static_cast<unsigned char *>(
        mmap(nullptr, kPages * g_page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    g_noCross = std::getenv("ARTPATCH_DIFF_NO_CROSS") != nullptr;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider fdp(data, size);
    reset();
    g_trace.clear();
    patch_t patch = artpatch_new();
    // Model of the patch object.
    std::vector<unsigned char> bytes, backup;
    size_t dest = 0;
    bool haveHex = false, haveAddr = false, applied = false;

    for (int step = 0; step < 16 && fdp.remaining_bytes() > 0; ++step) {
        switch (fdp.ConsumeIntegralInRange<int>(0, 3)) {
        case 0: {  // set_hex (CipherPatch::set_Opcode), only while not applied
            if (applied) break;
            const int n = fdp.ConsumeIntegralInRange<int>(1, kMaxBytes);
            if (haveAddr && g_noCross && dest % g_page + n > g_page) break;
            if (haveAddr && dest + n > kPages * g_page) break;
            std::string hex;
            bytes.clear();
            for (int i = 0; i < n; ++i) {
                const unsigned char b = fdp.ConsumeIntegral<unsigned char>();
                static const char digits[] = "0123456789ABCDEF";
                hex += digits[b >> 4];
                hex += digits[b & 15];
                if (fdp.ConsumeBool()) hex += ' ';  // separators are stripped
                bytes.push_back(b);
            }
            artpatch_set_hex(patch, hex.c_str());
            haveHex = true;
            g_trace += "set_hex(" + std::to_string(n) + " bytes) ";
            break;
        }
        case 1: {  // set_addr (first Fire()), only while not applied
            if (applied) break;
            const size_t pageNo = fdp.ConsumeIntegralInRange<size_t>(1, 3);
            size_t off;
            if (fdp.ConsumeBool()) off = g_page - 4 * fdp.ConsumeIntegralInRange<size_t>(1, 3);  // near the end
            else off = 4 * fdp.ConsumeIntegralInRange<size_t>(0, g_page / 4 - 1);
            const size_t n = haveHex ? bytes.size() : 0;
            if (g_noCross && off + n > g_page) break;
            dest = pageNo * g_page + off;
            artpatch_set_addr(patch, g_region + dest);
            haveAddr = true;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "set_addr(page %zu + 0x%zx) ", pageNo, off);
            g_trace += buf;
            break;
        }
        case 2: {  // apply (Fire())
            if (!haveHex || !haveAddr) break;
            g_trace += "apply ";
            if (!applied) backup.assign(g_model.begin() + dest, g_model.begin() + dest + bytes.size());
            std::memcpy(g_model.data() + dest, bytes.data(), bytes.size());
            applied = true;
            artpatch_apply(patch);
            check("bytes after apply");
            break;
        }
        case 3: {  // restore (Restore())
            g_trace += "restore ";
            if (applied) std::memcpy(g_model.data() + dest, backup.data(), backup.size());
            applied = false;
            artpatch_restore(patch);
            check("bytes after restore");
            break;
        }
        }
    }
    // Destructor path: artpatch_die restores an applied patch.
    if (applied) std::memcpy(g_model.data() + dest, backup.data(), backup.size());
    artpatch_die(patch);
    g_trace += "die";
    check("bytes after die");
    return 0;
}
