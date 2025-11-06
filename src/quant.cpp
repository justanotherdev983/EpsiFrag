#include "core.h"
#include <cstring>
#include <vector>

#define PI 3.14159265358979323846f

struct complex_float {
    float real = 0.0f;
    float imaginary = 0.0f;

    complex_float operator*(const complex_float &other) {
        return {real * other.real - imaginary * other.imaginary,
                real * other.imaginary + imaginary * other.real};
    }
};

extern "C" {
const uint32_t n_x = 64;
const uint32_t n_y = 64;
const uint32_t n_z = 64;
const uint32_t total_space_points = n_x * n_y * n_z;

const float l_x = 20.0f;
const float l_y = 20.0f;
const float l_z = 20.0f;
const float delta_time = 0.01f;

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
            state->kx[i] = (2 * PI / l_x) * i;
        } else {
            state->kx[i] = (2 * PI / l_x) * i - n_x;
        }
    }

    for (uint32_t i = 0; i < n_y; ++i) {
        if (i < n_y / 2) {
            state->ky[i] = (2 * PI / l_y) * i;
        } else {
            state->ky[i] = (2 * PI / l_y) * i - n_y;
        }
    }

    for (uint32_t i = 0; i < n_z; ++i) {
        if (i < n_z / 2) {
            state->kz[i] = (2 * PI / l_z) * i;
        } else {
            state->kz[i] = (2 * PI / l_z) * i - n_z;
        }
    }

    for (uint32_t i = 0; i < n_x; ++i) {
        for (uint32_t j = 0; j < n_y; ++j) {
            for (uint32_t k = 0; k < n_z; ++k) {
                uint32_t idx = i + n_x * (j + n_y * k);
                state->k_squared[idx] = state->kx[i] * state->kx[i] +
                                        state->ky[j] * state->ky[j] +
                                        state->kz[k] * state->kz[k];
            }
        }
    }

    return;
}

// for simplicity we made h_bar and m = 1
void compute_kinetic_factors(quant_state *state) {

    std::vector<complex_float> result;

    // We can use Euler's formula: exp(i*theta) =
    // cos(theta) + i*sin(theta)
    for (uint32_t i = 0; i < total_space_points; ++i) {
        float theta = state->k_squared[i] * delta_time / 4.0f;

        result[i].real = std::cos(theta);
        result[i].imaginary = -std::sin(theta);
    }
    state->kinetic_factor = result;
}

size_t game_state_size = sizeof(quant_state);

void compute_potential_factors(quant_state *state) {
    for (uint32_t i = 0; i < total_space_points; ++i) {
        float theta = state->potential[i] * delta_time;

        state->potential_factor[i].real = std::cos(theta);
        state->potential_factor[i].imaginary = -std::sin(theta);
    }
}

void init_free_particle_potential(quant_state *state) {
    // V = 0 everywhere (free particle for now; we can do harmonic oscillator later)
    for (uint32_t i = 0; i < total_space_points; ++i) {
        state->potential[i] = 0.0f;
    }
}

void init_wave_function(quant_state *state, float sigma) {
    float A = (PI * sigma * sigma) * std::exp(-3 / 4);
    for (uint32_t i = 0; i < total_space_points; ++i) {
        float gaussian_part = A;
    }
}

// compute k-space grid values: k_x, k_y, k_z and k^2
void game_init(candy_context *ctx, void *state) {

    (void)ctx;

    quant_state *quant_vis = (quant_state *)state;

    quant_vis->psi.resize(total_space_points);

    quant_vis->potential.resize(total_space_points);

    quant_vis->prob_dens.resize(total_space_points);

    quant_vis->kx.resize(total_space_points);

    quant_vis->ky.resize(total_space_points);

    quant_vis->kz.resize(total_space_points);

    quant_vis->k_squared.resize(total_space_points);

    quant_vis->kinetic_factor.resize(total_space_points);

    quant_vis->potential_factor.resize(total_space_points);

    compute_k_values(quant_vis);
    init_free_particle_potential(quant_vis);
    compute_kinetic_factors(quant_vis);
    compute_potential_factors(quant_vis);

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
