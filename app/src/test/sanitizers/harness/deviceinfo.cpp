// Real CipherUtils::get_DeviceInfo body (extracted at build time) + Canvas.h's DeviceInfo layout.
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "Cipher/CipherUtils.h"

namespace Canvas {
    struct DeviceInfo {
        float xdpi;
        float ydpi;
        float density;
        std::string deviceName;
        std::string deviceManufacturer;
        std::string deviceModel;
    };
    DeviceInfo deviceInfo;
}

#include "get_DeviceInfo.inc"

int main() {
    // main.cpp:384 - any of the three strings longer than libc++'s 22-char inline buffer
    Canvas::deviceInfo.deviceName = "Galaxy S24 Ultra of Serge";
    {
        DeviceInfo d = CipherUtils::get_DeviceInfo();
        std::printf("mod read: %s\n", d.deviceName.c_str());
    }
    std::printf("canvas still has: %s\n", Canvas::deviceInfo.deviceName.c_str());
    return 0;
}
