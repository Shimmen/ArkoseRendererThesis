#pragma once

#include "Commands.h"
#include "utility/Badge.h"
#include <vector>

class CommandList {

public:
    CommandList() = default;

    template<typename T, typename... Args>
    void add(Args&&... args)
    {
        m_vector.push_back(std::unique_ptr<T>(new T(std::forward<Args>(args)...)));
    }

    bool hasNext() const
    {
        return m_iterator < m_vector.size();
    }

    const FrontendCommand& peekNext() const
    {
        ASSERT(hasNext());
        const FrontendCommand& command = *m_vector[m_iterator];
        return command;
    }

    const FrontendCommand& next()
    {
        const FrontendCommand& command = *m_vector[m_iterator];
        m_iterator += 1;
        return command;
    }

private:
    std::vector<std::unique_ptr<FrontendCommand>> m_vector {};
    size_t m_iterator { 0 };
};
