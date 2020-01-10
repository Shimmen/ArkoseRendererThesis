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

    const std::vector<std::unique_ptr<FrontendCommand>>& vector(Badge<Backend>) const
    {
        return m_vector;
    }

private:
    std::vector<std::unique_ptr<FrontendCommand>> m_vector {};
};
