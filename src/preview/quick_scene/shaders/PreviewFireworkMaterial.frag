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
    vec4 timing;
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

vec4 compositeSourceOver(vec4 sourceColor, vec4 destColor);

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

float fourPointStar(vec2 point, float radius, float widthRatio, float concavity)
{
    // Preserve the established horizontal span and compress the height to the same extent.
    float compressedRadius = max(radius * widthRatio, 0.001);
    vec2 p = abs(point) / vec2(compressedRadius);
    float shape = max(p.x, p.y) + concavity * min(p.x, p.y);
    float feather = max(fwidth(shape), 0.025);
    return 1.0 - smoothstep(1.0 - feather, 1.0 + feather, shape);
}

float hashStar(float value)
{
    return fract(sin(value * 127.1 + 311.7) * 43758.5453);
}

float starAngle(int index, int batch, float angleSeed)
{
    // Sample the full circle directly: clustering and empty arcs are intentionally allowed.
    return hashStar(float(index + batch * 23) + 0.19 + angleSeed) * 6.28318530718;
}

float starRingRadius(int index, int batch)
{
    // Inner stars sit inside the colour-glow ring; outer stars sit just inside the judgment ring.
    // Keep slight variation within each ring without drifting into the centre.
    const float radii[12] = float[12](
        0.86, 0.42, 0.88, 0.45,
        0.84, 0.40, 0.89, 0.44,
        0.85, 0.41, 0.87, 0.46
    );
    int shuffledIndex = (index * 7 + batch * 3) % 12;
    return radii[shuffledIndex];
}

float starSize(int index, int batch)
{
    // Deterministic variation keeps playback stable while spanning the requested visible range.
    return mix(0.040, 0.050, hashStar(float(index + batch * 29) + 0.37));
}

float starDelay(int index, int batch)
{
    const float batchStarts[2] = float[2](0.0, 0.08);
    // Reveal the inner ring first, then the outer ring about 10 ms later.
    // Keep only a small per-ring jitter so an early outer star cannot overtake a late inner star.
    bool isOuterRing = starRingRadius(index, batch) >= 0.80;
    float ringDelay = isOuterRing ? 0.010 : 0.0;
    float withinRingJitter = hashStar(float(index + batch * 31) + 0.71) * 0.004;
    return batchStarts[batch] + ringDelay + withinRingJitter;
}

vec4 rainbowStarLayer(vec2 localPos, float clipTime, float maxRadius, float angleSeed)
{
    const float kStarFadeInSeconds = 0.115;
    const float kStarFadeOutSeconds = 0.25;
    const float kStarPulseSeconds = kStarFadeInSeconds + kStarFadeOutSeconds;
    vec4 layer = vec4(0.0);
    for (int batch = 0; batch < 2; ++batch) {
        for (int index = 0; index < 12; ++index) {
            float localTime = clipTime - starDelay(index, batch);
            if (localTime < 0.0 || localTime >= kStarPulseSeconds) {
                continue;
            }
            // Preserve the fast appearance, then add 0.2 s to the shrink/fade phase only.
            float pulse = localTime < kStarFadeInSeconds
                ? sin(1.57079632679 * clamp(localTime / kStarFadeInSeconds, 0.0, 1.0))
                : cos(1.57079632679 * clamp(
                    (localTime - kStarFadeInSeconds) / kStarFadeOutSeconds,
                    0.0,
                    1.0
                ));
            float fadeOut01 = clamp(
                (localTime - kStarFadeInSeconds) / kStarFadeOutSeconds,
                0.0,
                1.0
            );
            float fadeIn01 = clamp(localTime / kStarFadeInSeconds, 0.0, 1.0);
            float angle = starAngle(index, batch, angleSeed);
            vec2 direction = vec2(cos(angle), sin(angle));
            float widthRatio = 0.66 + float((index + batch * 2) % 5) * 0.035;
            float visualPeakRadius = maxRadius * starSize(index, batch);
            float peakRadius = visualPeakRadius / widthRatio;
            float judgmentInset = max(2.0, maxRadius * 0.015);
            // Appearance is alpha-only at full size; geometry starts shrinking only during fade-out.
            float sizePulse = localTime < kStarFadeInSeconds
                ? 1.0
                : pow(max(pulse, 0.0), 0.72);
            float radius = peakRadius * sizePulse;
            float visualRadius = radius * widthRatio;
            float initialCenterRadius = min(
                maxRadius * starRingRadius(index, batch),
                maxRadius - visualPeakRadius - judgmentInset
            );
            float appearanceOffset = maxRadius * 0.02 * smoothstep(0.0, 1.0, fadeIn01);
            float disappearanceOffset = maxRadius * 0.04 * smoothstep(0.0, 1.0, fadeOut01);
            float outwardOffset = appearanceOffset + disappearanceOffset;
            float centerRadius = min(
                initialCenterRadius + outwardOffset,
                maxRadius - visualRadius - judgmentInset
            );
            vec2 center = direction * centerRadius;
            // Keep the full horizontal span while the deeper waist preserves a slightly finer edge.
            float outer = fourPointStar(localPos - center, radius, widthRatio, 1.82);
            float core = fourPointStar(localPos - center, radius * 0.50, widthRatio * 0.80, 2.1);
            float alpha = outer * pulse * 0.95;
            int tintIndex = (index * 2 + batch + 1) % 5;
            vec3 tint = mix(vec3(1.0, 0.98, 0.78), sectorBaseColor(tintIndex), 0.24);
            vec3 color = mix(tint, vec3(1.0), core);
            vec4 star = vec4(color * alpha, alpha);
            layer = compositeSourceOver(star, layer);
        }
    }
    return layer;
}

