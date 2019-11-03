#pragma once

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <iostream>

#include <linalg.h>
using namespace linalg::aliases;

#ifdef NDEBUG
#define ASSERT(x) (x)
#else
// TODO: Use own assert so we don't have to include <cassert>!
#include <cassert>
#define ASSERT(x) assert(x)
#endif

#define ASSERT_NOT_REACHED()                  \
    do {                                      \
        ASSERT(false);                        \
        exit(0); /* for noreturn behaviour */ \
    } while (false)

#include <cstdarg>
#include <cstdio>

enum class LogLevel : uint32_t {
    None = 0,
    Error,
    Warning,
    Info,
    All
};

constexpr LogLevel currentLogLevel = LogLevel::Info;

inline void LogInfo(const char* format, ...)
{
    if constexpr (currentLogLevel < LogLevel::Info)
        return;

    va_list vaList;
    va_start(vaList, format);
    vfprintf(stdout, format, vaList);
    fflush(stdout);
    va_end(vaList);
}

inline void LogWarning(const char* format, ...)
{
    if constexpr (currentLogLevel < LogLevel::Warning)
        return;

    va_list vaList;
    va_start(vaList, format);
    vfprintf(stderr, format, vaList);
    fflush(stderr);
    va_end(vaList);
}

inline void LogError(const char* format, ...)
{
    if constexpr (currentLogLevel < LogLevel::Error)
        return;

    va_list vaList;
    va_start(vaList, format);
    vfprintf(stderr, format, vaList);
    fflush(stderr);
    va_end(vaList);
}

[[noreturn]] inline void LogErrorAndExit(const char* format, ...)
{
    if constexpr (currentLogLevel >= LogLevel::Error) {
        va_list vaList;
        va_start(vaList, format);
        vfprintf(stderr, format, vaList);
        fflush(stderr);
        va_end(vaList);
    }
    exit(12345);
}
