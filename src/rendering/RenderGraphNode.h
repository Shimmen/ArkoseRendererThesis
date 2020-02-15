#pragma once

#include "AppState.h"
#include "ResourceManager.h"
#include "Resources.h"
#include "rendering/CommandList.h"
#include "utility/ArenaAllocator.h"
#include "utility/copying.h"
#include <functional>
#include <memory>
#include <string>

class RenderGraphNode {
public:
    explicit RenderGraphNode(std::string name);
    virtual ~RenderGraphNode() = default;

    using ExecuteCallback = std::function<void(const AppState&, CommandList&)>;

    [[nodiscard]] const std::string& name() const;

    //! This is not const since we need to write to members here that are shared for the whole node.
    virtual void constructNode(ResourceManager&) = 0;

    //! This is const, since changing or writing to any members would probably break stuff
    //! since this is called n times, one for each frame at reconstruction.
    virtual ExecuteCallback constructFrame(ResourceManager&) const = 0;

private:
    std::string m_name;
};

class RenderGraphBasicNode final : public RenderGraphNode {
public:
    using ConstructorFunction = std::function<ExecuteCallback(ResourceManager&)>;
    RenderGraphBasicNode(std::string name, ConstructorFunction);

    void constructNode(ResourceManager&) override;
    ExecuteCallback constructFrame(ResourceManager&) const override;

private:
    ConstructorFunction m_constructorFunction;
};