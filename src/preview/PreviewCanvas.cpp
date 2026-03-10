#include "PreviewCanvas.h"
#include "common/AssetPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPointer>
#include <QRectF>
#include <QStringList>
#include <QTextStream>
#include <QThreadPool>
#include <QTimer>
#include <QTransform>
#include <QtMath>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace {
enum class SlideTrackTrimMode {
    AreaImmediate,
    UniformTime,
};

constexpr int kMargin = 0;
constexpr qreal kLogicalCanvasSize = 540.0;
constexpr qreal kLogicalCanvasCenter = kLogicalCanvasSize / 2.0;
constexpr qreal kLogicalDistanceTap = kLogicalCanvasSize * 122.5 / 1080.0;
constexpr qreal kLogicalDistanceEdge = kLogicalCanvasSize * 480.0 / 1080.0;
constexpr qreal kTapUnitsPerSecond = 540.0;
constexpr qreal kLaneUnitVectorBaseDegrees = -67.5;
constexpr qreal kLaneRotationBaseDegrees = 22.5;
constexpr qreal kLaneAngleStepDegrees = 45.0;
constexpr qreal kDistanceToScaleSlope = 0.008;
constexpr qreal kDistanceToScaleOffset = 0.51;
constexpr qreal kSlideStarFadeBaseScale = 0.45;
constexpr qreal kSlideStarFadeScaleDelta = 0.55;
constexpr qreal kSkinAssetScale = 0.5;
constexpr qreal kStarAssetScale = 90.0 / 126.0;
constexpr qreal kSlideSpawnStarRelativeScale = 1.08;
constexpr qreal kLogicalOutlineInset = 25.0;
constexpr qreal kNoteGuideSourceRadius = 240.75;
constexpr qreal kEachLine1SourceRadius = 240.02;
constexpr qreal kEachLine2SourceRadius = 239.89;
constexpr qreal kEachLine3SourceRadius = 239.65;
constexpr qreal kEachLine4SourceRadius = 239.92;
constexpr int kHoldTargetWidth = 60;
constexpr int kFpsSampleWindowMs = 250;
constexpr int kFrameStatsWindowSize = 120;
constexpr qreal kSlideTrackScale = 0.5;
constexpr qreal kSlideTrackFadeInSeconds = 0.2;
constexpr qreal kTouchDurationSeconds = 0.5;
constexpr qreal kTouchAppearPhase = 0.25;
constexpr qreal kTouchStartOffset = 30.0;
constexpr qreal kTouchClosedOffset = 12.0;
constexpr qreal kTouchHoldStartOffset = 24.0;
constexpr qreal kTouchHoldClosedOffset = 8.0;
constexpr qreal kTouchAssetScale = 0.5;
constexpr SlideTrackTrimMode kSlideTrackTrimMode = SlideTrackTrimMode::UniformTime;
constexpr qreal kAngleWrapOffset = 540.0;
constexpr qreal kAngleWrapCycle = 360.0;
constexpr qreal kAngleWrapCenter = 180.0;
constexpr bool kEnablePreviewCaches = true;
constexpr int kGuideTransformCacheLimit = 256;
constexpr int kSpriteTransformCacheLimit = 512;
constexpr int kGuideTransformSizeStep = 2;
constexpr int kSpriteTransformSizeStep = 4;
constexpr int kAtlasPadding = 2;
constexpr int kAtlasMaxWidth = 2048;
constexpr qreal kJudgeEffectClipDurationSeconds = 0.71666664;
constexpr qreal kJudgeEffectPlaybackSpeed = 1.0;
constexpr qreal kJudgeEffectDurationSeconds = kJudgeEffectClipDurationSeconds / kJudgeEffectPlaybackSpeed;
// Derived from source assets:
// Hex sprite width ~= 173.87 (ppu=100), tap sprite width ~= 122 (ppu=100).
constexpr qreal kJudgeEffectBaseRelativeToTap = 173.87256 / 122.0;
// One local transform unit in judgeEffect maps to world units; convert through tap width.
constexpr qreal kJudgeEffectOffsetRelativeToTap = 100.0 / 122.0;
constexpr qreal kJudgeEffectEdgeGlowScale = 1.012;
constexpr qreal kJudgeEffectEdgeGlowAlpha = 0.14;
constexpr qreal kJudgeEffectAlphaTailGamma = 1.00;
constexpr qreal kJudgeEffectLaneFacingAngleOffsetDegrees = 0.0;
// Texture-orientation: 
constexpr qreal kJudgeEffectTapTextureAngleOffsetDegrees = 0.0;
constexpr qreal kJudgeEffectBreakTextureAngleOffsetDegrees = 132.5;
constexpr qreal kJudgeEffectTouchDurationSeconds = 0.33333334;
constexpr qreal kJudgeEffectTouchDestroySeconds = 0.25;
constexpr qreal kJudgeEffectTouchCircleFadeEndSeconds = 0.31666666;
constexpr qreal kJudgeEffectHoldSustainLifetimeSeconds = 0.6;
constexpr int kJudgeEffectHoldSustainParticleCount = 5;
// Mapping from exported data:
// Circle.png = 256 px @ 100 PPU, Hold_Effect prefab local scale = 1.2,
// tap sprite width ~= 122 px @ 100 PPU.
constexpr qreal kJudgeEffectHoldSustainBaseRelativeToTap = (256.0 * 1.2) / 122.0;
// Alpha tightening (>1 narrows soft edges, making ring lines appear thinner).
constexpr qreal kJudgeEffectHoldSustainAlphaTightenGamma = 1.0;
// Mapping from exported data:
// TouchPoint sprite is 32px @ 100 PPU => 0.32 world units.
// Preview touch marker renders at 32 * 0.5 = 16 px, thus 1 world unit ~= 50 px.
// worldToPixels = touchBasePixels * kJudgeEffectTouchUnitRelativeToTouch => 16 * k = 50.
constexpr qreal kJudgeEffectTouchUnitRelativeToTouch = 3.125;
constexpr qreal kJudgeEffectTouchInnerScaleBase = 0.1725238;
constexpr qreal kJudgeEffectTouchOuterScaleBase = 0.3146144;
constexpr qreal kJudgeEffectTouchCircleSpriteWidthUnits = 512.0 / 100.0;
constexpr qreal kJudgeEffectTouchPartSpriteWidthUnits = 103.948685 / 100.0;
constexpr qreal kJudgeEffectFireworkTouchTriggerDelaySeconds = 0.05;
constexpr qreal kJudgeEffectFireworkDurationSeconds = 1.3333334;
constexpr qreal kJudgeEffectFireworkBaseWidthUnits = 10.8;
constexpr qreal kJudgeEffectFireworkColorBallBaseWidthUnits = 5.12;
constexpr qreal kJudgeEffectFireworkBrightnessGain = 1.25;
constexpr int kJudgeEffectFireworkSectorCount = 30;
constexpr int kJudgeEffectFireworkColoredSectorCount = kJudgeEffectFireworkSectorCount / 2;
constexpr qreal kJudgeEffectFireworkSectorSpanDegrees = 360.0 / static_cast<qreal>(kJudgeEffectFireworkSectorCount);
constexpr qreal kJudgeEffectFireworkSectorStepDegrees = kJudgeEffectFireworkSectorSpanDegrees * 2.0;
constexpr qreal kJudgeEffectFireworkSectorPhaseDegrees = -102.0;
constexpr int kJudgeEffectFireworkStepRotationSegmentCount = 3;
constexpr qreal kJudgeEffectFireworkStepRotationDegrees = 24.0;
// Hanabi.mat source params:
// _InnerLB=0.018, _InnerUB=0.054, _OuterLB=0.36, _OuterUB=0.429
constexpr qreal kHanabiInnerLB = 0.018;
constexpr qreal kHanabiInnerUB = 0.054;
constexpr qreal kHanabiOuterLB = 0.36;
constexpr qreal kHanabiOuterUB = 0.429;
constexpr qreal kJudgeEffectFireworkHoleStartRadiusRatio = kHanabiInnerUB;
constexpr qreal kJudgeEffectFireworkHoleEndRadiusRatio = 1.0;
constexpr qreal kJudgeEffectFireworkHoleBandRatio =
    (kHanabiInnerUB - kHanabiInnerLB) + (kHanabiOuterUB - kHanabiOuterLB);
const QColor kJudgeEffectTouchCircleTint = QColor::fromRgbF(1.0, 0.9943893, 0.4669811, 1.0);
const QColor kJudgeEffectTouchPartTint = QColor::fromRgbF(1.0, 0.9000474, 0.4666667, 1.0);
const std::array<QColor, 5> kJudgeEffectFireworkSectorColors = {{
    QColor(214, 106, 59),
    QColor(188, 86, 165),
    QColor(88, 157, 212),
    QColor(156, 186, 71),
    QColor(202, 178, 70),
}};

struct ScalarCurveKey {
    qreal time = 0.0;
    qreal value = 0.0;
};

struct ScalarHermiteKey {
    qreal time = 0.0;
    qreal value = 0.0;
    qreal inSlope = 0.0;
    qreal outSlope = 0.0;
};

struct Vec2CurveKey {
    qreal time = 0.0;
    QPointF value;
};

