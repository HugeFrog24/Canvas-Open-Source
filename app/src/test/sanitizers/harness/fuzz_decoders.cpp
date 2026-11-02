// libFuzzer target: ChatFlagFix + OtherAudio decoders over a fuzzed "code image".
// The read callback mirrors production's bounds check, so any memory error is a
// decoder bug. The image is mapped at a fixed virtual base (kBase), so fuzzed
// bytes can hold absolute pointers into it (OtherAudio::Decide follows a
// pointer chain) and ADRP page arithmetic sees a page-aligned image.
//
// Oracles: what ChatFlagFixInstall.cpp and OtherAudioApply.cpp rely on when
// they act on a result (pass 21):
//   ChatFlagFix: the site is a 4-byte-aligned word inside the image, `original`
//   is the word there and is the AND-keep-bits-5-7 instruction, `replacement`
//   is AND-keep-bits-0-1 on the same register, and Decide() tells them apart.
//   OtherAudio: decoded sites inside the image, vtable slots 8-byte multiples,
//   gate/fade return addresses after the site; Decide() == Swap only for an
//   8-byte-aligned slot inside RELRO that holds the return-false stub inside
//   code (Write() does an 8-byte compare-exchange there).
// A failed oracle prints the reason and traps, so libFuzzer keeps the input.
#include <cstdio>
#include <cstring>
#include "ChatFlagFix/ChatFlagFix.h"
#include "OtherAudio/OtherAudioSwap.h"

namespace {

constexpr std::uintptr_t kBase = 0x7000000000ull;

[[noreturn]] void fail(const char *what, std::uintptr_t at) {
    std::fprintf(stderr, "ORACLE: %s (at image+0x%llx)\n", what, static_cast<unsigned long long>(at - kBase));
    __builtin_trap();
}

bool inside(std::uintptr_t at, std::size_t n, std::size_t size) {
    return at >= kBase && n <= size && at - kBase <= size - n;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // A second, read-only region (pass 26): the input's last 24 bytes give a
    // signed displacement from kBase and 16 bytes served there. It stands for
    // the game's .rodata, which ChatFlagFix reaches through ADRP+ADD ("chat"
    // lies megabytes from the code it checks, far outside a fuzz input).
    std::int64_t rodataAt = 0;
    std::uint8_t rodata[16] = {};
    if (size >= 64) {
        std::memcpy(&rodataAt, data + size - 24, 8);
        std::memcpy(rodata, data + size - 16, 16);
    }
    const std::uintptr_t rodataBase = kBase + static_cast<std::uintptr_t>(rodataAt);
    auto read = [&](std::uintptr_t at, void *out, std::size_t n) {
        if (inside(at, n, size)) {
            std::memcpy(out, data + (at - kBase), n);
            return true;
        }
        if (size >= 64 && at >= rodataBase && n <= 16 && at - rodataBase <= 16 - n) {
            std::memcpy(out, rodata + (at - rodataBase), n);
            return true;
        }
        return false;
    };
    auto word = [&](std::uintptr_t at) {
        std::uint32_t w = 0;
        std::memcpy(&w, data + (at - kBase), 4);
        return w;
    };

    ChatFlagFix::Site site;
    if (ChatFlagFix::Find(read, kBase, size, site) > 0) {
        if (!inside(site.address, 4, size) || (site.address - kBase) % 4 != 0)
            fail("ChatFlagFix site outside the image or misaligned", site.address);
        if (site.original != word(site.address))
            fail("ChatFlagFix original is not the word at the site", site.address);
        const std::uint32_t reg = site.original & 31u;
        if ((site.original & 0xfffffc00u) != 0x121b7000u || ((site.original >> 5) & 31u) != reg)
            fail("ChatFlagFix original is not AND wN, wN, #0xe0", site.address);
        if (site.replacement != (0x12000400u | (reg << 5) | reg))
            fail("ChatFlagFix replacement is not AND wN, wN, #3 on the same register", site.address);
        if (ChatFlagFix::Decide(site.original, site) != ChatFlagFix::Decision::Write ||
            ChatFlagFix::Decide(site.replacement, site) != ChatFlagFix::Decision::AlreadyApplied)
            fail("ChatFlagFix Decide does not tell original and replacement apart", site.address);
        if (site.flagOffset > 0xfffu)
            fail("ChatFlagFix flag offset beyond a 12-bit immediate", site.address);
    }

    OtherAudio::Chain chain;
    OtherAudio::FmodSelect fmod;
    OtherAudio::Setter setter;
    const bool haveChain = OtherAudio::FindChain(read, kBase, size, chain) > 0;
    if (haveChain) {
        if (!inside(chain.site, 4, size) || (chain.site - kBase) % 4 != 0)
            fail("OtherAudio chain site outside the image or misaligned", chain.site);
        if (chain.slot % 8 != 0 || chain.fadeSlot % 8 != 0)
            fail("OtherAudio chain vtable slot not a multiple of 8", chain.site);
        if (chain.gateReturn != chain.site + 16 || chain.fadeReturn <= chain.gateReturn ||
            chain.fadeReturn > chain.site + 64)
            fail("OtherAudio chain return addresses outside the matched window", chain.site);
    }
    if (OtherAudio::FindFmod(read, kBase, size, fmod) > 0) {
        if (!inside(fmod.site, 4, size) || fmod.slot % 8 != 0)
            fail("OtherAudio fmod site outside the image or slot not a multiple of 8", fmod.site);
    }
    if (OtherAudio::FindSetter(read, kBase, size, setter) > 0) {
        if (!inside(setter.site, 4, size))
            fail("OtherAudio setter site outside the image", setter.site);
    }

    if (size >= 64) {
        // Decide over a located chain. When no chain decodes, take one from the
        // input so the pointer-chasing checks are reachable on their own.
        OtherAudio::Located located;
        located.base = kBase;
        located.chain = chain;
        if (!haveChain)
            std::memcpy(&located.chain.slot, data + 24, 4), located.chain.slot &= 0xfff8u;
        std::memcpy(&located.chain.audioGlobal, data, 8);
        std::memcpy(&located.systemIoOffset, data + 8, 4);
        std::memcpy(&located.systemIoGlobal, data + 16, 8);
        if (data[12] & 1)
            located.systemIoGlobal = 0;
        // Code in the first half, RELRO in the second, as in a real image.
        const std::uintptr_t mid = kBase + (size / 2 & ~std::size_t{7});
        OtherAudio::Image image{kBase, mid, mid, kBase + size};
        const std::uintptr_t replacement = 0x1234;
        const OtherAudio::Plan plan = OtherAudio::Decide(read, &located, true, image, replacement);
        if (plan.decision == OtherAudio::Decision::Swap) {
            if (plan.slotAddress % 8 != 0)
                fail("Swap on a misaligned slot (8-byte compare-exchange)", plan.slotAddress);
            if (plan.slotAddress < image.relroBegin || plan.slotAddress + 8 > image.relroEnd)
                fail("Swap on a slot outside RELRO", plan.slotAddress);
            if (plan.current < image.codeBegin || plan.current + 20 > image.codeEnd)
                fail("Swap with a target outside code", plan.current);
            std::uintptr_t slotValue = 0;
            if (!read(plan.slotAddress, &slotValue, 8) || slotValue != plan.current)
                fail("Swap plan's current value is not what the slot holds", plan.slotAddress);
        }
    }
    return 0;
}
