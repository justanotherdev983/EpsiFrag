#include "candy_imgui.h"
#include "core.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <sys/stat.h>
#include <vulkan/vulkan_core.h>

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

static void candy_framebuffer_resize_callback(GLFWwindow *window, int width, int height) {
    (void)width;
    (void)height;
    auto ctx = reinterpret_cast<candy_context *>(glfwGetWindowUserPointer(window));
    ctx->swapchain.has_framebuffer_resized = true;
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
// COMPUTE SHADERS
// ============================================================================

/*
void candy_init_vkfft(candy_context *ctx) {
    uint32_t nx = 64, ny = 64, nz = 64;

    // Verify buffer exists before proceeding
    if (ctx->compute.psi_freq_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Buffer not created before VkFFT init!" << std::endl;
        CANDY_ASSERT(false,
                     "psi_freq_buffer must be created before VkFFT initialization");
    }

    ctx->compute.buffer_size = nx * ny * nz * sizeof(float) * 2;

    // Zero-initialize the entire config structure
    memset(&ctx->compute.fft_config, 0, sizeof(VkFFTConfiguration));

    ctx->compute.fft_config.FFTdim = 3;
    ctx->compute.fft_config.size[0] = nx;
    ctx->compute.fft_config.size[1] = ny;
    ctx->compute.fft_config.size[2] = nz;

    ctx->compute.fft_config.device = &ctx->core.logical_device;
    ctx->compute.fft_config.queue = &ctx->core.graphics_queue;
    ctx->compute.fft_config.fence = &ctx->frame_data.in_flight_fences[0];
    ctx->compute.fft_config.commandPool = &ctx->compute.command_pool;
    ctx->compute.fft_config.physicalDevice = &ctx->core.physical_device;
    ctx->compute.fft_config.isCompilerInitialized = 1;

    ctx->compute.fft_config.buffer = &ctx->compute.psi_freq_buffer;
    ctx->compute.fft_config.bufferSize = &ctx->compute.buffer_size;

    // Complex-to-complex transform
    ctx->compute.fft_config.performR2C = 0;

    // Additional recommended settings
    ctx->compute.fft_config.doublePrecision = 0; // Using float, not double

    std::cout << "[CANDY] Initializing forward FFT..." << std::endl;
    VkFFTResult res_forward =
        initializeVkFFT(&ctx->compute.fft_app_forward, ctx->compute.fft_config);
    CANDY_ASSERT(res_forward == VKFFT_SUCCESS, "Failed to initialize forward FFT");

    // For inverse, create a copy and set inverse flag
    VkFFTConfiguration inverse_config = ctx->compute.fft_config;
    inverse_config.inverseReturnToInputBuffer = 1; // Set inverse flag

    std::cout << "[CANDY] Initializing inverse FFT..." << std::endl;
    VkFFTResult res_inverse =
        initializeVkFFT(&ctx->compute.fft_app_inverse, inverse_config);
    CANDY_ASSERT(res_inverse == VKFFT_SUCCESS, "Failed to initialize inverse FFT");

    std::cout << "[CANDY] VkFFT initialized for " << nx << "x" << ny << "x" << nz
              << " grid\n";
}
*/
void candy_init_vkfft(candy_context *ctx) {
    uint32_t nx = 64, ny = 64, nz = 64;

    // Verify buffer exists before proceeding
    if (ctx->compute.psi_freq_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Buffer not created before VkFFT init!" << std::endl;
        CANDY_ASSERT(false,
                     "psi_freq_buffer must be created before VkFFT initialization");
    }

    ctx->compute.buffer_size = nx * ny * nz * sizeof(float) * 2;

    // Create a fence for VkFFT to use
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkFence vkfft_fence;
    VkResult fence_result =
        vkCreateFence(ctx->core.logical_device, &fence_info, nullptr, &vkfft_fence);
    CANDY_ASSERT(fence_result == VK_SUCCESS, "Failed to create VkFFT fence");

    // Zero-initialize the entire config structure
    memset(&ctx->compute.fft_config, 0, sizeof(VkFFTConfiguration));

    ctx->compute.fft_config.FFTdim = 3;
    ctx->compute.fft_config.size[0] = nx;
    ctx->compute.fft_config.size[1] = ny;
    ctx->compute.fft_config.size[2] = nz;

    // Critical: Use the actual values, not pointers to context members
    // VkFFT stores these as values internally
    ctx->compute.fft_config.device = &ctx->core.logical_device;
    ctx->compute.fft_config.queue = &ctx->core.graphics_queue;
    ctx->compute.fft_config.physicalDevice = &ctx->core.physical_device;
    ctx->compute.fft_config.commandPool = &ctx->compute.command_pool;

    // Provide the fence pointer - VkFFT REQUIRES this
    ctx->compute.fft_config.fence = &vkfft_fence;

    ctx->compute.fft_config.isCompilerInitialized = 1;

    // Buffer configuration
    ctx->compute.fft_config.buffer = &ctx->compute.psi_freq_buffer;
    ctx->compute.fft_config.bufferSize = &ctx->compute.buffer_size;

    // Complex-to-complex transform
    ctx->compute.fft_config.performR2C = 0;
    ctx->compute.fft_config.doublePrecision = 0;

    // Important: Tell VkFFT we're using GPU memory
    ctx->compute.fft_config.bufferStride[0] = nx;
    ctx->compute.fft_config.bufferStride[1] = ny;
    ctx->compute.fft_config.bufferStride[2] = nz;

    std::cout << "[CANDY] Initializing forward FFT..." << std::endl;
    VkFFTResult res_forward =
        initializeVkFFT(&ctx->compute.fft_app_forward, ctx->compute.fft_config);

    if (res_forward != VKFFT_SUCCESS) {
        std::cerr << "[CANDY ERROR] Forward FFT init failed with code: " << res_forward
                  << std::endl;

        // Print more diagnostic info
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(ctx->core.physical_device, &props);
        std::cerr << "  Device: " << props.deviceName << std::endl;
        std::cerr << "  Buffer size: " << ctx->compute.buffer_size << " bytes"
                  << std::endl;
        std::cerr << "  Grid dimensions: " << nx << "x" << ny << "x" << nz << std::endl;

        vkDestroyFence(ctx->core.logical_device, vkfft_fence, nullptr);
        CANDY_ASSERT(false, "Failed to initialize forward FFT");
    }

    // For inverse, create a copy and set inverse flag
    VkFFTConfiguration inverse_config = ctx->compute.fft_config;
    inverse_config.inverseReturnToInputBuffer = 1;

    std::cout << "[CANDY] Initializing inverse FFT..." << std::endl;
    VkFFTResult res_inverse =
        initializeVkFFT(&ctx->compute.fft_app_inverse, inverse_config);

    if (res_inverse != VKFFT_SUCCESS) {
        std::cerr << "[CANDY ERROR] Inverse FFT init failed with code: " << res_inverse
                  << std::endl;
        deleteVkFFT(&ctx->compute.fft_app_forward); // Clean up forward transform
        vkDestroyFence(ctx->core.logical_device, vkfft_fence, nullptr);
        CANDY_ASSERT(false, "Failed to initialize inverse FFT");
    }

    // Store the fence so we can clean it up later
    // You'll need to add this to your candy_context struct:
    // VkFence vkfft_fence;
    ctx->compute.vkfft_fence = vkfft_fence;

    std::cout << "[CANDY] VkFFT initialized for " << nx << "x" << ny << "x" << nz
              << " grid\n";
}

void candy_perform_fft(candy_context *ctx, bool inverse) {
    VkFFTLaunchParams launch_params = {};
    launch_params.buffer = &ctx->compute.psi_freq_buffer;
    launch_params.commandBuffer = &ctx->compute.command_buffer;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    VkFFTResult result;
    if (inverse) {
        result = VkFFTAppend(&ctx->compute.fft_app_inverse, -1, &launch_params);
    } else {
        result = VkFFTAppend(&ctx->compute.fft_app_forward, -1, &launch_params);
    }

    CANDY_ASSERT(result == VKFFT_SUCCESS, "Failed to append FFT to command buffer");

    vkEndCommandBuffer(ctx->compute.command_buffer);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->compute.command_buffer,
    };

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);
}

/*
void candy_perform_fft(candy_context *ctx, bool inverse) {
    VkFFTLaunchParams launch_params = {};
    launch_params.buffer = &ctx->compute.psi_freq_buffer;
    launch_params.commandBuffer = &ctx->compute.fft_command_buffer; // ✅ Use FFT buffer

    // Reset FFT command buffer
    vkResetCommandBuffer(ctx->compute.fft_command_buffer, 0); // ✅ Changed

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult begin_result =
        vkBeginCommandBuffer(ctx->compute.fft_command_buffer, &begin_info); // ✅ Changed
    if (begin_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to begin command buffer for FFT" << std::endl;
        return;
    }

    VkFFTResult result;
    if (inverse) {
        result = VkFFTAppend(&ctx->compute.fft_app_inverse, -1, &launch_params);
    } else {
        result = VkFFTAppend(&ctx->compute.fft_app_forward, -1, &launch_params);
    }

    if (result != VKFFT_SUCCESS) {
        std::cerr << "[CANDY ERROR] VkFFTAppend failed with code: " << result
                  << std::endl;
        vkEndCommandBuffer(ctx->compute.fft_command_buffer); // ✅ Changed
        return;
    }

    vkEndCommandBuffer(ctx->compute.fft_command_buffer); // ✅ Changed

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->compute.fft_command_buffer, // ✅ Changed
    };

    VkResult submit_result =
        vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (submit_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to submit FFT command buffer" << std::endl;
        return;
    }

    vkQueueWaitIdle(ctx->core.graphics_queue);
}
*/

