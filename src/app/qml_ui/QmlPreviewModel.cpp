#include "QmlPreviewModel.h"

#include "app/quick_shell/QuickShellController.h"
#include "common/MuriRenderOptions.h"
#include "mainwindow/MainWindow.h"

#include <QRegularExpression>
#include <QVariantMap>

QmlPreviewModel::QmlPreviewModel(
    MainWindow& backend,
    QuickShellController& controller,
    QObject* parent)
    : QObject(parent)
    , backend_(&backend)
    , controller_(&controller)
{
    connect(controller_, &QuickShellController::shellStateChanged, this, &QmlPreviewModel::changed);
}

double QmlPreviewModel::positionSeconds() const { return controller_->previewPositionSeconds(); }
double QmlPreviewModel::durationSeconds() const { return controller_->previewDurationSeconds(); }
double QmlPreviewModel::lowerBoundSeconds() const { return controller_->previewLowerBoundSeconds(); }
double QmlPreviewModel::rate() const
{
    QString label = controller_->previewSpeedLabel().trimmed();
    label.remove(QLatin1Char('x'), Qt::CaseInsensitive);
    bool ok = false;
    const double value = label.toDouble(&ok);
    return ok ? value : 1.0;
}
bool QmlPreviewModel::playing() const { return controller_->previewPlaying(); }

QString QmlPreviewModel::renderMode() const
{
    return muriRenderModeToken(backend_->muriRenderMode());
}

QString QmlPreviewModel::renderModeLabel() const
{
    switch (backend_->muriRenderMode()) {
    case RenderMode::EraseByArea:
        return tr("按区消去");
    case RenderMode::MaimuriDxStyle:
        return tr("无理检测");
    case RenderMode::Native:
        break;
    }
    return tr("常规模式");
}

QVariantList QmlPreviewModel::statistics() const
{
    QVariantList result;
    const QStringList texts = controller_->previewStatsTexts();
    for (const QString& text : texts.mid(0, 6)) {
        const int separator = text.indexOf(QRegularExpression(QStringLiteral("\\s")));
        result.append(QVariantMap{
            {QStringLiteral("name"), separator > 0 ? text.left(separator) : text},
            {QStringLiteral("value"), separator > 0 ? text.mid(separator + 1).trimmed() : QString()},
        });
    }
    while (result.size() < 6) {
        result.append(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("—")},
            {QStringLiteral("value"), QStringLiteral("0/0")},
        });
    }
    return result;
}
QObject* QmlPreviewModel::runtime() const { return controller_->previewRuntime(); }
QObject* QmlPreviewModel::mediaHost() const { return controller_->previewStageMediaHost(); }

void QmlPreviewModel::setPositionSeconds(double value) { controller_->seekPreview(value); }
void QmlPreviewModel::setRate(double value) { controller_->setPreviewRate(value); }
void QmlPreviewModel::setPlaying(bool value)
{
    if (value != playing()) controller_->togglePreviewPlayback();
}

void QmlPreviewModel::toggleRenderMode()
{
    controller_->toggleMuriRenderMode();
    emit changed();
}

void QmlPreviewModel::stop() { controller_->stopPreview(); }

void QmlPreviewModel::beginScrub() { controller_->beginPreviewScrub(); }

void QmlPreviewModel::updateScrub(double second) { controller_->updatePreviewScrub(second, true); }

void QmlPreviewModel::endScrub(double second) { controller_->endPreviewScrub(second, true); }
