#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantMap>

class QQmlEngine;

namespace miacode::cover_export {

class CoverLayoutModel;

struct CoverExportResult {
    bool success = false;
    QString outputPath;
    QString errorMessage;
};

enum class CoverBackgroundMode {
    Jacket = 0,
    Custom = 1,
    Transparent = 2,
};

// Presentation inputs shared by the on-screen QML composer and the off-screen
// Quick export renderer. Geometry itself deliberately stays in CoverLayoutModel.
struct CoverComposerInputs {
    QVariantMap templateMap;
    QVariantMap trackOverrides;
    QString jacketPath;
    QString backgroundPath;
    CoverBackgroundMode backgroundMode = CoverBackgroundMode::Jacket;
    bool blurBackground = true;
    double coverBgBrightness = 0.45;
    bool cardShadow = false;
    bool chartFrameBackground = false;
    double chartFrameBgTransparency = 0.5;
    double chartFrameBgBrightness = 0.8;
    double chartFrameDiskDiameter = 0.0;
};

// Registers the provider used by CoverComposer.qml's image://coverchart URLs.
// The application QML engine owns the provider; the function is idempotent per
// engine and contains no QWidget or native-window dependency.
void registerCoverChartImageProvider(QQmlEngine* engine, CoverLayoutModel* model);

QImage renderCoverComposite(CoverLayoutModel* model,
                            const CoverComposerInputs& inputs,
                            const QSize& fullSize,
                            QString* errorMessage);

CoverExportResult exportCoverComposite(CoverLayoutModel* model,
                                       const CoverComposerInputs& inputs,
                                       const QSize& fullSize,
                                       const QString& outputDirectory);

}  // namespace miacode::cover_export
