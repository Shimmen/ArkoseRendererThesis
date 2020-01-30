#pragma once

#include "logging.h"
#include "utility/Badge.h"
#include "utility/util.h"
#include <cstddef>
#include <cstdlib>
#include <memory>

template<typename ResetClass>
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t);
    ~ArenaAllocator() = default;

    void reset(Badge<ResetClass>);

    std::byte* allocate(size_t);

    template<typename T>
    T* allocate(size_t count);

    template<typename T>
    T& allocateSingle();

private:
    struct FreeDeleter {
        void operator()(std::byte* x)
        {
            free(x);
        }
    };

    std::unique_ptr<std::byte, FreeDeleter> m_memory;
    size_t m_capacity;
    size_t m_cursor;
};

template<typename ResetClass>
ArenaAllocator<ResetClass>::ArenaAllocator(size_t capacity)
    : m_capacity(capacity)
    , m_cursor(0u)
{
    auto* data = static_cast<std::byte*>(malloc(capacity));
    ASSERT(data != nullptr);
    m_memory.reset(data);
}

template<typename ResetClass>
void ArenaAllocator<ResetClass>::reset(Badge<ResetClass>)
{
    m_cursor = 0;
}

template<typename ResetClass>
std::byte* ArenaAllocator<ResetClass>::allocate(size_t size)
{
    if (m_cursor + size >= m_capacity) {
        LogError("FrameAllocator::allocate(): trying to allocate more than the reserved capacity! Up the capacity or allocate less.\n");
        return nullptr;
    }

    std::byte* allocated = m_memory.get() + m_cursor;
    m_cursor += size;

    return allocated;
}

template<typename ResetClass>
template<typename T>
T* ArenaAllocator<ResetClass>::allocate(size_t count)
{
    std::byte* data = allocate(count * sizeof(T));
    ASSERT(data != nullptr);
    T* object = reinterpret_cast<T*>(data);
    return object;
}

template<typename ResetClass>
template<typename T>
T& ArenaAllocator<ResetClass>::allocateSingle()
{
    T* data = allocate<T>(1);
    return *data;
}

// It's important to keep it very clear what resources are to be used when/where/how, so these names I think are important
using FrameAllocator = ArenaAllocator<class Backend>;
