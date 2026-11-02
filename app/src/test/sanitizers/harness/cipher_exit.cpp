// Static destruction order at process exit: a mod that keeps its patch in a
// namespace-scope CipherPatch object (static storage) and fires it later.
// CipherRegistry.cpp keeps its state in a function-local static (State()),
// created on first use; objects are destroyed in reverse order of creation,
// so a registry first used after the mod's object was built is destroyed
// before that object, whose destructor then calls ForgetInstall(this).
//
// With REGISTRY_TOUCH_FIRST set, the registry is used before the mod's object
// is constructed (as when ChatFlagFix::Install patches at Canvas load): the
// control, which must exit cleanly.
//
// Link the Cipher sources before this file so their statics (s_InstanceVec)
// are built first, as in the app where Canvas loads before any mod.
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "Cipher/Cipher.h"

static bool g_touchedFirst = std::getenv("REGISTRY_TOUCH_FIRST") && (Cipher::IsAddressPatched(0), true);
static CipherPatch g_modPatch;  // the mod's global patch object

int main() {
    void *page = mmap(nullptr, getpagesize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    g_modPatch.set_Address(reinterpret_cast<std::uintptr_t>(page), false);
    g_modPatch.set_Opcode("1F2003D5");
    g_modPatch.Fire();  // first registry use unless REGISTRY_TOUCH_FIRST
    std::printf("main returning (registry touched first: %s, patched: %d)\n", g_touchedFirst ? "yes" : "no",
                Cipher::IsAddressPatched(reinterpret_cast<std::uintptr_t>(page)));
    std::fflush(stdout);
    return 0;  // exit(): static destructors run, ~CipherPatch -> ForgetInstall(this)
}
