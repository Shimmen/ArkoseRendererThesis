#include "CommandSubmitter.h"

#include <memory>

CommandSubmitter::CommandSubmitter(int numGraphicsQueues, int numComputeQueues)
{
}

CommandLink CommandSubmitter::submit(std::unique_ptr<command::Command> command)
{
    return {};
}

CommandLink CommandSubmitter::submitAfter(std::unique_ptr<command::Command>, CommandLink other)
{
    return {};
}
