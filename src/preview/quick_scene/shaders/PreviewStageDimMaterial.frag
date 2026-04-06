#version 440

layout(location = 0) in vec2 vPosition;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float outerDarkAlpha;
    float innerDarkAlpha;
    float smoothBrightness;
    vec4 stageRect;
    vec4 geometryParams;
};

float smoothStep01(float t)
{
    float x = clamp(t, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

void main()
{
    vec2 stageCenter = stageRect.xy + stageRect.zw * 0.5;
    float layoutSquareSide = max(1.0, max(1.0, geometryParams.y) * geometryParams.z);
    float outerRadius = max(1.0, layoutSquareSide * 0.5);
    float radius = distance(vPosition, stageCenter);

    float alpha = outerDarkAlpha;
    if (radius < outerRadius) {
        if (smoothBrightness < 0.5) {
            alpha = innerDarkAlpha;
        } else {
            float ringRadius = outerRadius * geometryParams.w;
            float blendStart = (outerRadius + ringRadius) * 0.5;
            if (radius <= blendStart) {
                alpha = innerDarkAlpha;
            } else {
                float blendSpan = outerRadius - blendStart;
                if (blendSpan <= 1e-6) {
                    alpha = innerDarkAlpha;
                } else {
                    float t = smoothStep01((radius - blendStart) / blendSpan);
                    alpha = innerDarkAlpha + (outerDarkAlpha - innerDarkAlpha) * t;
                }
            }
        }
    }

    alpha *= qt_Opacity;
    if (alpha <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    fragColor = vec4(0.0, 0.0, 0.0, alpha);
}
