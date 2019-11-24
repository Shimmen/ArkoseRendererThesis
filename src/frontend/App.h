#pragma once

class App {

public:
    virtual void setup() = 0;
    virtual void resize() = 0;
    virtual void render() = 0;

private:
    int m_screenWidth;
    int m_screenHeight;
};
