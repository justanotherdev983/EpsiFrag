#include <cassert>
#include <cstdint>
#include <iostream>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_core.h>

// ============================================================================
// ERROR HANDLING
// ============================================================================

#define CANDY_ASSERT(condition, message)                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::cerr << "[CANDY ASSERT FAILED] " << message << std::endl;               \
            assert(condition);                                                           \
        }                                                                                \
    } while (0)

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr const char *VALIDATION_LAYERS[] = {"VK_LAYER_KHRONOS_validation"};
constexpr size_t VALIDATION_LAYER_COUNT = 1;
constexpr uint32_t INVALID_QUEUE_FAMILY = UINT32_MAX;

#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION = false;
#else
constexpr bool ENABLE_VALIDATION = true;
#endif

// ============================================================================
// CANDY DATA STRUCTURES
// ============================================================================

// Cold data - only used during initialization
struct candy_config {
    uint32_t width;
    uint32_t height;
    bool enable_validation;
    const char *app_name;
    const char *window_title;
};

// Hot data - accessed every frame (cache-line aligned)
struct alignas(64) candy_frame_data {
    GLFWwindow *window;
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family;
    char _padding[28]; // Explicit padding to prevent false sharing
};

// Vulkan instance handles
struct candy_vk_instance {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
};

// Main candy renderer
struct candy_renderer {
    candy_frame_data frame_data;
    candy_vk_instance vk_instance;
    candy_config config;
};

// Helper for device selection
struct candy_device_list {
    VkPhysicalDevice handles[16];
    uint32_t graphics_queue_families[16];
    uint32_t count;
};

// ============================================================================
// DEBUG CALLBACKS
// ============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL candy_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data, void *user_data) {

    (void)severity;
    (void)type;
    (void)user_data;

    std::cerr << "[CANDY VALIDATION] " << callback_data->pMessage << std::endl;
    return VK_FALSE;
}

VkResult
candy_create_debug_messenger(VkInstance instance,
                             const VkDebugUtilsMessengerCreateInfoEXT *create_info,
                             VkDebugUtilsMessengerEXT *messenger) {

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");

    return func ? func(instance, create_info, nullptr, messenger)
                : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void candy_destroy_debug_messenger(VkInstance instance,
                                   VkDebugUtilsMessengerEXT messenger) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");

    if (func) {
        func(instance, messenger, nullptr);
    }
}

VkDebugUtilsMessengerCreateInfoEXT candy_make_debug_messenger_info() {
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = candy_debug_callback,
        .pUserData = nullptr,
    };
}

// ============================================================================
// VALIDATION LAYERS
// ============================================================================

bool candy_check_validation_layers() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    VkLayerProperties available_layers[64];
    if (layer_count > 64)
        layer_count = 64;

    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);

    for (size_t i = 0; i < VALIDATION_LAYER_COUNT; ++i) {
        bool found = false;
        for (uint32_t j = 0; j < layer_count; ++j) {
            if (strcmp(VALIDATION_LAYERS[i], available_layers[j].layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

// ============================================================================
// EXTENSIONS
// ============================================================================

void candy_get_required_extensions(const char **out_extensions, uint32_t *out_count,
                                   bool enable_validation) {
    uint32_t glfw_count = 0;
    const char **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_count);

    for (uint32_t i = 0; i < glfw_count; ++i) {
        out_extensions[i] = glfw_extensions[i];
    }
    *out_count = glfw_count;

    if (enable_validation) {
        out_extensions[(*out_count)++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }
}

// ============================================================================
// QUEUE FAMILIES
// ============================================================================

uint32_t candy_find_graphics_queue_family(VkPhysicalDevice device) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    VkQueueFamilyProperties queue_families[32];
    if (queue_family_count > 32)
        queue_family_count = 32;

    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            return i;
        }
    }
    return INVALID_QUEUE_FAMILY;
}

// ============================================================================
// DEVICE SELECTION
// ============================================================================

void candy_find_physical_devices(VkInstance instance, candy_device_list *devices) {
    devices->count = 0;

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count == 0)
        return;

    if (device_count > 16)
        device_count = 16;
    vkEnumeratePhysicalDevices(instance, &device_count, devices->handles);

    // Find graphics queue for each device
    for (uint32_t i = 0; i < device_count; ++i) {
        devices->graphics_queue_families[i] =
            candy_find_graphics_queue_family(devices->handles[i]);
    }
    devices->count = device_count;
}

