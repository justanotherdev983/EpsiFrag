#include "core.h"

#define MAX_PLAYERS 16

struct player {
    struct position {
        float x;
        float y;
        float z;
    } position;

    uint32_t kills;
};

struct game_state {
    player players[MAX_PLAYERS];
    time_t curr_time;
};

void game_init(candy_context *ctx, void *state) {

    game_state *game_state = (game_state *)state;

    for (uint32_t i = 0; i < MAX_PLAYERS; ++i) {
        game_state->players[i].position = {
            .x = 0.0f,
            .y = 0.0f,
            .z = 0.0f,
        };
        game_state->players[i].kills = 0;
    }

    return;
}

void game_update(candy_context *ctx, void *state, uint32_t delta_time) {

    game_state *game_state = (game_state *)state;

    if (glfwGetKey(ctx->core.window, GLFW_KEY_W) == GLFW_PRESS) {
        game_state->players[0].position.y += 0.001f * delta_time;
    }
    if (glfwGetKey(ctx->core.window, GLFW_KEY_S) == GLFW_PRESS) {
        game_state->players[0].position.y -= 0.001f * delta_time;
    }
    if (glfwGetKey(ctx->core.window, GLFW_KEY_A) == GLFW_PRESS) {
        game_state->players[0].position.x -= 0.001f * delta_time;
    }
    if (glfwGetKey(ctx->core.window, GLFW_KEY_D) == GLFW_PRESS) {
        game_state->players[0].position.x += 0.001f * delta_time;
    }
    return;
}

void game_render(candy_context *ctx, void *state) {

    game_state *game_state = (game_state *)state;

    if (ctx->imgui.show_menu) {
        ImGui::Begin("Game State");
        for (uint32_t i = 0; i < MAX_PLAYERS; ++i) {

            ImGui::Text("Player[%i] Position: (%.2f, %.2f)", i,
                        game_state->players[i].position.x,
                        game_state->players[i].position.y);

            ImGui::Text("Kills for player: %i: %d", i, game_state->kills);
        }
        ImGui::End();
    }

    return;
}

void game_on_reload(void *old_state, void *new_state) {

    game_state *old_game = (game_state *)old_state;

    game_state *new_game = (game_state *)new_state;

    return;
}

void game_cleanup(candy_context *ctx) {
    return;
}
