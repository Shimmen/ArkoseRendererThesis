#pragma once

#include "frontend/CommandSubmitter.h"
#include <string>

class Backend {
public:
    Backend() = default;
    virtual ~Backend() = default;

    virtual bool compileCommandSubmitter(const CommandSubmitter&) = 0;
    virtual bool executeFrame() = 0;
};
