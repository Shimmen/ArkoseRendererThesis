#pragma once

#include "Commands.h"
#include "utility/copying.h"

struct CommandLink {
    // TODO: something..?
};

// Defines a command submitter: the thing that accepts and submits commands!
// The idea is that we don't think about it as queues but a single submission
// entry point for all commands, which will split it up into queues for you.
class CommandSubmitter {
public:
    NON_COPYABLE(CommandSubmitter)

    CommandSubmitter(int numGraphicsQueues, int numComputeQueues);

    CommandLink submit(std::unique_ptr<FrontendCommand> command);
    CommandLink submitAfter(std::unique_ptr<FrontendCommand>, CommandLink other);

private:
};
