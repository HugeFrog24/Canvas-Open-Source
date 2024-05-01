#include "Cipher.h"

#include "Canvas/Canvas.h"

#include "../include/misc/Logger.h"
#include <shadowhook.h>

CipherHook::CipherHook()
        : p_Hook(0),
        p_Callback(0),
        stub(nullptr) {
    this->m_type = Types::e_hook;
    this->set_libName(Canvas::libName);
}

CipherHook::~CipherHook() {
    this->m_Restore();
    return;
}

//sets detour function
CipherHook* CipherHook::set_Hook(std::uintptr_t _hook) {
    this->p_Hook = _hook;
    LOGI("set_Hook: %lx", _hook);
    return this;
}

//sets callback to original function
CipherHook* CipherHook::set_Callback(std::uintptr_t _callback) {
    this->p_Callback = _callback;
    LOGI("set_Callback: %lx", _callback);
    return this;
}

CipherHook* CipherHook::Fire() {
    //check if fields are set
    const bool invalidHook = this->get_address() == 0 || this->p_Hook == 0;
    const bool invalidCallback = !this->get_Lock() && this->p_Callback == 0;
    const bool already = stub != nullptr;
    if (invalidHook || invalidCallback || already) {
        return this;
    }

    if (!this->get_Lock()) {
        for (auto& instance : CipherBase::s_InstanceVec) {
            CipherHook* pInstance = (CipherHook *)instance;
            if (pInstance->get_Lock()) {
                return this;
            } else if (pInstance->get_address() == this->get_address()
                       && pInstance->m_type == Types::e_hook) {
                this->set_Address(pInstance->p_Hook, false); //hooks the hooked function instead
            }
        }
    }

    LOGD(
        "address: %p detour: %p callback: %p",
        this->get_address(),
        this->p_Hook,
        this->p_Callback
    );

    this->stub = shadowhook_hook_func_addr(
        (void *)this->get_address(),
        (void *)this->p_Hook,
        (void **)this->p_Callback
    );

    if (this->stub == nullptr) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOGE("hook failed: %d - %s", error_num, error_msg);
    }

    CipherBase::s_InstanceVec.push_back((CipherBase *)this);
    return this;
}

void CipherHook::m_Restore() {
    if (this->stub == nullptr) {
        return;
    }

    shadowhook_unhook(this->stub);
    this->stub = nullptr;
    CipherBase::s_InstanceVec.erase(
        std::find(
            CipherBase::s_InstanceVec.begin(),
            CipherBase::s_InstanceVec.end(),
            (CipherBase *)this
        )
    );

    for (auto& instance : CipherBase::s_InstanceVec) {
        CipherHook* pInstance = (CipherHook *)instance;
        if (pInstance->get_address() == this->p_Hook && pInstance->m_type == Types::e_hook) {
            pInstance->Restore();
            return;
        }

        pInstance->set_Address(this->get_address(), false);
        shadowhook_unhook(pInstance->stub);
        pInstance->stub = nullptr;

        CipherBase::s_InstanceVec.erase(
            std::find(
                CipherBase::s_InstanceVec.begin(),
                CipherBase::s_InstanceVec.end(),
                (CipherBase *)pInstance
            )
        );
        pInstance->Fire();
    }
}

void CipherHook::Restore() {
    this->m_Restore();
}

