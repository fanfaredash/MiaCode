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


#include "PreviewCanvas.Runtime.cpp"
#include "PreviewCanvas.SkinAndAtlas.cpp"
#include "PreviewCanvas.GLAndTransforms.cpp"
#include "PreviewCanvas.Render.cpp"
