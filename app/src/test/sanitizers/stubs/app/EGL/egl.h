#pragma once
#include <stdint.h>
typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *EGLConfig;
typedef void *EGLNativeWindowType;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;

#define EGL_DEFAULT_DISPLAY ((void *)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_NONE 0x3038
#define EGL_ALPHA_SIZE 0x3021
#define EGL_BLUE_SIZE 0x3022
#define EGL_GREEN_SIZE 0x3023
#define EGL_RED_SIZE 0x3024
#define EGL_DEPTH_SIZE 0x3025
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

EGLDisplay eglGetDisplay(void *display_id);
EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list);
EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
