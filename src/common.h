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

#include <cstdio>
#define LogInfo(...) printf(__VA_ARGS__)
#define LogWarning(...) fprintf(stderr, __VA_ARGS__)
#define LogError(...) fprintf(stderr, __VA_ARGS__)
#define LogErrorAndExit(...)          \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        ASSERT_NOT_REACHED();         \
    } while (false)
