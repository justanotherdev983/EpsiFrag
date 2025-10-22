#include <algorithm>
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

// Device extensions required for rendering
constexpr const char *DEVICE_EXTENSIONS[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
constexpr size_t DEVICE_EXTENSION_COUNT = 1;

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
    VkSwapchainKHR swapchain;
};

// Hot data - accessed every frame (cache-line aligned)
struct alignas(64) candy_frame_data {
    GLFWwindow *window;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
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
    uint32_t present_queue_families[16]; // ADD: store present queue families too
    uint32_t count;
};

// Helper for storing queue family indices
struct candy_queue_family_indices {
    uint32_t graphics_family;
    uint32_t present_family;
};

// Helper for swapchain init
struct candy_swapchain_support_details {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR formats[32];
    uint32_t format_count;
    VkPresentModeKHR present_modes[16];
    uint32_t present_mode_count;
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

bool candy_queue_families_is_complete(const candy_queue_family_indices *indices) {
    return indices->graphics_family != INVALID_QUEUE_FAMILY &&
           indices->present_family != INVALID_QUEUE_FAMILY;
}

candy_queue_family_indices candy_find_queue_families(VkPhysicalDevice device,
                                                     VkSurfaceKHR surface) {
    candy_queue_family_indices indices = {
        .graphics_family = INVALID_QUEUE_FAMILY,
        .present_family = INVALID_QUEUE_FAMILY,
    };

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    // Limit to a reasonable number to stay on the stack
    if (queue_family_count > 32)
        queue_family_count = 32;
    VkQueueFamilyProperties queue_families[32];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

    for (uint32_t i = 0; i < queue_family_count; ++i) {
        // Check for graphics support
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
        }

        // Check for presentation support
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
        if (present_support) {
            indices.present_family = i;
        }

        // If we've found everything we need, we can stop searching
        if (candy_queue_families_is_complete(&indices)) {
            break;
        }
    }

    return indices;
}

// ============================================================================
// SWAPCHAIN
// ============================================================================

void candy_queury_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface,
                                    candy_swapchain_support_details *details) {
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details->capabilities);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details->format_count,
                                         nullptr);
    if (details->format_count > 32) {
        details->format_count = 32;
    }
    if (details->format_count != 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &details->format_count,
                                             details->formats);
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                              &details->present_mode_count, nullptr);
    if (details->present_mode_count > 16) {
        details->present_mode_count = 16;
    }
    if (details->present_mode_count != 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface, &details->present_mode_count, details->present_modes);
    }
}

void candy_choose_swap_surface_format(VkSurfaceFormatKHR *surface_fmt,
                                      const VkSurfaceFormatKHR *available_formats,
                                      uint32_t format_count) {

    for (uint32_t i = 0; i < format_count; ++i) {
        if (available_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            available_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            *surface_fmt = available_formats[i];
            return;
        }
    }

    // If that fails we will just use the first available one
    *surface_fmt = available_formats[0];
}

void candy_choose_swap_present_mode(VkPresentModeKHR *present_mode,
                                    const VkPresentModeKHR *available_present_modes,
                                    uint32_t present_mode_count) {
    for (uint32_t i = 0; i < present_mode_count; ++i) {
        if (available_present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            *present_mode =
                VK_PRESENT_MODE_MAILBOX_KHR; // Probably best, the triple buffer solution
            return;
        }
    }

    *present_mode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed to be available if other fails
}

void candy_choose_swap_extent(VkExtent2D *extent,
                              const VkSurfaceCapabilitiesKHR &capabilities,
                              const GLFWwindow *window) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        *extent = capabilities.currentExtent;
    } else {
        int width;
        int height;

        glfwGetFramebufferSize((GLFWwindow *)window, &width, &height);

        extent->width = (uint32_t)width;
        extent->height = (uint32_t)height;

        extent->width = std::clamp(extent->width, capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        extent->height = std::clamp(extent->height, capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);
    }
}

// ============================================================================
// DEVICE SELECTION
// ============================================================================

