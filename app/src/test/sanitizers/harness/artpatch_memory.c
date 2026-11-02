// Drives the real artpatch.c. Built under MSan (uninitialised bytes reaching
// the patched page), UBSan (null pointers handed to memcpy) and ASan.
//   lowercase: hex as ChatFlagFixInstall.cpp formats it ("%02x") for w10
//   relength:  set_Opcode with a longer opcode after Fire(), then Restore()
//   empty:     set_Opcode("") after a 4-byte opcode, then Fire()
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "artpatch/artpatch.h"

// procutils reads /proc/self/maps through stdio, which MSan cannot see into.
// The page is mmap'd read-write, which is the answer procutils would give.
int procutils_read_mprotection(void *addr) {
    (void)addr;
    return PROT_READ | PROT_WRITE;
}

static unsigned char *fresh_page(void) {
    unsigned char *p = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(p, 0x11, 16);
    return p;
}

static int check(const char *what, const unsigned char *code, const unsigned char *expected, size_t n) {
    const int ok = memcmp(code, expected, n) == 0;
    printf("%-10s %s", what, ok ? "ok" : "MISMATCH");
    if (!ok) {  // only reachable without MSan, which stops at the memcmp above
        printf("  page:");
        for (size_t i = 0; i < n; ++i) printf(" %02x", code[i]);
        printf("  expected:");
        for (size_t i = 0; i < n; ++i) printf(" %02x", expected[i]);
    }
    printf("\n");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "lowercase";
    unsigned char *code = fresh_page();
    patch_t patch = artpatch_new();
    artpatch_set_addr(patch, code);

    if (!strcmp(mode, "lowercase")) {
        const unsigned char expected[4] = {0x4a, 0x05, 0x00, 0x12};
        artpatch_set_hex(patch, "4a050012");
        artpatch_apply(patch);
        return check(mode, code, expected, sizeof(expected));
    }
    if (!strcmp(mode, "relength")) {
        unsigned char original[8];
        memcpy(original, code, sizeof(original));
        artpatch_set_hex(patch, "1F2003D5");
        artpatch_apply(patch);
        artpatch_set_hex(patch, "1F2003D51F2003D5");  // CipherPatch::set_Opcode, then Fire() re-applies
        artpatch_apply(patch);
        artpatch_restore(patch);                         // should put back the 8 original bytes
        return check(mode, code, original, sizeof(original));
    }
    if (!strcmp(mode, "empty")) {
        artpatch_set_hex(patch, "1F2003D5");
        artpatch_set_hex(patch, "");
        artpatch_apply(patch);
        puts("empty      survived");
        return 0;
    }
    return 2;
}
