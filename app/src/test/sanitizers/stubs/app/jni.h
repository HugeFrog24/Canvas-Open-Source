#pragma once
// Minimal fake JNI. Everything in a harness binary is compiled against this
// header, so only the methods Canvas actually calls exist. fake_jni.cpp backs
// GetStringUTFChars with malloc'd copies, so a missing ReleaseStringUTFChars is
// a real leak that LeakSanitizer reports.
#include <cstdarg>
#include <cstdint>

typedef int32_t jint;
typedef int64_t jlong;
typedef uint8_t jboolean;
typedef uint16_t jchar;
typedef float jfloat;
typedef double jdouble;
typedef jint jsize;

struct _jobject {};
typedef _jobject *jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jobject jobjectArray;
typedef jobject jintArray;
typedef jobject jthrowable;
struct _jmethodID;
typedef _jmethodID *jmethodID;

#define JNI_FALSE 0
#define JNI_TRUE 1
#define JNI_OK 0
#define JNI_EDETACHED (-2)
#define JNI_ABORT 2
#define JNI_VERSION_1_6 0x00010006
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL

struct _JavaVM;
typedef _JavaVM JavaVM;
struct _JNIEnv;
typedef _JNIEnv JNIEnv;

struct _JNIEnv {
    jint GetJavaVM(JavaVM **vm);
    jclass FindClass(const char *name);
    jint ThrowNew(jclass clazz, const char *message);
    jobject NewGlobalRef(jobject obj);
    void DeleteGlobalRef(jobject obj);
    void DeleteLocalRef(jobject obj);
    jboolean ExceptionCheck();
    void ExceptionClear();
    void ExceptionDescribe();
    jclass GetObjectClass(jobject obj);
    jmethodID GetMethodID(jclass clazz, const char *name, const char *sig);
    jmethodID GetStaticMethodID(jclass clazz, const char *name, const char *sig);
    jboolean CallBooleanMethod(jobject obj, jmethodID method, ...);
    jboolean CallStaticBooleanMethod(jclass clazz, jmethodID method, ...);
    void CallStaticVoidMethod(jclass clazz, jmethodID method, ...);
    jobject CallStaticObjectMethod(jclass clazz, jmethodID method, ...);
    jstring NewStringUTF(const char *utf);
    jsize GetStringUTFLength(jstring str);
    const char *GetStringUTFChars(jstring str, jboolean *isCopy);
    void ReleaseStringUTFChars(jstring str, const char *chars);
    jsize GetArrayLength(jarray array);
    jobjectArray NewObjectArray(jsize length, jclass clazz, jobject init);
    void SetObjectArrayElement(jobjectArray array, jsize index, jobject value);
    jint *GetIntArrayElements(jintArray array, jboolean *isCopy);
    void ReleaseIntArrayElements(jintArray array, jint *elems, jint mode);
};

struct _JavaVM {
    jint GetEnv(void **env, jint version);
    jint AttachCurrentThread(JNIEnv **env, void *args);
    jint DetachCurrentThread();
};

// Harness side.
JNIEnv *FakeJniEnv();
JavaVM *FakeJavaVM();
jstring FakeJniString(const char *utf);
long FakeJniOutstandingUtf();
long FakeJniLiveGlobalRefs();
jintArray FakeJniIntArray(const jint *values, jsize count);
long FakeJniOutstandingIntElements();
void FakeJniTrackAttachment();
long FakeJniAttaches();
long FakeJniDetaches();
