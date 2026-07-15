#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D source;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float quadRadius;
    float clipRadius;
    float outerRadius;
    vec4 fireworkAndHole;
    vec4 colorBallSmall;
    vec4 colorBallBig;
    vec4 sourceRect;
};

const float kSectorAlphaScale = 0.9;
const float kSectorSpanDegrees = 12.0;
const float kSectorStepDegrees = 24.0;
const float kSectorPhaseDegrees = -102.0;
const int kRenderFlagDrawStripe = 0x1;
const int kRenderFlagDrawBigBall = 0x2;
const int kRenderFlagDrawSmallBall = 0x4;
const int kRenderFlagUseTexture = 0x8;
const int kRenderFlagDrawFallbackBall = 0x10;

vec3 sectorBaseColor(int index)
{
    vec3 color;
    switch (index % 5) {
    case 0:
        color = vec3(255.0, 82.0, 20.0) / 255.0;
        break;
    case 1:
        color = vec3(255.0, 28.0, 176.0) / 255.0;
        break;
    case 2:
        color = vec3(0.0, 188.0, 255.0) / 255.0;
        break;
    case 3:
        color = vec3(186.0, 242.0, 0.0) / 255.0;
        break;
    default:
        color = vec3(255.0, 216.0, 0.0) / 255.0;
        break;
    }
    return color;
}

