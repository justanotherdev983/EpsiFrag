#pragma once

#include "core.h"

void candy_imgui_check_result(VkResult err);
void candy_create_imgui_descriptor_pool(candy_context *ctx);
void candy_create_imgui_render_pass(candy_context *ctx);
void candy_init_imgui(candy_context *ctx);
void candy_imgui_new_frame(candy_context *ctx);
void candy_imgui_render_menu(candy_context *ctx);
void candy_imgui_render(candy_context *ctx, VkCommandBuffer cmd_buffer,
                        uint32_t image_index);
void candy_cleanup_imgui(candy_context *ctx);