bool candy_check_device_extension_support(VkPhysicalDevice device) {
    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    if (extension_count == 0)
        return false;

    // Limit to reasonable number for stack allocation
    if (extension_count > 256)
        extension_count = 256;

    VkExtensionProperties available_extensions[256];
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                         available_extensions);

    // Check if all required extensions are available
    for (size_t i = 0; i < DEVICE_EXTENSION_COUNT; ++i) {
        bool found = false;
        for (uint32_t j = 0; j < extension_count; ++j) {
            if (strcmp(DEVICE_EXTENSIONS[i], available_extensions[j].extensionName) ==
                0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }

    return true;
}

void candy_find_physical_devices(VkInstance instance, VkSurfaceKHR surface,
                                 candy_device_list *devices) {
    devices->count = 0;

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count == 0)
        return;

    if (device_count > 16)
        device_count = 16;
    vkEnumeratePhysicalDevices(instance, &device_count, devices->handles);

    // Find queue families for each device
    for (uint32_t i = 0; i < device_count; ++i) {
        candy_queue_family_indices indices =
            candy_find_queue_families(devices->handles[i], surface);
        devices->graphics_queue_families[i] = indices.graphics_family;
        devices->present_queue_families[i] = indices.present_family;
    }
    devices->count = device_count;
}

bool candy_is_device_suitable(VkPhysicalDevice device, uint32_t graphics_family,
                              uint32_t present_family, VkSurfaceKHR surface) {
    // Check if queue families are valid
    bool has_queue_families = (graphics_family != INVALID_QUEUE_FAMILY &&
                               present_family != INVALID_QUEUE_FAMILY);

    // Check if device supports required extensions
    bool extensions_supported = candy_check_device_extension_support(device);

    bool is_swapchain_adequete = false;
    if (extensions_supported) {
        candy_swapchain_support_details swapchain_support = {};
        candy_queury_swapchain_support(device, surface, &swapchain_support);

        is_swapchain_adequete = (swapchain_support.format_count > 0 &&
                                 swapchain_support.present_mode_count > 0);
    }

    return has_queue_families && extensions_supported && is_swapchain_adequete;
}

uint32_t candy_pick_best_device(const candy_device_list *devices, VkSurfaceKHR surface) {
    for (uint32_t i = 0; i < devices->count; ++i) {
        if (candy_is_device_suitable(devices->handles[i],
                                     devices->graphics_queue_families[i],
                                     devices->present_queue_families[i], surface)) {
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

void candy_init_surface(candy_frame_data *frame_data,
                        const candy_vk_instance *vk_instance) {
    VkResult result = glfwCreateWindowSurface(vk_instance->instance, frame_data->window,
                                              nullptr, &frame_data->surface);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create window surface");
}

void candy_init_physical_device(candy_frame_data *frame_data,
                                const candy_vk_instance *vk_instance) {
    candy_device_list devices = {};
    candy_find_physical_devices(vk_instance->instance, frame_data->surface, &devices);

    CANDY_ASSERT(devices.count > 0, "No GPUs with Vulkan support found");

    uint32_t best = candy_pick_best_device(&devices, frame_data->surface);
    CANDY_ASSERT(best != INVALID_QUEUE_FAMILY, "No suitable GPU found");

    frame_data->physical_device = devices.handles[best];
    frame_data->graphics_queue_family = devices.graphics_queue_families[best];
    frame_data->present_queue_family = devices.present_queue_families[best];
}

void candy_init_logical_device(candy_frame_data *frame_data, const candy_config *config) {
    // We need to create queue infos for unique queue families
    uint32_t unique_queue_families[2];
    uint32_t unique_count = 0;

    unique_queue_families[unique_count++] = frame_data->graphics_queue_family;

    // Only add present family if it's different from graphics family
    if (frame_data->present_queue_family != frame_data->graphics_queue_family) {
        unique_queue_families[unique_count++] = frame_data->present_queue_family;
    }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2];

    for (uint32_t i = 0; i < unique_count; ++i) {
        queue_infos[i] = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = unique_queue_families[i],
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };
    }

    VkPhysicalDeviceFeatures device_features = {};

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = unique_count,
        .pQueueCreateInfos = queue_infos,
        .enabledLayerCount =
            config->enable_validation ? (uint32_t)VALIDATION_LAYER_COUNT : 0u,
        .ppEnabledLayerNames = config->enable_validation ? VALIDATION_LAYERS : nullptr,
        .enabledExtensionCount = DEVICE_EXTENSION_COUNT,
        .ppEnabledExtensionNames = DEVICE_EXTENSIONS,
        .pEnabledFeatures = &device_features,
    };

    VkResult result = vkCreateDevice(frame_data->physical_device, &create_info, nullptr,
                                     &frame_data->logical_device);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create logical device");

    // Get both queue handles
    vkGetDeviceQueue(frame_data->logical_device, frame_data->graphics_queue_family, 0,
                     &frame_data->graphics_queue);
    vkGetDeviceQueue(frame_data->logical_device, frame_data->present_queue_family, 0,
                     &frame_data->present_queue);
}