void candy_cleanup_vkfft(candy_context *ctx) {
    deleteVkFFT(&ctx->compute.fft_app_forward);
    deleteVkFFT(&ctx->compute.fft_app_inverse);

    vkDestroyFence(ctx->core.logical_device, ctx->compute.vkfft_fence, nullptr);
}

// =============================================================================
// UPLOAD DATA TO GPU
// =============================================================================

void candy_upload_compute_data(candy_context *ctx, void *game_state) {
    struct complex_float {
        float real;
        float imaginary;
    };

    struct quant_state {
        std::vector<complex_float> psi;
        std::vector<float> potential;
        std::vector<float> prob_dens;
        std::vector<float> kx;
        std::vector<float> ky;
        std::vector<float> kz;
        std::vector<float> k_squared;
        std::vector<complex_float> kinetic_factor;
        std::vector<complex_float> potential_factor;
        float dx, dy, dz, time;
    };

    quant_state *state = (quant_state *)game_state;

    uint32_t nx = 64, ny = 64, nz = 64;
    VkDeviceSize buffer_size = nx * ny * nz * sizeof(complex_float);

    void *data;

    // Upload kinetic factors
    vkMapMemory(ctx->core.logical_device, ctx->compute.kinetic_factor_memory, 0,
                buffer_size, 0, &data);
    memcpy(data, state->kinetic_factor.data(), buffer_size);
    vkUnmapMemory(ctx->core.logical_device, ctx->compute.kinetic_factor_memory);

    // Upload potential factors
    vkMapMemory(ctx->core.logical_device, ctx->compute.potential_factor_memory, 0,
                buffer_size, 0, &data);
    memcpy(data, state->potential_factor.data(), buffer_size);
    vkUnmapMemory(ctx->core.logical_device, ctx->compute.potential_factor_memory);

    // Upload initial wavefunction
    vkMapMemory(ctx->core.logical_device, ctx->compute.psi_freq_memory, 0, buffer_size, 0,
                &data);
    memcpy(data, state->psi.data(), buffer_size);
    vkUnmapMemory(ctx->core.logical_device, ctx->compute.psi_freq_memory);

    std::cout << "[CANDY] Uploaded compute data to GPU (" << buffer_size << " bytes)\n";
}
// =============================================================================
// SPLIT-OPERATOR TIME STEP
// =============================================================================
/*
void candy_quantum_timestep(candy_context *ctx) {
    uint32_t nx = 64, ny = 64, nz = 64;
    uint32_t push_constants[3] = {nx, ny, nz};

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    // Step 1: Apply first half kinetic evolution (in frequency space)
    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[0]);
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[0], 0, 1,
                            &ctx->compute.descriptor_sets[0], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[0],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    // Memory barrier
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };
    vkCmdPipelineBarrier(
        ctx->compute.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->compute.command_buffer,
    };

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);

    // Step 2: IFFT to real space
    candy_perform_fft(ctx, true);

    // Step 3: Apply full potential evolution (in real space)
    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[1]);
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[1], 0, 1,
                            &ctx->compute.descriptor_sets[1], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[1],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    vkCmdPipelineBarrier(
        ctx->compute.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);

    // Step 4: FFT back to frequency space
    candy_perform_fft(ctx, false);

    // Step 5: Apply last half kinetic evolution
    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[2]);
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[2], 0, 1,
                            &ctx->compute.descriptor_sets[2], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[2],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);
}
*/

void candy_quantum_timestep(candy_context *ctx) {
    uint32_t nx = 64, ny = 64, nz = 64;
    uint32_t push_constants[3] = {nx, ny, nz};

    // Reset command buffer at the start
    vkResetCommandBuffer(ctx->compute.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    // Step 1: Apply first half kinetic evolution (in frequency space)
    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[0]);
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[0], 0, 1,
                            &ctx->compute.descriptor_sets[0], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[0],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    // Proper memory barrier
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(
        ctx->compute.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    // Submit and wait
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->compute.command_buffer,
    };

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);

    // Step 2: IFFT to real space
    candy_perform_fft(ctx, true);

    // Step 3: Apply full potential evolution (in real space)
    vkResetCommandBuffer(ctx->compute.command_buffer, 0);
    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[1]);
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[1], 0, 1,
                            &ctx->compute.descriptor_sets[1], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[1],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    vkCmdPipelineBarrier(
        ctx->compute.command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);

    // Step 4: FFT back to frequency space
    candy_perform_fft(ctx, false);

    // Step 5: Apply last half kinetic evolution
    vkResetCommandBuffer(ctx->compute.command_buffer, 0);
    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[2]);
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[2], 0, 1,
                            &ctx->compute.descriptor_sets[2], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[2],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);
}

void candy_create_compute_buffers(candy_context *ctx) {
    uint32_t nx = 64, ny = 64, nz = 64;
    VkDeviceSize buffer_size = nx * ny * nz * sizeof(float) * 2; // vec2 for complex

    // Helper lambda to create and allocate a buffer
    auto create_buffer = [&](VkBuffer *buffer, VkDeviceMemory *memory) {
        VkBufferCreateInfo buffer_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = buffer_size,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        vkCreateBuffer(ctx->core.logical_device, &buffer_info, nullptr, buffer);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(ctx->core.logical_device, *buffer, &mem_reqs);

        VkMemoryAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mem_reqs.size,
            .memoryTypeIndex =
                candy_find_memory_type(ctx, mem_reqs.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        };

        vkAllocateMemory(ctx->core.logical_device, &alloc_info, nullptr, memory);
        vkBindBufferMemory(ctx->core.logical_device, *buffer, *memory, 0);
    };

    create_buffer(&ctx->compute.psi_freq_buffer, &ctx->compute.psi_freq_memory);
    create_buffer(&ctx->compute.kinetic_factor_buffer,
                  &ctx->compute.kinetic_factor_memory);
    create_buffer(&ctx->compute.potential_factor_buffer,
                  &ctx->compute.potential_factor_memory);
}

