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
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore image_available_semaphore;
    VkSemaphore render_finished_semaphore;
    VkFence in_flight_fence;
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

// Main candy context
struct candy_context {
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
// COMMAND BUFFERS
// ============================================================================

void candy_create_framebuffers(candy_context *ctx) {
    if (ctx->swapchain.image_view_count > MAX_SWAPCHAIN_IMAGES) {
        ctx->swapchain.image_view_count = MAX_SWAPCHAIN_IMAGES;
    }

    for (size_t i = 0; i < ctx->swapchain.image_view_count; ++i) {
        VkImageView attachments[] = {
            ctx->swapchain.image_views[i],
        };

        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderPass = ctx->pipeline.render_pass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = ctx->swapchain.extent.width,
            .height = ctx->swapchain.extent.height,
            .layers = 1,
        };

        VkResult result = vkCreateFramebuffer(ctx->core.logical_device, &framebuffer_info,
                                              nullptr, &ctx->swapchain.framebuffers[i]);
        CANDY_ASSERT(result == VK_SUCCESS, "Failed to create framebuffer");
    }

    return;
}

void candy_create_command_pool(candy_context *ctx) {
    candy_queue_family_indices queue_family_indicies =
        candy_find_queue_families(ctx->core.physical_device, ctx->core.surface);

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_indicies.graphics_family,
    };

    VkResult result = vkCreateCommandPool(ctx->core.logical_device, &pool_info, nullptr,
                                          &ctx->frame_data.command_pool);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create command pool");
    return;
}

void candy_create_command_buffer(candy_context *ctx) {

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = ctx->frame_data.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkResult result = vkAllocateCommandBuffers(ctx->core.logical_device, &alloc_info,
                                               &ctx->frame_data.command_buffer);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create command buffer");

    return;
}

void candy_record_command_buffer(candy_context *ctx, uint32_t image_index) {

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0, // INFO: might have to change this later
        .pInheritanceInfo = nullptr,
    };

    VkResult result = vkBeginCommandBuffer(ctx->frame_data.command_buffer, &begin_info);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to being record command buffer");

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = ctx->pipeline.render_pass,
        .framebuffer = ctx->swapchain.framebuffers[image_index],
        .renderArea.offset = {0, 0},
        .renderArea.extent = ctx->swapchain.extent,
        .clearValueCount = 1,
        .pClearValues = &clear_color,
    };

    vkCmdBeginRenderPass(ctx->frame_data.command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(ctx->frame_data.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx->pipeline.graphics_pipeline);

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)ctx->swapchain.extent.width,
        .height = (float)ctx->swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(ctx->frame_data.command_buffer, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = ctx->swapchain.extent,
    };
    vkCmdSetScissor(ctx->frame_data.command_buffer, 0, 1, &scissor);

    vkCmdDraw(ctx->frame_data.command_buffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(ctx->frame_data.command_buffer);

    VkResult result_end_cmd_buf = vkEndCommandBuffer(ctx->frame_data.command_buffer);
    CANDY_ASSERT(result_end_cmd_buf == VK_SUCCESS, "Failed to record command buffer");

    return;
}

// ============================================================================
// RENDERING
// ============================================================================

void candy_create_sync_objs(candy_context *ctx) {
    VkSemaphoreCreateInfo sema_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    VkResult sema_result_img =
        vkCreateSemaphore(ctx->core.logical_device, &sema_create_info, nullptr,
                          &ctx->frame_data.image_available_semaphore);
    CANDY_ASSERT(sema_result_img == VK_SUCCESS,
                 "failed to create image available semaphore");

    VkResult sema_result_rendr =
        vkCreateSemaphore(ctx->core.logical_device, &sema_create_info, nullptr,
                          &ctx->frame_data.render_finished_semaphore);
    CANDY_ASSERT(sema_result_rendr == VK_SUCCESS,
                 "Failed to create render finished semaphore");
    VkResult fence_result = vkCreateFence(ctx->core.logical_device, &fence_create_info,
                                          nullptr, &ctx->frame_data.in_flight_fence);
    CANDY_ASSERT(fence_result == VK_SUCCESS, "Failed to create fence");

    return;
}

