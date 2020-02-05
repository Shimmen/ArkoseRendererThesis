#include "ArenaAllocator.h"

#include "utility/logging.h"

ArenaAllocator::ArenaAllocator(size_t capacity)
    : m_capacity(capacity)
    , m_cursor(0u)
{
    auto* data = static_cast<std::byte*>(malloc(capacity));
    ASSERT(data != nullptr);
    m_memory.reset(data);
}

void ArenaAllocator::reset()
{
    m_cursor = 0;
}

std::byte* ArenaAllocator::allocateBytes(size_t size)
{
    if (m_cursor + size >= m_capacity) {
        LogError("FrameAllocator::allocateBytes(): trying to allocate more than the reserved capacity! Up the capacity or allocate less.\n");
        return nullptr;
    }

    std::byte* allocated = m_memory.get() + m_cursor;
    m_cursor += size;

    return allocated;
}
