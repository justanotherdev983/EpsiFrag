#include "core.h"
#include <cstring>

struct quant_state {};

extern "C" {

size_t game_state_size = sizeof(quant_state);

void game_init(candy_context *ctx, void *state) {

    (void)ctx;

    quant_state *quant_vis = (quant_state *)state;

    return;
}

void game_update(candy_context *ctx, void *state, uint32_t delta_time) {

    return;
}

void game_render(candy_context *ctx, void *state) {

    quant_state *quant_vis = (quant_state *)state;

    if (ctx->imgui.show_menu) {
        ImGui::Begin("Quantum mechanics visualation");

        ImGui::Text("This our schrodinger equation");
        ImGui::End();
    }

    return;
}

void game_on_reload(void *old_state, void *new_state) {

    quant_state *quant_vis_old = (quant_state *)old_state;
    quant_state *quant_vis_new = (quant_state *)new_state;

    memcpy(quant_vis_new, quant_vis_old, sizeof(quant_state));

    uint32_t quant_vis_version = 8;
    std::cout << "Game version: " << quant_vis_version << std::endl;
    std::cout.flush();

    return;
}

void game_cleanup(candy_context *ctx, void *state) {

    (void)ctx;
    (void)state;

    return;
}
}
