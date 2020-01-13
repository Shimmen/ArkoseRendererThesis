#pragma once

#include "utility/util.h"
#include <cstddef>
#include <cstdlib>
#include <memory>

class FrameAllocator {
public:
    explicit FrameAllocator(size_t);
    ~FrameAllocator() = default;

    void reset();

    std::byte* allocate(size_t);

    template<typename T>
    T& allocate();

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

template<typename T>
T& FrameAllocator::allocate()
{
    std::byte* data = allocate(sizeof(T));
    ASSERT(data != nullptr);
    T* object = reinterpret_cast<T*>(data);
    return *object;
}
