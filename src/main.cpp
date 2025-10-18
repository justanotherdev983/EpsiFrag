#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vulkan/vulkan_core.h>
#include <stdint.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

//#include "vulkan/vulkan.h"

#define WIDTH 480
#define HEIGHT 480

// Candy is our renderer
typedef struct {
    // We need our vulkan specific context here, so our swap chain, our shaders, pipeline ETC
    void* frag_shader;
    void* vert_shader;

    GLFWwindow* window;
    VkInstance instance;

} Candy;



void candy_init(Candy* candy) {

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    candy->window = window;

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Hello EpsiFrag triangle",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0), // TODO: make the version in the struct
        .pEngineName = "No engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    const char** glfwExtensions;
    uint32_t glfwExtensionCount = 0;

    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = glfwExtensionCount,
        .ppEnabledExtensionNames = glfwExtensions,
    };

    


    VkResult result = vkCreateInstance(&create_info, nullptr, &candy->instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create vulkan instance");
    }

}

void candy_loop(Candy* candy) {
    while (!glfwWindowShouldClose(candy->window)) {
        glfwPollEvents();
    }

}

void candy_cleanup(Candy* candy) {
    vkDestroyInstance(candy->instance, nullptr);
    
    glfwDestroyWindow(candy->window);
    glfwTerminate();
}


int main () {
    std::cout << "Hello triangles\n";

    Candy candy;

    candy_init(&candy);

    candy_loop(&candy);
    candy_cleanup(&candy);
    
    return 0;
}

