#pragma once

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
