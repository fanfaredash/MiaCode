#include "QmlPreviewModel.h"

#include "app/quick_shell/QuickShellController.h"
#include "common/MuriRenderOptions.h"
#include "mainwindow/MainWindow.h"

#include <array>
#include <QRegularExpression>
#include <QVariantMap>

namespace {

struct StatisticDescriptor {
    const char* kind;
    const char* fallbackName;
};

constexpr std::array<StatisticDescriptor, 6> kStatisticDescriptors{{
    {"tap", "Tap"},
    {"hold", "Hold"},
    {"slide", "Slide"},
    {"touch", "Touch"},
    {"break", "Break"},
    {"total", "Total"},
}};

} // namespace

QmlPreviewModel::QmlPreviewModel(
    MainWindow& backend,
    QuickShellController& controller,
    QObject* parent)
    : QObject(parent)
    , backend_(&backend)
    , controller_(&controller)
{
    connect(controller_, &QuickShellController::shellStateChanged, this, &QmlPreviewModel::changed);
    connect(backend_, &MainWindow::previewSkinDirectoryChanged, this, &QmlPreviewModel::changed);
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
    const QString skinRevision = QString::number(qHash(currentSkinDirectory()));
    for (qsizetype index = 0; index < static_cast<qsizetype>(kStatisticDescriptors.size()); ++index) {
        const StatisticDescriptor& descriptor = kStatisticDescriptors.at(static_cast<std::size_t>(index));
        const QString text = texts.value(index);
        const int separator = text.indexOf(QRegularExpression(QStringLiteral("\\s")));
        const QString name = separator > 0
            ? text.left(separator)
            : QString::fromLatin1(descriptor.fallbackName);
        const QString value = separator > 0 ? text.mid(separator + 1).trimmed() : QStringLiteral("0/0");
        const int valueSeparator = value.indexOf(QLatin1Char('/'));
        const int played = valueSeparator > 0 ? value.left(valueSeparator).toInt() : 0;
        const int total = valueSeparator > 0 ? value.mid(valueSeparator + 1).toInt() : 0;
        const QString kind = QString::fromLatin1(descriptor.kind);
        result.append(QVariantMap{
            {QStringLiteral("kind"), kind},
            {QStringLiteral("name"), name},
            {QStringLiteral("played"), played},
            {QStringLiteral("total"), total},
            {QStringLiteral("value"), value},
            {QStringLiteral("iconSource"), kind == QStringLiteral("total")
                ? QString()
                : QStringLiteral("image://noteicon/%1?skin=%2").arg(kind, skinRevision)},
        });
    }
    return result;
}

QString QmlPreviewModel::currentSkinDirectory() const
{
    return backend_->resolvePreviewSkinDir();
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