uint32_t candy_pick_best_device(const candy_device_list *devices) {
    for (uint32_t i = 0; i < devices->count; ++i) {
        if (devices->graphics_queue_families[i] != INVALID_QUEUE_FAMILY) {
            return i;
        }
    }
    return INVALID_QUEUE_FAMILY;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void candy_init_vulkan_instance(candy_vk_instance *vk_instance,
                                const candy_config *config) {
    if (config->enable_validation) {
        CANDY_ASSERT(candy_check_validation_layers(), "Validation layers not available");
    }

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = config->app_name,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Candy Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    const char *extensions[32];
    uint32_t extension_count = 0;
    candy_get_required_extensions(extensions, &extension_count,
                                  config->enable_validation);

    VkDebugUtilsMessengerCreateInfoEXT debug_info = candy_make_debug_messenger_info();

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = config->enable_validation ? &debug_info : nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount =
            config->enable_validation ? (uint32_t)VALIDATION_LAYER_COUNT : 0u,
        .ppEnabledLayerNames = config->enable_validation ? VALIDATION_LAYERS : nullptr,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };

    VkResult result = vkCreateInstance(&create_info, nullptr, &vk_instance->instance);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create Vulkan instance");

    if (config->enable_validation) {
        VkDebugUtilsMessengerCreateInfoEXT messenger_info =
            candy_make_debug_messenger_info();
        result = candy_create_debug_messenger(vk_instance->instance, &messenger_info,
                                              &vk_instance->debug_messenger);
        CANDY_ASSERT(result == VK_SUCCESS, "Failed to setup debug messenger");
    }
}

void candy_init_physical_device(candy_frame_data *frame_data,
                                const candy_vk_instance *vk_instance) {
    candy_device_list devices = {};
    candy_find_physical_devices(vk_instance->instance, &devices);

    CANDY_ASSERT(devices.count > 0, "No GPUs with Vulkan support found");

    uint32_t best = candy_pick_best_device(&devices);
    CANDY_ASSERT(best != INVALID_QUEUE_FAMILY, "No suitable GPU found");

    frame_data->physical_device = devices.handles[best];
    frame_data->graphics_queue_family = devices.graphics_queue_families[best];
}

void candy_init_logical_device(candy_frame_data *frame_data, const candy_config *config) {
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = frame_data->graphics_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    VkPhysicalDeviceFeatures device_features = {};

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .enabledLayerCount =
            config->enable_validation ? (uint32_t)VALIDATION_LAYER_COUNT : 0u,
        .ppEnabledLayerNames = config->enable_validation ? VALIDATION_LAYERS : nullptr,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = nullptr,
        .pEnabledFeatures = &device_features,
    };

    VkResult result = vkCreateDevice(frame_data->physical_device, &create_info, nullptr,
                                     &frame_data->logical_device);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create logical device");

    vkGetDeviceQueue(frame_data->logical_device, frame_data->graphics_queue_family, 0,
                     &frame_data->graphics_queue);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void candy_init(candy_renderer *candy) {
    candy->config = {
        .width = 800,
        .height = 600,
        .enable_validation = ENABLE_VALIDATION,
        .app_name = "Candy Renderer",
        .window_title = "Candy Window",
    };

    // Init GLFW
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    candy->frame_data.window =
        glfwCreateWindow(candy->config.width, candy->config.height,
                         candy->config.window_title, nullptr, nullptr);

    CANDY_ASSERT(candy->frame_data.window != nullptr, "Failed to create window");

    // Init Vulkan
    candy_init_vulkan_instance(&candy->vk_instance, &candy->config);
    candy_init_physical_device(&candy->frame_data, &candy->vk_instance);
    candy_init_logical_device(&candy->frame_data, &candy->config);

    std::cout << "[CANDY] Init complete\n";
}

void candy_cleanup(candy_renderer *candy) {
    vkDestroyDevice(candy->frame_data.logical_device, nullptr);

    if (candy->config.enable_validation) {
        candy_destroy_debug_messenger(candy->vk_instance.instance,
                                      candy->vk_instance.debug_messenger);
    }

    vkDestroyInstance(candy->vk_instance.instance, nullptr);
    glfwDestroyWindow(candy->frame_data.window);
    glfwTerminate();

    std::cout << "[CANDY] Cleanup complete\n";
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "[CANDY] Starting...\n";

    candy_renderer candy = {};

    candy_init(&candy);

    while (!glfwWindowShouldClose(candy.frame_data.window)) {
        glfwPollEvents();
    }

    candy_cleanup(&candy);

    return 0;
}
