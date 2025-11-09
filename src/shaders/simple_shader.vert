#version 450

layout(location = 0) in vec3 in_position; // 3D position
layout(location = 1) in float in_density; // probability density

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    float threshold;
} pc;

void main() {
    gl_Position = pc.viewProj * vec4(in_position, 1.0);
    gl_PointSize = 2.0;

    // Color based on density (blue to red)
    float normalized = clamp(in_density * 10.0, 0.0, 1.0);
    fragColor = vec4(normalized, 0.3, 1.0 - normalized, normalized);
}