void candy_draw_frame(candy_context *ctx) {
    vkWaitForFences(ctx->core.logical_device, 1, &ctx->frame_data.in_flight_fence,
                    VK_TRUE, UINT32_MAX); // For DoD we need to make arra of fences and
                                          // use that instead of 1 here
    vkResetFences(ctx->core.logical_device, 1, &ctx->frame_data.in_flight_fence);

    uint32_t image_index;

    vkAcquireNextImageKHR(ctx->core.logical_device, ctx->swapchain.handle, UINT64_MAX,
                          ctx->frame_data.image_available_semaphore, VK_NULL_HANDLE,
                          &image_index); // dont know if this is correct

    vkResetCommandBuffer(ctx->frame_data.command_buffer, 0);
    candy_record_command_buffer(ctx, image_index);

    VkSemaphore wait_semaphores[] = {ctx->frame_data.image_available_semaphore};
    VkSemaphore signal_semaphores[] = {ctx->frame_data.render_finished_semaphore};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = wait_semaphores,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->frame_data.command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signal_semaphores,
    };

    VkResult result = vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info,
                                    ctx->frame_data.in_flight_fence);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to submit draw command buffer");

    VkSwapchainKHR swapchains = {ctx->swapchain.handle};

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signal_semaphores,
        .swapchainCount = 1,
        .pSwapchains = &swapchains,
        .pImageIndices = &image_index,
        .pResults = nullptr, // WARNING: we only have a single swapchain for now
    };

    vkQueuePresentKHR(ctx->core.present_queue, &present_info);

    return;
}

// ============================================================================
// GRAPHICS PIPELINE
// ============================================================================

void candy_create_render_pass(candy_context *ctx) {
    VkAttachmentDescription color_attachment = {
        .flags = 0,
        .format = ctx->swapchain.image_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,      // no multi sample yet
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // no reuse of values
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, // change later for textures
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_attachment_ref = {
        .attachment =
            0, // we only have 1 for now, later post processing but for now index 0
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    };

    VkSubpassDescription subpass = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = nullptr,
    };

    VkRenderPassCreateInfo render_pass_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VkResult result = vkCreateRenderPass(ctx->core.logical_device, &render_pass_info,
                                         nullptr, &ctx->pipeline.render_pass);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create render pass");

    return;
}

static std::vector<char> candy_read_shader_file(const std::string &filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    CANDY_ASSERT(file.is_open(), "Failed to open shader file");

    size_t file_size = (size_t)file.tellg();
    std::vector<char> buffer(file_size);
    file.seekg(0);
    file.read(buffer.data(), file_size);
    file.close();
    return buffer;
}

VkShaderModule candy_create_shader_module(const std::vector<char> &shader_code,
                                          VkDevice device) {
    if (shader_code.size() % sizeof(uint32_t) != 0) {
        // You might want to log a more specific error here
        CANDY_ASSERT(false, "Shader code size is not a multiple of 4 bytes!");
        return VK_NULL_HANDLE; // Or handle error appropriately
    }
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = shader_code.size(),
        .pCode = reinterpret_cast<const uint32_t *>(shader_code.data()),
    };
    VkShaderModule shader_module;
    VkResult result = vkCreateShaderModule(device, &create_info, nullptr, &shader_module);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create shader module");

    return shader_module;
}

