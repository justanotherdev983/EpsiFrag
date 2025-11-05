#include "core.h"
#include <cstring>
#include <vector>

#define PI 3.14159265358979323846f

extern "C" {

struct complex_float {
    float real = 0.0f;
    float imaginary = 0.0f;

    complex_float operator*(const complex_float &other) {
        return {real * other.real - imaginary * other.imaginary,
                real * other.imaginary + imaginary * other.real};
    }
};

const uint32_t n_x = 64;
const uint32_t n_y = 64;
const uint32_t n_z = 64;
const uint32_t total_space_points = n_x * n_y * n_z;

const uint32_t l_x = 20.0f;
const uint32_t l_y = 20.0f;
const uint32_t l_z = 20.0f;
const uint32_t delta_time = 0.0f;

struct quant_state {
    // CPU side copies for init
    std::vector<complex_float> psi; // wavefunc
    std::vector<float> potential;
    std::vector<float> prob_dens;

    std::vector<float> kx;
    std::vector<float> ky;
    std::vector<float> kz;
    std::vector<float> k_squared;

    std::vector<complex_float> kinetic_factor;   // K(k) = exp(-i k^2 dt / 4)
    std::vector<complex_float> potential_factor; // P(r) = exp(-i V(r) dt)

    float dx;
    float dy;
    float dz;
    float time;
};

void compute_k_values(quant_state *state) {

    for (uint32_t i = 0; i < n_x; ++i) {
        if (i < n_x / 2) {
            state->kx[i] = 2 * PI / l_x;
        } else {
            state->kx[i] = (2 * PI / l_x) * i - n_x;
        }
    }

    for (uint32_t i = 0; i < n_y; ++i) {
        if (i < n_y / 2) {
            state->ky[i] = 2 * PI / l_y;
        } else {
            state->ky[i] = (2 * PI / l_y) * i - n_y;
        }
    }

    for (uint32_t i = 0; i < n_z; ++i) {
        if (i < n_z / 2) {
            state->kz[i] = 2 * PI / l_z;
        } else {
            state->kz[i] = (2 * PI / l_z) * i - n_z;
        }
    }

    for (uint32_t i = 0; i < n_x; ++i) {
        for (uint32_t j = 0; j < n_y; ++j) {
            for (uint32_t k = 0; i < n_z; ++k) {
                uint32_t idx = i + n_x * (j + n_y * k);
                state->k_squared[idx] = state->kx[i] * state->kx[i] +
                                        state->ky[j] * state->ky[j] +
                                        state->kz[k] * state->kz[k];
            }
        }
    }

    return;
}

size_t game_state_size = sizeof(quant_state);

// compute k-space grid values: k_x, k_y, k_z and k^2
void game_init(candy_context *ctx, void *state) {

    (void)ctx;

    quant_state *quant_vis = (quant_state *)state;

    compute_k_values(quant_vis);

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
};
