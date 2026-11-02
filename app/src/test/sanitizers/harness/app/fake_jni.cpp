// Backs stubs/app/jni.h. UTF chars are malloc'd copies, as ART hands out, so an
// unreleased GetStringUTFChars is a heap block LeakSanitizer can see.
#include <jni.h>

#include <pthread.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct FakeString : _jobject {
    std::string utf;
};

std::mutex g_stringsMutex;
std::deque<FakeString> g_strings;  // reachable for the whole run, never reported
std::atomic<long> g_outstandingUtf{0};
_jobject g_object;
JNIEnv g_env;
JavaVM g_vm;

}  // namespace

JNIEnv *FakeJniEnv() { return &g_env; }
JavaVM *FakeJavaVM() { return &g_vm; }
long FakeJniOutstandingUtf() { return g_outstandingUtf.load(); }

jstring FakeJniString(const char *utf) {
    std::lock_guard<std::mutex> lock(g_stringsMutex);
    g_strings.emplace_back();
    g_strings.back().utf = utf;
    return &g_strings.back();
}

// ---- Pending Java exception, per thread as in ART (pass 35, L20) -----------
// Android's JNI tips ("Exceptions") list the only calls allowed while one is
// pending: Delete*Ref, Exception*, MonitorExit, Push/PopLocalFrame and the
// Release* family. Every other fake call made while one is pending is counted;
// CheckJNI aborts on the first, and release ART does not check.
namespace {
thread_local bool t_pendingException = false;
std::atomic<long> g_callsWithPending{0};
std::mutex g_pendingMutex;
std::string g_firstCallWithPending;
void pending_check(const char *name) {
    if (!t_pendingException) return;
    if (g_callsWithPending.fetch_add(1) == 0) {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_firstCallWithPending = name;
    }
}
}  // namespace

bool FakeJniExceptionPending() { return t_pendingException; }
void FakeJniClearPending() { t_pendingException = false; }  // the harness, as the Java caller
long FakeJniCallsWithPendingException() { return g_callsWithPending.load(); }
std::string FakeJniFirstCallWithPendingException() {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    return g_firstCallWithPending;
}