void candy_create_graphics_pipeline(candy_context *candy) {
    std::vector<char> vert_shader_code =
        candy_read_shader_file("../src/shaders/simple_shader.vert.spv");
    std::vector<char> frag_shader_code =
        candy_read_shader_file("../src/shaders/simple_shader.frag.spv");

    VkShaderModule vert_shader_module =
        candy_create_shader_module(vert_shader_code, candy->core.logical_device);
    VkShaderModule frag_shader_module =
        candy_create_shader_module(frag_shader_code, candy->core.logical_device);

    CANDY_ASSERT(candy->pipeline.shader_module_count < MAX_SHADER_MODULES,
                 "Exceeded MAX_SHADER_MODULES");
    uint32_t vert_module_idx = candy->pipeline.shader_module_count;
    candy->pipeline.shader_modules[candy->pipeline.shader_module_count++] =
        vert_shader_module;

    CANDY_ASSERT(candy->pipeline.shader_module_count < MAX_SHADER_MODULES,
                 "Exceeded MAX_SHADER_MODULES");
    uint32_t frag_module_idx = candy->pipeline.shader_module_count;
    candy->pipeline.shader_modules[candy->pipeline.shader_module_count++] =
        frag_shader_module;

    VkPipelineShaderStageCreateInfo create_vert_shader_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = candy->pipeline.shader_modules[vert_module_idx],
        .pName = "main",
        .pSpecializationInfo = nullptr,
    };

    VkPipelineShaderStageCreateInfo create_frag_shader_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = candy->pipeline.shader_modules[frag_module_idx],
        .pName = "main",

        .pSpecializationInfo = nullptr,
    };

    VkPipelineShaderStageCreateInfo shader_stages[] = {create_vert_shader_info,
                                                       create_frag_shader_info};
    (void)shader_stages;

    std::vector<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        // We can in future use this for vars inside vertex shader
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        // We can use this to draw other things than triangles; set in topology
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable =
            VK_FALSE, //  possible to break up lines and triangles in the _STRIP topology
                      //  modes using index of 0xFFFF or 0xFFFFFFFF
    };

    // We use dynamic so these are currently not needed
    /*
    VkViewport viewport = {
        .x = 0.0f,

        .y = 0.0f,
        .width = (float)candy->swapchain.extent.width,
        .height = (float)candy->swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = candy->swapchain.extent,
    };
    */

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr, // this could break, bcuz we do dynamic
        .scissorCount = 1,
        .pScissors = nullptr, // this could also break due to dynamic
    };
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        // we want to fill triangle, if not, like skeleton, change here
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL, // basically draw mode
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE, // we wont use this depth constant
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        // i kinda hate anti-aliasing
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    // we curretly only have 1 framebuffer
    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .blendEnable = VK_TRUE, // can also be false for crisp
        .srcColorBlendFactor =
            VK_BLEND_FACTOR_SRC_ALPHA, // this will be VK_BLEND_FACTOR_ONE if false
        .dstColorBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // this will be VK_BLEND_FACTOR_ZERO then
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,

    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},

    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };

    VkResult result =
        vkCreatePipelineLayout(candy->core.logical_device, &pipeline_layout_info, nullptr,
                               &candy->pipeline.pipeline_layout);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create pipeline layout");

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = candy->pipeline.pipeline_layout,
        .renderPass = candy->pipeline.render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,

        .basePipelineIndex = -1,
    };

    VkResult result_pipelines = vkCreateGraphicsPipelines(
        candy->core.logical_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
        &candy->pipeline.graphics_pipeline);
    CANDY_ASSERT(result_pipelines == VK_SUCCESS, "Failed to create pipeline layout");

    vkDestroyShaderModule(candy->core.logical_device, vert_shader_module, nullptr);
    vkDestroyShaderModule(candy->core.logical_device, frag_shader_module, nullptr);
    candy->pipeline.shader_module_count = 0;

    return;
}

// ============================================================================
// SWAPCHAIN
// ============================================================================