void candy_create_prob_density_buffer(candy_context *ctx) {
    uint32_t nx = 64, ny = 64, nz = 64;
    VkDeviceSize buffer_size = nx * ny * nz * sizeof(float);

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(ctx->core.logical_device, &buffer_info, nullptr,
                   &ctx->compute.prob_density_buffer);

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->core.logical_device,
                                  ctx->compute.prob_density_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = candy_find_memory_type(
            ctx, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    vkAllocateMemory(ctx->core.logical_device, &alloc_info, nullptr,
                     &ctx->compute.prob_density_memory);
    vkBindBufferMemory(ctx->core.logical_device, ctx->compute.prob_density_buffer,
                       ctx->compute.prob_density_memory, 0);
}

/*
void candy_update_particle_vertices(candy_context *ctx, void *game_state) {

    struct complex_float {
        float real;
        float imaginary;
    };

    struct quant_state {
        std::vector<complex_float> psi;
        std::vector<float> potential;
        std::vector<float> prob_dens;
        std::vector<float> kx;
        std::vector<float> ky;
        std::vector<float> kz;
        std::vector<float> k_squared;
        std::vector<complex_float> kinetic_factor;
        std::vector<complex_float> potential_factor;
        float dx, dy, dz, time;

        glm::mat4 view_proj_matrix;
        float density_threshold;
    };
    quant_state *state = (quant_state *)game_state;
    uint32_t nx = 64, ny = 64, nz = 64;

    vkResetCommandBuffer(ctx->compute.command_buffer, 0);

    // Run visualize compute shader
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);

    uint32_t push_constants[3] = {nx, ny, nz};
    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[3]); // visualize shader
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[3], 0, 1,
                            &ctx->compute.descriptor_sets[3], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[3],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->compute.command_buffer,
    };

    vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->core.graphics_queue);

    // Read back density data
    void *data;
    vkMapMemory(ctx->core.logical_device, ctx->compute.prob_density_memory, 0,
                VK_WHOLE_SIZE, 0, &data);
    float *densities = (float *)data;

    // Generate particles for visualization
    std::vector<candy_particle> particles;
    particles.reserve(nx * ny * nz / 10); // Rough estimate

    float dx = state->dx;
    float dy = state->dy;
    float dz = state->dz;
    float cx = 20.0f / 2.0f;
    float cy = 20.0f / 2.0f;
    float cz = 20.0f / 2.0f;

    for (uint32_t i = 0; i < nx; ++i) {
        for (uint32_t j = 0; j < ny; ++j) {
            for (uint32_t k = 0; k < nz; ++k) {
                uint32_t idx = i + nx * (j + ny * k);
                float density = densities[idx];

                if (density > state->density_threshold) {
                    candy_particle p;
                    p.position = glm::vec3(i * dx - cx, j * dy - cy, k * dz - cz);
                    p.density = density;
                    particles.push_back(p);
                }
            }
        }
    }

    vkUnmapMemory(ctx->core.logical_device, ctx->compute.prob_density_memory);

    ctx->core.particle_count = particles.size();

    if (particles.empty()) {
        ctx->core.particle_count = 0;
        std::cout << "[CANDY] Generated 0 particles\n";
        return;
    }

    // Update particle vertex buffer
    if (ctx->core.particle_vertex_buffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->core.logical_device);
        vkDestroyBuffer(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                        nullptr);
        vkFreeMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory,
                     nullptr);
    }

    VkDeviceSize buffer_size = sizeof(candy_particle) * particles.size();

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    vkCreateBuffer(ctx->core.logical_device, &buffer_info, nullptr,
                   &ctx->core.particle_vertex_buffer);

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->core.logical_device,
                                  ctx->core.particle_vertex_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = candy_find_memory_type(
            ctx, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    vkAllocateMemory(ctx->core.logical_device, &alloc_info, nullptr,
                     &ctx->core.particle_vertex_buffer_memory);
    vkBindBufferMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                       ctx->core.particle_vertex_buffer_memory, 0);

    vkMapMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory, 0,
                buffer_size, 0, &data);
    memcpy(data, particles.data(), buffer_size);
    vkUnmapMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory);

    std::cout << "[CANDY] Generated " << particles.size() << " particles\n";
}
*/
/*
void candy_update_particle_vertices(candy_context *ctx, void *game_state) {

    struct complex_float {
        float real;
        float imaginary;
    };

    struct quant_state {
        std::vector<complex_float> psi;
        std::vector<float> potential;
        std::vector<float> prob_dens;
        std::vector<float> kx;
        std::vector<float> ky;
        std::vector<float> kz;
        std::vector<float> k_squared;
        std::vector<complex_float> kinetic_factor;
        std::vector<complex_float> potential_factor;
        float dx, dy, dz, time;
        glm::mat4 view_proj_matrix;
        float density_threshold;
    };

    quant_state *state = (quant_state *)game_state;
    uint32_t nx = 64, ny = 64, nz = 64;

    // Reset and begin command buffer
    vkResetCommandBuffer(ctx->compute.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VkResult begin_result =
        vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);
    if (begin_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to begin command buffer for visualization"
                  << std::endl;
        return;
    }

    // Bind pipeline and descriptor sets
    uint32_t push_constants[3] = {nx, ny, nz};
    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[3]); // visualize shader
    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[3], 0, 1,
                            &ctx->compute.descriptor_sets[3], 0, nullptr);
    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[3],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(ctx->compute.command_buffer, (nx + 7) / 8, (ny + 7) / 8, (nz + 7) / 8);

    // Add memory barrier to ensure compute shader writes are visible to host reads
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier(ctx->compute.command_buffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkEndCommandBuffer(ctx->compute.command_buffer);

    // Submit and wait for completion
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->compute.command_buffer,
    };

    VkResult submit_result =
        vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (submit_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to submit visualization command buffer"
                  << std::endl;
        return;
    }

    // CRITICAL: Wait for the compute shader to finish
    vkQueueWaitIdle(ctx->core.graphics_queue);

    // Now it's safe to read back density data
    void *data;
    VkResult map_result =
        vkMapMemory(ctx->core.logical_device, ctx->compute.prob_density_memory, 0,
                    VK_WHOLE_SIZE, 0, &data);
    if (map_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to map prob_density memory" << std::endl;
        return;
    }

    float *densities = (float *)data;

    // Generate particles for visualization
    std::vector<candy_particle> particles;
    particles.reserve(nx * ny * nz / 10); // Rough estimate

    float dx = state->dx;
    float dy = state->dy;
    float dz = state->dz;
    float cx = 20.0f / 2.0f;
    float cy = 20.0f / 2.0f;
    float cz = 20.0f / 2.0f;

    for (uint32_t i = 0; i < nx; ++i) {
        for (uint32_t j = 0; j < ny; ++j) {
            for (uint32_t k = 0; k < nz; ++k) {
                uint32_t idx = i + nx * (j + ny * k);
                float density = densities[idx];

                if (density > state->density_threshold) {
                    candy_particle p;
                    p.position = glm::vec3(i * dx - cx, j * dy - cy, k * dz - cz);
                    p.density = density;
                    particles.push_back(p);
                }
            }
        }
    }

    vkUnmapMemory(ctx->core.logical_device, ctx->compute.prob_density_memory);

    ctx->core.particle_count = particles.size();

    if (particles.empty()) {
        ctx->core.particle_count = 0;
        std::cout << "[CANDY] Generated 0 particles (threshold may be too high: "
                  << state->density_threshold << ")\n";
        return;
    }

    // Clean up old particle buffer if it exists
    if (ctx->core.particle_vertex_buffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->core.logical_device);
        vkDestroyBuffer(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                        nullptr);
        vkFreeMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory,
                     nullptr);
    }

    // Create new particle vertex buffer
    VkDeviceSize buffer_size = sizeof(candy_particle) * particles.size();

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkResult create_result = vkCreateBuffer(ctx->core.logical_device, &buffer_info,
                                            nullptr, &ctx->core.particle_vertex_buffer);
    if (create_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to create particle vertex buffer" << std::endl;
        return;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->core.logical_device,
                                  ctx->core.particle_vertex_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = candy_find_memory_type(
            ctx, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    VkResult alloc_result =
        vkAllocateMemory(ctx->core.logical_device, &alloc_info, nullptr,
                         &ctx->core.particle_vertex_buffer_memory);
    if (alloc_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to allocate particle vertex buffer memory"
                  << std::endl;
        vkDestroyBuffer(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                        nullptr);
        ctx->core.particle_vertex_buffer = VK_NULL_HANDLE;
        return;
    }

    vkBindBufferMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                       ctx->core.particle_vertex_buffer_memory, 0);

    // Upload particle data
    map_result =
        vkMapMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory, 0,
                    buffer_size, 0, &data);
    if (map_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to map particle vertex buffer memory"
                  << std::endl;
        return;
    }

    memcpy(data, particles.data(), buffer_size);
    vkUnmapMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory);

    std::cout << "[CANDY] Generated " << particles.size()
              << " particles (threshold: " << state->density_threshold << ")\n";
}
*/

void candy_update_particle_vertices(candy_context *ctx, void *game_state) {
    std::cout << "[CANDY DEBUG] === ENTERING candy_update_particle_vertices ==="
              << std::endl;

    if (!ctx || !game_state) {
        std::cerr << "[CANDY ERROR] NULL pointer!" << std::endl;
        return;
    }

    struct complex_float {
        float real;
        float imaginary;
    };

    struct quant_state {
        std::vector<complex_float> psi;
        std::vector<float> potential;
        std::vector<float> prob_dens;
        std::vector<float> kx;
        std::vector<float> ky;
        std::vector<float> kz;
        std::vector<float> k_squared;
        std::vector<complex_float> kinetic_factor;
        std::vector<complex_float> potential_factor;
        float dx, dy, dz, time;
        glm::mat4 view_proj_matrix;
        float density_threshold;
    };

    quant_state *state = (quant_state *)game_state;
    uint32_t nx = 64, ny = 64, nz = 64;

    // CRITICAL FIX: Wait for ALL previous compute operations to complete
    // This ensures the command buffer is not in use before we reset it
    std::cout
        << "[CANDY DEBUG] Waiting for queue to be idle before resetting command buffer..."
        << std::endl;
    VkResult wait_result = vkQueueWaitIdle(ctx->core.graphics_queue);
    if (wait_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to wait for queue idle: " << wait_result
                  << std::endl;
        return;
    }
    std::cout << "[CANDY DEBUG] Queue is idle, safe to reset command buffer" << std::endl;

    // Now it's safe to reset the command buffer
    std::cout << "[CANDY DEBUG] About to reset command buffer..." << std::endl;
    VkResult reset_result = vkResetCommandBuffer(ctx->compute.command_buffer, 0);
    if (reset_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to reset command buffer: " << reset_result
                  << std::endl;
        return;
    }
    std::cout << "[CANDY DEBUG] Command buffer reset successful" << std::endl;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };

    std::cout << "[CANDY DEBUG] About to begin command buffer..." << std::endl;
    VkResult begin_result =
        vkBeginCommandBuffer(ctx->compute.command_buffer, &begin_info);
    if (begin_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to begin command buffer: " << begin_result
                  << std::endl;
        return;
    }
    std::cout << "[CANDY DEBUG] Command buffer begin successful" << std::endl;

    // Bind pipeline
    std::cout << "[CANDY DEBUG] About to bind pipeline..." << std::endl;
    vkCmdBindPipeline(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      ctx->compute.pipelines[3]);
    std::cout << "[CANDY DEBUG] Pipeline bound" << std::endl;

    // Bind descriptor sets
    std::cout << "[CANDY DEBUG] About to bind descriptor sets..." << std::endl;
    std::cout << "[CANDY DEBUG]   Pipeline layout: " << ctx->compute.pipeline_layouts[3]
              << std::endl;
    std::cout << "[CANDY DEBUG]   Descriptor set: " << ctx->compute.descriptor_sets[3]
              << std::endl;

    vkCmdBindDescriptorSets(ctx->compute.command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            ctx->compute.pipeline_layouts[3], 0, 1,
                            &ctx->compute.descriptor_sets[3], 0, nullptr);
    std::cout << "[CANDY DEBUG] Descriptor sets bound" << std::endl;

    // Push constants
    uint32_t push_constants[3] = {nx, ny, nz};
    std::cout << "[CANDY DEBUG] About to push constants: " << push_constants[0] << ", "
              << push_constants[1] << ", " << push_constants[2] << std::endl;

    vkCmdPushConstants(ctx->compute.command_buffer, ctx->compute.pipeline_layouts[3],
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    std::cout << "[CANDY DEBUG] Push constants set" << std::endl;

    // Dispatch compute shader
    uint32_t dispatch_x = (nx + 7) / 8;
    uint32_t dispatch_y = (ny + 7) / 8;
    uint32_t dispatch_z = (nz + 7) / 8;
    std::cout << "[CANDY DEBUG] About to dispatch: " << dispatch_x << "x" << dispatch_y
              << "x" << dispatch_z << std::endl;

    vkCmdDispatch(ctx->compute.command_buffer, dispatch_x, dispatch_y, dispatch_z);
    std::cout << "[CANDY DEBUG] Dispatch recorded" << std::endl;

    // Memory barrier
    std::cout << "[CANDY DEBUG] About to add memory barrier..." << std::endl;
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    vkCmdPipelineBarrier(ctx->compute.command_buffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
    std::cout << "[CANDY DEBUG] Memory barrier added" << std::endl;

    // End command buffer
    std::cout << "[CANDY DEBUG] About to end command buffer..." << std::endl;
    VkResult end_result = vkEndCommandBuffer(ctx->compute.command_buffer);
    if (end_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to end command buffer: " << end_result
                  << std::endl;
        return;
    }
    std::cout << "[CANDY DEBUG] Command buffer ended" << std::endl;

    // Submit command buffer
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &ctx->compute.command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    std::cout << "[CANDY DEBUG] About to submit command buffer..." << std::endl;
    VkResult submit_result =
        vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (submit_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to submit command buffer: " << submit_result
                  << std::endl;
        return;
    }
    std::cout << "[CANDY DEBUG] Command buffer submitted" << std::endl;

    // Wait for completion
    std::cout << "[CANDY DEBUG] About to wait for queue idle..." << std::endl;
    wait_result = vkQueueWaitIdle(ctx->core.graphics_queue);
    if (wait_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to wait for queue idle: " << wait_result
                  << std::endl;
        return;
    }
    std::cout << "[CANDY DEBUG] Queue idle - compute shader finished" << std::endl;

    // Map memory and read results
    void *data = nullptr;
    std::cout << "[CANDY DEBUG] About to map memory..." << std::endl;

    VkResult map_result =
        vkMapMemory(ctx->core.logical_device, ctx->compute.prob_density_memory, 0,
                    VK_WHOLE_SIZE, 0, &data);
    if (map_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to map prob_density memory: " << map_result
                  << std::endl;
        return;
    }
    std::cout << "[CANDY DEBUG] Memory mapped successfully" << std::endl;

    if (!data) {
        std::cerr << "[CANDY ERROR] Mapped data pointer is NULL!" << std::endl;
        return;
    }

    float *densities = (float *)data;

    // Generate particles
    std::cout << "[CANDY DEBUG] Generating particles..." << std::endl;
    std::vector<candy_particle> particles;
    particles.reserve(nx * ny * nz / 10);

    float dx = state->dx;
    float dy = state->dy;
    float dz = state->dz;
    float cx = 20.0f / 2.0f;
    float cy = 20.0f / 2.0f;
    float cz = 20.0f / 2.0f;

    for (uint32_t i = 0; i < nx; ++i) {
        for (uint32_t j = 0; j < ny; ++j) {
            for (uint32_t k = 0; k < nz; ++k) {
                uint32_t idx = i + nx * (j + ny * k);
                float density = densities[idx];

                if (density > state->density_threshold) {
                    candy_particle p;
                    p.position = glm::vec3(i * dx - cx, j * dy - cy, k * dz - cz);
                    p.density = density;
                    particles.push_back(p);
                }
            }
        }
    }

    vkUnmapMemory(ctx->core.logical_device, ctx->compute.prob_density_memory);

    ctx->core.particle_count = particles.size();

    if (particles.empty()) {
        ctx->core.particle_count = 0;
        std::cout << "[CANDY] Generated 0 particles (threshold: "
                  << state->density_threshold << ")" << std::endl;
        return;
    }

    // Clean up old particle buffer
    if (ctx->core.particle_vertex_buffer != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->core.logical_device);
        vkDestroyBuffer(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                        nullptr);
        vkFreeMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory,
                     nullptr);
        ctx->core.particle_vertex_buffer = VK_NULL_HANDLE;
        ctx->core.particle_vertex_buffer_memory = VK_NULL_HANDLE;
    }

    // Create new particle vertex buffer
    VkDeviceSize buffer_size = sizeof(candy_particle) * particles.size();

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkResult create_result = vkCreateBuffer(ctx->core.logical_device, &buffer_info,
                                            nullptr, &ctx->core.particle_vertex_buffer);
    if (create_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to create particle vertex buffer: "
                  << create_result << std::endl;
        return;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->core.logical_device,
                                  ctx->core.particle_vertex_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = candy_find_memory_type(
            ctx, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    VkResult alloc_result =
        vkAllocateMemory(ctx->core.logical_device, &alloc_info, nullptr,
                         &ctx->core.particle_vertex_buffer_memory);
    if (alloc_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to allocate particle vertex buffer memory: "
                  << alloc_result << std::endl;
        vkDestroyBuffer(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                        nullptr);
        ctx->core.particle_vertex_buffer = VK_NULL_HANDLE;
        return;
    }

    vkBindBufferMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer,
                       ctx->core.particle_vertex_buffer_memory, 0);

    // Upload particle data
    map_result =
        vkMapMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory, 0,
                    buffer_size, 0, &data);
    if (map_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to map particle vertex buffer memory: "
                  << map_result << std::endl;
        return;
    }

    memcpy(data, particles.data(), buffer_size);
    vkUnmapMemory(ctx->core.logical_device, ctx->core.particle_vertex_buffer_memory);

    std::cout << "[CANDY] Generated " << particles.size()
              << " particles (threshold: " << state->density_threshold << ")"
              << std::endl;

    std::cout << "[CANDY DEBUG] === EXITING candy_update_particle_vertices ==="
              << std::endl;
}

