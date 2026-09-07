#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 baseColor;
    vec4 surfaceColor;
};
layout(binding = 1) uniform sampler2D source;

void main()
{
    vec4 wallpaper = texture(source, qt_TexCoord0);
    vec4 backing = wallpaper + baseColor * (1.0 - wallpaper.a);
    backing = surfaceColor + backing * (1.0 - surfaceColor.a);

    float distanceToCenter = length(qt_TexCoord0 - vec2(1.0));
    float edgeWidth = fwidth(distanceToCenter);
    float coverage = smoothstep(1.0 - edgeWidth * 0.5,
                               1.0 + edgeWidth * 0.5, distanceToCenter);
    fragColor = backing * coverage * qt_Opacity;
}
