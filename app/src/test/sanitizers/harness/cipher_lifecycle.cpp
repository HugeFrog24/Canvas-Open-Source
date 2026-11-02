// Drives the real CipherBase / CipherHook / CipherPatch + artpatch code.
#include "Cipher/Cipher.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <thread>

static void target() {}
static void *g_orig = nullptr;

static std::uintptr_t page() {
    void *p = mmap(nullptr, getpagesize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return reinterpret_cast<std::uintptr_t>(p);
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "";
    if (!std::strcmp(mode, "unfired-delete")) {
        // A mod whose pattern scan failed (address 0) and that cleans up its patch.
        auto *p = new CipherPatch();
        p->set_Opcode("1F2003D5");
        p->Fire();   // address == 0 -> early return, never pushed to s_InstanceVec
        delete p;    // ~CipherPatch: s_InstanceVec.erase(find(...)) == erase(end())
        std::puts("survived");
    } else if (!std::strcmp(mode, "mixed-types")) {
        auto *p = new CipherPatch();
        p->set_Address(page(), false);
        p->set_Opcode("1F2003D5");
        p->Fire();   // what ChatFlagFix::Install does at startup
        auto *h = new CipherHook();
        h->set_Address(page(), false);
        h->set_Hook(reinterpret_cast<std::uintptr_t>(&target));
        h->set_Callback(reinterpret_cast<std::uintptr_t>(&g_orig));
        h->Fire();   // walks s_InstanceVec casting every entry, including the CipherPatch, to CipherHook*
        std::puts("survived");
    } else if (!std::strcmp(mode, "race")) {
        // A mod toggling a patch from Draw() while another thread installs hooks.
        const std::uintptr_t a = page(), b = page();
        std::thread t1([&] {
            for (int i = 0; i < 2000; ++i) {
                auto *p = new CipherPatch();
                p->set_Address(a, false);
                p->set_Opcode("1F2003D5");
                p->Fire();
                delete p;
            }
        });
        std::thread t2([&] {
            for (int i = 0; i < 2000; ++i) {
                auto *h = new CipherHook();
                h->set_Address(b, false);
                h->set_Hook(1);
                h->set_Callback(reinterpret_cast<std::uintptr_t>(&g_orig));
                h->Fire();
                delete h;
            }
        });
        t1.join();
        t2.join();
        std::puts("survived");
    }
    return 0;
}
