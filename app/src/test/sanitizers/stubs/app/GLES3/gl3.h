#pragma once
#include <stdint.h>
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;

#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TEXTURE_2D 0x0DE1
#define GL_RED 0x1903
#define GL_TEXTURE_SWIZZLE_R 0x8E42
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#define GL_TEXTURE_SWIZZLE_B 0x8E44
#define GL_TEXTURE_SWIZZLE_A 0x8E45

#ifdef __cplusplus
extern "C" {
#endif
void glBindTexture(GLenum target, GLuint texture);
void glClear(GLbitfield mask);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glGenTextures(GLsizei n, GLuint *textures);
GLenum glGetError(void);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
#ifdef __cplusplus
}
#endif
