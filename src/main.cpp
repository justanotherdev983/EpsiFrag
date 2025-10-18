#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vulkan/vulkan_core.h>
#include <stdint.h>
#include <vector>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

//#include "vulkan/vulkan.h"

#define WIDTH 480
#define HEIGHT 480

const std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
    const bool enable_validation_layers = false;
#else
    const bool enable_validation_layers = true;
#endif

// Candy is our renderer
typedef struct {
    // We need our vulkan specific context here, so our swap chain, our shaders, pipeline ETC
    void* frag_shader;
    void* vert_shader;

    GLFWwindow* window;
    VkInstance instance;

} Candy;

bool checkValidationLayerSupport() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char* layer_name: validation_layers) {
        bool layer_found = false;

        for (const auto& layer_props : available_layers) {
            if (strcmp(layer_name, layer_props.layerName) == 0) {
                layer_found = true;
                break;
            }
        }
        if (!layer_found) {
            return false;
        }
    }

    return true;
}

void candy_init(Candy* candy) {
    
    if (enable_validation_layers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

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
    if (enable_validation_layers) {
        create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
        create_info.ppEnabledLayerNames = validation_layers.data();
    } else {
        create_info.enabledLayerCount = 0;
    }

    


    VkResult result = vkCreateInstance(&create_info, nullptr, &candy->instance);
    if (result != VK_SUCCESS) {
        std::cerr << "Vulkan instance creation failed with error code: " << result << std::endl;
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

