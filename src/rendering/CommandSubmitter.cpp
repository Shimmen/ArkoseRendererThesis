#include "CommandSubmitter.h"

#include <memory>

CommandSubmitter::CommandSubmitter(int numGraphicsQueues, int numComputeQueues)
{
}

CommandLink CommandSubmitter::submit(std::unique_ptr<FrontendCommand> command)
{
    return {};
}

CommandLink CommandSubmitter::submitAfter(std::unique_ptr<FrontendCommand>, CommandLink other)
{
    return {};
}
