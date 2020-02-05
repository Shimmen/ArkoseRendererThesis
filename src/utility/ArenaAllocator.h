#pragma once

#include "logging.h"
#include "utility/Badge.h"
#include "utility/util.h"
#include <cstddef>
#include <cstdlib>
#include <memory>

class ResourceManager;

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t);
    ~ArenaAllocator() = default;

    void reset();

    std::byte* allocateBytes(size_t);

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

template<typename T>
T* ArenaAllocator::allocate(size_t count)
{
    std::byte* data = allocateBytes(count * sizeof(T));
    ASSERT(data != nullptr);
    T* object = reinterpret_cast<T*>(data);
    return object;
}

template<typename T>
T& ArenaAllocator::allocateSingle()
{
    T* data = allocate<T>(1);
    return *data;
}
