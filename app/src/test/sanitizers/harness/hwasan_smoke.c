// Proves HWASan works in this environment (arm64 under QEMU) before any
// Canvas result is trusted: a heap use-after-free must be reported.
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    volatile char *p = malloc(32);
    p[0] = 1;
    free((void *)p);
    printf("reading freed memory: %d\n", p[0]);
    return 0;
}
