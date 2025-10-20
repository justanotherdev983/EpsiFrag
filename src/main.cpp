#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// #include "vulkan/vulkan.h"

const std::vector<const char *> validation_layers = {"VK_LAYER_KHRONOS_validation"};

typedef struct candy_config {
    uint32_t width = 480;
    uint32_t height = 480;

#ifdef NDEBUG
    const bool enable_validation_layers = false;
#else
    const bool enable_validation_layers = true;
#endif
} candy_config;

typedef struct {
    GLFWwindow *window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
} candy_ctx;

// Candy is our renderer
typedef struct {
    // We need our vulkan specific context here, so our swap chain, our shaders,
    // pipeline ETC
    void *frag_shader;
    void *vert_shader;

    candy_config cfg;
    candy_ctx ctx;

} Candy;

// It is not automatically loaded. We have to look up its address ourselves using
// vkGetInstanceProcAddr
VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

// TODO: determine what needs to be logged, what severity etc
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data, void *p_user_data) {

    (void)message_severity;
    (void)message_type;
    (void)p_user_data;

    std::cerr << "validation layer: " << p_callback_data->pMessage << std::endl;

    return VK_FALSE;
}

bool check_validation_layer_support() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const char *layer_name : validation_layers) {
        bool layer_found = false;

        for (const auto &layer_props : available_layers) {
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

std::vector<const char *> get_required_extensions(const candy_config *cfg) {
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> extensions(glfwExtensions,
                                         glfwExtensions + glfwExtensionCount);

    if (cfg->enable_validation_layers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

void candy_init(Candy *candy) {

    // Check if we use compiler enabled debug
    if (candy->cfg.enable_validation_layers && !check_validation_layer_support()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow *window =
        glfwCreateWindow(candy->cfg.width, candy->cfg.height, "Vulkan", nullptr, nullptr);
    candy->ctx.window = window;

    // Creating our vulkan instance
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "Hello EpsiFrag triangle",
        .applicationVersion =
            VK_MAKE_VERSION(1, 0, 0), // TODO: make the version in the struct
        .pEngineName = "No engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    std::vector<const char *> extensions = get_required_extensions(&candy->cfg);
    VkInstanceCreateInfo create_instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    if (candy->cfg.enable_validation_layers) {
        create_instance_info.enabledLayerCount =
            static_cast<uint32_t>(validation_layers.size());
        create_instance_info.ppEnabledLayerNames = validation_layers.data();
    } else {
        create_instance_info.enabledLayerCount = 0;
    }

    VkResult result =
        vkCreateInstance(&create_instance_info, nullptr, &candy->ctx.instance);
    if (result != VK_SUCCESS) {
        std::cerr << "Vulkan instance creation failed with error code: " << result
                  << std::endl;
        throw std::runtime_error("failed to create vulkan instance");
    }

    // Setup for debug_messenger
    VkDebugUtilsMessengerCreateInfoEXT create_messenger_info {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = nullptr,
    };
    if (CreateDebugUtilsMessengerEXT(candy->ctx.instance, &create_messenger_info, nullptr,
                                     &candy->ctx.debug_messenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void candy_loop(Candy *candy) {
    while (!glfwWindowShouldClose(candy->ctx.window)) {
        glfwPollEvents();
    }
}

void candy_cleanup(Candy *candy) {
    if (candy->cfg.enable_validation_layers) {
        DestroyDebugUtilsMessengerEXT(candy->ctx.instance, candy->ctx.debug_messenger,
                                      nullptr);
    }
    vkDestroyInstance(candy->ctx.instance, nullptr);

    glfwDestroyWindow(candy->ctx.window);
    glfwTerminate();
}

int main() {
    std::cout << "Hello triangles\n";

    Candy candy;

    candy_init(&candy);

    candy_loop(&candy);
    candy_cleanup(&candy);

    return 0;
}
