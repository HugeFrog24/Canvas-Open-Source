#pragma once
#include <jni.h>
#include "native_window.h"
ANativeWindow *ANativeWindow_fromSurface(JNIEnv *env, jobject surface);