void candy_init_swapchain(VkDevice device, VkPhysicalDevice physical_device,
                          VkSurfaceKHR surface, const GLFWwindow *window,
                          candy_config *config) {
    candy_swapchain_support_details swapchain_details = {};
    candy_queury_swapchain_support(physical_device, surface, &swapchain_details);

    VkSurfaceFormatKHR surface_fmt = {};
    candy_choose_swap_surface_format(&surface_fmt, swapchain_details.formats,
                                     swapchain_details.format_count);

    VkPresentModeKHR present_mode = {};
    candy_choose_swap_present_mode(&present_mode, swapchain_details.present_modes,
                                   swapchain_details.present_mode_count);

    VkExtent2D extent = {};
    candy_choose_swap_extent(&extent, swapchain_details.capabilities,
                             (GLFWwindow *)window);

    uint32_t image_count = swapchain_details.capabilities.minImageCount +
                           1; // +1 bcuz if its minImageCount we have to wait on driver

    if (swapchain_details.capabilities.maxImageCount > 0 &&
        image_count > swapchain_details.capabilities.maxImageCount) {
        image_count = swapchain_details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = surface_fmt.format,
        .imageColorSpace = surface_fmt.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,                             // We dont need other
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // Bcuz we dont do things like
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = swapchain_details.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE, // For now assume only create 1 swapchain

    };
    candy_queue_family_indices indices =
        candy_find_queue_families(physical_device, surface);
    uint32_t queue_family_indicies[] = {indices.graphics_family, indices.present_family};

    if (indices.graphics_family != indices.present_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indicies;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
    }
    VkResult result =
        vkCreateSwapchainKHR(device, &create_info, nullptr, &config->swapchain);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create swapchain");
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
        .swapchain = VK_NULL_HANDLE,
    };

    // Init GLFW
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    candy->frame_data.window =
        glfwCreateWindow(candy->config.width, candy->config.height,
                         candy->config.window_title, nullptr, nullptr);

    CANDY_ASSERT(candy->frame_data.window != nullptr, "Failed to create window");

    // Init Vulkan - ORDER MATTERS!
    candy_init_vulkan_instance(&candy->vk_instance, &candy->config);
    candy_init_surface(&candy->frame_data,
                       &candy->vk_instance); // Create surface before picking device
    candy_init_physical_device(&candy->frame_data, &candy->vk_instance);
    candy_init_logical_device(&candy->frame_data, &candy->config);
    candy_init_swapchain(candy->frame_data.logical_device,
                         candy->frame_data.physical_device, candy->frame_data.surface,
                         (const GLFWwindow *)candy->frame_data.window, &candy->config);

    std::cout << "[CANDY] Init complete\n";
}

void candy_cleanup(candy_renderer *candy) {
    vkDestroySwapchainKHR(candy->frame_data.logical_device, candy->config.swapchain,
                          nullptr);
    vkDestroyDevice(candy->frame_data.logical_device, nullptr);

    if (candy->config.enable_validation) {
        candy_destroy_debug_messenger(candy->vk_instance.instance,
                                      candy->vk_instance.debug_messenger);
    }
    vkDestroySurfaceKHR(candy->vk_instance.instance, candy->frame_data.surface, nullptr);
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