// 2. Allocate and update descriptor sets
/*
void candy_create_compute_descriptor_sets(candy_context *ctx) {
    VkDescriptorSetLayout layouts[4] = {
        ctx->compute.descriptor_layout,
        ctx->compute.descriptor_layout,
        ctx->compute.descriptor_layout,
        ctx->compute.descriptor_layout,
    };

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ctx->compute.descriptor_pool,
        .descriptorSetCount = 4,
        .pSetLayouts = layouts,
    };

    vkAllocateDescriptorSets(ctx->core.logical_device, &alloc_info,
                             ctx->compute.descriptor_sets);

    // Update descriptor sets for each shader
    for (int i = 0; i < 4; i++) {
        VkDescriptorBufferInfo buffer_info_0 = {
            .buffer = ctx->compute.psi_freq_buffer,
            .offset = 0,
            .range = VK_WHOLE_SIZE,
        };

        VkDescriptorBufferInfo buffer_info_1;
        if (i == 3) { // Visualize shader needs prob_density buffer
            buffer_info_1 = {
                .buffer = ctx->compute.prob_density_buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
        } else if (i == 1) { // Potential shader
            buffer_info_1 = {
                .buffer = ctx->compute.potential_factor_buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
        } else { // Kinetic shaders
            buffer_info_1 = {
                .buffer = ctx->compute.kinetic_factor_buffer,
                .offset = 0,
                .range = VK_WHOLE_SIZE,
            };
        }

        VkWriteDescriptorSet writes[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = ctx->compute.descriptor_sets[i],
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_info_0,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = ctx->compute.descriptor_sets[i],
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffer_info_1,
            },
        };

        vkUpdateDescriptorSets(ctx->core.logical_device, 2, writes, 0, nullptr);
    }
}
*/

