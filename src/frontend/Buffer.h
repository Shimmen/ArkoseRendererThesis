#pragma once

#include "common.h"

template<typename T>
class Buffer {
public:
    explicit Buffer<T>(uint64_t count);

    Buffer<T>(Buffer<T>&&) noexcept;
    Buffer<T>& operator=(Buffer<T>&&) noexcept;

    Buffer<T>(const Buffer<T>&) = delete;
    Buffer<T>& operator=(const Buffer<T>&) = delete;

    [[nodiscard]] uint64_t count() const { return m_count; }

    void set_sub_data(const std::vector<T>&, uint64_t element_offset = 0);

private:
    uint64_t m_count;
};

template<typename T>
Buffer<T>::Buffer(uint64_t count)
    : m_count(count)
{
}

template<typename T>
Buffer<T>::Buffer(Buffer&& other) noexcept
    : m_count(other.m_count)
{
}

template<typename T>
Buffer<T>& Buffer<T>::operator=(Buffer&& other) noexcept
{
    this->m_count = other.m_count;
}

template<typename T>
void Buffer<T>::set_sub_data(const std::vector<T>&, uint64_t element_offset)
{
    // TODO: Implement
}
