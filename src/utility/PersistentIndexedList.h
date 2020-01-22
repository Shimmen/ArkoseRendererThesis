#pragma once

/// This is pretty much just a list with an accompanying free list.
template<typename T>
class PersistentIndexedList {
public:
    PersistentIndexedList() = default;
    ~PersistentIndexedList() = default;

    size_t add(T val)
    {
        if (m_freeList.empty()) {
            size_t index = m_internal.size();
            m_internal.push_back(val);
            return index;
        } else {
            size_t index = m_freeList.back();
            m_freeList.pop_back();
            m_internal[index] = val;
            return index;
        }
    }

    void remove(size_t index)
    {
        if (index >= m_internal.size()) {
            return;
        }

        if (index == m_internal.size() - 1) {
            m_internal.pop_back();
        } else {
            m_freeList.push_back(index);
#if !NDEBUG
            // (if debug mode, write zeros to make it very clear that it's not active)
            //memset(&m_internal[index], 0, sizeof(T));
#endif
        }
    }

    [[nodiscard]] size_t size() const
    {
        return m_internal.size() - m_freeList.size();
    }

    T& operator[](size_t index)
    {
#if !NDEBUG
        // (assure index is not in the free list. could maybe catch some use-after-free bugs?)
        for (size_t idx : m_freeList) {
            ASSERT(idx != index);
        }
#endif
        return m_internal[index];
    }

    const std::vector<T>& vector() const
    {
        return m_internal;
    }

    // Iterator compatibility
    /*
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = T*;
    using reference = T&;

    typename std::vector<T>::iterator begin() { return m_internal.begin(); }
    typename std::vector<T>::const_iterator begin() const { return m_internal.begin(); }

    typename std::vector<T>::iterator end() { return m_internal.end(); }
    typename std::vector<T>::const_iterator end() const { return m_internal.end(); }
*/

private:
    std::vector<T> m_internal {};
    std::vector<size_t> m_freeList {};
};