void candy_create_compute_descriptor_sets(candy_context *ctx) {
    std::cout << "[CANDY DEBUG] Creating compute descriptor sets..." << std::endl;

    // Validate prerequisites
    if (ctx->compute.descriptor_pool == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Descriptor pool is NULL before allocation!"
                  << std::endl;
        CANDY_ASSERT(false, "Descriptor pool not created");
    }

    if (ctx->compute.descriptor_layout == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Descriptor layout is NULL before allocation!"
                  << std::endl;
        CANDY_ASSERT(false, "Descriptor layout not created");
    }

    // Validate all buffers exist
    if (ctx->compute.psi_freq_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] psi_freq_buffer is NULL!" << std::endl;
        CANDY_ASSERT(false, "psi_freq_buffer not created");
    }
    if (ctx->compute.kinetic_factor_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] kinetic_factor_buffer is NULL!" << std::endl;
        CANDY_ASSERT(false, "kinetic_factor_buffer not created");
    }
    if (ctx->compute.potential_factor_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] potential_factor_buffer is NULL!" << std::endl;
        CANDY_ASSERT(false, "potential_factor_buffer not created");
    }
    if (ctx->compute.prob_density_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] prob_density_buffer is NULL!" << std::endl;
        CANDY_ASSERT(false, "prob_density_buffer not created");
    }

    VkDescriptorSetLayout layouts[4] = {
        ctx->compute.descriptor_layout,
        ctx->compute.descriptor_layout,
        ctx->compute.descriptor_layout,
        ctx->compute.descriptor_layout,
    };

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = ctx->compute.descriptor_pool,
        .descriptorSetCount = 4,
        .pSetLayouts = layouts,
    };

    VkResult alloc_result = vkAllocateDescriptorSets(
        ctx->core.logical_device, &alloc_info, ctx->compute.descriptor_sets);
    if (alloc_result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to allocate descriptor sets: " << alloc_result
                  << std::endl;
        CANDY_ASSERT(false, "Failed to allocate descriptor sets");
    }

    std::cout << "[CANDY DEBUG] Descriptor sets allocated successfully" << std::endl;
    for (int i = 0; i < 4; i++) {
        std::cout << "[CANDY DEBUG]   Set " << i << ": "
                  << ctx->compute.descriptor_sets[i] << std::endl;
    }

    // Update descriptor sets for each shader
    for (int i = 0; i < 4; i++) {
        std::cout << "[CANDY DEBUG] Updating descriptor set " << i << "..." << std::endl;

        // Binding 0 - always psi_freq_buffer
        VkDescriptorBufferInfo buffer_info_0 = {
            .buffer = ctx->compute.psi_freq_buffer,
            .offset = 0,
            .range = VK_WHOLE_SIZE,
        };

        // Binding 1 - depends on shader
        VkDescriptorBufferInfo buffer_info_1;
        const char *binding1_name;

        if (i == 3) {
            // Visualize shader
            buffer_info_1.buffer = ctx->compute.prob_density_buffer;
            binding1_name = "prob_density_buffer";
        } else if (i == 1) {
            // Potential shader
            buffer_info_1.buffer = ctx->compute.potential_factor_buffer;
            binding1_name = "potential_factor_buffer";
        } else {
            // Kinetic shaders
            buffer_info_1.buffer = ctx->compute.kinetic_factor_buffer;
            binding1_name = "kinetic_factor_buffer";
        }

        buffer_info_1.offset = 0;
        buffer_info_1.range = VK_WHOLE_SIZE;

        std::cout << "[CANDY DEBUG]   Binding 0: psi_freq_buffer = "
                  << ctx->compute.psi_freq_buffer << std::endl;
        std::cout << "[CANDY DEBUG]   Binding 1: " << binding1_name << " = "
                  << buffer_info_1.buffer << std::endl;

        VkWriteDescriptorSet writes[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = ctx->compute.descriptor_sets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo = nullptr,
                .pBufferInfo = &buffer_info_0,
                .pTexelBufferView = nullptr,
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = nullptr,
                .dstSet = ctx->compute.descriptor_sets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pImageInfo = nullptr,
                .pBufferInfo = &buffer_info_1,
                .pTexelBufferView = nullptr,
            },
        };

        vkUpdateDescriptorSets(ctx->core.logical_device, 2, writes, 0, nullptr);
        std::cout << "[CANDY DEBUG] Descriptor set " << i << " updated successfully"
                  << std::endl;
    }

    std::cout << "[CANDY DEBUG] All descriptor sets created and updated" << std::endl;
}

void candy_validate_compute_pipeline(candy_context *ctx) {
    std::cout << "\n[CANDY DEBUG] ========== VALIDATING COMPUTE PIPELINE =========="
              << std::endl;

    bool all_valid = true;

    // Check descriptor pool
    if (ctx->compute.descriptor_pool == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Descriptor pool is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ Descriptor pool: " << ctx->compute.descriptor_pool
                  << std::endl;
    }

    // Check descriptor layout
    if (ctx->compute.descriptor_layout == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Descriptor layout is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ Descriptor layout: "
                  << ctx->compute.descriptor_layout << std::endl;
    }

    if (ctx->compute.fft_command_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] FFT Command buffer is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ FFT Command buffer: "
                  << ctx->compute.fft_command_buffer << std::endl;
    }

    // Check all descriptor sets
    for (int i = 0; i < 4; i++) {
        if (ctx->compute.descriptor_sets[i] == VK_NULL_HANDLE) {
            std::cerr << "[CANDY ERROR] Descriptor set " << i << " is NULL!" << std::endl;
            all_valid = false;
        } else {
            std::cout << "[CANDY DEBUG] ✓ Descriptor set " << i << ": "
                      << ctx->compute.descriptor_sets[i] << std::endl;
        }
    }

    // Check pipeline layouts
    for (int i = 0; i < 4; i++) {
        if (ctx->compute.pipeline_layouts[i] == VK_NULL_HANDLE) {
            std::cerr << "[CANDY ERROR] Pipeline layout " << i << " is NULL!"
                      << std::endl;
            all_valid = false;
        } else {
            std::cout << "[CANDY DEBUG] ✓ Pipeline layout " << i << ": "
                      << ctx->compute.pipeline_layouts[i] << std::endl;
        }
    }

    // Check pipelines
    for (int i = 0; i < 4; i++) {
        if (ctx->compute.pipelines[i] == VK_NULL_HANDLE) {
            std::cerr << "[CANDY ERROR] Pipeline " << i << " is NULL!" << std::endl;
            all_valid = false;
        } else {
            std::cout << "[CANDY DEBUG] ✓ Pipeline " << i << ": "
                      << ctx->compute.pipelines[i] << std::endl;
        }
    }

    // Check buffers
    if (ctx->compute.psi_freq_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] psi_freq_buffer is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ psi_freq_buffer: " << ctx->compute.psi_freq_buffer
                  << std::endl;
    }

    if (ctx->compute.kinetic_factor_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] kinetic_factor_buffer is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ kinetic_factor_buffer: "
                  << ctx->compute.kinetic_factor_buffer << std::endl;
    }

    if (ctx->compute.potential_factor_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] potential_factor_buffer is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ potential_factor_buffer: "
                  << ctx->compute.potential_factor_buffer << std::endl;
    }

    if (ctx->compute.prob_density_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] prob_density_buffer is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ prob_density_buffer: "
                  << ctx->compute.prob_density_buffer << std::endl;
    }

    // Check command pool and buffer
    if (ctx->compute.command_pool == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Command pool is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ Command pool: " << ctx->compute.command_pool
                  << std::endl;
    }

    if (ctx->compute.command_buffer == VK_NULL_HANDLE) {
        std::cerr << "[CANDY ERROR] Command buffer is NULL!" << std::endl;
        all_valid = false;
    } else {
        std::cout << "[CANDY DEBUG] ✓ Command buffer: " << ctx->compute.command_buffer
                  << std::endl;
    }

    if (all_valid) {
        std::cout << "[CANDY DEBUG] ✓✓✓ ALL COMPUTE PIPELINE COMPONENTS VALID ✓✓✓"
                  << std::endl;
    } else {
        std::cerr << "[CANDY ERROR] ✗✗✗ COMPUTE PIPELINE VALIDATION FAILED ✗✗✗"
                  << std::endl;
    }

    std::cout << "[CANDY DEBUG] =============================================\n"
              << std::endl;
}

VkShaderModule candy_create_compute_shader_module(VkDevice device, const char *filepath) {
    std::vector<char> code = candy_read_shader_file(filepath);

    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t *>(code.data()),
    };

    VkShaderModule shader_module;
    VkResult result = vkCreateShaderModule(device, &create_info, nullptr, &shader_module);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create compute shader module");

    return shader_module;
}

/*
void candy_create_compute_descriptor_layout(candy_context *ctx) {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 2,
        .pBindings = bindings,
    };

    VkResult result = vkCreateDescriptorSetLayout(
        ctx->core.logical_device, &layout_info, nullptr, &ctx->compute.descriptor_layout);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create compute descriptor layout");
}
*/

void candy_create_compute_descriptor_layout(candy_context *ctx) {
    std::cout << "[CANDY DEBUG] Creating compute descriptor layout..." << std::endl;

    VkDescriptorSetLayoutBinding bindings[2] = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        },
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = 2,
        .pBindings = bindings,
    };

    VkResult result = vkCreateDescriptorSetLayout(
        ctx->core.logical_device, &layout_info, nullptr, &ctx->compute.descriptor_layout);

    if (result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to create descriptor layout: " << result
                  << std::endl;
        CANDY_ASSERT(false, "Failed to create compute descriptor layout");
    }

    std::cout << "[CANDY DEBUG] Descriptor layout created: "
              << ctx->compute.descriptor_layout << std::endl;
}

/*
void candy_create_compute_descriptor_pool(candy_context *ctx) {
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 8, // 2 bindings * 4 shaders
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = 4,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    VkResult result = vkCreateDescriptorPool(ctx->core.logical_device, &pool_info,
                                             nullptr, &ctx->compute.descriptor_pool);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create compute descriptor pool");
}
*/

void candy_create_compute_descriptor_pool(candy_context *ctx) {
    std::cout << "[CANDY DEBUG] Creating compute descriptor pool..." << std::endl;

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 8, // 2 bindings * 4 shaders
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, // Allow freeing
                                                                    // individual sets
        .maxSets = 4,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    VkResult result = vkCreateDescriptorPool(ctx->core.logical_device, &pool_info,
                                             nullptr, &ctx->compute.descriptor_pool);

    if (result != VK_SUCCESS) {
        std::cerr << "[CANDY ERROR] Failed to create descriptor pool: " << result
                  << std::endl;
        CANDY_ASSERT(false, "Failed to create compute descriptor pool");
    }

    std::cout << "[CANDY DEBUG] Descriptor pool created: " << ctx->compute.descriptor_pool
              << std::endl;
}