// ---- Java clipboard: ImGUI.getClipboard()/setClipboard(String), pass 34 ----
// The String between the two calls is held as UTF-16 and converted the way
// current ART does (runtime/jni/jni_internal.cc), not by the JNI spec:
//   NewStringUTF: the lead byte alone picks the length (0xxx 1, 110x 2, 1110 3,
//     else 4; continuation bits unchecked), 4-byte sequences become a surrogate
//     pair, 3-byte surrogate halves are kept as-is, and a sequence cut short by
//     the terminating NUL becomes '?';
//   GetStringUTFChars/GetStringUTFLength: kUtfUseShortZero = false (U+0000 is
//     C0 80), kUtfUse4ByteSequence = true (a valid pair is one 4-byte sequence,
//     "against the JNI spec"), kUtfReplaceBadSurrogates = false (an unpaired
//     surrogate is a 3-byte sequence).
// ImGUI.getClipboard() returns "" when there is no clip and never null.
namespace {
_jobject g_getClipboardId, g_setClipboardId;
std::mutex g_clipboardMutex;
std::u16string g_javaClipboard;
std::atomic<long> g_clipboardGets{0}, g_clipboardSets{0}, g_clipboardSetThrows{0};
// Pass 35 (L20): setPrimaryClip throws RuntimeException(TransactionTooLargeException)
// when the clip's parcel is too large (a report shows 1,193,472 bytes failing).
// Model: a String longer than this many UTF-16 units throws and leaves the clip
// unchanged. 0 = never throws.
std::atomic<size_t> g_setClipboardThrowAbove{0};

std::u16string ArtNewStringUtf(const char *utf) {
    std::u16string out;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(utf);
    const unsigned char *end = p + std::strlen(utf);
    while (p < end) {
        const unsigned c = *p;
        const size_t n = (c & 0x80) == 0 ? 1 : (c & 0x20) == 0 ? 2 : (c & 0x10) == 0 ? 3 : 4;
        if (static_cast<size_t>(end - p) < n) { out.push_back(u'?'); break; }
        if (n == 1) out.push_back(static_cast<char16_t>(c));
        else if (n == 2) out.push_back(static_cast<char16_t>(((c & 0x1f) << 6) | (p[1] & 0x3f)));
        else if (n == 3) out.push_back(static_cast<char16_t>(((c & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f)));
        else {
            const uint32_t bits = (((c & 0x07) << 18) | ((p[1] & 0x3f) << 12) | ((p[2] & 0x3f) << 6) | (p[3] & 0x3f)) - 0x10000u;
            out.push_back(static_cast<char16_t>(0xd800u | ((bits >> 10) & 0x3ffu)));
            out.push_back(static_cast<char16_t>(0xdc00u | (bits & 0x3ffu)));
        }
        p += n;
    }
    return out;
}

std::string ArtStringUtfChars(const std::u16string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t c = s[i];
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff) {
            c = 0x10000u + ((c - 0xd800u) << 10) + (s[++i] - 0xdc00u);
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c != 0 && c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}
}  // namespace

long FakeJavaClipboardGets() { return g_clipboardGets.load(); }
long FakeJavaClipboardSetThrows() { return g_clipboardSetThrows.load(); }
void FakeJavaClipboardThrowAbove(size_t utf16Units) { g_setClipboardThrowAbove = utf16Units; }
long FakeJavaClipboardSets() { return g_clipboardSets.load(); }
// The current clip as GetStringUTFChars would hand it to native code.
std::string FakeJavaClipboardUtf8() {
    std::lock_guard<std::mutex> lock(g_clipboardMutex);
    return ArtStringUtfChars(g_javaClipboard);
}
void FakeJavaClipboardSetUtf16(const std::u16string &s) {
    std::lock_guard<std::mutex> lock(g_clipboardMutex);
    g_javaClipboard = s;
}

jint _JNIEnv::GetJavaVM(JavaVM **vm) { pending_check("GetJavaVM"); *vm = &g_vm; return JNI_OK; }
jclass _JNIEnv::FindClass(const char *) { pending_check("FindClass"); return &g_object; }
jint _JNIEnv::ThrowNew(jclass, const char *) { pending_check("ThrowNew"); t_pendingException = true; return 0; }
static std::atomic<long> g_globalRefs{0};
long FakeJniLiveGlobalRefs() { return g_globalRefs.load(); }
jobject _JNIEnv::NewGlobalRef(jobject obj) { pending_check("NewGlobalRef"); ++g_globalRefs; return obj; }
void _JNIEnv::DeleteGlobalRef(jobject) { --g_globalRefs; }
void _JNIEnv::DeleteLocalRef(jobject) {}
jboolean _JNIEnv::ExceptionCheck() { return t_pendingException ? JNI_TRUE : JNI_FALSE; }
void _JNIEnv::ExceptionClear() { t_pendingException = false; }
void _JNIEnv::ExceptionDescribe() {}
jclass _JNIEnv::GetObjectClass(jobject) { pending_check("GetObjectClass"); return &g_object; }
jmethodID _JNIEnv::GetMethodID(jclass, const char *, const char *) { pending_check("GetMethodID"); return reinterpret_cast<jmethodID>(&g_object); }
jmethodID _JNIEnv::GetStaticMethodID(jclass, const char *name, const char *) {
    pending_check("GetStaticMethodID");
    if (!std::strcmp(name, "getClipboard")) return reinterpret_cast<jmethodID>(&g_getClipboardId);
    if (!std::strcmp(name, "setClipboard")) return reinterpret_cast<jmethodID>(&g_setClipboardId);
    return reinterpret_cast<jmethodID>(&g_object);
}
jboolean _JNIEnv::CallBooleanMethod(jobject, jmethodID, ...) { pending_check("CallBooleanMethod"); return JNI_TRUE; }
jboolean _JNIEnv::CallStaticBooleanMethod(jclass, jmethodID, ...) { pending_check("CallStaticBooleanMethod"); return JNI_TRUE; }
void _JNIEnv::CallStaticVoidMethod(jclass, jmethodID method, ...) {
    pending_check("CallStaticVoidMethod");
    if (method != reinterpret_cast<jmethodID>(&g_setClipboardId)) return;
    va_list ap;
    va_start(ap, method);
    auto *str = static_cast<FakeString *>(va_arg(ap, jstring));
    va_end(ap);
    ++g_clipboardSets;
    std::u16string clip = ArtNewStringUtf(str->utf.c_str());
    const size_t limit = g_setClipboardThrowAbove.load();
    if (limit && clip.size() > limit) {
        ++g_clipboardSetThrows;
        t_pendingException = true;  // RuntimeException out of setClipboard
        return;
    }
    FakeJavaClipboardSetUtf16(clip);
}
jobject _JNIEnv::CallStaticObjectMethod(jclass, jmethodID method, ...) {
    pending_check("CallStaticObjectMethod");
    if (method != reinterpret_cast<jmethodID>(&g_getClipboardId)) return FakeJniString("");
    ++g_clipboardGets;
    return FakeJniString(FakeJavaClipboardUtf8().c_str());
}
jstring _JNIEnv::NewStringUTF(const char *utf) { pending_check("NewStringUTF"); return FakeJniString(utf); }
jsize _JNIEnv::GetStringUTFLength(jstring str) { pending_check("GetStringUTFLength"); return static_cast<jsize>(static_cast<FakeString *>(str)->utf.size()); }

const char *_JNIEnv::GetStringUTFChars(jstring str, jboolean *isCopy) {
    pending_check("GetStringUTFChars");
    if (isCopy)
        *isCopy = JNI_TRUE;
    g_outstandingUtf.fetch_add(1);
    return strdup(static_cast<FakeString *>(str)->utf.c_str());
}

void _JNIEnv::ReleaseStringUTFChars(jstring, const char *chars) {
    g_outstandingUtf.fetch_sub(1);
    std::free(const_cast<char *>(chars));
}

// Int arrays: GetIntArrayElements hands out a malloc'd copy (isCopy = true), as
// ART may, and ReleaseIntArrayElements frees it, so a use after release is a
// heap-use-after-free ASan reports.
namespace {
struct FakeIntArray : _jobject {
    std::vector<jint> values;
};
std::mutex g_arraysMutex;
std::deque<FakeIntArray> g_intArrays;
std::atomic<long> g_outstandingIntElements{0};
FakeIntArray *as_int_array(jobject obj) {
    std::lock_guard<std::mutex> lock(g_arraysMutex);
    for (auto &a : g_intArrays)
        if (&a == obj) return &a;
    return nullptr;
}
}
jintArray FakeJniIntArray(const jint *values, jsize count) {
    std::lock_guard<std::mutex> lock(g_arraysMutex);
    g_intArrays.emplace_back();
    g_intArrays.back().values.assign(values, values + count);
    return &g_intArrays.back();
}
long FakeJniOutstandingIntElements() { return g_outstandingIntElements.load(); }

jsize _JNIEnv::GetArrayLength(jarray array) {
    pending_check("GetArrayLength");
    FakeIntArray *a = as_int_array(array);
    return a ? static_cast<jsize>(a->values.size()) : 0;
}
jobjectArray _JNIEnv::NewObjectArray(jsize, jclass, jobject) { pending_check("NewObjectArray"); return &g_object; }
void _JNIEnv::SetObjectArrayElement(jobjectArray, jsize, jobject) { pending_check("SetObjectArrayElement"); }
jint *_JNIEnv::GetIntArrayElements(jintArray array, jboolean *isCopy) {
    pending_check("GetIntArrayElements");
    FakeIntArray *a = as_int_array(array);
    if (!a) return nullptr;
    if (isCopy) *isCopy = JNI_TRUE;
    auto *copy = static_cast<jint *>(std::malloc(sizeof(jint) * (a->values.size() ? a->values.size() : 1)));
    std::memcpy(copy, a->values.data(), sizeof(jint) * a->values.size());
    ++g_outstandingIntElements;
    return copy;
}
void _JNIEnv::ReleaseIntArrayElements(jintArray, jint *elems, jint) {
    --g_outstandingIntElements;
    std::free(elems);
}

// ---- Thread attachment (opt-in via FakeJniTrackAttachment) -----------------
// Off by default: every thread counts as attached, as the other harnesses
// assume. On: only the enabling thread (standing in for a Java thread) and
// threads that called AttachCurrentThread are attached, and a thread that
// exits while still attached runs ART's ThreadExitCallback logic
// (art/runtime/thread.cc): warn and re-arm on the first destructor pass, then
// LOG(FATAL), which aborts the process.
namespace {
std::atomic<bool> g_trackAttachment{false};
std::atomic<long> g_attaches{0}, g_detaches{0};
thread_local bool t_attached = false;
pthread_key_t g_exitKey;
struct ExitCheck { int count = 0; };

void ArtThreadExitModel(void *arg) {
    auto *check = static_cast<ExitCheck *>(arg);
    if (check->count == 0) {
        std::fprintf(stderr, "W art: Native thread exiting without having called DetachCurrentThread "
                             "(maybe it's going to use a pthread_key_create destructor?)\n");
        check->count = 1;
        pthread_setspecific(g_exitKey, check);
    } else {
        std::fprintf(stderr, "F art: Native thread exited without calling DetachCurrentThread\n");
        std::fflush(stderr);
        std::abort();
    }
}
}

void FakeJniTrackAttachment() {
    pthread_key_create(&g_exitKey, ArtThreadExitModel);
    t_attached = true;
    g_trackAttachment = true;
}
long FakeJniAttaches() { return g_attaches.load(); }
long FakeJniDetaches() { return g_detaches.load(); }

jint _JavaVM::GetEnv(void **env, jint) {
    if (g_trackAttachment && !t_attached) {
        *env = nullptr;
        return JNI_EDETACHED;
    }
    *env = &g_env;
    return JNI_OK;
}
jint _JavaVM::AttachCurrentThread(JNIEnv **env, void *) {
    *env = &g_env;
    if (g_trackAttachment && !t_attached) {
        t_attached = true;
        ++g_attaches;
        pthread_setspecific(g_exitKey, new ExitCheck);  // leaked on purpose if the thread aborts
    }
    return JNI_OK;
}
jint _JavaVM::DetachCurrentThread() {
    if (g_trackAttachment && t_attached) {
        t_attached = false;
        ++g_detaches;
        delete static_cast<ExitCheck *>(pthread_getspecific(g_exitKey));
        pthread_setspecific(g_exitKey, nullptr);
    }
    return JNI_OK;
}
