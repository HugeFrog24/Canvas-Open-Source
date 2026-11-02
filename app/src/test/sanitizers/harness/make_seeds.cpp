// Writes the unit tests' real instruction windows out as libFuzzer seeds for
// fuzz_decoders, so the fuzzer starts from inputs the decoders accept.
//
// Pass 26: fuzz_decoders maps the input at a fixed virtual base and serves a
// second read-only region described by the input's last 24 bytes (signed
// displacement from the base, then 16 bytes). Two seed families use that:
//   chatpos_N   a ChatFlagFix fixture window at its real page offset, with
//               "chat" at the displacement the fixture's string address gives,
//               so Find() can accept it (the unit tests' layout);
//   decide_*    pointer chains for OtherAudio::Decide in fuzz_decoders' layout
//               (code half, RELRO half), one reaching Swap and one stopping
//               at each refusal after the vtable checks.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ChatFlagFixFixtures.h"
#include "OtherAudioFixtures.h"

namespace {

constexpr std::uint64_t kBase = 0x7000000000ull;  // fuzz_decoders.cpp

void write(const std::string &path, const void *data, std::size_t size) {
    if (FILE *f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(data, 1, size, f);
        std::fclose(f);
    }
}

void put64(std::vector<std::uint8_t> &b, std::size_t at, std::uint64_t v) { std::memcpy(b.data() + at, &v, 8); }
void put32(std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t v) { std::memcpy(b.data() + at, &v, 4); }

// Tail: displacement of the read-only region from kBase, then its 16 bytes.
void tail(std::vector<std::uint8_t> &b, std::int64_t displacement, const char *text) {
    std::uint8_t t[24] = {};
    std::memcpy(t, &displacement, 8);
    std::memcpy(t + 8, text, std::strlen(text) + 1);
    b.insert(b.end(), t, t + 24);
}

enum class Stop { Swap, AlreadyApplied, NotTheStub, TargetOutsideCode, SlotOutsideRelro, NotAVtable };

// fuzz_decoders' Decide layout: audioGlobal at [0,8), systemIoOffset at
// [8,12), data[12] & 1 drops systemIoGlobal, slot from [24,28) when no chain
// decodes. Image of 2048 bytes: code [0,1024), RELRO [1024,2048).
std::vector<std::uint8_t> decide(Stop stop) {
    std::vector<std::uint8_t> b(2048, 0);
    const std::uint32_t stub[5] = {0xa9bf7bfdu, 0x910003fdu, 0x2a1f03e0u, 0xa8c17bfdu, 0xd65f03c0u};
    put64(b, 0, kBase + 0x100);          // chain.audioGlobal -> [0x100]
    put32(b, 8, 0x10);                   // systemIoOffset
    b[12] = 1;                           // no systemIoGlobal
    put32(b, 24, 0x18);                  // chain.slot
    put64(b, 0x100, kBase + 0x200);      // audio object
    put64(b, 0x210, kBase + 0x300);      // audio + 0x10 -> SystemIO
    std::uint64_t vtable = kBase + 0x500;  // RELRO starts at 0x400 (mid of 2048 / 2)
    if (stop == Stop::SlotOutsideRelro) vtable = kBase + 0x1f0;  // slot lands in the code half
    put64(b, 0x300, vtable);             // SystemIO -> vtable; [vtable - 16, vtable) stays zero
    if (stop == Stop::NotAVtable) put64(b, vtable - kBase - 8, 1);  // non-zero header word
    std::uint64_t current = kBase + 0x80;  // the stub, in the code half
    if (stop == Stop::AlreadyApplied) current = 0x1234;            // fuzz_decoders' replacement value
    if (stop == Stop::TargetOutsideCode) current = kBase + 0x600;  // RELRO, not code
    put64(b, vtable - kBase + 0x18, current);
    for (int i = 0; i < 5; ++i) put32(b, 0x80 + 4 * i, stub[i]);
    if (stop == Stop::NotTheStub) put32(b, 0x80 + 8, 0x52800020u);  // mov w0, #1 instead of mov w0, wzr
    return b;
}

}  // namespace

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    using namespace ChatFlagFixFixtures;
    const Window *chat[] = {&k0_35_1, &k0_34_5, &k0_34_3, &k0_33_0};
    int n = 0;
    for (const Window *w : chat)
        write(dir + "/chat_" + std::to_string(n++), w->words, w->count * sizeof(std::uint32_t));

    n = 0;
    for (const Window *w : chat) {
        // Window at its real offset within the page, so ADRP page arithmetic
        // matches; "chat" where the fixture's string address falls relative to it.
        const std::uintptr_t page = w->codeBase & ~std::uintptr_t{0xfff};
        std::vector<std::uint8_t> b(w->codeBase - page, 0);
        const auto *words = reinterpret_cast<const std::uint8_t *>(w->words);
        b.insert(b.end(), words, words + w->count * sizeof(std::uint32_t));
        while (b.size() < 64) b.push_back(0);
        tail(b, static_cast<std::int64_t>(w->stringAddress) - static_cast<std::int64_t>(page), w->string);
        write(dir + "/chatpos_" + std::to_string(n++), b.data(), b.size());
    }

    const char *names[] = {"swap", "already_applied", "not_the_stub", "target_outside_code", "slot_outside_relro",
                           "not_a_vtable"};
    for (int s = 0; s < 6; ++s) {
        const auto b = decide(static_cast<Stop>(s));
        write(dir + "/decide_" + names[s], b.data(), b.size());
    }

    int a = 0;
    for (const auto &image : Fixtures::All())
        for (const auto &window : image.windows)
            write(dir + "/audio_" + std::to_string(a++), window.bytes.data(), window.bytes.size());
    std::printf("wrote %d audio + 4 chat + 4 chatpos + 6 decide seeds\n", a);
    return 0;
}
