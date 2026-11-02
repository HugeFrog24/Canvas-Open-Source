// Replays a libFuzzer corpus through LLVMFuzzerTestOneInput without linking
// libFuzzer, for sanitizers whose runtime cannot share a process with this
// image's libstdc++-built libFuzzer (MSan) or where per-input reporting is wanted.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
    int replayed = 0;
    for (int i = 1; i < argc; ++i) {
        FILE *f = fopen(argv[i], "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *data = malloc(size > 0 ? (size_t)size : 1);
        size_t got = fread(data, 1, (size_t)size, f);
        fclose(f);
        LLVMFuzzerTestOneInput(data, got);
        free(data);
        ++replayed;
    }
    printf("replayed %d inputs\n", replayed);
    return 0;
}
