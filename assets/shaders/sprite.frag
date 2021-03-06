#version 450
precision highp float;
precision highp int;

#if defined(VARIANT_BIT_0) && VARIANT_BIT_0 && defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
#define BANDLIMITED_PIXEL
#include "inc/bandlimited_pixel_filter.h"
#endif

#if defined(VARIANT_BIT_1) && VARIANT_BIT_1 && defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
#define SPRITE_BLENDING
#endif

#ifdef BANDLIMITED_PIXEL
layout(push_constant, std430) uniform Globals
{
    vec2 tex_resolution;
    vec2 inv_tex_resolution;
} constants;
#endif

layout(location = 0) out mediump vec4 Color;

#if HAVE_VERTEX_COLOR
layout(location = 0) flat in mediump vec4 vColor;
#endif

#if defined(HAVE_UV) && HAVE_UV
    #ifdef SPRITE_BLENDING
        layout(location = 1) in highp vec3 vTex;
    #else
        layout(location = 1) in highp vec2 vTex;
    #endif
#endif

#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
    layout(set = 2, binding = 0) uniform mediump sampler2D uTex;
    #ifdef SPRITE_BLENDING
        layout(set = 2, binding = 1) uniform mediump sampler2D uTexAlt;
    #endif
#endif

void main()
{
#if defined(HAVE_BASECOLORMAP) && HAVE_BASECOLORMAP
    #ifdef BANDLIMITED_PIXEL
        BandlimitedPixelInfo info = compute_pixel_weights(vTex.xy, constants.tex_resolution, constants.inv_tex_resolution, 1.0);
        #ifdef SPRITE_BLENDING
            mediump vec4 c0 = sample_bandlimited_pixel(uTex, vTex.xy, info, 0.0);
            mediump vec4 c1 = sample_bandlimited_pixel(uTexAlt, vTex.xy, info, 0.0);
            mediump vec4 color = mix(c0, c1, vTex.z);
        #else
            mediump vec4 color = sample_bandlimited_pixel(uTex, vTex, info, 0.0);
        #endif
    #else
        #ifdef SPRITE_BLENDING
            mediump vec4 c0 = texture(uTex, vTex.xy);
            mediump vec4 c1 = texture(uTexAlt, vTex.xy);
            mediump vec4 color = mix(c0, c1, vTex.z);
        #else
            mediump vec4 color = texture(uTex, vTex);
        #endif
    #endif
    #if defined(ALPHA_TEST)
        if (color.a < 0.5)
            discard;
    #endif
#else
    mediump vec4 color = vec4(1.0);
#endif

#if defined(VARIANT_BIT_2) && VARIANT_BIT_2
    float luma = max(color.b, max(color.r, color.g));
    #if HAVE_VERTEX_COLOR
        color *= vColor;
    #endif
    Color = vec4(color.rgb, sqrt(clamp(luma, 0.0, 1.0)));
#else
    #if HAVE_VERTEX_COLOR
        color *= vColor;
    #endif
    Color = color;
#endif

#if defined(VARIANT_BIT_3)
    Color.a = 0.0;
#endif
}
