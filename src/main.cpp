#include "backend/Backend.h"
#include "backend/vulkan/VulkanBackend.h"
#include "rendering/App.h"
#include "rendering/ResourceManager.h"
#include "rendering/StaticResourceManager.h"
#include "utility/GlobalState.h"
#include "utility/logging.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/TestApp.h"

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

std::unique_ptr<Backend> createBackend(BackendType backendType, GLFWwindow* window, App& app)
{
    std::unique_ptr<Backend> backend;

    switch (backendType) {
    case BackendType::Vulkan:
        backend = std::make_unique<VulkanBackend>(window, app);
        break;
    }

    glfwSetWindowUserPointer(window, backend.get());
    return backend;
}

int main()
{
    if (!glfwInit()) {
        LogErrorAndExit("ArkoseRenderer::main(): could not initialize GLFW, exiting.\n");
    }

    BackendType backendType = BackendType::Vulkan;
    GLFWwindow* window = createWindow(backendType, WindowType::Windowed, { 1200, 800 });

    {
        auto app = std::make_unique<TestApp>();
        auto backend = createBackend(backendType, window, *app);

        LogInfo("ArkoseRenderer: main loop begin.\n");

        // TODO: It's (probably) important that this is set to true before any frontend code is run,
        //  in case it has logic for stopping if the application exits etc.
        GlobalState::getMutable().setApplicationRunning(true);

        glfwSetTime(0.0);
        double lastTime = 0.0;
        while (!glfwWindowShouldClose(window)) {

            glfwPollEvents();

            double elapsedTime = glfwGetTime();
            double deltaTime = elapsedTime - lastTime;
            lastTime = elapsedTime;

            bool frameExecuted = false;
            while (!frameExecuted) {
                frameExecuted = backend->executeFrame(elapsedTime, deltaTime);
            }
        }
        GlobalState::getMutable().setApplicationRunning(false);
        LogInfo("ArkoseRenderer: main loop end.\n");
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
