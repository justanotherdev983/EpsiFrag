#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <dlfcn.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector> // For reading our shader files

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

#define VKFFT_BACKEND 0
#include "vkFFT.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

// ============================================================================
// ERROR HANDLING
// ============================================================================

#define CANDY_ASSERT(condition, message)                                                 \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::cerr << "[CANDY ASSERT FAILED] " << message << std::endl                \
                      << "at function: " << __FUNCTION__ << std::endl                    \
                      << "in file: " << __FILE__ << std::endl                            \
                      << "at line: " << __LINE__ << std::endl;                           \
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
//  FORWARD DECLARATIONS
// ============================================================================

struct candy_context;

// ============================================================================
// CANDY DATA STRUCTURES
// ============================================================================

// Cold data - only used during initialization
struct candy_config {
    uint32_t width;
    uint32_t height;
    bool enable_validation;
    bool enable_hot_reloading;
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

    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_buffer_memory;
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
    bool has_framebuffer_resized = false;
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

// Compute pipeline structures
struct candy_compute_pipeline {
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkPipelineLayout pipeline_layouts[4];
    VkPipeline pipelines[4]; // first_half_kin, full_potential, last_half_kin, visualize
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkDescriptorSet descriptor_sets[4];

    VkBuffer psi_freq_buffer;
    VkDeviceMemory psi_freq_memory;
    VkBuffer kinetic_factor_buffer;
    VkDeviceMemory kinetic_factor_memory;
    VkBuffer potential_factor_buffer;
    VkDeviceMemory potential_factor_memory;

    VkFFTConfiguration fft_config;
    VkFFTApplication fft_app_forward;
    VkFFTApplication fft_app_inverse;
    VkBuffer fft_buffer;
    VkDeviceMemory fft_buffer_memory;

    uint64_t buffer_size;
};

struct candy_game_api {
    void (*init)(candy_context *ctx, void *game_state);
    void (*update)(candy_context *ctx, void *game_state, uint32_t delta_time);
    void (*render)(candy_context *ctx, void *game_state);
    void (*cleanup)(candy_context *ctx, void *game_state);

    void (*on_reload)(void *old_state, void *new_state);
    size_t state_size;
};

struct candy_game_module {
    void *dll_handle;
    candy_game_api api;
    void *game_state;

    time_t last_write_time;
    uint32_t reload_count;
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
    candy_compute_pipeline compute;

    // --- Hot Data ---
    candy_frame_data frame_data;

    // --- Hot reload ---
    candy_game_module game_module;
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

struct candy_vertex {
    glm::vec2 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription get_bindings_description() {

        VkVertexInputBindingDescription binding_description {
            .binding = 0,
            .stride = sizeof(candy_vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX}; // we wont use instanced rendering

        return binding_description;
    };

    static std::array<VkVertexInputAttributeDescription, 2> get_attribute_description() {
        std::array<VkVertexInputAttributeDescription, 2> attribute_description {};

        attribute_description[0].binding = 0;
        attribute_description[0].location = 0;
        attribute_description[0].format = VK_FORMAT_R32G32_SFLOAT;
        attribute_description[0].offset = offsetof(candy_vertex, pos);

        attribute_description[1].binding = 0;
        attribute_description[1].location = 1;
        attribute_description[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attribute_description[1].offset = offsetof(candy_vertex, color);

        return attribute_description;
    }
};
const std::vector<candy_vertex> vertices = {{{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                            {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},
                                            {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}};
void candy_recreate_swapchain(candy_context *ctx);

void candy_destroy_swapchain(candy_context *ctx);
candy_queue_family_indices candy_find_queue_families(VkPhysicalDevice device,
                                                     VkSurfaceKHR surface);
static std::vector<char> candy_read_shader_file(const std::string &filename);
uint32_t candy_find_memory_type(candy_context *ctx, uint32_t type_filter,
                                VkMemoryPropertyFlags props);

void candy_upload_compute_data(candy_context *ctx, void *state);
void candy_init_vkfft(candy_context *ctx);
void candy_init_compute_pipeline(candy_context *ctx);
