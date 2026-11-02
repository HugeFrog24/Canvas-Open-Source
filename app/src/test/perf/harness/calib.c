// Host speed calibration for the perf track (pass 50). Host CPU time for
// identical code varied 1.8x between sessions (pass 49), most likely from where
// the Docker VM's virtual CPU ran on the hybrid i9-14900HX (P-core or E-core,
// boost state). This times a dependent chain of integer adds, about one add
// per cycle on these cores, and reports adds per nanosecond: roughly the
// effective clock in GHz for the CPU it ran on. Five repetitions, fastest kept
// (the mhz/calcmhz approach). Printed next to timing results so they can be
// normalised or set aside.
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}

int main(void) {
    const uint64_t n = 200000000ull;  // ~40-50 ms at 4-5 GHz
    double best = 1e30;
    uint64_t sink = 0;
    for (int rep = 0; rep < 5; ++rep) {
        uint64_t x = rep;
        const double t0 = now_ns();
        for (uint64_t i = 0; i < n; ++i) {
            x += i;
            __asm__ volatile("" : "+r"(x));  // keep the chain serial and in a register
        }
        const double dt = now_ns() - t0;
        if (dt < best) best = dt;
        sink += x;
    }
    printf("calibration: %.2f dependent adds/ns (about GHz) [%llu]\n", n / best, (unsigned long long)(sink & 1));
    return 0;
}
