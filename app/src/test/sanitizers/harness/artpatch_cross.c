// A patch whose bytes cross a page boundary: real artpatch.c and procutils.c on
// the kernel's /proc/self/maps. artpatch_apply makes only the page holding the
// first byte writable (mprotect(pagestart, PAGE_SIZE, ...), artpatch.c:92) and
// then copies all patchLength bytes. Two adjacent code pages (one r-x mapping)
// and an 8-byte patch (two arm64 instructions, e.g. "mov x0, #0; ret") whose
// first instruction is the last one on the first page, as when a mod patches a
// function that starts 4 bytes before a page boundary.
//
// Oracle: after apply both instructions are patched and both pages are still
// r-x; after restore the original bytes are back. A fault in the copy is the
// failure.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "artpatch/artpatch.h"

static void perms_of(uintptr_t addr, char out[5]) {
    strcpy(out, "????");
    FILE *f = fopen("/proc/self/maps", "r");
    char line[512];
    while (f && fgets(line, sizeof(line), f)) {
        unsigned long lo = 0, hi = 0;
        char p[5] = "";
        if (sscanf(line, "%lx-%lx %4s", &lo, &hi, p) == 3 && addr >= lo && addr < hi) {
            memcpy(out, p, 5);
            break;
        }
    }
    if (f)
        fclose(f);
}

int main(void) {
    const long page = sysconf(_SC_PAGESIZE);
    unsigned char *code = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(code, 0xAB, 2 * page);
    mprotect(code, 2 * page, PROT_READ | PROT_EXEC);
    unsigned char *site = code + page - 4;  // last instruction of the first page

    patch_t patch = artpatch_new();
    artpatch_set_hex(patch, "000080D2C0035FD6");  // mov x0, #0 ; ret
    artpatch_set_addr(patch, site);
    printf("patching %p..%p (page boundary at %p)\n", (void *)site, (void *)(site + 8), (void *)(code + page));
    fflush(stdout);
    artpatch_apply(patch);

    const unsigned char want[8] = {0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6};
    char p0[5], p1[5];
    perms_of((uintptr_t)code, p0);
    perms_of((uintptr_t)(code + page), p1);
    const int bytes_ok = !memcmp(site, want, 8);
    printf("survived apply: bytes %s, pages %s / %s\n", bytes_ok ? "patched" : "NOT PATCHED", p0, p1);
    artpatch_restore(patch);
    const int restored = site[0] == 0xAB && site[7] == 0xAB;
    printf("restore: %s\n", restored ? "ok" : "NOT RESTORED");
    artpatch_die(patch);
    return bytes_ok && restored ? 0 : 1;
}
