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
    float shortSide = min(max(geometryParams.x, 1.0), max(geometryParams.y, 1.0));
    float layoutSquareSide = max(1.0, shortSide * geometryParams.z);
    float outerRadius = max(1.0, layoutSquareSide * 0.5);
    float radius = distance(vPosition, stageCenter);

    float innerAlpha = innerDarkAlpha;
    if (smoothBrightness >= 0.5) {
        float ringRadius = outerRadius * geometryParams.w;
        float blendStart = (outerRadius + ringRadius) * 0.5;
        float blendSpan = outerRadius - blendStart;
        if (blendSpan > 1e-6 && radius > blendStart) {
            float t = smoothStep01((radius - blendStart) / blendSpan);
            innerAlpha = innerDarkAlpha + (outerDarkAlpha - innerDarkAlpha) * t;
        }
    }

    // Keep the circle boundary coverage-aware. A hard radius comparison makes
    // the dim layer stair-step at small preview sizes and fractional DPRs.
    float edgeHalfWidth = max(fwidth(radius) * 0.5, 0.5);
    float circleCoverage = 1.0 - smoothstep(
        outerRadius - edgeHalfWidth,
        outerRadius + edgeHalfWidth,
        radius
    );
    float alpha = mix(outerDarkAlpha, innerAlpha, circleCoverage);

    alpha *= qt_Opacity;
    if (alpha <= 0.0) {
        fragColor = vec4(0.0);
        return;
    }
    fragColor = vec4(0.0, 0.0, 0.0, alpha);
}