void candy_create_image_views(candy_context *ctx) {
    if (ctx->swapchain.image_count > MAX_SWAPCHAIN_IMAGES) {
        ctx->swapchain.image_count = MAX_SWAPCHAIN_IMAGES;
    }
    for (size_t i = 0; i < ctx->swapchain.image_count; ++i) {
        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = ctx->swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx->swapchain.image_format,
            .components =
                {
                    // Initialize the VkComponentMapping struct directly
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    // Initialize the VkImageSubresourceRange struct directly
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        VkResult result = vkCreateImageView(ctx->core.logical_device, &create_info,
                                            nullptr, &ctx->swapchain.image_views[i]);
        CANDY_ASSERT(result == VK_SUCCESS, "Failed to create image views");
    }
    ctx->swapchain.image_view_count = ctx->swapchain.image_count;
}

void candy_query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface,
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
        candy_query_swapchain_support(device, surface, &swapchain_support);

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

void candy_init_vulkan_instance(candy_core *vk_instance, const candy_config *config) {
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
        .apiVersion = VK_API_VERSION_1_0, // WARNING: Make this newer after we are done
                                          // with triangel
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

void candy_init_surface(candy_core *core) {
    VkResult result =
        glfwCreateWindowSurface(core->instance, core->window, nullptr, &core->surface);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create window surface");
}

void candy_init_physical_device(candy_core *core) {
    candy_device_list devices = {};
    candy_find_physical_devices(core->instance, core->surface, &devices);

    CANDY_ASSERT(devices.count > 0, "No GPUs with Vulkan support found");

    uint32_t best = candy_pick_best_device(&devices, core->surface);
    CANDY_ASSERT(best != INVALID_QUEUE_FAMILY, "No suitable GPU found");

    core->physical_device = devices.handles[best];
    core->graphics_queue_family = devices.graphics_queue_families[best];
    core->present_queue_family = devices.present_queue_families[best];
}

void candy_init_logical_device(candy_context *ctx) {
    // We need to create queue infos for unique queue families
    uint32_t unique_queue_families[2];
    uint32_t unique_count = 0;

    unique_queue_families[unique_count++] = ctx->core.graphics_queue_family;

    // Only add present family if it's different from graphics family
    if (ctx->core.present_queue_family != ctx->core.graphics_queue_family) {
        unique_queue_families[unique_count++] = ctx->core.present_queue_family;
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
            ctx->config.enable_validation ? (uint32_t)VALIDATION_LAYER_COUNT : 0u,
        .ppEnabledLayerNames =
            ctx->config.enable_validation ? VALIDATION_LAYERS : nullptr,
        .enabledExtensionCount = DEVICE_EXTENSION_COUNT,
        .ppEnabledExtensionNames = DEVICE_EXTENSIONS,
        .pEnabledFeatures = &device_features,
    };

    VkResult result = vkCreateDevice(ctx->core.physical_device, &create_info, nullptr,
                                     &ctx->core.logical_device);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create logical device");

    // Get both queue handles
    vkGetDeviceQueue(ctx->core.logical_device, ctx->core.graphics_queue_family, 0,
                     &ctx->core.graphics_queue);
    vkGetDeviceQueue(ctx->core.logical_device, ctx->core.present_queue_family, 0,
                     &ctx->core.present_queue);
}

void candy_init_swapchain(candy_context *ctx) {
    candy_swapchain_support_details swapchain_details = {};
    candy_query_swapchain_support(ctx->core.physical_device, ctx->core.surface,
                                  &swapchain_details);

    VkSurfaceFormatKHR surface_fmt = {};
    candy_choose_swap_surface_format(&surface_fmt, swapchain_details.formats,
                                     swapchain_details.format_count);

    VkPresentModeKHR present_mode = {};
    candy_choose_swap_present_mode(&present_mode, swapchain_details.present_modes,
                                   swapchain_details.present_mode_count);

    VkExtent2D extent = {};
    candy_choose_swap_extent(&extent, swapchain_details.capabilities,
                             (GLFWwindow *)ctx->core.window);

    ctx->swapchain.image_count =
        swapchain_details.capabilities.minImageCount +
        1; // +1 bcuz if its minImageCount we have to wait on driver

    if (swapchain_details.capabilities.maxImageCount > 0 &&
        ctx->swapchain.image_count > swapchain_details.capabilities.maxImageCount) {
        ctx->swapchain.image_count = swapchain_details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = ctx->core.surface,
        .minImageCount = ctx->swapchain.image_count,
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
        candy_find_queue_families(ctx->core.physical_device, ctx->core.surface);
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
    VkResult result = vkCreateSwapchainKHR(ctx->core.logical_device, &create_info,
                                           nullptr, &ctx->swapchain.handle);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create swapchain");
    vkGetSwapchainImagesKHR(ctx->core.logical_device, ctx->swapchain.handle,
                            &ctx->swapchain.image_count, nullptr);
    if (ctx->swapchain.image_count > 8) {
        ctx->swapchain.image_count = 8;
    }
    vkGetSwapchainImagesKHR(ctx->core.logical_device, ctx->swapchain.handle,
                            &ctx->swapchain.image_count, ctx->swapchain.images);
    ctx->swapchain.image_format = surface_fmt.format;
    ctx->swapchain.extent = extent;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void candy_init(candy_context *ctx) {
    ctx->config = {
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

    ctx->core.window = glfwCreateWindow(ctx->config.width, ctx->config.height,
                                        ctx->config.window_title, nullptr, nullptr);

    CANDY_ASSERT(ctx->core.window != nullptr, "Failed to create window");

    // Init Vulkan
    candy_init_vulkan_instance(&ctx->core, &ctx->config);
    candy_init_surface(&ctx->core);
    candy_init_physical_device(&ctx->core);
    candy_init_logical_device(ctx);
    candy_init_swapchain(ctx);
    candy_create_image_views(ctx);
    candy_create_render_pass(ctx);
    candy_create_graphics_pipeline(ctx);
    candy_create_framebuffers(ctx);
    candy_create_command_pool(ctx);
    candy_create_command_buffer(ctx);
    candy_create_sync_objs(ctx);

    std::cout << "[CANDY] Init complete\n";
}

void candy_cleanup(candy_context *ctx) {

    for (uint32_t i = 0; i < ctx->swapchain.image_view_count; ++i) {
        vkDestroyFramebuffer(ctx->core.logical_device, ctx->swapchain.framebuffers[i],
                             nullptr);
    }

    vkDestroySwapchainKHR(ctx->core.logical_device, ctx->swapchain.handle, nullptr);
    for (uint32_t i = 0; i < ctx->swapchain.image_view_count; ++i) {
        vkDestroyImageView(ctx->core.logical_device, ctx->swapchain.image_views[i],
                           nullptr);
    }

    vkDestroyRenderPass(ctx->core.logical_device, ctx->pipeline.render_pass, nullptr);
    vkDestroyPipelineLayout(ctx->core.logical_device, ctx->pipeline.pipeline_layout,
                            nullptr);

    vkDestroyPipeline(ctx->core.logical_device, ctx->pipeline.graphics_pipeline, nullptr);
    vkDestroyCommandPool(ctx->core.logical_device, ctx->frame_data.command_pool, nullptr);

    vkDestroySemaphore(ctx->core.logical_device,
                       ctx->frame_data.image_available_semaphore, nullptr);
    vkDestroySemaphore(ctx->core.logical_device,
                       ctx->frame_data.render_finished_semaphore, nullptr);
    vkDestroyFence(ctx->core.logical_device, ctx->frame_data.in_flight_fence, nullptr);

    vkDestroyDevice(ctx->core.logical_device, nullptr);

    if (ctx->config.enable_validation) {
        candy_destroy_debug_messenger(ctx->core.instance, ctx->core.debug_messenger);
    }
    vkDestroySurfaceKHR(ctx->core.instance, ctx->core.surface, nullptr);
    vkDestroyInstance(ctx->core.instance, nullptr);

    glfwDestroyWindow(ctx->core.window);
    glfwTerminate();

    std::cout << "[CANDY] Cleanup complete\n";
}

void candy_loop(candy_context *ctx) {

    while (!glfwWindowShouldClose(ctx->core.window)) {
        glfwPollEvents();
        candy_draw_frame(ctx);
    }
    vkDeviceWaitIdle(ctx->core.logical_device);

    return;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    std::cout << "[CANDY] Starting...\n";

    candy_context candy_ctx = {};

    candy_init(&candy_ctx);

    candy_loop(&candy_ctx);

    candy_cleanup(&candy_ctx);

    return 0;
}
