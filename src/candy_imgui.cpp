#include "candy_imgui.h"

// ============================================================================
// IMGUI INTEGRATION
// ============================================================================

void candy_imgui_check_result(VkResult err) {
    if (err == 0)
        return;
    std::cerr << "[CANDY IMGUI] Vulkan Error: " << err << std::endl;
    if (err < 0)
        abort();
}

void candy_create_imgui_descriptor_pool(candy_context *ctx) {
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1000,
        .poolSizeCount = 11,
        .pPoolSizes = pool_sizes,
    };

    VkResult result = vkCreateDescriptorPool(ctx->core.logical_device, &pool_info,
                                             nullptr, &ctx->imgui.descriptor_pool);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create ImGui descriptor pool");
}

void candy_create_imgui_render_pass(candy_context *ctx) {
    VkAttachmentDescription attachment = {
        .flags = 0,
        .format = ctx->swapchain.image_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, // Load existing content
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_attachment = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
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

    VkRenderPassCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    VkResult result = vkCreateRenderPass(ctx->core.logical_device, &info, nullptr,
                                         &ctx->imgui.render_pass);
    CANDY_ASSERT(result == VK_SUCCESS, "Failed to create ImGui render pass");
}

void candy_init_imgui(candy_context *ctx) {
    candy_create_imgui_descriptor_pool(ctx);
    candy_create_imgui_render_pass(ctx);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(ctx->core.window, true);

    int window_width, window_height;
    int framebuffer_width, framebuffer_height;
    glfwGetWindowSize(ctx->core.window, &window_width, &window_height);
    glfwGetFramebufferSize(ctx->core.window, &framebuffer_width, &framebuffer_height);

    float scale_x = (float)framebuffer_width / (float)window_width;
    float scale_y = (float)framebuffer_height / (float)window_height;

    io.DisplaySize = ImVec2((float)window_width, (float)window_height);
    io.DisplayFramebufferScale = ImVec2(scale_x, scale_y);

    std::cout << "[CANDY IMGUI] Window size: " << window_width << "x" << window_height
              << std::endl;
    std::cout << "[CANDY IMGUI] Framebuffer size: " << framebuffer_width << "x"
              << framebuffer_height << std::endl;
    std::cout << "[CANDY IMGUI] Scale: " << scale_x << "x" << scale_y << std::endl;

    // Load Vulkan functions
    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_0,
        [](const char *function_name, void *user_data) -> PFN_vkVoidFunction {
            VkInstance instance = *(VkInstance *)user_data;
            return vkGetInstanceProcAddr(instance, function_name);
        },
        &ctx->core.instance);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = ctx->core.instance;
    init_info.PhysicalDevice = ctx->core.physical_device;
    init_info.Device = ctx->core.logical_device;
    init_info.QueueFamily = ctx->core.graphics_queue_family;
    init_info.Queue = ctx->core.graphics_queue;
    init_info.ApiVersion = VK_API_VERSION_1_0;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = ctx->imgui.descriptor_pool;
    init_info.MinImageCount = MAX_FRAME_IN_FLIGHT;
    init_info.ImageCount = ctx->swapchain.image_count;
    init_info.Allocator = nullptr;
    init_info.PipelineInfoMain.RenderPass = ctx->imgui.render_pass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = candy_imgui_check_result;

    ImGui_ImplVulkan_Init(&init_info);

    ctx->imgui.initialized = true;
    ctx->imgui.show_menu = true;
    ctx->imgui.menu_alpha = 1.0f;
}

void candy_imgui_new_frame(candy_context *ctx) {
    ImGuiIO &io = ImGui::GetIO();

    int window_width, window_height;
    int framebuffer_width, framebuffer_height;
    glfwGetWindowSize(ctx->core.window, &window_width, &window_height);
    glfwGetFramebufferSize(ctx->core.window, &framebuffer_width, &framebuffer_height);

    float scale_x =
        (window_width > 0) ? ((float)framebuffer_width / (float)window_width) : 1.0f;
    float scale_y =
        (window_height > 0) ? ((float)framebuffer_height / (float)window_height) : 1.0f;

    io.DisplaySize = ImVec2((float)window_width, (float)window_height);
    io.DisplayFramebufferScale = ImVec2(scale_x, scale_y);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void candy_imgui_render_menu(candy_context *ctx) {
    if (!ctx->imgui.show_menu)
        return;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(ctx->imgui.menu_alpha);

    if (ImGui::Begin("Candy Game Menu", &ctx->imgui.show_menu)) {
        ImGui::Text("Welcome to Candy Engine!");
        ImGui::Separator();

        ImGui::Text("Rendering:");
        ImGui::Text("  FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("  Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

        // Display debug info
        ImGuiIO &io = ImGui::GetIO();
        ImGui::Text("  Display: %.0fx%.0f", io.DisplaySize.x, io.DisplaySize.y);
        ImGui::Text("  Scale: %.2fx%.2f", io.DisplayFramebufferScale.x,
                    io.DisplayFramebufferScale.y);

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Settings")) {
            ImGui::SliderFloat("Menu Alpha", &ctx->imgui.menu_alpha, 0.0f, 1.0f);

            ImGui::Text("Resolution: %dx%d", ctx->swapchain.extent.width,
                        ctx->swapchain.extent.height);
        }

        ImGui::Separator();

        if (ImGui::CollapsingHeader("Game Options")) {
            static bool vsync = true;
            static int difficulty = 1;

            ImGui::Checkbox("VSync", &vsync);
            ImGui::Combo("Difficulty", &difficulty, "Easy\0Medium\0Hard\0");

            if (ImGui::Button("Start Game", ImVec2(-1, 0))) {
                std::cout << "[CANDY] Starting game...\n";
            }

            if (ImGui::Button("Load Game", ImVec2(-1, 0))) {
                std::cout << "[CANDY] Loading game...\n";
            }
        }

        ImGui::Separator();

        if (ImGui::Button("Exit", ImVec2(-1, 0))) {
            glfwSetWindowShouldClose(ctx->core.window, GLFW_TRUE);
        }
    }
    ImGui::End();

    // Demo window for reference
    static bool show_demo = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Debug")) {
            ImGui::MenuItem("Show Demo", NULL, &show_demo);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (show_demo) {
        ImGui::ShowDemoWindow(&show_demo);
    }
}

void candy_imgui_render(candy_context *ctx, VkCommandBuffer cmd_buffer,
                        uint32_t image_index) {
    candy_imgui_render_menu(ctx);
    ImGui::Render();

    VkRenderPassBeginInfo info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = ctx->imgui.render_pass,
        .framebuffer = ctx->swapchain.framebuffers[image_index],
        .renderArea.extent = ctx->swapchain.extent,
        .clearValueCount = 0,
        .pClearValues = nullptr,
    };

    vkCmdBeginRenderPass(cmd_buffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_buffer);
    vkCmdEndRenderPass(cmd_buffer);
}

void candy_cleanup_imgui(candy_context *ctx) {
    if (!ctx->imgui.initialized)
        return;

    vkDeviceWaitIdle(ctx->core.logical_device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    vkDestroyRenderPass(ctx->core.logical_device, ctx->imgui.render_pass, nullptr);
    vkDestroyDescriptorPool(ctx->core.logical_device, ctx->imgui.descriptor_pool,
                            nullptr);
}
