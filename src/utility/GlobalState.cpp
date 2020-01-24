#include "GlobalState.h"

const GlobalState& GlobalState::get()
{
    static GlobalState s_globalState {};
    return s_globalState;
}

GlobalState& GlobalState::getMutable(Badge<Backend>)
{
    const GlobalState& constGlobalState = GlobalState::get();
    return const_cast<GlobalState&>(constGlobalState);
}

Extent2D GlobalState::windowExtent() const
{
    return m_windowExtent;
}
void GlobalState::updateWindowExtent(Extent2D newExtent)
{
    m_windowExtent = newExtent;
}