float wrapAngleDegrees(float angle)
{
    float wrapped = mod(angle, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped;
}

float shortestAngleDistanceDegrees(float angleA, float angleB)
{
    float delta = wrapAngleDegrees(angleA - angleB);
    if (delta > 180.0) {
        delta = 360.0 - delta;
    }
    return abs(delta);
}

float fireworkSectorCoverage(float angleDegrees, float sectorStartDegrees, float radius, float outerRadius)
{
    float sectorHalfSpan = kSectorSpanDegrees * 0.5;
    float sectorCenterDegrees = sectorStartDegrees + sectorHalfSpan;
    float angularDistance = shortestAngleDistanceDegrees(angleDegrees, sectorCenterDegrees);
    float angularFeatherDegrees = clamp(max(fwidth(angleDegrees), 0.35), 0.35, sectorHalfSpan);
    float radialFeatherPixels = max(fwidth(radius), 0.75);
    float angularCoverage =
        1.0 - smoothstep(sectorHalfSpan - angularFeatherDegrees, sectorHalfSpan + angularFeatherDegrees, angularDistance);
    float radialCoverage =
        1.0 - smoothstep(outerRadius - radialFeatherPixels, outerRadius + radialFeatherPixels, radius);
    return angularCoverage * radialCoverage;
}

vec4 proceduralColorBall(vec2 localRect)
{
    float radial = length((localRect - vec2(0.5)) * 2.0);
    if (radial >= 1.0) {
        return vec4(0.0);
    }

    vec4 core = vec4(255.0 / 255.0, 245.0 / 255.0, 160.0 / 255.0, 235.0 / 255.0);
    vec4 mid = vec4(255.0 / 255.0, 110.0 / 255.0, 220.0 / 255.0, 166.0 / 255.0);
    vec4 outer = vec4(110.0 / 255.0, 190.0 / 255.0, 255.0 / 255.0, 107.0 / 255.0);
    vec4 edge = vec4(255.0 / 255.0, 240.0 / 255.0, 120.0 / 255.0, 0.0);

    vec4 sampleColor;
    if (radial <= 0.3) {
        float t = radial / 0.3;
        sampleColor = mix(core, mid, t);
    } else if (radial <= 0.68) {
        float t = (radial - 0.3) / (0.68 - 0.3);
        sampleColor = mix(mid, outer, t);
    } else {
        float t = (radial - 0.68) / (1.0 - 0.68);
        sampleColor = mix(outer, edge, t);
    }

    return vec4(sampleColor.rgb * sampleColor.a, sampleColor.a);
}

int renderFlagsFromFloat(float flagsValue)
{
    return int(floor(flagsValue + 0.5));
}

bool hasRenderFlag(int renderFlags, int renderFlag)
{
    return (renderFlags & renderFlag) != 0;
}

vec4 compositeSourceOver(vec4 sourceColor, vec4 destColor)
{
    return sourceColor + destColor * (1.0 - sourceColor.a);
}

vec4 sampleColorBallLayer(vec2 localPos, float radius, float alpha, float aspect, bool useTextureBall)
{
    if (radius <= 0.0 || alpha <= 0.0) {
        return vec4(0.0);
    }

    float halfWidth = radius;
    float halfHeight = max(0.01, radius * aspect);
    vec2 localRect = vec2(localPos.x / (halfWidth * 2.0) + 0.5, localPos.y / (halfHeight * 2.0) + 0.5);
    if (any(lessThan(localRect, vec2(0.0))) || any(greaterThan(localRect, vec2(1.0)))) {
        return vec4(0.0);
    }

    vec4 premulColor;
    if (useTextureBall) {
        vec2 sampleUv = sourceRect.xy + localRect * sourceRect.zw;
        vec4 texel = texture(source, sampleUv);
        premulColor = texel;
    } else {
        premulColor = proceduralColorBall(localRect);
    }
    return premulColor * alpha;
}

void main()
{
    vec2 localPos = (vTexCoord * 2.0 - 1.0) * quadRadius;
    float radius = length(localPos);

    float fireworkAlpha = fireworkAndHole.x;
    float fireworkRotationDegrees = fireworkAndHole.y;
    float holeRadius = fireworkAndHole.z;
    float holeMaskRadius = fireworkAndHole.w;
    float colorBallRadius = colorBallSmall.x;
    float colorBallAlpha = colorBallSmall.y;
    float sourceAspect = max(0.01, colorBallSmall.z);
    float fallbackColorBallRadius = colorBallSmall.w;
    float colorBallBigRadius = colorBallBig.x;
    float colorBallBigAlpha = colorBallBig.y;
    float fallbackColorBallAlpha = colorBallBig.z;
    int renderFlags = renderFlagsFromFloat(colorBallBig.w);
    bool drawFirework = hasRenderFlag(renderFlags, kRenderFlagDrawStripe);
    bool drawColorBallBig = hasRenderFlag(renderFlags, kRenderFlagDrawBigBall);
    bool drawColorBallSmall = hasRenderFlag(renderFlags, kRenderFlagDrawSmallBall);
    bool useTextureBall = hasRenderFlag(renderFlags, kRenderFlagUseTexture);
    bool drawFallbackColorBall = hasRenderFlag(renderFlags, kRenderFlagDrawFallbackBall);

    float holeMask = 1.0;
    if (radius <= holeRadius) {
        holeMask = 0.0;
    } else if (radius < holeMaskRadius) {
        holeMask = clamp((radius - holeRadius) / max(0.0001, holeMaskRadius - holeRadius), 0.0, 1.0);
    }

    vec4 layerColor = vec4(0.0);
    if (drawFirework && radius <= outerRadius && fireworkAlpha > 0.0) {
        float angleDegrees = wrapAngleDegrees(degrees(atan(-localPos.y, localPos.x)));
        float fireworkContributionAlpha = fireworkAlpha * kSectorAlphaScale;
        for (int sectorIndex = 0; sectorIndex < 15; ++sectorIndex) {
            float sectorStart = kSectorPhaseDegrees + fireworkRotationDegrees + float(sectorIndex) * kSectorStepDegrees;
            float sectorCoverage = fireworkSectorCoverage(angleDegrees, sectorStart, radius, outerRadius);
            if (sectorCoverage > 0.0) {
                float sectorAlpha = fireworkContributionAlpha * sectorCoverage;
                vec4 fireworkLayer = vec4(
                    sectorBaseColor(sectorIndex) * sectorAlpha,
                    sectorAlpha
                );
                layerColor = compositeSourceOver(fireworkLayer, layerColor);
                break;
            }
        }
    }

    if (drawColorBallBig) {
        layerColor = compositeSourceOver(
            sampleColorBallLayer(localPos, colorBallBigRadius, colorBallBigAlpha, sourceAspect, useTextureBall),
            layerColor
        );
    }
    if (drawColorBallSmall) {
        layerColor = compositeSourceOver(
            sampleColorBallLayer(localPos, colorBallRadius, colorBallAlpha, sourceAspect, useTextureBall),
            layerColor
        );
    }
    if (drawFallbackColorBall) {
        layerColor = compositeSourceOver(
            sampleColorBallLayer(localPos, fallbackColorBallRadius, fallbackColorBallAlpha, 1.0, false),
            layerColor
        );
    }

    fragColor = layerColor * (holeMask * qt_Opacity);
}
