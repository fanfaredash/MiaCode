#pragma once

#include <QString>

enum class PreviewOutlineVariant {
    Point = 0,
    Line = 1,
    JudgeArea = 2,
    JudgeAreaLabeled = 3,
};

enum class PreviewBackgroundScaleMode {
    FillCrop = 0,
    FitContain = 1,
    SquareFitContain = 2,
    InnerCircleFitOuterFill = 3,
};

enum class PreviewTapJudgeTextDistance {
    Inner = 0,
    Middle = 1,
    Outer = 2,
};

enum class PreviewJudgeEffectStyle {
    Standard = 0,
    Starry = 1,
};

inline const char* tapJudgeTextDistanceToken(PreviewTapJudgeTextDistance distance)
{
    switch (distance) {
    case PreviewTapJudgeTextDistance::Middle:
        return "middle";
    case PreviewTapJudgeTextDistance::Outer:
        return "outer";
    case PreviewTapJudgeTextDistance::Inner:
    default:
        return "inner";
    }
}

inline PreviewTapJudgeTextDistance tapJudgeTextDistanceFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QLatin1String("middle") || normalized == QLatin1String("mid")) {
        return PreviewTapJudgeTextDistance::Middle;
    }
    if (normalized == QLatin1String("outer") || normalized == QLatin1String("far")) {
        return PreviewTapJudgeTextDistance::Outer;
    }
    return PreviewTapJudgeTextDistance::Inner;
}

inline const char* judgeEffectStyleToken(PreviewJudgeEffectStyle style)
{
    switch (style) {
    case PreviewJudgeEffectStyle::Starry:
        return "starry";
    case PreviewJudgeEffectStyle::Standard:
    default:
        return "standard";
    }
}

inline PreviewJudgeEffectStyle judgeEffectStyleFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QLatin1String("starry") || normalized == QLatin1String("new") || normalized == QLatin1String("alt")) {
        return PreviewJudgeEffectStyle::Starry;
    }
    return PreviewJudgeEffectStyle::Standard;
}
