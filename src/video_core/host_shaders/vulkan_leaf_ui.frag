#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec2 frag_tex_coord;
layout (location = 0) out vec4 color;

layout (push_constant, std140) uniform DrawInfo {
    mat4 modelview_matrix;
    vec4 i_resolution;
    vec4 o_resolution;
    int screen_id_l;
    int screen_id_r;
    int layer;
    int reverse_interlaced;
};

layout (set = 0, binding = 0) uniform sampler2D screen_textures[3];

vec4 LeafParams() {
    return vec4(screen_id_r==1 ? 0.96 : 1.0,0.06,0.08,0.0);
}

const vec3 LEAF_KEY = vec3(1.0, 0.0, 1.0);

const vec3 LEAF_RIM = vec3(0.54, 0.54, 0.885);

vec4 LeafKeyTexel(vec3 c) {
    float a = 1.0 - clamp(min(c.r, c.b) - c.g, 0.0, 1.0);
    vec3 col = clamp((c - (1.0 - a) * LEAF_KEY) / max(a, 0.001), 0.0, 1.0);
    float rim = 1.0 - smoothstep(0.06, 0.14, distance(col, LEAF_RIM));
    col = mix(col, vec3(0.06, 0.05, 0.09), rim);
    a *= mix(1.0, 0.4, rim);
    a *= smoothstep(LeafParams().y, LeafParams().y + LeafParams().z, a);
    return vec4(col * a, a);
}

vec4 LeafComposite(vec4 premul) {
    float a = premul.a;
    vec3 col = a > 0.0001 ? premul.rgb / a : vec3(0.0);
    float total = 1.0 - (1.0 - a) * (1.0 - LeafParams().w);
    return vec4(col * a / max(total, 0.001), total * LeafParams().x);
}

vec4 LeafSampleKeyed() {
    ivec2 size = textureSize(screen_textures[2],0);
    ivec2 hi = size - ivec2(1);
    vec2 p = frag_tex_coord * vec2(size) - 0.5;
    vec2 f = fract(p);
    ivec2 i = ivec2(floor(p));
    vec4 c00 = LeafKeyTexel(texelFetch(screen_textures[2],clamp(i,ivec2(0),hi),0).rgb);
    vec4 c10 = LeafKeyTexel(texelFetch(screen_textures[2],clamp(i+ivec2(1,0),ivec2(0),hi),0).rgb);
    vec4 c01 = LeafKeyTexel(texelFetch(screen_textures[2],clamp(i+ivec2(0,1),ivec2(0),hi),0).rgb);
    vec4 c11 = LeafKeyTexel(texelFetch(screen_textures[2],clamp(i+ivec2(1,1),ivec2(0),hi),0).rgb);
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

void main() {
    color=LeafComposite(LeafSampleKeyed());
}
