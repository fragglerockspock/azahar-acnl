// Copyright 2022-2024 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

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

vec4 GetScreen(int screen_id) {
#ifdef ARRAY_DYNAMIC_INDEX
    return texture(screen_textures[screen_id], frag_tex_coord);
#else
    switch (screen_id) {
    case 0:
        return texture(screen_textures[0], frag_tex_coord);
    case 1:
        return texture(screen_textures[1], frag_tex_coord);
    case 2:
        return texture(screen_textures[2], frag_tex_coord);
    }
#endif
}

vec4 LeafKeyTexel(vec3 c) {
    float a=1.0-clamp(min(c.r,c.b)-c.g,0.0,1.0);
    vec3 col=clamp((c-(1.0-a)*vec3(1,0,1))/max(a,0.001),0.0,1.0);
    a*=smoothstep(0.06,0.14,a);
    return vec4(col*a,a);
}
vec4 LeafKeySample() {
    ivec2 size=textureSize(screen_textures[2],0);
    ivec2 hi=size-ivec2(1);
    vec2 p=frag_tex_coord*vec2(size)-0.5;
    ivec2 i=ivec2(floor(p));
    vec2 f=fract(p);
    vec4 a=LeafKeyTexel(texelFetch(screen_textures[2],clamp(i,ivec2(0),hi),0).rgb);
    vec4 b=LeafKeyTexel(texelFetch(screen_textures[2],clamp(i+ivec2(1,0),ivec2(0),hi),0).rgb);
    vec4 c=LeafKeyTexel(texelFetch(screen_textures[2],clamp(i+ivec2(0,1),ivec2(0),hi),0).rgb);
    vec4 d=LeafKeyTexel(texelFetch(screen_textures[2],clamp(i+ivec2(1,1),ivec2(0),hi),0).rgb);
    vec4 mixed=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
    return vec4(mixed.a>0.0001 ? mixed.rgb/mixed.a : vec3(0),mixed.a);
}
void main() {
    color=screen_id_r==-1 && screen_id_l==2 ? LeafKeySample() : GetScreen(screen_id_l);
}