qreal smoothStep01(qreal t)
{
    const qreal x = qBound<qreal>(0.0, t, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

QColor scaleRgb(const QColor& color, qreal gain, qreal alpha)
{
    QColor result(
        qBound(0, qRound(static_cast<qreal>(color.red()) * gain), 255),
        qBound(0, qRound(static_cast<qreal>(color.green()) * gain), 255),
        qBound(0, qRound(static_cast<qreal>(color.blue()) * gain), 255)
    );
    result.setAlphaF(qBound<qreal>(0.0, alpha, 1.0));
    return result;
}

const std::array<ScalarCurveKey, 13> kJudgeEffectRootScaleKeys = {{
    {0.0, 0.5},
    {0.016666668, 0.50919664},
    {0.033333335, 0.53476083},
    {0.05, 0.57365394},
    {0.06666667, 0.6228372},
    {0.083333336, 0.67927206},
    {0.1, 0.7399199},
    {0.11666667, 0.80174196},
    {0.13333334, 0.8616997},
    {0.15, 0.91675436},
    {0.16666667, 0.96386737},
    {0.18333334, 1.0},
    {0.45, 1.3},
}};

const std::array<ScalarHermiteKey, 4> kJudgeEffectAlphaKeys = {{
    {0.0, 1.0, 0.0, 0.0},
    {0.23333333, 0.995328, -0.04485111, -0.044851113},
    {0.41666666, 8.059014e-08, 1.7378422e-06, -std::numeric_limits<qreal>::infinity()},
    {0.5833333, 0.0, 0.0, 0.0},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectRotationKeys = {{
    {0.0, 0.0},
    {0.46666667, 180.0},
    {0.71666664, 360.0},
}};

const std::array<std::array<Vec2CurveKey, 4>, 4> kJudgeEffectTapHexPositionKeys = {{
    {{
        {0.0, QPointF(0.552, 0.5)},
        {0.25, QPointF(0.43683612, 0.39568493)},
        {0.56666666, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
    {{
        {0.0, QPointF(-0.431, -0.527)},
        {0.25, QPointF(-0.31925926, -0.39037037)},
        {0.5, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
    {{
        {0.0, QPointF(0.473, -0.468)},
        {0.25, QPointF(0.35037035, -0.34666663)},
        {0.5, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
    {{
        {0.0, QPointF(-0.502, 0.492)},
        {0.25, QPointF(-0.3718518, 0.36444443)},
        {0.5, QPointF(0.0, 0.0)},
        {0.56666666, QPointF(0.0, 0.0)},
    }},
}};

const std::array<std::array<ScalarCurveKey, 2>, 4> kJudgeEffectTapHexScaleKeys = {{
    {{
        {0.0, 0.4788},
        {0.26666668, 0.6},
    }},
    {{
        {0.0, 0.4788},
        {0.26666668, 0.6},
    }},
    {{
        {0.0, 0.4788},
        {0.26666668, 0.8},
    }},
    {{
        {0.0, 0.4788},
        {0.26666668, 0.8},
    }},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchCircleScaleKeys = {{
    {0.0, 0.1},
    {0.3, 0.35},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchInnerParentScaleKeys = {{
    {0.0, 1.0},
    {0.25, 1.5},
}};

const std::array<ScalarCurveKey, 2> kJudgeEffectTouchOuterParentScaleKeys = {{
    {0.0, 2.0},
    {0.25, 0.2},
}};

const std::array<ScalarCurveKey, 5> kJudgeEffectFireworkScaleKeys = {{
    // fire.anim -> Firework.m_LocalScale (x/y)
    {0.0, 0.0},
    {0.1, 0.0},
    {0.13333334, 0.6},
    {0.23333333, 1.25},
    {1.3333334, 5.0},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkRotationKeys = {{
    {0.0, 0.0},
    {1.2166667, -72.0},
    {1.3333334, -78.85715},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkAlphaKeys = {{
    // fire.anim -> Firework.material._Alpha (with Hanabi.mat default _Alpha=0.589 at t=0)
    {0.0, 0.589},
    {0.5, 0.589},
    {1.3333334, 0.0},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkColorBallScaleKeys = {{
    {0.0, 0.2},
    {0.1, 0.5},
    {1.3333334, 0.5},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectFireworkColorBallBigScaleKeys = {{
    {0.0, 1.0},
    {0.3, 1.15},
    {1.3333334, 1.15},
}};

const std::array<ScalarCurveKey, 4> kJudgeEffectFireworkColorBallAlphaKeys = {{
    {0.0, 1.0},
    {0.2, 0.1},
    {0.3, 0.0},
    {1.3333334, 0.0},
}};

const std::array<ScalarCurveKey, 4> kJudgeEffectFireworkColorBallBigAlphaKeys = {{
    {0.0, 1.0},
    {0.16666667, 0.1},
    {0.8833333, 0.0},
    {1.3333334, 0.0},
}};

const std::array<ScalarCurveKey, 3> kJudgeEffectHoldSustainSizeKeys = {{
    {0.0, 0.22448978},
    {0.43861136, 0.8877539},
    {0.99853516, 1.0},
}};

const std::array<ScalarCurveKey, 7> kJudgeEffectHoldSustainAlphaKeys = {{
    {0.0, 0.007843138},
    {0.09459098, 0.5411765},
    {0.14285539, 0.9774867},
    {0.50193027, 1.0},
    {0.59652174, 0.30085242},
    {0.66602579, 0.30980393},
    {1.0, 0.0},
}};

// Avoid perfectly even phase spacing, which tends to flatten aggregate brightness and hide fade tuning.
const std::array<qreal, 5> kJudgeEffectHoldSustainPhaseOffsets = {
    0.00, 0.18, 0.43, 0.67, 0.86,
};

const std::array<QPointF, 4> kJudgeEffectTouchInnerOffsets = {
    QPointF(0.0, -0.246),
    QPointF(0.0, 0.246),
    QPointF(0.246, 0.0),
    QPointF(-0.246, 0.0),
};

const std::array<QPointF, 4> kJudgeEffectTouchOuterDiagonalOffsets = {
    QPointF(0.254, 0.256),
    QPointF(0.254, -0.256),
    QPointF(-0.254, -0.256),
    QPointF(-0.254, 0.256),
};

const std::array<QPointF, 4> kJudgeEffectTouchOuterCardinalOffsets = {
    QPointF(0.0, 0.359),
    QPointF(0.0, -0.359),
    QPointF(-0.359, 0.0),
    QPointF(0.359, 0.0),
};

const std::array<qreal, 4> kJudgeEffectParentRotationDirection = {1.0, 1.0, -1.0, -1.0};

template <std::size_t N>
qreal sampleScalarCurve(const std::array<ScalarCurveKey, N>& keys, qreal time)
{
    if (keys.empty()) {
        return 0.0;
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const ScalarCurveKey& next = keys[i];
        if (time > next.time) {
            continue;
        }
        const ScalarCurveKey& prev = keys[i - 1];
        const qreal span = next.time - prev.time;
        if (qFuzzyIsNull(span)) {
            return next.value;
        }
        const qreal t = (time - prev.time) / span;
        return prev.value + (next.value - prev.value) * t;
    }
    return keys.back().value;
}

template <std::size_t N>
qreal sampleScalarHermiteCurve(const std::array<ScalarHermiteKey, N>& keys, qreal time)
{
    if (keys.empty()) {
        return 0.0;
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const ScalarHermiteKey& next = keys[i];
        if (time > next.time) {
            continue;
        }
        const ScalarHermiteKey& prev = keys[i - 1];
        const qreal span = next.time - prev.time;
        if (qFuzzyIsNull(span)) {
            return next.value;
        }
        const qreal t = (time - prev.time) / span;
        const qreal prevOutSlope = prev.outSlope;
        const qreal nextInSlope = next.inSlope;
        if (!std::isfinite(static_cast<double>(prevOutSlope))
            || !std::isfinite(static_cast<double>(nextInSlope))) {
            return prev.value + (next.value - prev.value) * t;
        }
        const qreal t2 = t * t;
        const qreal t3 = t2 * t;
        const qreal h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const qreal h10 = t3 - 2.0 * t2 + t;
        const qreal h01 = -2.0 * t3 + 3.0 * t2;
        const qreal h11 = t3 - t2;
        return h00 * prev.value
            + h10 * span * prevOutSlope
            + h01 * next.value
            + h11 * span * nextInSlope;
    }
    return keys.back().value;
}

template <std::size_t N>
QPointF sampleVec2Curve(const std::array<Vec2CurveKey, N>& keys, qreal time)
{
    if (keys.empty()) {
        return QPointF();
    }
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    if (time >= keys.back().time) {
        return keys.back().value;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        const Vec2CurveKey& next = keys[i];
        if (time > next.time) {
            continue;
        }
        const Vec2CurveKey& prev = keys[i - 1];
        const qreal span = next.time - prev.time;
        if (qFuzzyIsNull(span)) {
            return next.value;
        }
        const qreal t = (time - prev.time) / span;
        return QPointF(
            prev.value.x() + (next.value.x() - prev.value.x()) * t,
            prev.value.y() + (next.value.y() - prev.value.y()) * t
        );
    }
    return keys.back().value;
}

qreal judgeEffectClipTime(qreal elapsedSeconds)
{
    return qBound<qreal>(0.0, elapsedSeconds * kJudgeEffectPlaybackSpeed, kJudgeEffectClipDurationSeconds);
}

qreal judgeEffectTouchClipTime(qreal elapsedSeconds)
{
    return qBound<qreal>(0.0, elapsedSeconds, kJudgeEffectTouchDurationSeconds);
}

QPointF rotatePointDegrees(const QPointF& point, qreal angleDegrees)
{
    const qreal radians = qDegreesToRadians(angleDegrees);
    const qreal c = qCos(radians);
    const qreal s = qSin(radians);
    return QPointF(point.x() * c - point.y() * s, point.x() * s + point.y() * c);
}

QImage buildJudgeEffectTapFallbackImage()
{
    constexpr int kSize = 192;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal radius = kSize * 0.32;

    QPolygonF hex;
    hex.reserve(6);
    for (int i = 0; i < 6; ++i) {
        const qreal angle = qDegreesToRadians(60.0 * i - 30.0);
        hex.append(QPointF(
            center.x() + qCos(angle) * radius,
            center.y() + qSin(angle) * radius
        ));
    }

    // Extracted Hex.png dominant color is around #FFF39C; fallback keeps only hollow strokes.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 248, 184, 230), radius * 0.12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(hex);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 243, 156, 240), radius * 0.078, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(hex);
    painter.end();
    return image;
}

QImage buildJudgeEffectTapBreakFallbackImage()
{
    constexpr int kSize = 192;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal outerRadius = kSize * 0.34;
    const qreal innerRadius = outerRadius * 0.46;

    QPolygonF star;
    star.reserve(10);
    for (int i = 0; i < 10; ++i) {
        const qreal radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const qreal angle = qDegreesToRadians(-90.0 + i * 36.0);
        star.append(QPointF(
            center.x() + qCos(angle) * radius,
            center.y() + qSin(angle) * radius
        ));
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 248, 184, 230), outerRadius * 0.16, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(star);
    painter.setPen(QPen(QColor(255, 243, 156, 240), outerRadius * 0.095, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolygon(star);
    painter.end();
    return image;
}

QImage buildJudgeEffectFireworkFallbackImage()
{
    constexpr int kSize = 512;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal outerR = kSize * 0.45;
    const qreal innerR = outerR * 0.48;

    QPainterPath ring;
    ring.addEllipse(center, outerR, outerR);
    QPainterPath hole;
    hole.addEllipse(center, innerR, innerR);
    ring = ring.subtracted(hole);

    QRadialGradient gradient(center, outerR);
    gradient.setColorAt(0.0, QColor(255, 255, 220, 20));
    gradient.setColorAt(0.5, QColor(255, 245, 150, 180));
    gradient.setColorAt(1.0, QColor(255, 235, 120, 35));
    painter.fillPath(ring, gradient);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(255, 246, 157, 210), kSize * 0.012, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, outerR, outerR);
    painter.setPen(QPen(QColor(255, 249, 190, 160), kSize * 0.008, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawEllipse(center, innerR, innerR);

    painter.end();
    return image;
}

QImage buildJudgeEffectFireworkColorBallFallbackImage()
{
    constexpr int kSize = 512;
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPointF center(kSize / 2.0, kSize / 2.0);
    const qreal radius = kSize * 0.46;
    QRadialGradient gradient(center, radius);
    gradient.setColorAt(0.0, QColor(255, 245, 130, 255));
    gradient.setColorAt(0.33, QColor(255, 72, 210, 235));
    gradient.setColorAt(0.62, QColor(83, 185, 255, 215));
    gradient.setColorAt(1.0, QColor(255, 238, 96, 0));

    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(center, radius, radius);
    painter.end();
    return image;
}

QRectF nonTransparentBounds(const QImage& image)
{
    if (image.isNull()) {
        return QRectF();
    }
    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) <= 0) {
                continue;
            }
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }
    if (maxX < minX || maxY < minY) {
        return QRectF();
    }
    return QRectF(
        static_cast<qreal>(minX),
        static_cast<qreal>(minY),
        static_cast<qreal>(maxX - minX + 1),
        static_cast<qreal>(maxY - minY + 1)
    );
}

QImage tintedSpriteImage(const QImage& source, const QColor& tint)
{
    if (source.isNull()) {
        return QImage();
    }
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int tr = tint.red();
    const int tg = tint.green();
    const int tb = tint.blue();
    for (int y = 0; y < result.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int a = qAlpha(row[x]);
            if (a == 0) {
                continue;
            }
            const int r = qRed(row[x]) * tr / 255;
            const int g = qGreen(row[x]) * tg / 255;
            const int b = qBlue(row[x]) * tb / 255;
            row[x] = qRgba(r, g, b, a);
        }
    }
    return result;
}

QImage alphaTightenedSpriteImage(const QImage& source, qreal tightenGamma)
{
    if (source.isNull()) {
        return QImage();
    }
    const qreal gamma = qMax<qreal>(1e-4, tightenGamma);
    if (qFuzzyCompare(gamma, 1.0)) {
        return source;
    }
    QImage result = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < result.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const int a = qAlpha(row[x]);
            if (a == 0) {
                continue;
            }
            const qreal alpha01 = static_cast<qreal>(a) / 255.0;
            const int tightenedAlpha = qBound(0, qRound(qPow(alpha01, gamma) * 255.0), 255);
            row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), tightenedAlpha);
        }
    }
    return result;
}

struct SampleStats {
    bool hasValue = false;
    double avgMs = 0.0;
    double p95Ms = 0.0;
    double maxMs = 0.0;
};

SampleStats computeSampleStats(const QVector<double>& samples)
{
    SampleStats stats;
    if (samples.isEmpty()) {
        return stats;
    }

    stats.hasValue = true;
    const double sum = std::accumulate(samples.cbegin(), samples.cend(), 0.0);
    stats.avgMs = sum / static_cast<double>(samples.size());

    QVector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    stats.maxMs = sorted.constLast();
    const int p95Index = qBound(0, static_cast<int>(qCeil(sorted.size() * 0.95)) - 1, sorted.size() - 1);
    stats.p95Ms = sorted.at(p95Index);
    return stats;
}

QString formatHudTimeLabel(double seconds)
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(seconds * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 sec = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    return QString("%1:%2:%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

QFont hudMonoFont(int pointSize, QFont::Weight weight = QFont::Medium)
{
    QFont font;
    for (const QString& family : QStringList{"Cascadia Mono", "JetBrains Mono", "Cascadia Code", "Consolas"}) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

int quantizeDimension(int value, int step)
{
    const int clampedValue = qMax(1, value);
    if (step <= 1) {
        return clampedValue;
    }
    return qMax(step, ((clampedValue + step / 2) / step) * step);
}

QPointF interpolatePoint(const QVector<QPointF>& points, qreal proportion)
{
    if (points.isEmpty()) {
        return QPointF();
    }
    if (points.size() == 1) {
        return points.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (points.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), points.size() - 2);
    const qreal t = scaled - index;
    const QPointF& a = points[index];
    const QPointF& b = points[index + 1];
    return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
}

qreal interpolateAngle(const QVector<double>& angles, qreal proportion)
{
    if (angles.isEmpty()) {
        return 0.0;
    }
    if (angles.size() == 1) {
        return angles.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (angles.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), angles.size() - 2);
    const qreal t = scaled - index;
    const qreal a = angles[index];
    const qreal b = angles[index + 1];
    qreal delta = std::fmod(b - a + kAngleWrapOffset, kAngleWrapCycle) - kAngleWrapCenter;
    return a + delta * t;
}

int currentAreaIndexForProportion(const QVector<double>& thresholds, qreal proportion, int areaCount)
{
    if (areaCount <= 1) {
        return 0;
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    if (!thresholds.isEmpty()) {
        const int limit = qMin(areaCount, thresholds.size());
        int currentArea = 0;
        for (int nextArea = 1; nextArea < limit; ++nextArea) {
            if (clamped >= thresholds[nextArea]) {
                currentArea = nextArea;
            } else {
                break;
            }
        }
        return qBound(0, currentArea, areaCount - 1);
    }
    return qBound(0, static_cast<int>(qFloor(clamped * (areaCount - 1))), areaCount - 1);
}

QPointF laneUnitVector(int lane)
{
    if (lane < 1 || lane > 8) {
        return QPointF(0.0, 0.0);
    }
    const qreal angleDeg = kLaneUnitVectorBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
    const qreal angleRad = qDegreesToRadians(angleDeg);
    return QPointF(
        qCos(angleRad),
        qSin(angleRad)
    );
}

QPointF mapLogicalPointToRect(const QPointF& logicalPoint, const QRectF& targetRect)
{
    const qreal scale = targetRect.width() / kLogicalCanvasSize;
    return QPointF(
        targetRect.left() + logicalPoint.x() * scale,
        targetRect.top() + logicalPoint.y() * scale
    );
}

qreal mapLogicalLengthToRect(qreal logicalLength, const QRectF& targetRect)
{
    return logicalLength * (targetRect.width() / kLogicalCanvasSize);
}

qreal tapScaleForDistance(qreal distance)
{
    return distance * kDistanceToScaleSlope + kDistanceToScaleOffset;
}

qreal laneRotationDegrees(int lane)
{
    if (lane < 1 || lane > 8) {
        return 0.0;
    }
    return kLaneRotationBaseDegrees + (lane - 1) * kLaneAngleStepDegrees;
}

qreal laneRotationDegreesForIndex(qreal laneIndex)
{
    return (kLaneRotationBaseDegrees - kLaneAngleStepDegrees) + laneIndex * kLaneAngleStepDegrees;
}

QColor tapColorForMarker(const TimelineNoteMarker& marker)
{
    const bool slideHeadStar = marker.type == "slide" || marker.type == "wifi";
    const bool isBreak = slideHeadStar ? marker.headBreak : marker.isBreak;
    if (isBreak) {
        return QColor("#F39C12");
    }
    // Slide/wifi head stars use `headEach`; the trace body uses `slideEach`.
    // `isEach` is intentionally ignored for slide-like notes to avoid overlap.
    const bool each = (marker.type == "slide" || marker.type == "wifi") ? marker.headEach : marker.isEach;
    if (each) {
        return QColor("#3FD7FF");
    }
    return QColor("#F7E45C");
}

QColor exTintColor(bool isBreak, bool isEach)
{
    if (isBreak) {
        return QColor("#F59E0B");
    }
    if (isEach) {
        return QColor("#FFF05C");
    }
    return QColor("#FF9FD6");
}

QColor exStarTintColor(bool isBreak, bool isEach)
{
    if (isBreak) {
        return QColor("#F59E0B");
    }
    if (isEach) {
        return QColor("#FFF05C");
    }
    return QColor("#6FB6FF");
}

QString defaultOutlinePath()
{
    return miacode::assets::assetPath("background/outline.png");
}

QString defaultNoteGuideDir()
{
    return miacode::assets::assetPath("noteguide");
}

bool previewStartupTimingEnabled()
{
    static const bool enabled = []() {
        const QString raw = qEnvironmentVariable(
            "MIACODE_ENABLE_STARTUP_TIMING",
            qEnvironmentVariable("MAIMURI_ENABLE_STARTUP_TIMING")
        ).trimmed();
        return raw == "1" || raw.compare("true", Qt::CaseInsensitive) == 0;
    }();
    return enabled;
}

QString startupTimingLogPath()
{
    return QDir::temp().filePath("miacode_startup_timing.log");
}

void appendPreviewStartupTiming(const QString& stage, qint64 deltaMs)
{
    if (!previewStartupTimingEnabled()) {
        return;
    }
    static QElapsedTimer timer;
    static qint64 lastMs = 0;
    if (!timer.isValid()) {
        timer.start();
        lastMs = 0;
    }
    const qint64 elapsedMs = timer.elapsed();
    const qint64 resolvedDeltaMs = deltaMs >= 0 ? deltaMs : (elapsedMs - lastMs);
    lastMs = elapsedMs;

    QFile logFile(startupTimingLogPath());
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&logFile);
    out << "stage=" << stage
        << ", elapsed_ms=" << elapsedMs
        << ", delta_ms=" << resolvedDeltaMs
        << "\n";
}
}

PreviewCanvas::PreviewCanvas(QWindow* parent)
    : QOpenGLWindow(NoPartialUpdate, parent)
{
    const QString outlinePath = defaultOutlinePath();
    if (QFileInfo::exists(outlinePath)) {
        outlineImage_ = QImage(outlinePath);
    }
    judgeEffectTapImage_ = buildJudgeEffectTapFallbackImage();
    judgeEffectTapSourceRect_ = nonTransparentBounds(judgeEffectTapImage_);
    judgeEffectTapBreakImage_ = buildJudgeEffectTapBreakFallbackImage();
    judgeEffectTapBreakSourceRect_ = nonTransparentBounds(judgeEffectTapBreakImage_);
    judgeEffectFireworkImage_ = buildJudgeEffectFireworkFallbackImage();
    judgeEffectFireworkSourceRect_ = nonTransparentBounds(judgeEffectFireworkImage_);
    judgeEffectFireworkColorBallImage_ = buildJudgeEffectFireworkColorBallFallbackImage();
    judgeEffectFireworkColorBallSourceRect_ = nonTransparentBounds(judgeEffectFireworkColorBallImage_);
}

PreviewCanvas::~PreviewCanvas()
{
    if (context() != nullptr) {
        makeCurrent();
        if (gpuTimerQueriesSupported_) {
            QOpenGLContext* ctx = QOpenGLContext::currentContext();
            QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
            if (extra != nullptr) {
                extra->glDeleteQueries(4, gpuTimeQueries_);
            }
        }
        glRenderer_.shutdown();
        doneCurrent();
    }
}

void PreviewCanvas::setPlayheadSeconds(double seconds)
{
    const double clamped = seconds < 0.0 ? 0.0 : seconds;
    if (qFuzzyCompare(playheadSeconds_ + 1.0, clamped + 1.0)) {
        return;
    }
    playheadSeconds_ = clamped;
    update();
}

void PreviewCanvas::setMediaFrame(const QImage& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    videoFrame_ = QVideoFrame();
#endif
    mediaFrame_ = frame;
}

void PreviewCanvas::setVideoFrame(const QVideoFrame& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    mediaFrame_ = QImage();
    videoFrame_ = frame;
#else
    Q_UNUSED(frame);
#endif
}

void PreviewCanvas::setNoteMarkers(const QVector<TimelineNoteMarker>& notes)
{
    noteMarkers_ = notes;
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
    update();
}

struct PreviewCanvas::SkinLoadResult {
    quint64 generation = 0;
    QImage tapImage;
    QImage tapEachImage;
    QImage tapBreakImage;
    QImage tapExImage;
    QImage slideTrackImage;
    QImage slideTrackEachImage;
    QImage slideTrackBreakImage;
    QImage starImage;
    QImage starEachImage;
    QImage starBreakImage;
    QImage starBreakDoubleImage;
    QImage starDoubleImage;
    QImage starEachDoubleImage;
    QImage starExImage;
    QImage starExDoubleImage;
    QVector<QImage> wifiImages;
    QVector<QImage> wifiEachImages;
    QVector<QImage> wifiBreakImages;
    QImage holdImage;
    QImage holdEachImage;
    QImage holdBreakImage;
    QImage holdExImage;
    QImage noteGuideNormalImage;
    QImage noteGuideBreakImage;
    QImage noteGuideEachImage;
    QImage noteGuideEachLine1Image;
    QImage noteGuideEachLine2Image;
    QImage noteGuideEachLine3Image;
    QImage noteGuideEachLine4Image;
    QImage noteGuideHoldEndImage;
    QImage noteGuideHoldEachEndImage;
    QImage noteGuideHoldBreakEndImage;
    QImage noteGuideSlideImage;
    QImage touchCornerImage;
    QImage touchCornerEachImage;
    QImage touchPointImage;
    QImage touchPointEachImage;
    QImage touchHold0Image;
    QImage touchHold1Image;
    QImage touchHold2Image;
    QImage touchHold3Image;
    QImage touchHoldBorderImage;
    QImage judgeEffectTapImage;
    QImage judgeEffectTapBreakImage;
    QImage judgeEffectHoldSustainCircleImage;
    QImage judgeEffectTouchCircleImage;
    QImage judgeEffectTouchPart01Image;
    QImage judgeEffectTouchPart02Image;
    QImage judgeEffectFireworkImage;
    QImage judgeEffectFireworkColorBallImage;
    QImage tapAtlasImage;
    QImage trackAtlasImage;
    QImage touchAtlasImage;
    QImage guideAtlasImage;
    QHash<quint64, QRect> tapAtlasRegions;
    QHash<quint64, QRect> trackAtlasRegions;
    QHash<quint64, QRect> touchAtlasRegions;
    QHash<quint64, QRect> guideAtlasRegions;
};

namespace {
struct AtlasBuildResult {
    QImage atlasImage;
    QHash<quint64, QRect> regions;
};

QImage loadImageIfExists(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        return QImage();
    }
    return QImage(path);
}

QImage loadGuideImageScaled(const QDir& noteGuideDir, const QString& name)
{
    const QString path = noteGuideDir.filePath(name);
    if (!QFileInfo::exists(path)) {
        return QImage();
    }
    QImage image(path);
    if (image.isNull()) {
        return QImage();
    }
    const int width = qMax(1, qRound(image.width() * kSkinAssetScale));
    const int height = qMax(1, qRound(image.height() * kSkinAssetScale));
    if (width != image.width() || height != image.height()) {
        image = image.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

AtlasBuildResult buildAtlasFromImages(const QVector<const QImage*>& images)
{
    AtlasBuildResult result;

    struct Placement {
        const QImage* source = nullptr;
        QRect rect;
    };

    QVector<Placement> placements;
    QHash<quint64, bool> seen;
    int x = kAtlasPadding;
    int y = kAtlasPadding;
    int rowHeight = 0;
    int atlasWidth = 0;

    for (const QImage* image : images) {
        if (image == nullptr || image->isNull()) {
            continue;
        }

        const quint64 key = image->cacheKey();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key, true);

        const int width = image->width();
        const int height = image->height();
        if (width <= 0 || height <= 0) {
            continue;
        }

        if (x > kAtlasPadding && x + width + kAtlasPadding > kAtlasMaxWidth) {
            x = kAtlasPadding;
            y += rowHeight + kAtlasPadding;
            rowHeight = 0;
        }

        Placement placement;
        placement.source = image;
        placement.rect = QRect(x, y, width, height);
        placements.append(placement);

        x += width + kAtlasPadding;
        rowHeight = qMax(rowHeight, height);
        atlasWidth = qMax(atlasWidth, x);
    }

    if (placements.isEmpty()) {
        return result;
    }

    const int finalWidth = qMax(kAtlasPadding * 2 + 1, atlasWidth);
    const int finalHeight = qMax(kAtlasPadding * 2 + 1, y + rowHeight + kAtlasPadding);
    result.atlasImage = QImage(finalWidth, finalHeight, QImage::Format_ARGB32_Premultiplied);
    result.atlasImage.fill(Qt::transparent);

    QPainter atlasPainter(&result.atlasImage);
    atlasPainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Placement& placement : placements) {
        atlasPainter.drawImage(placement.rect.topLeft(), *placement.source);
        result.regions.insert(placement.source->cacheKey(), placement.rect);
    }
    atlasPainter.end();
    return result;
}

PreviewCanvas::SkinLoadResult loadSkinAssets(const QString& skinDir, quint64 generation)
{
    PreviewCanvas::SkinLoadResult result;
    result.generation = generation;

    if (skinDir.isEmpty()) {
        return result;
    }

    const QDir dir(skinDir);
    const QDir noteGuideDir(defaultNoteGuideDir());

    result.tapImage = loadImageIfExists(dir.filePath("tap.png"));
    result.tapEachImage = loadImageIfExists(dir.filePath("tap_each.png"));
    result.tapBreakImage = loadImageIfExists(dir.filePath("tap_break.png"));
    result.tapExImage = loadImageIfExists(dir.filePath("tap_ex.png"));
    result.slideTrackImage = loadImageIfExists(dir.filePath("slide.png"));
    result.slideTrackEachImage = loadImageIfExists(dir.filePath("slide_each.png"));
    result.slideTrackBreakImage = loadImageIfExists(dir.filePath("slide_break.png"));
    result.starImage = loadImageIfExists(dir.filePath("star.png"));
    result.starEachImage = loadImageIfExists(dir.filePath("star_each.png"));
    result.starBreakImage = loadImageIfExists(dir.filePath("star_break.png"));
    result.starBreakDoubleImage = loadImageIfExists(dir.filePath("star_break_double.png"));
    result.starDoubleImage = loadImageIfExists(dir.filePath("star_double.png"));
    result.starEachDoubleImage = loadImageIfExists(dir.filePath("star_each_double.png"));
    result.starExImage = loadImageIfExists(dir.filePath("star_ex.png"));
    result.starExDoubleImage = loadImageIfExists(dir.filePath("star_ex_double.png"));
    result.holdImage = loadImageIfExists(dir.filePath("hold.png"));
    result.holdEachImage = loadImageIfExists(dir.filePath("hold_each.png"));
    result.holdBreakImage = loadImageIfExists(dir.filePath("hold_break.png"));
    result.holdExImage = loadImageIfExists(dir.filePath("hold_ex.png"));
    result.touchCornerImage = loadImageIfExists(dir.filePath("touch.png"));
    result.touchCornerEachImage = loadImageIfExists(dir.filePath("touch_each.png"));
    result.touchPointImage = loadImageIfExists(dir.filePath("touch_point.png"));
    result.touchPointEachImage = loadImageIfExists(dir.filePath("touch_point_each.png"));
    result.touchHold0Image = loadImageIfExists(dir.filePath("touchhold_0.png"));
    result.touchHold1Image = loadImageIfExists(dir.filePath("touchhold_1.png"));
    result.touchHold2Image = loadImageIfExists(dir.filePath("touchhold_2.png"));
    result.touchHold3Image = loadImageIfExists(dir.filePath("touchhold_3.png"));
    result.touchHoldBorderImage = loadImageIfExists(dir.filePath("touchhold_border.png"));
    result.judgeEffectTapImage = loadImageIfExists(dir.filePath("judge_effect_tap.png"));
    result.judgeEffectTapBreakImage = loadImageIfExists(dir.filePath("judge_effect_tap_break.png"));
    result.judgeEffectHoldSustainCircleImage = loadImageIfExists(dir.filePath("judge_effect_hold_sustain_circle.png"));
    if (result.judgeEffectHoldSustainCircleImage.isNull()) {
        result.judgeEffectHoldSustainCircleImage = loadImageIfExists(dir.filePath("circle.png"));
    }
    result.judgeEffectTouchCircleImage = loadImageIfExists(dir.filePath("judge_effect_touch_circle.png"));
    result.judgeEffectTouchPart01Image = loadImageIfExists(dir.filePath("judge_effect_touch_part_01.png"));
    result.judgeEffectTouchPart02Image = loadImageIfExists(dir.filePath("judge_effect_touch_part_02.png"));
    result.judgeEffectFireworkImage = loadImageIfExists(dir.filePath("judge_effect_firework_tinted_sample.png"));
    if (result.judgeEffectFireworkImage.isNull()) {
        result.judgeEffectFireworkImage = loadImageIfExists(dir.filePath("judge_effect_firework.png"));
    }
    result.judgeEffectFireworkColorBallImage = loadImageIfExists(dir.filePath("judge_effect_firework_color_ball.png"));
    if (result.judgeEffectFireworkColorBallImage.isNull()) {
        result.judgeEffectFireworkColorBallImage = loadImageIfExists(dir.filePath("ColorBall.png"));
    }

    result.noteGuideNormalImage = loadGuideImageScaled(noteGuideDir, "Normal.png");
    result.noteGuideBreakImage = loadGuideImageScaled(noteGuideDir, "Break.png");
    if (result.noteGuideBreakImage.isNull()) {
        result.noteGuideBreakImage = result.noteGuideNormalImage;
    }
    result.noteGuideEachImage = loadGuideImageScaled(noteGuideDir, "Each.png");
    if (result.noteGuideEachImage.isNull()) {
        result.noteGuideEachImage = result.noteGuideNormalImage;
    }
    result.noteGuideEachLine1Image = loadGuideImageScaled(noteGuideDir, "EachLine1.png");
    result.noteGuideEachLine2Image = loadGuideImageScaled(noteGuideDir, "EachLine2.png");
    result.noteGuideEachLine3Image = loadGuideImageScaled(noteGuideDir, "EachLine3.png");
    result.noteGuideEachLine4Image = loadGuideImageScaled(noteGuideDir, "EachLine4.png");
    result.noteGuideHoldEndImage = loadGuideImageScaled(noteGuideDir, "Hold_End.png");
    result.noteGuideHoldEachEndImage = loadGuideImageScaled(noteGuideDir, "Hold_Each_End.png");
    if (result.noteGuideHoldEachEndImage.isNull()) {
        result.noteGuideHoldEachEndImage = result.noteGuideHoldEndImage;
    }
    result.noteGuideHoldBreakEndImage = loadGuideImageScaled(noteGuideDir, "Hold_Break_End.png");
    if (result.noteGuideHoldBreakEndImage.isNull()) {
        result.noteGuideHoldBreakEndImage = result.noteGuideHoldEndImage;
    }
    result.noteGuideSlideImage = loadGuideImageScaled(noteGuideDir, "Slide.png");
    if (result.noteGuideSlideImage.isNull()) {
        result.noteGuideSlideImage = result.noteGuideNormalImage;
    }

    for (int i = 0; i <= 10; ++i) {
        const QImage wifiImage = loadImageIfExists(dir.filePath(QString("wifi_%1.png").arg(i)));
        if (!wifiImage.isNull()) {
            result.wifiImages.append(wifiImage);
        }
        const QImage wifiEachImage = loadImageIfExists(dir.filePath(QString("wifi_each_%1.png").arg(i)));
        if (!wifiEachImage.isNull()) {
            result.wifiEachImages.append(wifiEachImage);
        }
        const QImage wifiBreakImage = loadImageIfExists(dir.filePath(QString("wifi_break_%1.png").arg(i)));
        if (!wifiBreakImage.isNull()) {
            result.wifiBreakImages.append(wifiBreakImage);
        }
    }

    {
        const AtlasBuildResult tapAtlas = buildAtlasFromImages(
            QVector<const QImage*>{
                &result.tapImage,
                &result.tapEachImage,
                &result.tapBreakImage,
                &result.starImage,
                &result.starEachImage,
                &result.starBreakImage,
                &result.holdImage,
                &result.holdEachImage,
                &result.holdBreakImage,
            }
        );
        result.tapAtlasImage = tapAtlas.atlasImage;
        result.tapAtlasRegions = tapAtlas.regions;
    }

    {
        QVector<const QImage*> trackImages{
            &result.slideTrackImage,
            &result.slideTrackEachImage,
            &result.slideTrackBreakImage,
        };
        for (const QImage& image : result.wifiImages) {
            trackImages.append(&image);
        }
        for (const QImage& image : result.wifiEachImages) {
            trackImages.append(&image);
        }
        for (const QImage& image : result.wifiBreakImages) {
            trackImages.append(&image);
        }
        const AtlasBuildResult trackAtlas = buildAtlasFromImages(trackImages);
        result.trackAtlasImage = trackAtlas.atlasImage;
        result.trackAtlasRegions = trackAtlas.regions;
    }

    {
        const AtlasBuildResult touchAtlas = buildAtlasFromImages(
            QVector<const QImage*>{
                &result.touchCornerImage,
                &result.touchCornerEachImage,
                &result.touchPointImage,
                &result.touchPointEachImage,
                &result.touchHold0Image,
                &result.touchHold1Image,
                &result.touchHold2Image,
                &result.touchHold3Image,
                &result.touchHoldBorderImage,
            }
        );
        result.touchAtlasImage = touchAtlas.atlasImage;
        result.touchAtlasRegions = touchAtlas.regions;
    }

    {
        const AtlasBuildResult guideAtlas = buildAtlasFromImages(
            QVector<const QImage*>{
                &result.noteGuideNormalImage,
                &result.noteGuideBreakImage,
                &result.noteGuideEachImage,
                &result.noteGuideEachLine1Image,
                &result.noteGuideEachLine2Image,
                &result.noteGuideEachLine3Image,
                &result.noteGuideEachLine4Image,
                &result.noteGuideHoldEndImage,
                &result.noteGuideHoldEachEndImage,
                &result.noteGuideHoldBreakEndImage,
                &result.noteGuideSlideImage,
            }
        );
        result.guideAtlasImage = guideAtlas.atlasImage;
        result.guideAtlasRegions = guideAtlas.regions;
    }

    return result;
}
} // namespace

void PreviewCanvas::setSkinDirectory(const QString& skinDir)
{
    const quint64 generation = ++skinLoadGeneration_;
    lastSkinLoadDispatchMs_ = QDateTime::currentMSecsSinceEpoch();
    appendPreviewStartupTiming("preview_canvas/skin_load_dispatch", -1);
    QPointer<PreviewCanvas> guard(this);
    QThreadPool::globalInstance()->start([guard, skinDir, generation]() {
        QElapsedTimer workerTimer;
        workerTimer.start();
        SkinLoadResult result = loadSkinAssets(skinDir, generation);
        const qint64 workerElapsedMs = workerTimer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result), workerElapsedMs]() mutable {
                if (guard.isNull()) {
                    return;
                }
                appendPreviewStartupTiming("preview_canvas/skin_load_worker_done", workerElapsedMs);
                guard->applySkinLoadResult(std::move(result));
            },
            Qt::QueuedConnection
        );
    }, -1);
}

void PreviewCanvas::applySkinLoadResult(SkinLoadResult&& result)
{
    if (result.generation != skinLoadGeneration_) {
        return;
    }

    tapImage_ = std::move(result.tapImage);
    tapEachImage_ = std::move(result.tapEachImage);
    tapBreakImage_ = std::move(result.tapBreakImage);
    tapExImage_ = std::move(result.tapExImage);
    slideTrackImage_ = std::move(result.slideTrackImage);
    slideTrackEachImage_ = std::move(result.slideTrackEachImage);
    slideTrackBreakImage_ = std::move(result.slideTrackBreakImage);
    starImage_ = std::move(result.starImage);
    starEachImage_ = std::move(result.starEachImage);
    starBreakImage_ = std::move(result.starBreakImage);
    starBreakDoubleImage_ = std::move(result.starBreakDoubleImage);
    starDoubleImage_ = std::move(result.starDoubleImage);
    starEachDoubleImage_ = std::move(result.starEachDoubleImage);
    starExImage_ = std::move(result.starExImage);
    starExDoubleImage_ = std::move(result.starExDoubleImage);
    wifiImages_ = std::move(result.wifiImages);
    wifiEachImages_ = std::move(result.wifiEachImages);
    wifiBreakImages_ = std::move(result.wifiBreakImages);
    holdImage_ = std::move(result.holdImage);
    holdEachImage_ = std::move(result.holdEachImage);
    holdBreakImage_ = std::move(result.holdBreakImage);
    holdExImage_ = std::move(result.holdExImage);
    noteGuideNormalImage_ = std::move(result.noteGuideNormalImage);
    noteGuideBreakImage_ = std::move(result.noteGuideBreakImage);
    noteGuideEachImage_ = std::move(result.noteGuideEachImage);
    noteGuideEachLine1Image_ = std::move(result.noteGuideEachLine1Image);
    noteGuideEachLine2Image_ = std::move(result.noteGuideEachLine2Image);
    noteGuideEachLine3Image_ = std::move(result.noteGuideEachLine3Image);
    noteGuideEachLine4Image_ = std::move(result.noteGuideEachLine4Image);
    noteGuideHoldEndImage_ = std::move(result.noteGuideHoldEndImage);
    noteGuideHoldEachEndImage_ = std::move(result.noteGuideHoldEachEndImage);
    noteGuideHoldBreakEndImage_ = std::move(result.noteGuideHoldBreakEndImage);
    noteGuideSlideImage_ = std::move(result.noteGuideSlideImage);
    touchCornerImage_ = std::move(result.touchCornerImage);
    touchCornerEachImage_ = std::move(result.touchCornerEachImage);
    touchPointImage_ = std::move(result.touchPointImage);
    touchPointEachImage_ = std::move(result.touchPointEachImage);
    touchHold0Image_ = std::move(result.touchHold0Image);
    touchHold1Image_ = std::move(result.touchHold1Image);
    touchHold2Image_ = std::move(result.touchHold2Image);
    touchHold3Image_ = std::move(result.touchHold3Image);
    touchHoldBorderImage_ = std::move(result.touchHoldBorderImage);
    if (!result.judgeEffectTapImage.isNull()) {
        judgeEffectTapImage_ = std::move(result.judgeEffectTapImage);
    } else if (judgeEffectTapImage_.isNull()) {
        judgeEffectTapImage_ = buildJudgeEffectTapFallbackImage();
    }
    judgeEffectTapSourceRect_ = nonTransparentBounds(judgeEffectTapImage_);

    if (!result.judgeEffectTapBreakImage.isNull()) {
        judgeEffectTapBreakImage_ = std::move(result.judgeEffectTapBreakImage);
    } else if (judgeEffectTapBreakImage_.isNull()) {
        judgeEffectTapBreakImage_ = buildJudgeEffectTapBreakFallbackImage();
    }
    judgeEffectTapBreakSourceRect_ = nonTransparentBounds(judgeEffectTapBreakImage_);
    judgeEffectHoldSustainCircleImage_ = alphaTightenedSpriteImage(
        result.judgeEffectHoldSustainCircleImage,
        kJudgeEffectHoldSustainAlphaTightenGamma
    );

    judgeEffectTouchCircleImage_ = result.judgeEffectTouchCircleImage.isNull()
        ? QImage()
        : tintedSpriteImage(result.judgeEffectTouchCircleImage, kJudgeEffectTouchCircleTint);
    judgeEffectTouchPart01Image_ = result.judgeEffectTouchPart01Image.isNull()
        ? QImage()
        : tintedSpriteImage(result.judgeEffectTouchPart01Image, kJudgeEffectTouchPartTint);
    judgeEffectTouchPart02Image_ = result.judgeEffectTouchPart02Image.isNull()
        ? QImage()
        : tintedSpriteImage(result.judgeEffectTouchPart02Image, kJudgeEffectTouchPartTint);

    if (!result.judgeEffectFireworkImage.isNull()) {
        judgeEffectFireworkImage_ = std::move(result.judgeEffectFireworkImage);
    } else if (judgeEffectFireworkImage_.isNull()) {
        judgeEffectFireworkImage_ = buildJudgeEffectFireworkFallbackImage();
    }
    judgeEffectFireworkSourceRect_ = nonTransparentBounds(judgeEffectFireworkImage_);
    if (!result.judgeEffectFireworkColorBallImage.isNull()) {
        judgeEffectFireworkColorBallImage_ = std::move(result.judgeEffectFireworkColorBallImage);
    } else if (judgeEffectFireworkColorBallImage_.isNull()) {
        judgeEffectFireworkColorBallImage_ = buildJudgeEffectFireworkColorBallFallbackImage();
    }
    judgeEffectFireworkColorBallSourceRect_ = nonTransparentBounds(judgeEffectFireworkColorBallImage_);

    tapAtlasImage_ = std::move(result.tapAtlasImage);
    trackAtlasImage_ = std::move(result.trackAtlasImage);
    touchAtlasImage_ = std::move(result.touchAtlasImage);
    guideAtlasImage_ = std::move(result.guideAtlasImage);

    atlasRegions_.clear();
    const auto appendAtlasRegions =
        [this](const QHash<quint64, QRect>& regions, const QImage* atlasImage) {
            if (atlasImage == nullptr || atlasImage->isNull()) {
                return;
            }
            for (auto it = regions.cbegin(); it != regions.cend(); ++it) {
                AtlasRegionRef region;
                region.atlasImage = atlasImage;
                region.rect = it.value();
                atlasRegions_.insert(it.key(), region);
            }
        };
    appendAtlasRegions(result.tapAtlasRegions, &tapAtlasImage_);
    appendAtlasRegions(result.trackAtlasRegions, &trackAtlasImage_);
    appendAtlasRegions(result.touchAtlasRegions, &touchAtlasImage_);
    appendAtlasRegions(result.guideAtlasRegions, &guideAtlasImage_);

    overlayCache_.clear();
    guideTransformCache_.clear();
    guideTransformCacheOrder_.clear();
    spriteTransformCache_.clear();
    spriteTransformCacheOrder_.clear();
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();

    if (glRenderer_.isInitialized() && context() != nullptr) {
        scheduleTexturePrewarm();
    }
    if (lastSkinLoadDispatchMs_ >= 0) {
        const qint64 totalMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - lastSkinLoadDispatchMs_);
        appendPreviewStartupTiming("preview_canvas/skin_load_apply_done", totalMs);
        lastSkinLoadDispatchMs_ = -1;
    } else {
        appendPreviewStartupTiming("preview_canvas/skin_load_apply_done", -1);
    }
    update();
}

void PreviewCanvas::setBackgroundBrightness(double brightness)
{
    const double clamped = qBound(0.0, brightness, 1.0);
    if (qFuzzyCompare(backgroundBrightness_ + 1.0, clamped + 1.0)) {
        return;
    }
    backgroundBrightness_ = clamped;
    update();
}

void PreviewCanvas::setShowDebugInfo(bool show)
{
    if (showDebugInfo_ == show) {
        return;
    }
    showDebugInfo_ = show;
    update();
}

void PreviewCanvas::reset()
{
    overlayCache_.clear();
    guideTransformCache_.clear();
    guideTransformCacheOrder_.clear();
    spriteTransformCache_.clear();
    spriteTransformCacheOrder_.clear();
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
    fpsTimer_.invalidate();
    fpsFrameCounter_ = 0;
    fpsDisplay_ = 0.0;
    lastFrameTimestampNs_ = 0;
    frameIntervalsMs_.clear();
    frameIntervalWriteIndex_ = 0;
    frameIntervalCount_ = 0;
    frameMsAverage_ = 0.0;
    frameMsP95_ = 0.0;
    frameMsMax_ = 0.0;
    playheadSeconds_ = 0.0;
    mediaFrame_ = QImage();
#ifdef HAVE_QT_MULTIMEDIA
    videoFrame_ = QVideoFrame();
#endif
    resetProfilingSession();
    update();
}

void PreviewCanvas::resetProfilingSession()
{
    if (context() != nullptr && gpuTimerQueriesSupported_) {
        makeCurrent();
        collectGpuProfilingResults(true);
        doneCurrent();
    }
    profileCpuPrepTotalMs_ = 0.0;
    profileCpuUploadTotalMs_ = 0.0;
    profileGpuDrawTotalMs_ = 0.0;
    profileFrameCount_ = 0;
    profileGpuSampleCount_ = 0;
    profileCpuPrepSamplesMs_.clear();
    profileCpuUploadSamplesMs_.clear();
    profileGpuDrawSamplesMs_.clear();
    profilePresentApproxSamplesMs_.clear();
    profileTickToPaintSamplesMs_.clear();
    profileVideoMapSamplesMs_.clear();
    profileVideoUploadSamplesMs_.clear();
    profileSessionClock_.invalidate();
    lastProfileFrameStartNs_ = -1;
    lastProfileCpuFrameNs_ = 0;
    pendingTickToPaintStartNs_ = -1;
}

void PreviewCanvas::noteTickForProfiling()
{
    if (!profileSessionClock_.isValid()) {
        profileSessionClock_.start();
        lastProfileFrameStartNs_ = 0;
    }
    pendingTickToPaintStartNs_ = profileSessionClock_.nsecsElapsed();
}

QString PreviewCanvas::writeProfilingSummaryToFile()
{
    if (profileFrameCount_ == 0) {
        return QString();
    }

    if (context() != nullptr && gpuTimerQueriesSupported_) {
        makeCurrent();
        collectGpuProfilingResults(true);
        doneCurrent();
    }

    QFile file(profilingSummaryPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return QString();
    }

    const SampleStats cpuPrepStats = computeSampleStats(profileCpuPrepSamplesMs_);
    const SampleStats cpuUploadStats = computeSampleStats(profileCpuUploadSamplesMs_);
    const SampleStats gpuDrawStats = computeSampleStats(profileGpuDrawSamplesMs_);
    const SampleStats presentApproxStats = computeSampleStats(profilePresentApproxSamplesMs_);
    const SampleStats tickToPaintStats = computeSampleStats(profileTickToPaintSamplesMs_);
    const SampleStats videoMapStats = computeSampleStats(profileVideoMapSamplesMs_);
    const SampleStats videoUploadStats = computeSampleStats(profileVideoUploadSamplesMs_);

    const auto writeStats = [](QTextStream& stream, const QString& prefix, const SampleStats& stats) {
        if (!stats.hasValue) {
            stream << prefix << "_avg_ms=N/A\n";
            stream << prefix << "_p95_ms=N/A\n";
            stream << prefix << "_max_ms=N/A\n";
            return;
        }
        stream << prefix << "_avg_ms=" << QString::number(stats.avgMs, 'f', 4) << '\n';
        stream << prefix << "_p95_ms=" << QString::number(stats.p95Ms, 'f', 4) << '\n';
        stream << prefix << "_max_ms=" << QString::number(stats.maxMs, 'f', 4) << '\n';
    };

    QTextStream stream(&file);
    stream << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << '\n';
    stream << "frame_samples=" << profileFrameCount_ << '\n';
    stream << "gpu_frame_samples=" << profileGpuSampleCount_ << '\n';
    stream << "present_approx_note=frame interval residual; includes event loop/compositor/vsync and is not exact swap time\n";
    writeStats(stream, "cpu_prepare", cpuPrepStats);
    writeStats(stream, "cpu_upload", cpuUploadStats);
    writeStats(stream, "gpu_draw", gpuDrawStats);
    writeStats(stream, "present_approx", presentApproxStats);
    writeStats(stream, "tick_to_paint", tickToPaintStats);
    writeStats(stream, "video_frame_map", videoMapStats);
    writeStats(stream, "video_frame_upload", videoUploadStats);
    file.close();
    return file.fileName();
}

void PreviewCanvas::initializeGL()
{
    glRenderer_.initialize();
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    if (extra != nullptr && (ctx->hasExtension("GL_ARB_timer_query")
        || ctx->hasExtension("GL_EXT_disjoint_timer_query")
        || ctx->format().majorVersion() >= 3)) {
        extra->glGenQueries(4, gpuTimeQueries_);
        gpuTimerQueriesSupported_ = true;
    }
    scheduleTexturePrewarm();
}

void PreviewCanvas::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void PreviewCanvas::beginNativeBatch(QPainter& painter)
{
    if (nativePaintingActive_ || !glRenderer_.isInitialized()) {
        return;
    }
    painter.beginNativePainting();
    nativePaintingActive_ = true;
}

void PreviewCanvas::endNativeBatch(QPainter& painter)
{
    if (!nativePaintingActive_) {
        return;
    }
    painter.endNativePainting();
    nativePaintingActive_ = false;
}

void PreviewCanvas::scheduleTexturePrewarm()
{
    pendingTexturePrewarmImages_.clear();
    pendingTexturePrewarmImages_.append(tapAtlasImage_);
    pendingTexturePrewarmImages_.append(trackAtlasImage_);
    pendingTexturePrewarmImages_.append(touchAtlasImage_);
    pendingTexturePrewarmImages_.append(guideAtlasImage_);
    pendingTexturePrewarmImages_.append(outlineImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTapImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTapBreakImage_);
    pendingTexturePrewarmImages_.append(judgeEffectHoldSustainCircleImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTouchCircleImage_);
    pendingTexturePrewarmImages_.append(judgeEffectTouchPart01Image_);
    pendingTexturePrewarmImages_.append(judgeEffectTouchPart02Image_);
    pendingTexturePrewarmImages_.append(judgeEffectFireworkImage_);
    pendingTexturePrewarmImages_.append(judgeEffectFireworkColorBallImage_);
    texturePrewarmStartMs_ = QDateTime::currentMSecsSinceEpoch();
    appendPreviewStartupTiming("preview_canvas/texture_prewarm_schedule", -1);

    if (texturePrewarmTimer_ == nullptr) {
        texturePrewarmTimer_ = new QTimer(this);
        texturePrewarmTimer_->setInterval(16);
        texturePrewarmTimer_->setTimerType(Qt::CoarseTimer);
        connect(texturePrewarmTimer_, &QTimer::timeout, this, &PreviewCanvas::processTexturePrewarmQueue);
    }
    if (!texturePrewarmTimer_->isActive()) {
        texturePrewarmTimer_->start();
    }
}

void PreviewCanvas::processTexturePrewarmQueue()
{
    if (pendingTexturePrewarmImages_.isEmpty()) {
        if (texturePrewarmTimer_ != nullptr) {
            texturePrewarmTimer_->stop();
        }
        if (texturePrewarmStartMs_ >= 0) {
            const qint64 elapsedMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - texturePrewarmStartMs_);
            appendPreviewStartupTiming("preview_canvas/texture_prewarm_done", elapsedMs);
            texturePrewarmStartMs_ = -1;
        }
        return;
    }
    if (!glRenderer_.isInitialized() || context() == nullptr) {
        return;
    }

    const QImage image = pendingTexturePrewarmImages_.takeFirst();
    if (!image.isNull()) {
        makeCurrent();
        glRenderer_.prewarmTexture(image);
        doneCurrent();
    }

    if (pendingTexturePrewarmImages_.isEmpty() && texturePrewarmTimer_ != nullptr) {
        texturePrewarmTimer_->stop();
    }
}

const QImage* PreviewCanvas::selectTapImage(const TimelineNoteMarker& marker) const
{
    const QImage* tapImage = &tapImage_;
    if (marker.isBreak && !tapBreakImage_.isNull()) {
        tapImage = &tapBreakImage_;
    } else if (marker.isEach && !tapEachImage_.isNull()) {
        tapImage = &tapEachImage_;
    }
    return tapImage;
}

const QImage* PreviewCanvas::selectHoldImage(const TimelineNoteMarker& marker) const
{
    const QImage* holdImage = &holdImage_;
    if (marker.isBreak && !holdBreakImage_.isNull()) {
        holdImage = &holdBreakImage_;
    } else if (marker.isEach && !holdEachImage_.isNull()) {
        holdImage = &holdEachImage_;
    }
    return holdImage;
}

const QImage* PreviewCanvas::selectTapNoteGuideImage(const TimelineNoteMarker& marker) const
{
    const bool slideHeadStar = marker.type == "slide" || marker.type == "wifi";
    if (slideHeadStar) {
        if (!marker.hasHeadStar) {
            return nullptr;
        }
        if (marker.headBreak && !noteGuideBreakImage_.isNull()) {
            return &noteGuideBreakImage_;
        }
        if (marker.headEach && !noteGuideEachImage_.isNull()) {
            return &noteGuideEachImage_;
        }
        return noteGuideSlideImage_.isNull() ? &noteGuideNormalImage_ : &noteGuideSlideImage_;
    }
    if (marker.isBreak && !noteGuideBreakImage_.isNull()) {
        return &noteGuideBreakImage_;
    }
    if (marker.isEach && !noteGuideEachImage_.isNull()) {
        return &noteGuideEachImage_;
    }
    return &noteGuideNormalImage_;
}

const QImage* PreviewCanvas::selectHoldEndNoteGuideImage(const TimelineNoteMarker& marker) const
{
    if (marker.isBreak && !noteGuideHoldBreakEndImage_.isNull()) {
        return &noteGuideHoldBreakEndImage_;
    }
    if (marker.isEach && !noteGuideHoldEachEndImage_.isNull()) {
        return &noteGuideHoldEachEndImage_;
    }
    return &noteGuideHoldEndImage_;
}

QImage PreviewCanvas::composeOverlay(
    const QImage& base,
    const QImage& overlay,
    qreal mix,
    qreal lighten,
    const QImage* accentSource,
    const QColor* accentOverride
)
{
    if (base.isNull() || overlay.isNull()) {
        return base;
    }
    const quint64 key = static_cast<quint64>(base.cacheKey())
        ^ (static_cast<quint64>(overlay.cacheKey()) << 1)
        ^ (static_cast<quint64>(qRound(mix * 1000.0)) << 2)
        ^ (static_cast<quint64>(qRound(lighten * 1000.0)) << 12)
        ^ (accentSource != nullptr ? static_cast<quint64>(accentSource->cacheKey()) : 0ULL)
        ^ (accentOverride != nullptr ? (static_cast<quint64>(accentOverride->rgba()) << 3) : 0ULL);
    if (kEnablePreviewCaches) {
        const auto cached = overlayCache_.constFind(key);
        if (cached != overlayCache_.cend()) {
            return cached.value();
        }
    }

    QColor tint = accentOverride != nullptr ? *accentOverride : QColor(255, 255, 255);
    if (accentOverride == nullptr && accentSource != nullptr && !accentSource->isNull()) {
        const QImage source = accentSource->convertToFormat(QImage::Format_ARGB32);
        qint64 r = 0;
        qint64 g = 0;
        qint64 b = 0;
        qint64 n = 0;
        for (int y = 0; y < source.height(); ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
            for (int x = 0; x < source.width(); ++x) {
                const QColor c = QColor::fromRgba(line[x]);
                if (c.alpha() == 0) {
                    continue;
                }
                r += c.red();
                g += c.green();
                b += c.blue();
                ++n;
            }
        }
        if (n > 0) {
            tint = QColor(static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n));
        }
    }

    QImage overlayTinted = overlay.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < overlayTinted.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(overlayTinted.scanLine(y));
        for (int x = 0; x < overlayTinted.width(); ++x) {
            QColor c = QColor::fromRgba(line[x]);
            if (c.alpha() == 0) {
                continue;
            }
            const int alpha = c.alpha();
            const int nr = qBound(0, qRound(c.red() * (1.0 - mix) + tint.red() * mix), 255);
            const int ng = qBound(0, qRound(c.green() * (1.0 - mix) + tint.green() * mix), 255);
            const int nb = qBound(0, qRound(c.blue() * (1.0 - mix) + tint.blue() * mix), 255);
            const int outR = qBound(0, qRound(nr + (255 - nr) * lighten), 255);
            const int outG = qBound(0, qRound(ng + (255 - ng) * lighten), 255);
            const int outB = qBound(0, qRound(nb + (255 - nb) * lighten), 255);
            line[x] = qRgba(outR, outG, outB, alpha);
        }
    }

    const int width = qMax(base.width(), overlayTinted.width());
    const int height = qMax(base.height(), overlayTinted.height());
    QImage composed(width, height, QImage::Format_ARGB32);
    composed.fill(Qt::transparent);
    QPainter p(&composed);
    const QPoint baseTopLeft((width - base.width()) / 2, (height - base.height()) / 2);
    const QPoint overlayTopLeft((width - overlayTinted.width()) / 2, (height - overlayTinted.height()) / 2);
    p.drawImage(baseTopLeft, base);
    p.drawImage(overlayTopLeft, overlayTinted);
    p.end();
    if (kEnablePreviewCaches) {
        overlayCache_.insert(key, composed);
    }
    return composed;
}

void PreviewCanvas::rebuildAtlases()
{
    atlasRegions_.clear();

    rebuildAtlas(
        tapAtlasImage_,
        QVector<const QImage*>{
            &tapImage_,
            &tapEachImage_,
            &tapBreakImage_,
            &starImage_,
            &starEachImage_,
            &starBreakImage_,
            &holdImage_,
            &holdEachImage_,
            &holdBreakImage_,
        }
    );

    QVector<const QImage*> trackImages{
        &slideTrackImage_,
        &slideTrackEachImage_,
        &slideTrackBreakImage_,
    };
    for (const QImage& image : wifiImages_) {
        trackImages.append(&image);
    }
    for (const QImage& image : wifiEachImages_) {
        trackImages.append(&image);
    }
    for (const QImage& image : wifiBreakImages_) {
        trackImages.append(&image);
    }
    rebuildAtlas(trackAtlasImage_, trackImages);

    rebuildAtlas(
        touchAtlasImage_,
        QVector<const QImage*>{
            &touchCornerImage_,
            &touchCornerEachImage_,
            &touchPointImage_,
            &touchPointEachImage_,
            &touchHold0Image_,
            &touchHold1Image_,
            &touchHold2Image_,
            &touchHold3Image_,
            &touchHoldBorderImage_,
        }
    );

    rebuildAtlas(
        guideAtlasImage_,
        QVector<const QImage*>{
            &noteGuideNormalImage_,
            &noteGuideBreakImage_,
            &noteGuideEachImage_,
            &noteGuideEachLine1Image_,
            &noteGuideEachLine2Image_,
            &noteGuideEachLine3Image_,
            &noteGuideEachLine4Image_,
            &noteGuideHoldEndImage_,
            &noteGuideHoldEachEndImage_,
            &noteGuideHoldBreakEndImage_,
            &noteGuideSlideImage_,
        }
    );
}

void PreviewCanvas::rebuildAtlas(QImage& atlasImage, const QVector<const QImage*>& images)
{
    atlasImage = QImage();

    struct Placement {
        const QImage* source = nullptr;
        QRect rect;
    };

    QVector<Placement> placements;
    QHash<quint64, bool> seen;
    int x = kAtlasPadding;
    int y = kAtlasPadding;
    int rowHeight = 0;
    int atlasWidth = 0;

    for (const QImage* image : images) {
        if (image == nullptr || image->isNull()) {
            continue;
        }

        const quint64 key = image->cacheKey();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key, true);

        const int width = image->width();
        const int height = image->height();
        if (width <= 0 || height <= 0) {
            continue;
        }

        if (x > kAtlasPadding && x + width + kAtlasPadding > kAtlasMaxWidth) {
            x = kAtlasPadding;
            y += rowHeight + kAtlasPadding;
            rowHeight = 0;
        }

        Placement placement;
        placement.source = image;
        placement.rect = QRect(x, y, width, height);
        placements.append(placement);

        x += width + kAtlasPadding;
        rowHeight = qMax(rowHeight, height);
        atlasWidth = qMax(atlasWidth, x);
    }

    if (placements.isEmpty()) {
        return;
    }

    const int finalWidth = qMax(kAtlasPadding * 2 + 1, atlasWidth);
    const int finalHeight = qMax(kAtlasPadding * 2 + 1, y + rowHeight + kAtlasPadding);
    atlasImage = QImage(finalWidth, finalHeight, QImage::Format_ARGB32_Premultiplied);
    atlasImage.fill(Qt::transparent);

    QPainter atlasPainter(&atlasImage);
    atlasPainter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const Placement& placement : placements) {
        atlasPainter.drawImage(placement.rect.topLeft(), *placement.source);
        AtlasRegionRef region;
        region.atlasImage = &atlasImage;
        region.rect = placement.rect;
        atlasRegions_.insert(placement.source->cacheKey(), region);
    }
    atlasPainter.end();
}

bool PreviewCanvas::resolveAtlasImage(
    const QImage& image,
    const QRectF& sourceRect,
    const QImage*& atlasImage,
    QRectF& atlasSourceRect
) const
{
    const auto it = atlasRegions_.constFind(image.cacheKey());
    if (it == atlasRegions_.cend() || it.value().atlasImage == nullptr || it.value().atlasImage->isNull()) {
        atlasImage = &image;
        atlasSourceRect = sourceRect;
        return false;
    }

    atlasImage = it.value().atlasImage;
    const QRectF atlasRegion(it.value().rect);
    if (sourceRect.isValid() && !sourceRect.isEmpty()) {
        atlasSourceRect = QRectF(
            atlasRegion.left() + sourceRect.left(),
            atlasRegion.top() + sourceRect.top(),
            sourceRect.width(),
            sourceRect.height()
        );
    } else {
        atlasSourceRect = atlasRegion;
    }
    return true;
}

void PreviewCanvas::flushTapAtlasBatch(QPainter& painter)
{
    if (tapAtlasBatch_.isEmpty()) {
        return;
    }

    const bool hadNative = nativePaintingActive_;
    if (!hadNative && glRenderer_.isInitialized()) {
        painter.beginNativePainting();
        nativePaintingActive_ = true;
    }

    for (const BatchedSprite& sprite : tapAtlasBatch_) {
        if (sprite.image == nullptr || sprite.image->isNull()) {
            continue;
        }

        const QRectF targetRect(
            sprite.center.x() - sprite.targetWidth / 2.0,
            sprite.center.y() - sprite.targetHeight / 2.0,
            sprite.targetWidth,
            sprite.targetHeight
        );
        bool renderedByGl = false;
        if (nativePaintingActive_ && glRenderer_.isInitialized()) {
            renderedByGl = glRenderer_.drawImageQuad(
                *sprite.image,
                targetRect,
                sprite.angleDegrees,
                sprite.opacity,
                sprite.sourceRect
            );
        }

        if (renderedByGl) {
            usedGpuRendererThisFrame_ = true;
            continue;
        }

        if (nativePaintingActive_) {
            painter.endNativePainting();
            nativePaintingActive_ = false;
        }

        painter.save();
        painter.setOpacity(sprite.opacity);
        painter.translate(sprite.center);
        painter.rotate(sprite.angleDegrees);
        painter.drawImage(
            QRectF(-sprite.targetWidth / 2.0, -sprite.targetHeight / 2.0, sprite.targetWidth, sprite.targetHeight),
            *sprite.image,
            sprite.sourceRect
        );
        painter.restore();
        ++cpuFallbackCount_;

        if (hadNative && glRenderer_.isInitialized()) {
            painter.beginNativePainting();
            nativePaintingActive_ = true;
        }
    }

    if (!hadNative && nativePaintingActive_) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }

    tapAtlasBatch_.clear();
}

const QImage* PreviewCanvas::selectSlideStarImage(const TimelineNoteMarker& marker) const
{
    const QImage* starImage = &starImage_;
    if (marker.headBreak) {
        if (marker.sameHeadSlide && !starBreakDoubleImage_.isNull()) {
            starImage = &starBreakDoubleImage_;
        } else if (!starBreakImage_.isNull()) {
            starImage = &starBreakImage_;
        }
    } else if (marker.headEach) {
        if (marker.sameHeadSlide && !starEachDoubleImage_.isNull()) {
            starImage = &starEachDoubleImage_;
        } else if (!starEachImage_.isNull()) {
            starImage = &starEachImage_;
        } else if (marker.sameHeadSlide && !starDoubleImage_.isNull()) {
            starImage = &starDoubleImage_;
        }
    } else if (marker.sameHeadSlide && !starDoubleImage_.isNull()) {
        starImage = &starDoubleImage_;
    }
    return starImage;
}

const QImage* PreviewCanvas::selectSlideMovingStarImage(const TimelineNoteMarker& marker) const
{
    const QImage* starImage = &starImage_;
    if (marker.trackBreak && !starBreakImage_.isNull()) {
        starImage = &starBreakImage_;
    } else if (marker.slideEach && !starEachImage_.isNull()) {
        starImage = &starEachImage_;
    }
    return starImage;
}

qreal PreviewCanvas::slideStartupStarInitialScale(const QImage& starImage) const
{
    if (starImage.isNull()) {
        return kStarAssetScale;
    }

    const qreal headWidth = (!tapImage_.isNull() ? tapImage_.width() * kSkinAssetScale : starImage.width() * kStarAssetScale)
        * kSlideSpawnStarRelativeScale;
    return qMax<qreal>(0.01, headWidth / qMax(1, starImage.width()));
}

const QImage* PreviewCanvas::selectSlideTrackImage(const TimelineNoteMarker& marker) const
{
    const QImage* image = &slideTrackImage_;
    if (marker.trackBreak && !slideTrackBreakImage_.isNull()) {
        image = &slideTrackBreakImage_;
    } else if (marker.slideEach && !slideTrackEachImage_.isNull()) {
        image = &slideTrackEachImage_;
    }
    return image;
}

const QImage* PreviewCanvas::selectWifiTrackImage(const TimelineNoteMarker& marker, int sampleIndex, int sampleCount) const
{
    const QVector<QImage>* images = &wifiImages_;
    if (marker.trackBreak && !wifiBreakImages_.isEmpty()) {
        images = &wifiBreakImages_;
    } else if (marker.slideEach && !wifiEachImages_.isEmpty()) {
        images = &wifiEachImages_;
    }
    if (images->isEmpty()) {
        return nullptr;
    }
    const int maxIndex = images->size() - 1;
    const int sourceIndex = sampleCount <= 0
        ? qBound(0, sampleIndex, maxIndex)
        : sampleCount <= 1
        ? 0
        : qBound(0, qRound(static_cast<qreal>(sampleIndex) * maxIndex / qMax(1, sampleCount - 1)), maxIndex);
    return &images->at(sourceIndex);
}

QImage PreviewCanvas::cachedGuideTransform(const QImage& image, int targetWidth, int targetHeight, qreal angleDegrees)
{
    if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        return QImage();
    }
    const int width = quantizeDimension(targetWidth, kGuideTransformSizeStep);
    const int height = quantizeDimension(targetHeight, kGuideTransformSizeStep);
    const int angleBucket = qRound(angleDegrees);
    const QString key = QStringLiteral("%1|%2|%3|%4")
        .arg(static_cast<qulonglong>(image.cacheKey()))
        .arg(width)
        .arg(height)
        .arg(angleBucket);
    if (kEnablePreviewCaches) {
        const auto cached = guideTransformCache_.constFind(key);
        if (cached != guideTransformCache_.cend()) {
            return cached.value();
        }
    }

    QImage scaled = image;
    if (scaled.width() != width || scaled.height() != height) {
        scaled = scaled.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QImage transformed = scaled;
    if ((angleBucket % 360) != 0) {
        transformed = scaled.transformed(QTransform().rotate(angleBucket), Qt::SmoothTransformation);
    }
    if (kEnablePreviewCaches) {
        while (guideTransformCache_.size() >= kGuideTransformCacheLimit && !guideTransformCacheOrder_.isEmpty()) {
            const QString oldestKey = guideTransformCacheOrder_.takeFirst();
            guideTransformCache_.remove(oldestKey);
        }
        guideTransformCache_.insert(key, transformed);
        guideTransformCacheOrder_.append(key);
    }
    return transformed;
}

QImage PreviewCanvas::cachedSpriteTransform(const QImage& image, int targetWidth, int targetHeight, qreal angleDegrees)
{
    if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        return QImage();
    }
    const int width = quantizeDimension(targetWidth, kSpriteTransformSizeStep);
    const int height = quantizeDimension(targetHeight, kSpriteTransformSizeStep);
    const int angleBucket = qRound(angleDegrees);
    const QString key = QStringLiteral("%1|%2|%3|%4")
        .arg(static_cast<qulonglong>(image.cacheKey()))
        .arg(width)
        .arg(height)
        .arg(angleBucket);
    if (kEnablePreviewCaches) {
        const auto cached = spriteTransformCache_.constFind(key);
        if (cached != spriteTransformCache_.cend()) {
            return cached.value();
        }
    }

    QImage scaled = image;
    if (scaled.width() != width || scaled.height() != height) {
        scaled = scaled.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    QImage transformed = scaled;
    if ((angleBucket % 360) != 0) {
        transformed = scaled.transformed(QTransform().rotate(angleBucket), Qt::SmoothTransformation);
    }
    if (kEnablePreviewCaches) {
        while (spriteTransformCache_.size() >= kSpriteTransformCacheLimit && !spriteTransformCacheOrder_.isEmpty()) {
            const QString oldestKey = spriteTransformCacheOrder_.takeFirst();
            spriteTransformCache_.remove(oldestKey);
        }
        spriteTransformCache_.insert(key, transformed);
        spriteTransformCacheOrder_.append(key);
    }
    return transformed;
}

QRectF PreviewCanvas::currentStageRect() const
{
    return QRectF(
        kMargin,
        kMargin,
        qMax<qreal>(1.0, width() - kMargin * 2),
        qMax<qreal>(1.0, height() - kMargin * 2)
    );
}

QRectF PreviewCanvas::stagePlayfieldRect(const QRectF& stageRect) const
{
    const QRectF innerRect = stageRect.adjusted(18.0, 18.0, -18.0, -18.0);
    const qreal playfieldSide = qMin<qreal>(qMin(innerRect.width(), innerRect.height()), kLogicalCanvasSize);
    return QRectF(
        innerRect.center().x() - playfieldSide / 2.0,
        innerRect.center().y() - playfieldSide / 2.0,
        playfieldSide,
        playfieldSide
    );
}

QRectF PreviewCanvas::currentPlayfieldRect() const
{
    return stagePlayfieldRect(currentStageRect());
}

void PreviewCanvas::drawStageBackground(QPainter& painter, const QRectF& stageRect)
{
    painter.fillRect(stageRect, QColor("#1F2833"));

    QSize mediaSize = mediaFrame_.size();
    bool hasVideoFrame = false;
#ifdef HAVE_QT_MULTIMEDIA
    hasVideoFrame = videoFrame_.isValid();
    if (hasVideoFrame) {
        mediaSize = videoFrame_.surfaceFormat().viewport().size();
        if (mediaSize.isEmpty()) {
            mediaSize = videoFrame_.surfaceFormat().frameSize();
        }
    }
#endif

    if (!mediaSize.isEmpty()) {
        QSize fittedSize = mediaSize;
        const QSize mediaBounds(
            qMax(1, qRound(stageRect.width())),
            qMax(1, qRound(stageRect.height()))
        );
        fittedSize.scale(mediaBounds, Qt::KeepAspectRatioByExpanding);
        if (!fittedSize.isEmpty()) {
            const QRectF targetRect(
                stageRect.center().x() - fittedSize.width() / 2.0,
                stageRect.center().y() - fittedSize.height() / 2.0,
                fittedSize.width(),
                fittedSize.height()
            );
            bool renderedByGl = false;
            if (glRenderer_.isInitialized()) {
                painter.beginNativePainting();
                if (hasVideoFrame) {
#ifdef HAVE_QT_MULTIMEDIA
                    renderedByGl = glRenderer_.drawVideoFrame(videoFrame_, targetRect, 1.0);
#endif
                } else if (!mediaFrame_.isNull()) {
                    renderedByGl = glRenderer_.drawImageQuad(mediaFrame_, targetRect, 0.0, 1.0, QRectF(), false);
                }
                painter.endNativePainting();
            }
            if (renderedByGl) {
                usedGpuRendererThisFrame_ = true;
            } else if (!mediaFrame_.isNull()) {
                ++cpuFallbackCount_;
                painter.drawImage(targetRect, mediaFrame_);
            } else if (hasVideoFrame) {
#ifdef HAVE_QT_MULTIMEDIA
                const QImage fallbackImage = videoFrame_.toImage();
                if (!fallbackImage.isNull()) {
                    ++cpuFallbackCount_;
                    painter.drawImage(targetRect, fallbackImage);
                }
#endif
            }
        }
    }

    const int darkAlpha = qBound(0, qRound((1.0 - backgroundBrightness_) * 255.0), 255);
    if (darkAlpha > 0) {
        painter.fillRect(stageRect, QColor(0, 0, 0, darkAlpha));
    }

}

void PreviewCanvas::drawPlayfieldBackdrop(QPainter& painter, const QRectF& playfieldRect)
{
    if (outlineImage_.isNull()) {
        return;
    }

    const QPointF outlineTopLeft = mapLogicalPointToRect(QPointF(kLogicalOutlineInset, kLogicalOutlineInset), playfieldRect);
    const qreal outlineSide = mapLogicalLengthToRect(kLogicalCanvasSize - kLogicalOutlineInset * 2.0, playfieldRect);
    const QRectF targetRect(outlineTopLeft.x(), outlineTopLeft.y(), outlineSide, outlineSide);
    bool renderedByGl = false;
    if (glRenderer_.isInitialized()) {
        painter.beginNativePainting();
        renderedByGl = glRenderer_.drawImageQuad(outlineImage_, targetRect);
        painter.endNativePainting();
    }
    if (renderedByGl) {
        usedGpuRendererThisFrame_ = true;
        return;
    }

    ++cpuFallbackCount_;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(targetRect, outlineImage_);
    painter.restore();
}

#include "PreviewCanvas.Objects.cpp"

void PreviewCanvas::updateFpsSample()
{
    if (!fpsTimer_.isValid()) {
        fpsTimer_.start();
        fpsFrameCounter_ = 0;
        fpsDisplay_ = 0.0;
        lastFrameTimestampNs_ = 0;
        frameIntervalsMs_.clear();
        frameIntervalsMs_.resize(kFrameStatsWindowSize);
        frameIntervalsMs_.fill(0.0);
        frameIntervalWriteIndex_ = 0;
        frameIntervalCount_ = 0;
        frameMsAverage_ = 0.0;
        frameMsP95_ = 0.0;
        frameMsMax_ = 0.0;
    }
    const qint64 nowNs = fpsTimer_.nsecsElapsed();
    if (lastFrameTimestampNs_ > 0) {
        const double intervalMs = static_cast<double>(nowNs - lastFrameTimestampNs_) / 1000000.0;
        if (!frameIntervalsMs_.isEmpty() && intervalMs > 0.0 && intervalMs < 250.0) {
            frameIntervalsMs_[frameIntervalWriteIndex_] = intervalMs;
            frameIntervalWriteIndex_ = (frameIntervalWriteIndex_ + 1) % frameIntervalsMs_.size();
            frameIntervalCount_ = qMin(frameIntervalCount_ + 1, frameIntervalsMs_.size());
        }
    }
    lastFrameTimestampNs_ = nowNs;
    ++fpsFrameCounter_;
    const qint64 elapsedMs = nowNs / 1000000;
    if (elapsedMs < kFpsSampleWindowMs) {
        return;
    }
    fpsDisplay_ = static_cast<double>(fpsFrameCounter_) * 1000.0 / static_cast<double>(elapsedMs);
    if (frameIntervalCount_ > 0) {
        QVector<double> samples;
        samples.reserve(frameIntervalCount_);
        for (int i = 0; i < frameIntervalCount_; ++i) {
            samples.append(frameIntervalsMs_[i]);
        }
        const double sum = std::accumulate(samples.cbegin(), samples.cend(), 0.0);
        frameMsAverage_ = sum / static_cast<double>(samples.size());
        std::sort(samples.begin(), samples.end());
        frameMsMax_ = samples.constLast();
        const int p95Index = qBound(0, static_cast<int>(qCeil(samples.size() * 0.95)) - 1, samples.size() - 1);
        frameMsP95_ = samples[p95Index];
    }
    fpsFrameCounter_ = 0;
    fpsTimer_.restart();
    lastFrameTimestampNs_ = 0;
}

void PreviewCanvas::paintGL()
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    collectGpuProfilingResults(false);

    if (profileSessionClock_.isValid() && pendingTickToPaintStartNs_ >= 0) {
        const qint64 nowNs = profileSessionClock_.nsecsElapsed();
        profileTickToPaintSamplesMs_.append(
            static_cast<double>(qMax<qint64>(0, nowNs - pendingTickToPaintStartNs_)) / 1000000.0
        );
        pendingTickToPaintStartNs_ = -1;
    }

    if (!profileSessionClock_.isValid()) {
        profileSessionClock_.start();
        lastProfileFrameStartNs_ = 0;
    } else {
        const qint64 frameStartNs = profileSessionClock_.nsecsElapsed();
        if (lastProfileFrameStartNs_ >= 0) {
            const qint64 intervalNs = frameStartNs - lastProfileFrameStartNs_;
            const qint64 residualNs = qMax<qint64>(0, intervalNs - lastProfileCpuFrameNs_);
            profilePresentApproxSamplesMs_.append(static_cast<double>(residualNs) / 1000000.0);
        }
        lastProfileFrameStartNs_ = frameStartNs;
    }

    bool gpuQueryActive = false;
    if (gpuTimerQueriesSupported_ && extra != nullptr && !gpuTimeQueryPending_[gpuTimeQueryCursor_]) {
        extra->glBeginQuery(GL_TIME_ELAPSED, gpuTimeQueries_[gpuTimeQueryCursor_]);
        gpuQueryActive = true;
    }

    QElapsedTimer cpuFrameTimer;
    cpuFrameTimer.start();
    glRenderer_.beginFrame(size(), devicePixelRatioF());
    {
        QPainter painter(this);
        renderCanvas(painter);
    }
    const qint64 cpuFrameNs = cpuFrameTimer.nsecsElapsed();
    const qint64 cpuUploadNs = static_cast<qint64>(glRenderer_.frameCpuUploadNs());
    const qint64 videoMapNs = static_cast<qint64>(glRenderer_.frameVideoMapNs());
    const qint64 videoUploadNs = static_cast<qint64>(glRenderer_.frameVideoUploadNs());
    glRenderer_.endFrame();

    if (gpuQueryActive && extra != nullptr) {
        extra->glEndQuery(GL_TIME_ELAPSED);
        gpuTimeQueryPending_[gpuTimeQueryCursor_] = true;
        gpuTimeQueryCursor_ = (gpuTimeQueryCursor_ + 1) % 4;
    }

    const double cpuUploadMs = static_cast<double>(cpuUploadNs) / 1000000.0;
    const double cpuPrepMs = static_cast<double>(qMax<qint64>(0, cpuFrameNs - cpuUploadNs)) / 1000000.0;
    lastProfileCpuFrameNs_ = cpuFrameNs;
    profileCpuPrepTotalMs_ += cpuPrepMs;
    profileCpuUploadTotalMs_ += cpuUploadMs;
    profileCpuPrepSamplesMs_.append(cpuPrepMs);
    profileCpuUploadSamplesMs_.append(cpuUploadMs);
    if (videoMapNs > 0) {
        profileVideoMapSamplesMs_.append(static_cast<double>(videoMapNs) / 1000000.0);
    }
    if (videoUploadNs > 0) {
        profileVideoUploadSamplesMs_.append(static_cast<double>(videoUploadNs) / 1000000.0);
    }
    ++profileFrameCount_;
}

void PreviewCanvas::renderCanvas(QPainter& painter)
{
    usedGpuRendererThisFrame_ = false;
    cpuFallbackCount_ = 0;
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.fillRect(QRect(QPoint(0, 0), size()), QColor("#1F2833"));

    const QRectF stageRect = currentStageRect();
    const QRectF playfieldRect = stagePlayfieldRect(stageRect);

    drawStageBackground(painter, stageRect);
    drawPlayfieldBackdrop(painter, playfieldRect);
    const bool batchNative = glRenderer_.isInitialized();
    if (batchNative) {
        beginNativeBatch(painter);
    }
    drawJudgeEffectFireworkLayer(painter, playfieldRect);
    drawGuideLayer(painter, playfieldRect);
    drawTrackLayer(painter, playfieldRect);
    drawSlideMotionLayer(painter, playfieldRect);
    drawJudgeEffectLayer(painter, playfieldRect);
    drawJudgeEffectTouchLayer(painter, playfieldRect);
    drawHoldLayer(painter, playfieldRect);
    drawTapLayer(painter, playfieldRect);
    drawTouchLayer(painter, playfieldRect);
    drawTouchHoldLayer(painter, playfieldRect);
    if (batchNative) {
        endNativeBatch(painter);
    }

    updateFpsSample();
    drawHud(painter, stageRect);
}

void PreviewCanvas::collectGpuProfilingResults(bool waitForAll)
{
    if (!gpuTimerQueriesSupported_) {
        return;
    }

    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    QOpenGLExtraFunctions* extra = ctx != nullptr ? ctx->extraFunctions() : nullptr;
    if (extra == nullptr) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!gpuTimeQueryPending_[i]) {
            continue;
        }

        bool ready = waitForAll;
        if (!waitForAll) {
            GLuint available = 0;
            extra->glGetQueryObjectuiv(gpuTimeQueries_[i], GL_QUERY_RESULT_AVAILABLE, &available);
            ready = available != 0;
        }

        if (!ready) {
            continue;
        }

        GLuint resultNs = 0;
        extra->glGetQueryObjectuiv(gpuTimeQueries_[i], GL_QUERY_RESULT, &resultNs);
        const double gpuMs = static_cast<double>(resultNs) / 1000000.0;
        profileGpuDrawTotalMs_ += gpuMs;
        ++profileGpuSampleCount_;
        profileGpuDrawSamplesMs_.append(gpuMs);
        gpuTimeQueryPending_[i] = false;
    }
}

QString PreviewCanvas::profilingSummaryPath() const
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.filePath("preview_profile_summary.txt"));
}

QSize PreviewCanvas::preferredSize() const
{
    return QSize(620, 620);
}

