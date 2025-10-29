#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string.h>
#include <vector> // For reading our shader files

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
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
constexpr uint32_t MAX_SWAPCHAIN_IMAGES = 8;
constexpr uint32_t MAX_SHADER_MODULES = 16;

constexpr uint32_t MAX_FRAME_IN_FLIGHT = 2;

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
    VkCommandPool command_pools[MAX_FRAME_IN_FLIGHT];
    VkCommandBuffer command_buffers[MAX_FRAME_IN_FLIGHT];
    VkSemaphore image_available_semaphores[MAX_FRAME_IN_FLIGHT];
    VkSemaphore render_finished_semaphores[MAX_FRAME_IN_FLIGHT];
    VkFence in_flight_fences[MAX_FRAME_IN_FLIGHT];
    uint32_t current_frame;
};

// All core long-lived vulkan handles
struct candy_core {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;

    GLFWwindow *window;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice logical_device;

    VkQueue graphics_queue;
    VkQueue present_queue;
    uint32_t graphics_queue_family;
    uint32_t present_queue_family;
};

// This is "warm" data. This is all recreated together when the window is resized.
struct candy_swapchain {
    VkSwapchainKHR handle;
    VkFormat image_format;
    VkExtent2D extent;
    uint32_t image_count;
    uint32_t image_view_count;

    // We get the images from the swapchain, but we create the views and framebuffers.
    VkImageView image_views[MAX_SWAPCHAIN_IMAGES];
    VkFramebuffer framebuffers[MAX_SWAPCHAIN_IMAGES];
    VkImage images[MAX_SWAPCHAIN_IMAGES];
};

struct candy_pipeline {
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

    // Shader modules are only needed for pipeline creation.
    // They are effectively "cold" data for a specific pipeline.
    VkShaderModule shader_modules[MAX_SHADER_MODULES];
    uint32_t shader_module_count;
};

// ImGui-specific data (kept separate for DoD)
struct candy_imgui {
    VkDescriptorPool descriptor_pool;
    VkRenderPass render_pass;
    bool initialized;

    // Menu state data
    bool show_menu;
    float menu_alpha;
};

// Main candy context
struct candy_context {
    // --- ImGui ---
    candy_imgui imgui;

    // --- Cold Data ---
    candy_config config;

    // --- Core Systems ---
    candy_core core;

    // --- Rendering Pipeline (recreated if swapchain changes format) ---
    candy_swapchain swapchain;
    candy_pipeline pipeline;

    // --- Hot Data ---
    candy_frame_data frame_data;
};

// Helper for device selection

struct candy_device_list {
    VkPhysicalDevice handles[16];
    uint32_t graphics_queue_families[16];
    uint32_t present_queue_families[16];
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

void candy_recreate_swapchain(candy_context *ctx);

void candy_destroy_swapchain(candy_context *ctx);