float halftoneMask(vec2 localPos, float radius, float outerRadius, float angleDegrees)
{
    vec2 grid = mod(localPos + vec2(5.0), vec2(10.0)) - vec2(5.0);
    float dotRadius = mix(1.0, 2.55, smoothstep(0.12, 0.88, radius / max(outerRadius, 1.0)));
    float dot = 1.0 - smoothstep(dotRadius - 0.55, dotRadius + 0.55, length(grid));
    float alternatingBeam = step(0.5, fract((angleDegrees - kSectorPhaseDegrees) / (kSectorStepDegrees * 2.0)));
    return dot * alternatingBeam;
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
                float dots = halftoneMask(localPos, radius, outerRadius, angleDegrees);
                float dotAlpha = fireworkContributionAlpha * sectorCoverage * dots * 0.34;
                vec4 dotLayer = vec4(vec3(0.06) * dotAlpha, dotAlpha);
                layerColor = compositeSourceOver(dotLayer, layerColor);
                break;
            }
        }
    }

    float clipTime = timing.x;
    float rayLife = timing.y;
    float coreRadius = max(7.0, clipRadius * mix(0.045, 0.105, smoothstep(0.0, 0.18, rayLife)));
    float coreDistance = radius / coreRadius;
    float rayStarted = drawFirework ? 1.0 : 0.0;
    float bloom = exp(-coreDistance * coreDistance * 1.55)
        * (1.0 - smoothstep(0.68, 1.0, rayLife)) * rayStarted;
    float whiteCore = 1.0 - smoothstep(0.0, 0.32, coreDistance);
    float glowAlpha = clamp(bloom * 0.92 + whiteCore * rayStarted, 0.0, 1.0);
    vec3 glowColor = mix(vec3(1.0, 0.72, 0.08), vec3(1.0, 1.0, 0.86), whiteCore);
    layerColor = compositeSourceOver(vec4(glowColor * glowAlpha, glowAlpha), layerColor);

    vec4 stars = rainbowStarLayer(localPos, clipTime, clipRadius, timing.z);
    layerColor = compositeSourceOver(stars, layerColor);

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

    float rayAndBallMask = holeMask;
    float preserveCenterAndStars = max(glowAlpha, stars.a);
    fragColor = layerColor * (max(rayAndBallMask, preserveCenterAndStars) * qt_Opacity);
}
