// Drives the real CipherRegistry.cpp (the install registry behind
// Cipher::IsAddressPatched / DescribePatchesAt / DescribePatchesIn /
// OnInstallEvent and the CipherInternal hooks Fire() and the destructors
// call). The registry only uses CipherBase* as map keys, so the keys here
// are distinct addresses in a static array, never dereferenced.
//
//   concurrent   installers (RecordInstall/ForgetInstall/NotePatchBytes, as
//                mods' Fire() and destructors on their own threads), queries
//                (as mods asking from Draw() or the UI thread) and listener
//                registration at once; checks what callers rely on
//   ranges       DescribePatchesIn with a length that wraps the address
//                space and with an empty range, against an overflow-safe
//                reference
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "Cipher/Cipher.h"
#include "Cipher/CipherRegistry.h"

namespace {

constexpr int kInstallers = 4, kKeysPerInstaller = 32, kRounds = 120, kListeners = 6;
alignas(16) char g_slots[kInstallers * kKeysPerInstaller][16];
CipherBase *key(int i) { return reinterpret_cast<CipherBase *>(g_slots[i]); }
std::uintptr_t addrOf(int i) { return 0x70000000u + static_cast<std::uintptr_t>(i) * 0x100; }
void callerFn() {}

struct Received {
    std::mutex m;
    std::vector<std::uint64_t> seqs;  // in delivery order
    long stale = 0;                   // delivered while IsAddressPatched(address) was already false
};
Received g_recv[kListeners];
int g_failures = 0;
void fail(const char *what) { ++g_failures; std::printf("FAIL: %s\n", what); }

void listen(int id) {
    Cipher::OnInstallEvent([id](const CipherInstallInfo &info) {
        const bool present = Cipher::IsAddressPatched(info.address);  // listeners may query (header contract)
        std::lock_guard<std::mutex> g(g_recv[id].m);
        g_recv[id].seqs.push_back(info.installSeq);
        if (!present) ++g_recv[id].stale;
    });
}

int concurrent() {
    listen(0);  // registered before any install: must see every install exactly once
    std::atomic<long> installs{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < kInstallers; ++t) {
        threads.emplace_back([t, &installs] {
            for (int r = 0; r < kRounds; ++r) {
                for (int k = 0; k < kKeysPerInstaller; ++k) {
                    const int i = t * kKeysPerInstaller + k;
                    if ((r + k) % 3 == 2) {  // a mod deleting its patch
                        CipherInternal::ForgetInstall(key(i));
                    } else {                 // set_Opcode then Fire()
                        CipherInternal::NotePatchBytes(key(i), 4);
                        CipherInternal::RecordInstall(key(i), reinterpret_cast<void *>(&callerFn), Types::e_patch,
                                                      addrOf(i), CipherInternal::LookupPatchBytes(key(i)),
                                                      "libBootloader.so");
                        installs.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });
    }
    for (int q = 0; q < 2; ++q) {
        threads.emplace_back([q, &stop] {
            unsigned n = q;
            while (!stop.load(std::memory_order_relaxed)) {
                const int i = static_cast<int>(n++ % (kInstallers * kKeysPerInstaller));
                const std::uintptr_t a = addrOf(i) + 2;
                (void)Cipher::IsAddressPatched(a);
                const auto at = Cipher::DescribePatchesAt(a);
                for (size_t j = 0; j < at.size(); ++j) {
                    if (!(a >= at[j].address && a < at[j].address + at[j].bytes)) fail("DescribePatchesAt returned an install not covering the address");
                    if (j && at[j - 1].installSeq >= at[j].installSeq) fail("DescribePatchesAt not sorted by installSeq");
                }
                const auto in = Cipher::DescribePatchesIn(addrOf(i), 0x400);
                for (const auto &info : in)
                    if (!(info.address < addrOf(i) + 0x400 && info.address + info.bytes > addrOf(i))) fail("DescribePatchesIn returned a non-overlapping install");
            }
        });
    }
    threads.emplace_back([] {  // mods registering listeners while others fire
        for (int id = 1; id < kListeners; ++id) {
            listen(id);
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    });
    for (int t = 0; t < kInstallers; ++t) threads[t].join();
    stop = true;
    for (size_t t = kInstallers; t < threads.size(); ++t) threads[t].join();

    // Every install is delivered exactly once to a listener registered before it.
    std::vector<std::uint64_t> all = g_recv[0].seqs;
    std::sort(all.begin(), all.end());
    if (static_cast<long>(all.size()) != installs.load()) fail("listener 0 did not receive every install");
    if (std::adjacent_find(all.begin(), all.end()) != all.end()) fail("an install was delivered twice (or installSeq repeated)");
    long outOfOrder = 0, staleTotal = 0;
    for (int id = 0; id < kListeners; ++id) {
        auto &s = g_recv[id].seqs;
        for (size_t j = 1; j < s.size(); ++j) outOfOrder += s[j - 1] > s[j];
        std::vector<std::uint64_t> sorted = s;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) fail("a later listener received an install twice");
        staleTotal += g_recv[id].stale;
    }

    // Final state equals the model: key i is installed iff its last round recorded it.
    std::set<std::uintptr_t> expected;
    for (int i = 0; i < kInstallers * kKeysPerInstaller; ++i) {
        const int k = i % kKeysPerInstaller;
        const bool live = ((kRounds - 1) + k) % 3 != 2;
        if (live) expected.insert(addrOf(i));
        if (Cipher::IsAddressPatched(addrOf(i) + 1) != live) fail("IsAddressPatched disagrees with the model");
        if (CipherInternal::LookupPatchBytes(key(i)) != (live ? 4u : 0u)) fail("LookupPatchBytes disagrees with the model");
    }
    std::set<std::uintptr_t> actual;
    for (const auto &info : Cipher::DescribePatchesIn(addrOf(0), addrOf(kInstallers * kKeysPerInstaller) - addrOf(0)))
        actual.insert(info.address);
    if (actual != expected) fail("DescribePatchesIn over all keys disagrees with the model");

    std::printf("installs=%ld delivered_to_first_listener=%zu listeners=%d out_of_order_deliveries=%ld "
                "stale_deliveries=%ld live=%zu failures=%d\n",
                installs.load(), g_recv[0].seqs.size(), kListeners, outOfOrder, staleTotal, expected.size(), g_failures);
    return g_failures ? 1 : 0;
}

// Overflow-safe reference for "overlaps [start, start+len)".
bool overlaps(std::uintptr_t a, std::size_t bytes, std::uintptr_t start, std::size_t len) {
    if (len == 0 || bytes == 0) return false;
    const std::uintptr_t endIncl = (len - 1 > UINTPTR_MAX - start) ? UINTPTR_MAX : start + (len - 1);
    return a <= endIncl && a + (bytes - 1) >= start;
}

int ranges() {
    CipherInternal::RecordInstall(key(0), nullptr, Types::e_patch, 0x10000, 4, "libBootloader.so");
    CipherInternal::RecordInstall(key(1), nullptr, Types::e_hook, 0x20000, 16, "libBootloader.so");
    struct Q { const char *what; std::uintptr_t start; std::size_t len; } qs[] = {
        {"len = SIZE_MAX (\"everything from start\")", 0x8000, SIZE_MAX},
        {"len reaching the top of the address space", 0x8000, UINTPTR_MAX - 0x8000 + 1},
        {"empty range inside an install", 0x10002, 0},
        {"ordinary range", 0x8000, 0x10000},
    };
    int mismatches = 0;
    for (const auto &q : qs) {
        const auto got = Cipher::DescribePatchesIn(q.start, q.len);
        size_t want = overlaps(0x10000, 4, q.start, q.len) + overlaps(0x20000, 16, q.start, q.len);
        const bool ok = got.size() == want;
        mismatches += !ok;
        std::printf("%s DescribePatchesIn(0x%zx, 0x%zx) [%s]: got %zu, reference %zu\n", ok ? "ok      " : "MISMATCH",
                    static_cast<size_t>(q.start), q.len, q.what, got.size(), want);
    }
    std::printf("ranges mismatches=%d\n", mismatches);
    return 0;  // contract observations, recorded rather than failed
}

}  // namespace

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "concurrent";
    if (!std::strcmp(mode, "concurrent")) return concurrent();
    if (!std::strcmp(mode, "ranges")) return ranges();
    std::printf("unknown mode %s\n", mode);
    return 2;
}
