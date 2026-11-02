#pragma once
#include <jni.h>
#include "asset_manager.h"
AAssetManager *AAssetManager_fromJava(JNIEnv *env, jobject assetManager);
