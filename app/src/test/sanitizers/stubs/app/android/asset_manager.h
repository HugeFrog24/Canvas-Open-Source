#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AAssetManager AAssetManager;
typedef struct AAsset AAsset;
enum { AASSET_MODE_UNKNOWN = 0, AASSET_MODE_RANDOM = 1, AASSET_MODE_STREAMING = 2, AASSET_MODE_BUFFER = 3 };
AAsset *AAssetManager_open(AAssetManager *mgr, const char *filename, int mode);
int64_t AAsset_getLength64(AAsset *asset);
int AAsset_read(AAsset *asset, void *buf, size_t count);
void AAsset_close(AAsset *asset);
#ifdef __cplusplus
}
#endif
