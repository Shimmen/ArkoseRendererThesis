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

///

// Scoped exit, i.e. kind of like defer but instead of function scopes it works for all scopes
// From: http://the-witness.net/news/2012/11/scopeexit-in-c11/
template<typename F>
class ScopeExit {
public:
    explicit ScopeExit(F f)
        : m_f(f)
    {
    }
    ~ScopeExit() { m_f(); }

private:
    F m_f;
};

template<typename F>
ScopeExit<F> makeScopeExit(F f)
{
    return ScopeExit<F>(f);
}

#define STRING_CONCAT(x, y) x##y
#define STRING_WITH_LINE(str) STRING_CONCAT(str, __LINE__)
#define MAKE_UNIQUE_SCOPE_EXIT_NAME STRING_WITH_LINE(ScopeExitAtLine)
#define AT_SCOPE_EXIT(lambda) auto MAKE_UNIQUE_SCOPE_EXIT_NAME = makeScopeExit(lambda);
#undef MAKE_UNIQUE_SCOPE_EXIT_NAME
#undef STRING_WITH_LINE
#undef STRING_CONCAT

///

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
