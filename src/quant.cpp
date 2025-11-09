#include "core.h"
#include <cmath>
#include <cstring>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define PI 3.14159265358979323846f

extern "C" {
const uint32_t n_x = 64;
const uint32_t n_y = 64;
const uint32_t n_z = 64;
const uint32_t total_space_points = n_x * n_y * n_z;

const float l_x = 20.0f;
const float l_y = 20.0f;
const float l_z = 20.0f;
const float delta_time = 0.01f;

struct complex_float {
    float real = 0.0f;
    float imaginary = 0.0f;

    complex_float operator*(const complex_float &other) {
        return {real * other.real - imaginary * other.imaginary,
                real * other.imaginary + imaginary * other.real};
    }
};

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

    glm::mat4 view_proj_matrix;
    float density_threshold;
};

void compute_k_values(quant_state *state) {
    for (uint32_t i = 0; i < n_x; ++i) {
        if (i < n_x / 2) {
            state->kx[i] = (2.0f * PI / l_x) * i;
        } else {
            state->kx[i] = (2.0f * PI / l_x) * (i - n_x);
        }
    }

    for (uint32_t i = 0; i < n_y; ++i) {
        if (i < n_y / 2) {
            state->ky[i] = (2.0f * PI / l_y) * i;
        } else {
            state->ky[i] = (2.0f * PI / l_y) * (i - n_y);
        }
    }

    for (uint32_t i = 0; i < n_z; ++i) {
        if (i < n_z / 2) {
            state->kz[i] = (2.0f * PI / l_z) * i;
        } else {
            state->kz[i] = (2.0f * PI / l_z) * (i - n_z);
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
}

// for simplicity we made h_bar and m = 1
void compute_kinetic_factors(quant_state *state) {

    std::vector<complex_float> result(total_space_points);

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

void init_wave_function(quant_state *state, float x0, float y0, float z0, float sigma,
                        float k0x, float k0y, float k0z) {
    float A = std::pow(PI * sigma * sigma, -0.75f);

    float cx = l_x / 2.0f;
    float cy = l_y / 2.0f;
    float cz = l_z / 2.0f;

    for (uint32_t i = 0; i < n_x; ++i) {
        for (uint32_t j = 0; j < n_y; ++j) {
            for (uint32_t k = 0; k < n_z; ++k) {
                uint32_t idx = i + n_x * (j + n_y * k);

                float x = i * state->dx - cx;
                float y = j * state->dy - cy;
                float z = k * state->dz - cz;

                float dx = x - x0;
                float dy = y - y0;
                float dz = z - z0;

                float r_squared = dx * dx + dy * dy + dz * dz;

                float gaussian_part = A * std::exp(-r_squared / (4.0f * sigma * sigma));

                float phase = k0x * x + k0y * y + k0z * z;

                state->psi[idx].real = gaussian_part * std::cos(phase);
                state->psi[idx].imaginary = gaussian_part * std::sin(phase);
            }
        }
    }
}

void game_init(candy_context *ctx, void *state) {
    (void)ctx;

    quant_state *quant_vis = (quant_state *)state;

    // Resize all vectors
    quant_vis->psi.resize(total_space_points);
    quant_vis->potential.resize(total_space_points);
    quant_vis->prob_dens.resize(total_space_points);
    quant_vis->kx.resize(n_x);
    quant_vis->ky.resize(n_y);
    quant_vis->kz.resize(n_z);
    quant_vis->k_squared.resize(total_space_points);
    quant_vis->kinetic_factor.resize(total_space_points);
    quant_vis->potential_factor.resize(total_space_points);

    quant_vis->dx = l_x / n_x;
    quant_vis->dy = l_y / n_y;
    quant_vis->dz = l_z / n_z;
    quant_vis->time = 0.0f;

    quant_vis->density_threshold = 0.001f;

    // Simple camera setup
    glm::vec3 eye = glm::vec3(30.0f, 30.0f, 30.0f);
    glm::vec3 center = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    glm::mat4 view = glm::lookAt(eye, center, up);
    glm::mat4 proj =
        glm::perspective(glm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 100.0f);
    proj[1][1] *= -1; // Flip Y for Vulkan

    quant_vis->view_proj_matrix = proj * view;

    // Compute initial values on CPU
    compute_k_values(quant_vis);
    init_free_particle_potential(quant_vis);
    compute_kinetic_factors(quant_vis);
    compute_potential_factors(quant_vis);
    init_wave_function(quant_vis, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);

    // if (!ctx->compute.descriptor_layout) {
    //  candy_init_compute_pipeline(ctx);
    //}

    candy_upload_compute_data(ctx, state);
    candy_init_vkfft(ctx);

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

    if (quant_vis_old && quant_vis_new) {
        *quant_vis_new = *quant_vis_old;
    }

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

/*
#include "core.h"
#include <cmath>
#include <cstring>
// We no longer need <vector> in the shared state
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define PI 3.14159265358979323846f

extern "C" {
// These constants define the simulation grid and are safe to keep here.
const uint32_t n_x = 64;
const uint32_t n_y = 64;
const uint32_t n_z = 64;
const uint32_t total_space_points = n_x * n_y * n_z;

const float l_x = 20.0f;
const float l_y = 20.0f;
const float l_z = 20.0f;
const float delta_time = 0.01f;

// This struct is safe as it contains no memory-managing objects.
struct complex_float {
    float real = 0.0f;
    float imaginary = 0.0f;

    complex_float operator*(const complex_float &other) {
        return {real * other.real - imaginary * other.imaginary,
                real * other.imaginary + imaginary * other.real};
    }
};

// ============================================================================
// CRITICAL CHANGE: The quant_state is now a POD (Plain Old Data) struct.
// It contains only pointers. The main application will allocate/deallocate the memory.
// ============================================================================
struct quant_state {
    // CPU side data is now represented by raw pointers.
    complex_float *psi;
    float *potential;
    float *prob_dens; // Note: prob_dens is calculated on GPU, so this CPU buffer may not
                      // be needed.

    float *kx;
    float *ky;
    float *kz;
    float *k_squared;

    complex_float *kinetic_factor;
    complex_float *potential_factor;

    // Scalar values are fine.
    float dx;
    float dy;
    float dz;
    float time;

    glm::mat4 view_proj_matrix;
    float density_threshold;
};

// All functions are updated to work with pointers instead of vectors.
// The core logic remains the same since pointer access `ptr[i]` is the same as vector
// access.

void compute_k_values(quant_state *state) {
    for (uint32_t i = 0; i < n_x; ++i) {
        state->kx[i] =
            (i < n_x / 2) ? (2.0f * PI / l_x) * i : (2.0f * PI / l_x) * (i - n_x);
    }
    for (uint32_t i = 0; i < n_y; ++i) {
        state->ky[i] =
            (i < n_y / 2) ? (2.0f * PI / l_y) * i : (2.0f * PI / l_y) * (i - n_y);
    }
    for (uint32_t i = 0; i < n_z; ++i) {
        state->kz[i] =
            (i < n_z / 2) ? (2.0f * PI / l_z) * i : (2.0f * PI / l_z) * (i - n_z);
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
}

void compute_kinetic_factors(quant_state *state) {
    for (uint32_t i = 0; i < total_space_points; ++i) {
        float theta = state->k_squared[i] * delta_time / 4.0f;
        state->kinetic_factor[i].real = std::cos(theta);
        state->kinetic_factor[i].imaginary = -std::sin(theta);
    }
}

// The DLL still tells the host how large its state struct is.
// This is now much smaller as it only contains pointers.
size_t game_state_size = sizeof(quant_state);

void compute_potential_factors(quant_state *state) {
    for (uint32_t i = 0; i < total_space_points; ++i) {
        float theta = state->potential[i] * delta_time;
        state->potential_factor[i].real = std::cos(theta);
        state->potential_factor[i].imaginary = -std::sin(theta);
    }
}

void init_free_particle_potential(quant_state *state) {
    for (uint32_t i = 0; i < total_space_points; ++i) {
        state->potential[i] = 0.0f;
    }
}

void init_wave_function(quant_state *state, float x0, float y0, float z0, float sigma,
                        float k0x, float k0y, float k0z) {
    float A = std::pow(PI * sigma * sigma, -0.75f);
    float cx = l_x / 2.0f, cy = l_y / 2.0f, cz = l_z / 2.0f;

    for (uint32_t i = 0; i < n_x; ++i) {
        for (uint32_t j = 0; j < n_y; ++j) {
            for (uint32_t k = 0; k < n_z; ++k) {
                uint32_t idx = i + n_x * (j + n_y * k);
                float x = i * state->dx - cx, y = j * state->dy - cy,
                      z = k * state->dz - cz;
                float dx = x - x0, dy = y - y0, dz = z - z0;
                float r_squared = dx * dx + dy * dy + dz * dz;
                float gaussian_part = A * std::exp(-r_squared / (4.0f * sigma * sigma));
                float phase = k0x * x + k0y * y + k0z * z;
                state->psi[idx].real = gaussian_part * std::cos(phase);
                state->psi[idx].imaginary = gaussian_part * std::sin(phase);
            }
        }
    }
}

// ============================================================================
// CRITICAL CHANGE: game_init
// The DLL no longer allocates memory. It assumes the pointers in `quant_state`
// are valid and have been allocated by the main application.
// ============================================================================
void game_init(candy_context *ctx, void *state) {
    quant_state *quant_vis = (quant_state *)state;

    // Check that host provided valid pointers (optional but good practice)
    CANDY_ASSERT(quant_vis->psi != nullptr, "Host did not allocate psi buffer");

    // Initialize scalar values
    quant_vis->dx = l_x / n_x;
    quant_vis->dy = l_y / n_y;
    quant_vis->dz = l_z / n_z;
    quant_vis->time = 0.0f;
    quant_vis->density_threshold = 0.001f;

    // Simple camera setup
    glm::vec3 eye = glm::vec3(30.0f, 30.0f, 30.0f);
    glm::vec3 center = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::mat4 view = glm::lookAt(eye, center, up);
    glm::mat4 proj =
        glm::perspective(glm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 100.0f);
    proj[1][1] *= -1;
    quant_vis->view_proj_matrix = proj * view;

    // Compute initial values on CPU using the provided pointers
    compute_k_values(quant_vis);
    init_free_particle_potential(quant_vis);
    compute_kinetic_factors(quant_vis);
    compute_potential_factors(quant_vis);
    init_wave_function(quant_vis, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);

    // Upload the CPU-side data to the GPU and initialize VkFFT
    candy_upload_compute_data(ctx, state);
    candy_init_vkfft(ctx);
}

void game_update(candy_context *ctx, void *state, uint32_t delta_time) {
    // No changes needed here
}

void game_render(candy_context *ctx, void *state) {
    quant_state *quant_vis = (quant_state *)state;

    if (ctx->imgui.show_menu) {
        ImGui::Begin("Quantum mechanics visualation");
        ImGui::Text("Delta Time: %f", delta_time);
        ImGui::SliderFloat("Density Threshold", &quant_vis->density_threshold, 0.0001f,
                           0.01f);
        ImGui::End();
    }
}

// ============================================================================
// CRITICAL CHANGE: game_on_reload
// This is now a simple, safe memory copy of the small POD struct.
// It just copies the pointers. The host is responsible for making sure the
// underlying data remains valid across reloads.
// ============================================================================
void game_on_reload(void *old_state, void *new_state) {
    if (old_state && new_state) {
        memcpy(new_state, old_state, sizeof(quant_state));
    }
    std::cout << "[GAME] Reloaded. Version: 9" << std::endl;
}

// ============================================================================
// CRITICAL CHANGE: game_cleanup
// The DLL owns no memory, so it does nothing on cleanup.
// ============================================================================
void game_cleanup(candy_context *ctx, void *state) {
    (void)ctx;
    (void)state;
    // No cleanup necessary from the DLL side.
}
};
*/