void candy_create_compute_pipelines(candy_context *ctx) {
    VkPushConstantRange push_constant = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(uint32_t) * 3, // nx, ny, nz
    };

    const char *shader_paths[4] = {
        "../src/shaders/compute/first_half_kin.comp.spv",
        "../src/shaders/compute/full_potential.comp.spv",
        "../src/shaders/compute/last_half_kin.comp.spv",
        "../src/shaders/compute/visualize.comp.spv",
    };

    for (int i = 0; i < 4; ++i) {
        VkPipelineLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 1,
            .pSetLayouts = &ctx->compute.descriptor_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant,
        };

        VkResult result =
            vkCreatePipelineLayout(ctx->core.logical_device, &layout_info, nullptr,
                                   &ctx->compute.pipeline_layouts[i]);
        CANDY_ASSERT(result == VK_SUCCESS, "Failed to create compute pipeline layout");

        VkShaderModule shader =
            candy_create_compute_shader_module(ctx->core.logical_device, shader_paths[i]);

        VkPipelineShaderStageCreateInfo shader_stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shader,
            .pName = "main",
            .pSpecializationInfo = nullptr,
        };

        VkComputePipelineCreateInfo pipeline_info = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = shader_stage,
            .layout = ctx->compute.pipeline_layouts[i],
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };

        VkResult pipeline_result =
            vkCreateComputePipelines(ctx->core.logical_device, VK_NULL_HANDLE, 1,
                                     &pipeline_info, nullptr, &ctx->compute.pipelines[i]);
        CANDY_ASSERT(pipeline_result == VK_SUCCESS, "Failed to create compute pipeline");

        vkDestroyShaderModule(ctx->core.logical_device, shader, nullptr);
    }
}
void candy_create_compute_command_pool(candy_context *ctx) {
    candy_queue_family_indices indices =
        candy_find_queue_families(ctx->core.physical_device, ctx->core.surface);

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = indices.graphics_family, // Can use compute queue if available
    };

    VkResult result = vkCreateCommandPool(ctx->core.logical_device, &pool_info, nullptr,
                                          &ctx->compute.command_pool);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create compute command pool");

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = ctx->compute.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkResult cmd_result = vkAllocateCommandBuffers(ctx->core.logical_device, &alloc_info,
                                                   &ctx->compute.command_buffer);
    CANDY_ASSERT(cmd_result == VK_SUCCESS, "Failed to allocate compute command buffer");
}

void candy_init_compute_pipeline(candy_context *ctx) {
    candy_create_compute_descriptor_layout(ctx);
    candy_create_compute_descriptor_pool(ctx);
    candy_create_compute_buffers(ctx);
    candy_create_prob_density_buffer(ctx);
    candy_create_compute_descriptor_sets(ctx);
    candy_create_compute_pipelines(ctx);
    candy_create_compute_command_pool(ctx);

    candy_validate_compute_pipeline(ctx);

    std::cout << "[CANDY] Compute pipeline initialized\n";
}

void candy_cleanup_compute_pipeline(candy_context *ctx) {
    candy_cleanup_vkfft(ctx);

    vkDestroyBuffer(ctx->core.logical_device, ctx->compute.psi_freq_buffer, nullptr);
    vkFreeMemory(ctx->core.logical_device, ctx->compute.psi_freq_memory, nullptr);

    vkDestroyBuffer(ctx->core.logical_device, ctx->compute.kinetic_factor_buffer,
                    nullptr);
    vkFreeMemory(ctx->core.logical_device, ctx->compute.kinetic_factor_memory, nullptr);

    vkDestroyBuffer(ctx->core.logical_device, ctx->compute.potential_factor_buffer,
                    nullptr);
    vkFreeMemory(ctx->core.logical_device, ctx->compute.potential_factor_memory, nullptr);

    vkDestroyBuffer(ctx->core.logical_device, ctx->compute.prob_density_buffer, nullptr);
    vkFreeMemory(ctx->core.logical_device, ctx->compute.prob_density_memory, nullptr);

    vkDestroyCommandPool(ctx->core.logical_device, ctx->compute.command_pool, nullptr);

    for (int i = 0; i < 4; ++i) {
        vkDestroyPipeline(ctx->core.logical_device, ctx->compute.pipelines[i], nullptr);
        vkDestroyPipelineLayout(ctx->core.logical_device,
                                ctx->compute.pipeline_layouts[i], nullptr);
    }

    vkDestroyDescriptorPool(ctx->core.logical_device, ctx->compute.descriptor_pool,
                            nullptr);
    vkDestroyDescriptorSetLayout(ctx->core.logical_device, ctx->compute.descriptor_layout,
                                 nullptr);
}

// ============================================================================
// HOT RELOADING
// ============================================================================

void candy_reload_code(candy_context *ctx) {
#ifdef _WIN32
    CANDY_ASSERT(false, "Win is not implemented");
#endif // _WIN32

    if (ctx->game_module.dll_handle) {
        int dl_result = dlclose(ctx->game_module.dll_handle);
        CANDY_ASSERT(dl_result == 0, "Failed to close dll handle");
        ctx->game_module.dll_handle = nullptr;
    }

    // TODO: Implement lock file syste, for this instead time buffer
    usleep(100000); // 100ms

    const char *dll_path = "output/libgame.so";

    struct stat file_stat;
    if (stat(dll_path, &file_stat) != 0) {
        std::cerr << "[CANDY ERROR] Cannot access file for reload: " << dll_path
                  << std::endl;
        std::cerr << "              errno: " << strerror(errno) << std::endl;
        return;
    }

    ctx->game_module.dll_handle = dlopen(dll_path, RTLD_NOW | RTLD_LOCAL);

    if (ctx->game_module.dll_handle == nullptr) {
        const char *error = dlerror();
        std::cerr << "[CANDY ERROR] Failed to reload game module: " << dll_path
                  << std::endl;
        std::cerr << "              dlopen error: " << (error ? error : "unknown")
                  << std::endl;
        return;
    }

    dlerror();

    ctx->game_module.api.init = (void (*)(candy_context *, void *))dlsym(
        ctx->game_module.dll_handle, "game_init");
    ctx->game_module.api.update = (void (*)(candy_context *, void *, uint32_t))dlsym(
        ctx->game_module.dll_handle, "game_update");
    ctx->game_module.api.render = (void (*)(candy_context *, void *))dlsym(
        ctx->game_module.dll_handle, "game_render");
    ctx->game_module.api.cleanup = (void (*)(candy_context *, void *))dlsym(
        ctx->game_module.dll_handle, "game_cleanup");
    ctx->game_module.api.on_reload =
        (void (*)(void *, void *))dlsym(ctx->game_module.dll_handle, "game_on_reload");

    size_t *state_size_ptr =
        (size_t *)dlsym(ctx->game_module.dll_handle, "game_state_size");
    if (state_size_ptr) {
        ctx->game_module.api.state_size = *state_size_ptr;
    }

    const char *dlsym_error = dlerror();
    if (dlsym_error != nullptr) {
        std::cerr << "[CANDY ERROR] Failed to load dll symbols: " << dlsym_error
                  << std::endl;
        return;
    }

    struct complex_float {
        float real;
        float imaginary;
    };

    struct quant_state {
        std::vector<complex_float> psi;
        std::vector<float> potential;
        std::vector<float> prob_dens;
        std::vector<float> kx;
        std::vector<float> ky;
        std::vector<float> kz;
        std::vector<float> k_squared;
        std::vector<complex_float> kinetic_factor;
        std::vector<complex_float> potential_factor;
        float dx, dy, dz, time;
    };

    void *new_state = new (std::nothrow) quant_state();
    if (!new_state) {
        std::cerr << "[CANDY ERROR] Failed to allocate new game state" << std::endl;
        return;
    }

    if (ctx->game_module.api.on_reload && ctx->game_module.game_state) {
        ctx->game_module.api.on_reload(ctx->game_module.game_state, new_state);
    } else if (ctx->game_module.api.init) {
        ctx->game_module.api.init(ctx, new_state);
    }

    if (ctx->game_module.game_state) {
        delete static_cast<quant_state *>(ctx->game_module.game_state);
    }
    ctx->game_module.game_state = new_state;

    return;
}

void candy_check_hot_reload(candy_context *ctx) {
    if (!ctx->config.enable_hot_reloading) {
        return;
    }

    struct stat file_stat;
    const char *dll_path = "output/libgame.so";

    if (stat(dll_path, &file_stat) != 0) {
        // File doesn't exist or can't be accessed
        return;
    }

    if (file_stat.st_mtime > ctx->game_module.last_write_time) {
        std::cout << "[CANDY] Detected game module change, reloading..." << std::endl;
        ctx->game_module.last_write_time = file_stat.st_mtime;

        candy_reload_code(ctx);

        // Only increment if reload succeeded
        if (ctx->game_module.dll_handle) {
            ctx->game_module.reload_count++;
            std::cout << "[CANDY] Hot reload complete (reload #"
                      << ctx->game_module.reload_count << ")" << std::endl;
        } else {
            std::cerr << "[CANDY] Hot reload failed!" << std::endl;
        }
    }

    return;
}

