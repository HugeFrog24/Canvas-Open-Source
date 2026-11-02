// End to end for procutils' inclusive end address: real artpatch.c, real
// procutils.c, and the kernel's /proc/self/maps. Two adjacent pages are mapped
// r-- and r-x (as a library's read-only segment is followed by its code), and a
// patch is applied to the first byte of the r-x page. artpatch puts back the
// protection procutils reports, so the page's protection afterwards is the
// oracle: it must still be r-x.
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
    unsigned char *base = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(base, 0, 2 * page);
    mprotect(base, page, PROT_READ);
    mprotect(base + page, page, PROT_READ | PROT_EXEC);
    unsigned char *code = base + page;

    char before[5], after_apply[5], after_restore[5];
    perms_of((uintptr_t)code, before);

    patch_t patch = artpatch_new();
    artpatch_set_hex(patch, "1F2003D5");
    artpatch_set_addr(patch, code);
    artpatch_apply(patch);
    perms_of((uintptr_t)code, after_apply);
    artpatch_restore(patch);
    perms_of((uintptr_t)code, after_restore);

    const int ok = !strcmp(after_apply, before) && !strcmp(after_restore, before);
    printf("code page %p: before %s, after apply %s, after restore %s -> %s\n", (void *)code, before, after_apply,
           after_restore, ok ? "ok" : "PROTECTION CHANGED");
    return ok ? 0 : 1;
}
