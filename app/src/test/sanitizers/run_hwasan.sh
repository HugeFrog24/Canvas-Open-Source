#!/bin/bash
# HWASan (hardware-assisted ASan, the memory sanitizer Android supports on
# device) re-runs of confirmed findings, in canvas-hwasan-arm64:local: arm64
# under Docker's QEMU emulation on this x86 host. See FINDINGS.md for the
# docker command. Mounts: /repo = app/src (read-only), /out = output.
set -u
REPO=/repo
SRC=$REPO/main/cpp
H=$REPO/test/sanitizers
OUT=/out
GEN=$OUT/gen
bash $H/run.sh gen > /dev/null || { echo "extraction failed"; exit 1; }  # verbatim .inc files (arch-independent)
mkdir -p $OUT/hwasan && cd $OUT/hwasan

HW="-fsanitize=hwaddress -g -O1 -fno-omit-frame-pointer -w"
CXX="clang++ -std=c++17 -stdlib=libc++ $HW"
CC="clang -include stdint.h $HW"
INC_CIPHER="-I$H/stubs/common -I$H/stubs/cipher -I$SRC/Utils -I$SRC/Core -I$SRC"
INC_APP="-I$GEN -I$H/stubs/app -I$H/stubs/common -I$SRC/Utils -I$SRC/Utils/KittyMemory -I$SRC/Core/imgui -I$SRC/Core -I$SRC"
export HWASAN_OPTIONS=halt_on_error=1
# Under QEMU an uncaught guest signal is followed by a guest core dump that can
# stall for minutes, and some report paths spin (F4, pass 14): no cores, and
# every step is killed after HW_TIMEOUT seconds (exit 137 = timed out).
ulimit -c 0
T="timeout -s KILL ${HW_TIMEOUT:-180}"

banner() { echo; echo "================ $*"; }
report() { grep -E "HWAddressSanitizer|SUMMARY|^    #[0-3] |tag-mismatch|survived|mod read|canvas still|reading freed" "$1" | cut -c1-200 | head -${2:-10}; }

banner "uname: $(uname -m); clang: $(clang --version | head -1)"

banner "smoke: heap use-after-free must be reported"
$CC $H/harness/hwasan_smoke.c -o smoke && { $T ./smoke > smoke.log 2>&1; echo "exit=$?"; report smoke.log 6; }

banner "F3: get_DeviceInfo memcpy of std::string"
$CXX -include cstdint -I$GEN $INC_CIPHER $H/harness/deviceinfo.cpp -o deviceinfo &&
    { $T ./deviceinfo > deviceinfo.log 2>&1; echo "exit=$?"; report deviceinfo.log; }

banner "F1: patternToBytes stack overflow (stored crash input, replayed without libFuzzer)"
$CXX -include cstdint -I$GEN $INC_CIPHER -c $H/harness/fuzz_pattern.cpp -o fuzz_pattern.o &&
    clang $HW -c $H/harness/replay_main.c -o replay.o &&
    $CXX fuzz_pattern.o replay.o -o replay_pattern &&
    { $T ./replay_pattern $H/artifacts/patternToBytes-stack-overflow.bin > pattern.log 2>&1; echo "exit=$?"; report pattern.log; }

banner "F9: keyboard listener use-after-free (controlled interleaving)"
$CXX $INC_APP $H/harness/app/jni_entry.cpp $H/harness/app/fake_jni.cpp -o jni_hw -pthread &&
    { $T ./jni_hw interleave > jni.log 2>&1; echo "exit=$?"; report jni.log; }

banner "F4: CipherPatch deleted after a no-op Fire()"
$CC -I$H/stubs/common -c $SRC/Utils/artpatch/artpatch.c -o artpatch.o &&
    $CC -I$H/stubs/common -c $SRC/Utils/artpatch/procutils.c -o procutils.o &&
    $CXX $INC_CIPHER $SRC/Core/Cipher/CipherBase.cpp $SRC/Core/Cipher/CipherHook.cpp $SRC/Core/Cipher/CipherPatch.cpp \
        $H/harness/cipher_env.cpp $H/harness/cipher_lifecycle.cpp artpatch.o procutils.o -o cipher_hw &&
    { $T ./cipher_hw unfired-delete > cipher.log 2>&1; echo "exit=$?"; report cipher.log; }

banner "F4 diagnostic: same run with HWASan's SEGV handler off (does the hang come from the report path?)"
[ -x cipher_hw ] && { HWASAN_OPTIONS=halt_on_error=1:handle_segv=0 $T ./cipher_hw unfired-delete > cipher_nosegv.log 2>&1; echo "exit=$?"; head -c 600 cipher_nosegv.log; }
