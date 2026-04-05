#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in float vOpacity;
layout(location = 2) in float vWave;
layout(location = 3) in float vAbsWave;
layout(location = 4) in float vEffect;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

void main()
{
    if (vOpacity <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec4 texel = texture(source, vTexCoord);
    if (texel.a <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }

    vec3 color = texel.rgb / texel.a;

    if (vEffect > 1.5) {
        float brightness = 0.95 + max(vWave * 0.65, 0.0);
        float contrast = 1.0 + min(vWave * -0.55, 0.0);
        color *= brightness;
        color = vec3(0.5) + (color - vec3(0.5)) * contrast;
    } else if (vEffect > 0.5) {
        float brightness = 0.95 + vAbsWave * 0.5;
        color *= brightness;
    }

    color = clamp(color, 0.0, 1.0);
    fragColor = vec4(color * texel.a, texel.a) * vOpacity;
}
