#pragma once
// Host stand-in for <android/log.h>. Quiet unless CANVAS_SAN_LOG is set, so
// sanitizer reports are not buried under Canvas's own logging.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

enum { ANDROID_LOG_DEBUG = 3, ANDROID_LOG_INFO = 4, ANDROID_LOG_WARN = 5, ANDROID_LOG_ERROR = 6, ANDROID_LOG_FATAL = 7 };

static inline int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio;
    if (!getenv("CANVAS_SAN_LOG"))
        return 0;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[%s] ", tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    return 0;
}
