#include "backend/Backend.h"
#include "backend/vulkan/VulkanBackend.h"
#include "utility/GlobalState.h"
#include "utility/logging.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

enum class BackendType {
    Vulkan
};

enum class WindowType {
    Windowed,
    Fullscreen
};

GLFWwindow* createWindow(BackendType backendType, WindowType windowType, Extent2D windowSize)
{
    std::string windowTitle = "Arkose Renderer";

    switch (backendType) {
    case BackendType::Vulkan:
        if (!glfwVulkanSupported()) {
            LogErrorAndExit("ArkoseRenderer::createWindow(): Vulkan is not supported but the Vulkan backend is requested, exiting.\n");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        windowTitle += " [Vulkan]";
        break;
    }

    GLFWwindow* window = nullptr;

    switch (windowType) {
    case WindowType::Fullscreen: {
        GLFWmonitor* defaultMonitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* defaultVideoMode = glfwGetVideoMode(defaultMonitor);
        window = glfwCreateWindow(defaultVideoMode->width, defaultVideoMode->height, windowTitle.c_str(), defaultMonitor, nullptr);
        break;
    }
    case WindowType::Windowed: {
        window = glfwCreateWindow(windowSize.width(), windowSize.height(), windowTitle.c_str(), nullptr, nullptr);
        break;
    }
    }

    if (!window) {
        LogErrorAndExit("ArkoseRenderer::createWindow(): could not create GLFW window with specified settings, exiting.\n");
    }

    return window;
}

Backend* createBackend(BackendType backendType, GLFWwindow* window)
{
    Backend* backend;

    switch (backendType) {
    case BackendType::Vulkan:
        backend = new VulkanBackend(window);
        break;
    }

    glfwSetWindowUserPointer(window, backend);
    return backend;
}

int main()
{
    if (!glfwInit()) {
        LogErrorAndExit("ArkoseRenderer::main(): could not initialize GLFW, exiting.\n");
    }

    BackendType backendType = BackendType::Vulkan;
    GLFWwindow* window = createWindow(backendType, WindowType::Windowed, { 1200, 800 });
    Backend* backend = createBackend(backendType, window);

    LogInfo("ArkoseRenderer: main loop begin.\n");

    // TODO: It's (probably) important that this is set to true before any frontend code is run,
    //  in case it has logic for stopping if the application exits etc.
    GlobalState::getMutable().setApplicationRunning(true);

    while (!glfwWindowShouldClose(window)) {

        glfwPollEvents();

        bool frameExecuted = false;
        while (!frameExecuted) {
            frameExecuted = backend->executeFrame();
        }
    }
    GlobalState::getMutable().setApplicationRunning(false);
    LogInfo("ArkoseRenderer: main loop end.\n");

    delete backend;
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
