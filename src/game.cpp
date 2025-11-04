#include "core.h"
#include <cstring>

#define MAX_PLAYERS 16

struct player {
    struct position {
        float x;
        float y;
        float z;
    } position;

    uint32_t kill_count;
};

struct game_state {
    player players[MAX_PLAYERS];
    time_t curr_time;
};

extern "C" {

size_t game_state_size = sizeof(game_state);
}

void game_init(candy_context *ctx, void *state) {

    (void)ctx;

    game_state *game = (game_state *)state;

    for (uint32_t i = 0; i < MAX_PLAYERS; ++i) {
        game->players[i].position = {
            .x = 0.0f,
            .y = 0.0f,
            .z = 0.0f,
        };
        game->players[i].kill_count = 0;
    }

    return;
}

void game_update(candy_context *ctx, void *state, uint32_t delta_time) {

    game_state *game = (game_state *)state;

    if (glfwGetKey(ctx->core.window, GLFW_KEY_W) == GLFW_PRESS) {
        game->players[0].position.y += 0.001f * delta_time;
    }
    if (glfwGetKey(ctx->core.window, GLFW_KEY_S) == GLFW_PRESS) {
        game->players[0].position.y -= 0.001f * delta_time;
    }
    if (glfwGetKey(ctx->core.window, GLFW_KEY_A) == GLFW_PRESS) {
        game->players[0].position.x -= 0.001f * delta_time;
    }
    if (glfwGetKey(ctx->core.window, GLFW_KEY_D) == GLFW_PRESS) {
        game->players[0].position.x += 0.001f * delta_time;
    }

    return;
}

void game_render(candy_context *ctx, void *state) {

    game_state *game = (game_state *)state;

    if (ctx->imgui.show_menu) {
        ImGui::Begin("Game State idiot");
        for (uint32_t i = 0; i < MAX_PLAYERS; ++i) {

            ImGui::Text("Player[%i] Position: (%.2f, %.2f)", i,
                        game->players[i].position.x, game->players[i].position.y);

            ImGui::Text("Kills for player: %i: %d", i, game->players[i].kill_count);
        }
        ImGui::End();
    }

    return;
}

void game_on_reload(void *old_state, void *new_state) {

    game_state *old_game = (game_state *)old_state;

    game_state *new_game = (game_state *)new_state;

    memcpy(new_game, old_game, sizeof(game_state));

    uint32_t version = 8;
    std::cout << "Game version: " << version << std::endl;
    std::cout.flush();

    return;
}

void game_cleanup(candy_context *ctx, void *state) {

    (void)ctx;
    (void)state;

    return;
}
