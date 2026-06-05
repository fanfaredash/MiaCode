#version 440

// Subtle animated "micro-motion" texture for the intro's blurred 曲绘 backdrop.
// Applied to the backdrop layer ONLY (it sits below the card, so the difficulty
// card is never affected). Three small-amplitude modes, selected by `mode`:
//   0 = grain   (fine moving shimmer)
//   1 = warp    (gentle flowing domain-warp)
//   2 = caustic (soft drifting light filaments)
// `time` is seconds (intro frame / fps); `amp` is an overall strength multiplier.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float amp;
    int mode;
};

layout(binding = 1) uniform sampler2D source;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec4 col;

    if (mode == 1) {
        // flow warp — small UV displacement from a slowly moving noise field
        vec2 disp = vec2(
            vnoise(uv * 5.0 + vec2(time * 0.20, 0.0)),
            vnoise(uv * 5.0 + vec2(0.0, time * 0.16) + 11.3)
        ) - 0.5;
        col = texture(source, uv + disp * (amp * 0.010));
    } else if (mode == 2) {
        // soft caustic light — drifting filaments added faintly
        float c = sin(uv.x * 20.0 + uv.y * 12.0 + time * 0.7)
                + sin(uv.y * 16.0 - uv.x *  9.0 - time * 0.5);
        c = clamp(c * 0.5 + 0.5, 0.0, 1.0);
        c = pow(c, 3.0);
        col = texture(source, uv);
        col.rgb += c * (amp * 0.14) * vec3(0.55, 0.50, 0.65);
    } else {
        // grain / shimmer — fine time-varying noise on luminance
        float g = hash(uv * vec2(700.0, 700.0) + vec2(time * 17.0, time * 13.0)) - 0.5;
        col = texture(source, uv);
        col.rgb += g * (amp * 0.05);
    }

    fragColor = col * qt_Opacity;
}
