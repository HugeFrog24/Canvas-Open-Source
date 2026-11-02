// AAsset / GLES / libktx stand-ins for driving IconLoader.cpp on the host.
// An asset exists when its path contains "present"; ktxTexture_GLUpload
// returns whatever the driver puts in g_uploadResult.
#include <GLES3/gl3.h>
#include <android/asset_manager.h>

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "ktx/ktx.h"

KTX_error_code g_uploadResult = KTX_SUCCESS;

struct AAsset { size_t size; };
AAsset *AAssetManager_open(AAssetManager *, const char *name, int) {
    return std::strstr(name, "present") ? new AAsset{64} : nullptr;
}
int64_t AAsset_getLength64(AAsset *asset) { return static_cast<int64_t>(asset->size); }
int AAsset_read(AAsset *, void *buf, size_t count) {
    std::memset(buf, 0xab, count);
    return static_cast<int>(count);
}
void AAsset_close(AAsset *asset) { delete asset; }

static void destroy_texture(ktxTexture *texture) { std::free(texture); }
static ktxTexture_vtbl g_vtbl = {destroy_texture};

KTX_error_code ktxTexture_CreateFromMemory(const ktx_uint8_t *, ktx_size_t, ktxTextureCreateFlags, ktxTexture **out) {
    auto *texture = static_cast<ktxTexture *>(std::calloc(1, sizeof(ktxTexture)));
    texture->vtbl = &g_vtbl;
    texture->baseWidth = texture->baseHeight = 64;
    *out = texture;
    return KTX_SUCCESS;
}
KTX_error_code ktxTexture_GLUpload(ktxTexture *, GLuint *, GLenum *target, GLenum *glError) {
    *target = GL_TEXTURE_2D;
    *glError = GL_NO_ERROR;
    return g_uploadResult;
}

static std::atomic<GLuint> g_nextTexture{1};
unsigned FakeKtxTexturesCreated() { return g_nextTexture.load() - 1; }
void glBindTexture(GLenum, GLuint) {}
void glClear(GLbitfield) {}
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat) {}
void glDeleteTextures(GLsizei, const GLuint *) {}
void glGenTextures(GLsizei n, GLuint *textures) { for (GLsizei i = 0; i < n; ++i) textures[i] = g_nextTexture++; }
GLenum glGetError() { return GL_NO_ERROR; }
void glTexParameteri(GLenum, GLenum, GLint) {}
void glViewport(GLint, GLint, GLsizei, GLsizei) {}