void candy_cleanup_hot_reloading(candy_context *ctx) {

    if (ctx->game_module.api.cleanup) {
        ctx->game_module.api.cleanup(ctx, ctx->game_module.game_state);
    }

    if (ctx->game_module.game_state) {
        free(ctx->game_module.game_state);
    }

    if (ctx->game_module.dll_handle) {
        dlclose(ctx->game_module.dll_handle);
    }

    return;
}

void candy_init_game_module(candy_context *ctx) {
    if (!ctx->config.enable_hot_reloading) {
        std::cout << "[CANDY] Hot reloading disabled, skipping game module" << std::endl;
        return;
    }
    const char *dll_path = "output/libgame.so";
    struct stat file_stat;
    if (stat(dll_path, &file_stat) == 0) {
        ctx->game_module.last_write_time = file_stat.st_mtime;
    } else {
        std::cerr << "[CANDY ERROR] Cannot stat file: " << dll_path << std::endl;
        std::cerr << "              errno: " << strerror(errno) << std::endl;
    }
    ctx->game_module.dll_handle = dlopen(dll_path, RTLD_NOW);
    if (ctx->game_module.dll_handle == nullptr) {
        const char *error = dlerror();
        std::cerr << "[CANDY ERROR] Failed to load game module: " << dll_path
                  << std::endl;
        std::cerr << "              dlopen error: " << (error ? error : "unknown")
                  << std::endl;
        std::cerr << "              Current working directory: ";
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::cerr << cwd << std::endl;
        }
    }
    CANDY_ASSERT(ctx->game_module.dll_handle != nullptr, "Failed to load game module");
    dlerror();

    ctx->game_module.api.init = (void (*)(candy_context *, void *))dlsym(
        ctx->game_module.dll_handle, "game_init");
    ctx->game_module.api.update = (void (*)(candy_context *, void *, uint32_t))dlsym(
        ctx->game_module.dll_handle, "game_update");
    ctx->game_module.api.render = (void (*)(candy_context *, void *))dlsym(
        ctx->game_module.dll_handle, "game_render");
    ctx->game_module.api.cleanup = (void (*)(candy_context *, void *))dlsym(
        ctx->game_module.dll_handle, "game_cleanup");
    ctx->game_module.api.on_reload =
        (void (*)(void *, void *))dlsym(ctx->game_module.dll_handle, "game_on_reload");

    size_t *state_size_ptr =
        (size_t *)dlsym(ctx->game_module.dll_handle, "game_state_size");
    if (state_size_ptr) {
        ctx->game_module.api.state_size = *state_size_ptr;
    } else {
        std::cerr << "[CANDY ERROR] Failed to load game_state_size symbol" << std::endl;
        ctx->game_module.api.state_size = 0;
    }

    const char *dlsym_error = dlerror();
    if (dlsym_error != nullptr) {
        std::cerr << "[CANDY ERROR] Failed to load dll symbols: " << dlsym_error
                  << std::endl;
        return;
    }

    std::cout << "[CANDY] Game state size: " << ctx->game_module.api.state_size
              << " bytes" << std::endl;

    struct complex_float {
        float real;
        float imaginary;
    };

    struct quant_state {
        std::vector<complex_float> psi;
        std::vector<float> potential;
        std::vector<float> prob_dens;
        std::vector<float> kx;
        std::vector<float> ky;
        std::vector<float> kz;
        std::vector<float> k_squared;
        std::vector<complex_float> kinetic_factor;
        std::vector<complex_float> potential_factor;
        float dx, dy, dz, time;
    };

    ctx->game_module.game_state = new (std::nothrow) quant_state();
    CANDY_ASSERT(ctx->game_module.game_state != nullptr, "Failed to allocate game state");

    if (ctx->game_module.api.init) {
        ctx->game_module.api.init(ctx, ctx->game_module.game_state);
    }
    ctx->game_module.reload_count = 0;
    std::cout << "[CANDY] Game module loaded successfully" << std::endl;
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

void candy_create_command_pools(candy_context *ctx) {
    candy_queue_family_indices queue_family_indicies =
        candy_find_queue_families(ctx->core.physical_device, ctx->core.surface);

    for (uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; ++i) {
        VkCommandPoolCreateInfo pool_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = queue_family_indicies.graphics_family,
        };

        VkResult result = vkCreateCommandPool(ctx->core.logical_device, &pool_info,
                                              nullptr, &ctx->frame_data.command_pools[i]);
        CANDY_ASSERT(result == VK_SUCCESS, "Failed to create command pool");
    }

    return;
}

void candy_create_command_buffers(candy_context *ctx) {

    for (uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; ++i) {

        VkCommandBufferAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = ctx->frame_data.command_pools[i],
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkResult result = vkAllocateCommandBuffers(ctx->core.logical_device, &alloc_info,
                                                   &ctx->frame_data.command_buffers[i]);
        CANDY_ASSERT(result == VK_SUCCESS, "Failed to create command buffer");
    }
    return;
}

void candy_record_command_buffer(candy_context *ctx, uint32_t image_index,
                                 uint32_t cmd_buf_indx) {

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = 0, // INFO: might have to change this later
        .pInheritanceInfo = nullptr,
    };

    VkResult result =
        vkBeginCommandBuffer(ctx->frame_data.command_buffers[cmd_buf_indx], &begin_info);
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

    vkCmdBeginRenderPass(ctx->frame_data.command_buffers[cmd_buf_indx], &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(ctx->frame_data.command_buffers[cmd_buf_indx],
                      VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline.graphics_pipeline);

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)ctx->swapchain.extent.width,
        .height = (float)ctx->swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(ctx->frame_data.command_buffers[cmd_buf_indx], 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = ctx->swapchain.extent,
    };
    vkCmdSetScissor(ctx->frame_data.command_buffers[cmd_buf_indx], 0, 1, &scissor);

    VkBuffer vertex_buffers[] = {ctx->core.vertex_buffer};
    VkDeviceSize offsets[] = {0};
    // vkCmdBindVertexBuffers(ctx->frame_data.command_buffers[cmd_buf_indx], 0,
    // 1,vertex_buffers, offsets);

    // vkCmdDraw(ctx->frame_data.command_buffers[cmd_buf_indx], 3,
    // 1, 0, 0);

    struct {
        glm::mat4 viewProj;
        float threshold;
    } push_data;

    struct complex_float {
        float real;
        float imaginary;
    };

    struct quant_state {
        std::vector<complex_float> psi;
        std::vector<float> potential;
        std::vector<float> prob_dens;
        std::vector<float> kx;
        std::vector<float> ky;
        std::vector<float> kz;
        std::vector<float> k_squared;
        std::vector<complex_float> kinetic_factor;
        std::vector<complex_float> potential_factor;
        float dx, dy, dz, time;
        glm::mat4 view_proj_matrix;
        float density_threshold;
    };

    if (ctx->game_module.game_state && ctx->core.particle_count > 0) {
        quant_state *state = (quant_state *)ctx->game_module.game_state;
        push_data.viewProj = state->view_proj_matrix;
        push_data.threshold = state->density_threshold;
    } else {
        // Fallback: identity matrix
        push_data.viewProj = glm::mat4(1.0f);
        push_data.threshold = 0.001f;
    }

    // Push constants BEFORE binding vertex buffer
    vkCmdPushConstants(ctx->frame_data.command_buffers[cmd_buf_indx],
                       ctx->pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(push_data), &push_data);

    if (ctx->core.particle_count > 0) {
        VkBuffer particle_buffers[] = {ctx->core.particle_vertex_buffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(ctx->frame_data.command_buffers[cmd_buf_indx], 0, 1,
                               particle_buffers, offsets);
        vkCmdDraw(ctx->frame_data.command_buffers[cmd_buf_indx], ctx->core.particle_count,
                  1, 0, 0);
    }
    vkCmdEndRenderPass(ctx->frame_data.command_buffers[cmd_buf_indx]);

    candy_imgui_render(ctx, ctx->frame_data.command_buffers[cmd_buf_indx], image_index);

    VkResult result_end_cmd_buf =
        vkEndCommandBuffer(ctx->frame_data.command_buffers[cmd_buf_indx]);
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

    for (uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; ++i) {

        VkResult sema_result_img =
            vkCreateSemaphore(ctx->core.logical_device, &sema_create_info, nullptr,
                              &ctx->frame_data.image_available_semaphores[i]);
        CANDY_ASSERT(sema_result_img == VK_SUCCESS,
                     "failed to create image available semaphore");

        VkResult sema_result_rendr =
            vkCreateSemaphore(ctx->core.logical_device, &sema_create_info, nullptr,
                              &ctx->frame_data.render_finished_semaphores[i]);
        CANDY_ASSERT(sema_result_rendr == VK_SUCCESS,
                     "Failed to create render finished semaphore");

        VkResult fence_result =
            vkCreateFence(ctx->core.logical_device, &fence_create_info, nullptr,
                          &ctx->frame_data.in_flight_fences[i]);
        CANDY_ASSERT(fence_result == VK_SUCCESS, "Failed to create fence");
    }

    return;
}

