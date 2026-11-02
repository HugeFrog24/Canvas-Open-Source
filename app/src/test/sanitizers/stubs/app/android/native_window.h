#pragma once
#include <stdint.h>
typedef struct ANativeWindow ANativeWindow;
void ANativeWindow_acquire(ANativeWindow *window);
void ANativeWindow_release(ANativeWindow *window);
int32_t ANativeWindow_getWidth(ANativeWindow *window);
int32_t ANativeWindow_getHeight(ANativeWindow *window);
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *window, int32_t width, int32_t height, int32_t format);
