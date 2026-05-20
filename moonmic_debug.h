#pragma once

#include <stdio.h>
#include <stdarg.h>

#if defined(__PSV__) || defined(__vita__)
#include <psp2/kernel/clib.h>
#endif

// Enable debug logging (can be disabled for release builds)
#define MOONMIC_DEBUG 1

#ifdef __cplusplus
extern "C" {
#endif

// Simple logging function for moonmic
static inline void moonmic_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
#if defined(__PSV__) || defined(__vita__)
    char buffer[512];
    sceClibVsnprintf(buffer, sizeof(buffer), fmt, args);
    sceClibPrintf("%s\n", buffer);
#else
    vprintf(fmt, args);
    printf("\n");
#endif
    va_end(args);
}

#ifdef __cplusplus
}
#endif

// Logging macro
#define MOONMIC_LOG(...) moonmic_log(__VA_ARGS__)
