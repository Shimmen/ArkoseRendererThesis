#pragma once

class GlobalState {
    friend int main();

private:
    [[nodiscard]] static inline GlobalState& getMutable()
    {
        static GlobalState globalState {};
        return globalState;
    }

public:
    [[nodiscard]] static inline const GlobalState& get()
    {
        const GlobalState& nonMutableGlobalState = getMutable();
        return nonMutableGlobalState;
    }

    [[nodiscard]] bool applicationRunning() const
    {
        return m_applicationRunning;
    }

    void setApplicationRunning(bool running)
    {
        m_applicationRunning = running;
    }

private:
    GlobalState() = default;
    bool m_applicationRunning { true };
};
