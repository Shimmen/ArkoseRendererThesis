#include "backend/Backend.h"
#include "backend/vulkan/VulkanBackend.h"
#include "utility/GlobalState.h"
#include "utility/logging.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

enum class BackendType {
    Vulkan
};

int main()
{
    if (!glfwInit()) {
        LogErrorAndExit("ArkoseRenderer::main(): could not initialize GLFW, exiting.\n");
    }

    BackendType backendType = BackendType::Vulkan;
    uint32_t windowWidth = 1200;
    uint32_t windowHeight = 800;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "Arkose Renderer", nullptr, nullptr);
    if (!window) {
        LogErrorAndExit("ArkoseRenderer::main(): could not create GLFW window with specified settings, exiting.\n");
    }

    Backend* backend = nullptr;
    {
        switch (backendType) {
        case BackendType::Vulkan:
            LogInfo("ArkoseRenderer: using the Vulkan backend.\n");
            if (!glfwVulkanSupported()) {
                LogErrorAndExit("ArkoseRenderer: Vulkan is not supported but the Vulkan backend is requested. Exiting.\n");
            }
            backend = new VulkanBackend(window);
            glfwSetWindowUserPointer(window, backend);
            break;
        }

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
    }
    delete backend;

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