void candy_draw_frame(candy_context *ctx) {
    vkWaitForFences(ctx->core.logical_device, 1,
                    &ctx->frame_data.in_flight_fences[ctx->frame_data.current_frame],
                    VK_TRUE,
                    UINT32_MAX); // For DoD we need to make arra of fences and
                                 // use that instead of 1 here
    uint32_t image_index;

    VkResult result_acq_img = vkAcquireNextImageKHR(
        ctx->core.logical_device, ctx->swapchain.handle, UINT64_MAX,
        ctx->frame_data.image_available_semaphores[ctx->frame_data.current_frame],
        VK_NULL_HANDLE,
        &image_index); // dont know if this is correct

    if (result_acq_img == VK_ERROR_OUT_OF_DATE_KHR) {
        ctx->swapchain.has_framebuffer_resized = true;
        candy_recreate_swapchain(ctx);
        return;
    } else {
        CANDY_ASSERT(result_acq_img == VK_SUCCESS || result_acq_img == VK_SUBOPTIMAL_KHR,
                     "Failed to acquire swapchain image");
    }
    vkResetFences(ctx->core.logical_device, 1,
                  &ctx->frame_data.in_flight_fences[ctx->frame_data.current_frame]);

    VkSemaphore wait_semaphores[] = {
        ctx->frame_data.image_available_semaphores[ctx->frame_data.current_frame]};
    VkSemaphore signal_semaphores[] = {
        ctx->frame_data.render_finished_semaphores[ctx->frame_data.current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    // candy_imgui_new_frame(ctx);

    vkResetCommandBuffer(ctx->frame_data.command_buffers[ctx->frame_data.current_frame],
                         0);
    candy_record_command_buffer(ctx, image_index, ctx->frame_data.current_frame);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = wait_semaphores,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers =
            &ctx->frame_data.command_buffers[ctx->frame_data.current_frame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signal_semaphores,
    };

    VkResult result =
        vkQueueSubmit(ctx->core.graphics_queue, 1, &submit_info,
                      ctx->frame_data.in_flight_fences[ctx->frame_data.current_frame]);
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

    VkResult result_q_pres = vkQueuePresentKHR(ctx->core.present_queue, &present_info);
    if (result_q_pres == VK_ERROR_OUT_OF_DATE_KHR) {
        candy_recreate_swapchain(ctx);
        return;
    } else {
        CANDY_ASSERT(result_q_pres == VK_SUCCESS || result_q_pres == VK_SUBOPTIMAL_KHR,
                     "Failed to persent swapchain image");
    }
    ctx->frame_data.current_frame =
        (ctx->frame_data.current_frame + 1) % MAX_FRAME_IN_FLIGHT;

    return;
}

// ============================================================================
// GRAPHICS PIPELINE
// ============================================================================

uint32_t candy_find_memory_type(candy_context *ctx, uint32_t type_filter,
                                VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(ctx->core.physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if (type_filter & (1 << i) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }

    CANDY_ASSERT(false, "Failed to find suitable memory");

    return UINT32_MAX;
}

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
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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

void candy_create_vertex_buffer(candy_context *ctx) {

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = sizeof(vertices[0]) * vertices.size(),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    VkResult result = vkCreateBuffer(ctx->core.logical_device, &buffer_info, nullptr,
                                     &ctx->core.vertex_buffer);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create vertex buffer");

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->core.logical_device, ctx->core.vertex_buffer,
                                  &mem_reqs);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = candy_find_memory_type(
            ctx, mem_reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    VkResult result_alloc_mem = vkAllocateMemory(
        ctx->core.logical_device, &alloc_info, nullptr, &ctx->core.vertex_buffer_memory);

    CANDY_ASSERT(result_alloc_mem == VK_SUCCESS, "Failed to allocate memory");

    vkBindBufferMemory(ctx->core.logical_device, ctx->core.vertex_buffer,
                       ctx->core.vertex_buffer_memory, 0);

    void *data;
    vkMapMemory(ctx->core.logical_device, ctx->core.vertex_buffer_memory, 0,
                buffer_info.size, 0, &data);
    memcpy(data, vertices.data(), buffer_info.size);
    vkUnmapMemory(ctx->core.logical_device, ctx->core.vertex_buffer_memory);

    return;
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

    // auto bindings_description = candy_vertex::get_bindings_description();
    // auto attribute_description = candy_vertex::get_attribute_description();

    auto bindings_description = candy_particle::get_bindings_description();
    auto attribute_description = candy_particle::get_attribute_description();

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {
        // We can in future use this for vars inside vertex shader
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindings_description,
        .vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attribute_description.size()),
        .pVertexAttributeDescriptions = attribute_description.data(),
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        // We can use this to draw other things than triangles; set in topology
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
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

    VkPushConstantRange push_constant = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(glm::mat4) + sizeof(float), // mat4 viewProj + float threshold
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant,
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

void candy_create_swapchain(candy_context *ctx) {
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

void candy_recreate_swapchain(candy_context *ctx) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(ctx->core.window, &width, &height);

    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(ctx->core.window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(ctx->core.logical_device);
    candy_destroy_swapchain(ctx);
    candy_create_swapchain(ctx);
    candy_create_image_views(ctx);
    candy_create_framebuffers(ctx);
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

void candy_destroy_swapchain(candy_context *ctx) {
    for (size_t i = 0; i < ctx->swapchain.image_count; ++i) {
        vkDestroyFramebuffer(ctx->core.logical_device, ctx->swapchain.framebuffers[i],
                             nullptr);
    }

    for (size_t i = 0; i < MAX_SWAPCHAIN_IMAGES; ++i) {
        vkDestroyImageView(ctx->core.logical_device, ctx->swapchain.image_views[i],
                           nullptr);
    }

    vkDestroySwapchainKHR(ctx->core.logical_device, ctx->swapchain.handle, nullptr);
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

// ============================================================================
// PUBLIC API
// ============================================================================

void candy_init(candy_context *ctx) {
    ctx->config = {
        .width = 1920,
        .height = 1080,
        .enable_validation = ENABLE_VALIDATION,
        .enable_hot_reloading = true,
        .app_name = "Candy Renderer",
        .window_title = "Candy Window",
    };

    // Init GLFW
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    ctx->core.window = glfwCreateWindow(ctx->config.width, ctx->config.height,
                                        ctx->config.window_title, nullptr, nullptr);

    CANDY_ASSERT(ctx->core.window != nullptr, "Failed to create window");
    glfwSetWindowUserPointer(ctx->core.window, ctx);
    glfwSetFramebufferSizeCallback(ctx->core.window, candy_framebuffer_resize_callback);

    // Init Vulkan
    candy_init_vulkan_instance(&ctx->core, &ctx->config);
    candy_init_surface(&ctx->core);
    candy_init_physical_device(&ctx->core);
    candy_init_logical_device(ctx);
    candy_create_swapchain(ctx);
    candy_init_imgui(ctx);
    candy_create_image_views(ctx);
    candy_create_render_pass(ctx);
    candy_create_graphics_pipeline(ctx);
    candy_create_framebuffers(ctx);
    candy_create_command_pools(ctx);
    candy_create_vertex_buffer(ctx);
    candy_create_command_buffers(ctx);
    candy_create_sync_objs(ctx);
    candy_init_compute_pipeline(ctx);

    candy_init_game_module(ctx);

    std::cout << "[CANDY] Init complete\n";
}

void candy_cleanup(candy_context *ctx) {
    vkDeviceWaitIdle(ctx->core.logical_device);

    candy_destroy_swapchain(ctx);

    candy_cleanup_imgui(ctx);

    vkDestroyPipeline(ctx->core.logical_device, ctx->pipeline.graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(ctx->core.logical_device, ctx->pipeline.pipeline_layout,
                            nullptr);
    vkDestroyRenderPass(ctx->core.logical_device, ctx->pipeline.render_pass, nullptr);

    vkDestroyBuffer(ctx->core.logical_device, ctx->core.vertex_buffer, nullptr);
    vkFreeMemory(ctx->core.logical_device, ctx->core.vertex_buffer_memory, nullptr);

    for (uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; ++i) {
        vkDestroySemaphore(ctx->core.logical_device,
                           ctx->frame_data.image_available_semaphores[i], nullptr);
        vkDestroySemaphore(ctx->core.logical_device,
                           ctx->frame_data.render_finished_semaphores[i], nullptr);
        vkDestroyFence(ctx->core.logical_device, ctx->frame_data.in_flight_fences[i],
                       nullptr);
    }

    for (uint32_t i = 0; i < MAX_FRAME_IN_FLIGHT; ++i) {
        vkDestroyCommandPool(ctx->core.logical_device, ctx->frame_data.command_pools[i],
                             nullptr);
    }

    candy_cleanup_compute_pipeline(ctx);

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

    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(ctx->core.window)) {
        glfwPollEvents();

        candy_check_hot_reload(ctx);
        double curr_time = glfwGetTime();
        double delta_time = (curr_time - last_time) * 1000.0;
        last_time = curr_time;

        candy_imgui_new_frame(ctx);

        if (ctx->game_module.api.update) {
            ctx->game_module.api.update(ctx, ctx->game_module.game_state, delta_time);
        }

        if (ctx->game_module.game_state) {
            std::cout << "[CANDY DEBUG] Starting quantum timestep..." << std::endl;
            candy_quantum_timestep(ctx);
            std::cout << "[CANDY DEBUG] Quantum timestep complete" << std::endl;

            std::cout << "[CANDY DEBUG] Updating particles..." << std::endl;
            candy_update_particle_vertices(ctx, ctx->game_module.game_state);
            std::cout << "[CANDY DEBUG] Particles updated" << std::endl;
        }

        if (ctx->game_module.api.render) {
            ctx->game_module.api.render(ctx, ctx->game_module.game_state);
        }

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
