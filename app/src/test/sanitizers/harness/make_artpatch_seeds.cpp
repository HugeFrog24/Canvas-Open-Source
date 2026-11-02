// Seeds for fuzz_artpatch_diff from the ChatFlagFix unit-test fixtures: real
// arm64 instruction words from Sky builds, used as patch bytes. Each seed
// encodes "set_hex(words) set_addr(page, offset) apply restore apply" in
// FuzzedDataProvider's encoding: every value fuzz_artpatch_diff consumes is
// an integer, and FuzzedDataProvider takes integers from the END of the
// input (big-endian within a value, then reduced modulo the range), so a seed
// is the consumed bytes in reverse order.
//
//   make_artpatch_seeds <out-dir> [page-size]
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ChatFlagFixFixtures.h"

namespace {

struct Seed {
    std::vector<unsigned char> consumed;  // in consumption order
    void byte(unsigned v) { consumed.push_back(static_cast<unsigned char>(v)); }
    void u16(unsigned v) { byte(v >> 8); byte(v & 0xff); }
};

void set_hex(Seed &s, const std::uint32_t *words, int count) {
    s.byte(0);                    // op 0
    s.byte(count * 4 - 1);        // n in [1, 12]: one byte, n - 1
    for (int w = 0; w < count; ++w)
        for (int i = 0; i < 4; ++i) {
            s.byte((words[w] >> (8 * i)) & 0xff);  // little-endian, as in memory
            s.byte(i == 3);                        // separator after each word
        }
}

void set_addr(Seed &s, int pageNo, bool nearEnd, unsigned value) {
    s.byte(1);           // op 1
    s.byte(pageNo - 1);  // page in [1, 3]
    s.byte(nearEnd);
    if (nearEnd) s.byte(value - 1);  // page end - 4 * value, value in [1, 3]
    else s.u16(value);               // offset / 4, in [0, page / 4 - 1]
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    const unsigned page = argc > 2 ? static_cast<unsigned>(std::atoi(argv[2])) : 4096;
    const ChatFlagFixFixtures::Window *windows[] = {&ChatFlagFixFixtures::k0_35_1, &ChatFlagFixFixtures::k0_34_5,
                                                    &ChatFlagFixFixtures::k0_34_3, &ChatFlagFixFixtures::k0_33_0};
    int n = 0;
    for (const auto *win : windows) {
        for (std::size_t at = 0; at + 3 <= win->count; at += 7) {
            for (int count = 1; count <= 3; ++count) {
                Seed s;
                set_hex(s, win->words + at, count);
                // Inside a page for most seeds; one near the end of page 2 per window.
                const bool nearEnd = at == 0 && count > 1;
                set_addr(s, 1 + static_cast<int>(at % 3), nearEnd, nearEnd ? 1 : static_cast<unsigned>((at * 37) % (page / 4)));
                s.byte(2);  // apply
                s.byte(3);  // restore
                s.byte(2);  // apply again (Fire() after Restore())
                std::vector<unsigned char> data(s.consumed.rbegin(), s.consumed.rend());
                const std::string path = std::string(argv[1]) + "/fixture_" + std::to_string(n++);
                FILE *f = std::fopen(path.c_str(), "wb");
                if (!f) return 1;
                std::fwrite(data.data(), 1, data.size(), f);
                std::fclose(f);
            }
        }
    }
    std::printf("wrote %d seeds\n", n);
    return 0;
}
