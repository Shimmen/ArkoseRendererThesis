#pragma once

#include "utility/Badge.h"
#include "utility/util.h"

class App;

class Backend {
public:
    Backend() = default;
    virtual ~Backend() = default;

    virtual bool executeFrame(double elapsedTime, double deltaTime) = 0;

    App& app()
    {
        return *m_app;
    }
    void setApp(App* app)
    {
        ASSERT(!m_app);
        m_app = app;
    }

    virtual int multiplicity() const = 0;

protected:
    [[nodiscard]] static Badge<Backend> backendBadge()
    {
        return {};
    }

private:
    App* m_app {};
};
