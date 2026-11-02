#pragma once
// Host stand-in for shadowhook: every hook "succeeds" with a heap token, so
// CipherHook bookkeeping runs exactly as it does after a real install.
#include <cstdint>
#include <cstdlib>

#define SHADOWHOOK_MODE_MULTI 1
#define SHADOWHOOK_HOOK_WITH_UNIQUE_MODE 1u
#define SHADOWHOOK_HOOK_WITH_MULTI_MODE 2u

inline int shadowhook_init(int, bool) { return 0; }
inline void *shadowhook_hook_func_addr_2(void *, void *, void **, uint32_t) { return std::malloc(1); }
inline int shadowhook_unhook(void *stub) { std::free(stub); return 0; }
inline int shadowhook_get_errno() { return 0; }
inline const char *shadowhook_to_errmsg(int) { return "stub"; }
