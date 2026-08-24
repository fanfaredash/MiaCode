#include "../../MainWindow.h"
#include "../../MainWindowShared.h"
#include "MainWindow.FrameSection.h"
#include "../editor/MainWindow.EditorSection.h"
#include "../dialogs/MainWindow.DialogsSection.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../export/MainWindow.ExportSection.h"
#include "../preferences/MainWindow.PreferencesSection.h"
#include "../preview/MainWindow.PreviewSection.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "../validation/MainWindow.ValidationSection.h"
#include "../validation/EditorSelectionUtils.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "ShortcutRegistry.h"
#include "TimelineView.h"
#include "BusySpinner.h"
#include "UiText.h"
#include "UiTheme.h"
#include "WindowParityMetrics.h"
#include "app/ui/AppBackgroundPainter.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "extensions/ExtensionManager.h"
#include "extensions/ExtensionOpenBridge.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/export_page/ExportLauncherPage.h"
#include "tools/latency/LatencyDetectionPage.h"
#include "tools/latency/LatencySandboxController.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <algorithm>
#include <functional>

using namespace miacode::mainwindow::shared;

namespace {

QColor colorFromJsonValue(const QJsonValue& value, QColor fallback)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return fallback;
    }
    const QColor color(text);
    return color.isValid() ? color : fallback;
}

QJsonObject muriReportToJson(const MuriAnalysisReport& report)
{
    QJsonArray diagnostics;
    for (const MuriDiagnostic& diagnostic : report.diagnostics) {
        diagnostics.append(QJsonObject{
            {QStringLiteral("kind"), muriKindDisplayName(diagnostic.kind, false)},
            {QStringLiteral("level"), muriAlertLevelDisplayName(diagnostic.alertLevel, false)},
            {QStringLiteral("second"), diagnostic.second},
            {QStringLiteral("anchorSecond"), diagnostic.anchorSecond},
            {QStringLiteral("line"), diagnostic.line},
            {QStringLiteral("col"), diagnostic.col},
            {QStringLiteral("markerKey"), diagnostic.markerKey},
            {QStringLiteral("title"), diagnostic.title},
            {QStringLiteral("detail"), diagnostic.detail},
        });
    }
    return QJsonObject{
        {QStringLiteral("revision"), static_cast<double>(report.revision)},
        {QStringLiteral("sourceSignature"), report.sourceSignature},
        {QStringLiteral("empty"), report.isEmpty()},
        {QStringLiteral("diagnostics"), diagnostics},
        {QStringLiteral("diagnosticCount"), diagnostics.size()},
        {QStringLiteral("markerStateCount"), report.markerStates.size()},
        {QStringLiteral("judgeSpriteEventCount"), report.judgeSpriteEvents.size()},
        {QStringLiteral("padWindowCount"), report.padWindows.size()},
        {QStringLiteral("actionTrailCount"), report.actionTrails.size()},
    };
}

QJsonObject timelineNoteMarkerToJson(const TimelineNoteMarker& marker)
{
    return QJsonObject{
        {QStringLiteral("second"), marker.second},
        {QStringLiteral("endSecond"), marker.endSecond},
        {QStringLiteral("slideTraceSecond"), marker.slideTraceSecond},
        {QStringLiteral("availableSecond"), marker.availableSecond},
        {QStringLiteral("parseOrder"), marker.parseOrder},
        {QStringLiteral("sourceLine"), marker.sourceLine},
        {QStringLiteral("sourceCol"), marker.sourceCol},
        {QStringLiteral("lane"), marker.lane},
        {QStringLiteral("endLane"), marker.endLane},
        {QStringLiteral("type"), marker.type},
        {QStringLiteral("slideDisplayKey"), marker.slideDisplayKey},
        {QStringLiteral("slideTrackKey"), marker.slideTrackKey},
        {QStringLiteral("touchPad"), marker.touchPad},
        {QStringLiteral("isEach"), marker.isEach},
        {QStringLiteral("isBreak"), marker.isBreak},
        {QStringLiteral("isEx"), marker.isEx},
        {QStringLiteral("isFirework"), marker.isFirework},
        {QStringLiteral("isMine"), marker.isMine},
        {QStringLiteral("onSlide"), marker.onSlide},
        {QStringLiteral("slideHead"), marker.slideHead},
        {QStringLiteral("trackMine"), marker.trackMine},
        {QStringLiteral("headMine"), marker.headMine},
        {QStringLiteral("hsMultiplier"), marker.hsMultiplier},
    };
}

QJsonObject registeredContribution(const QJsonObject& params, const QString& fallbackKind)
{
    QJsonObject contribution = params;
    if (!contribution.contains(QStringLiteral("kind"))) {
        contribution.insert(QStringLiteral("kind"), fallbackKind);
    }
    if (!contribution.contains(QStringLiteral("id"))) {
        contribution.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    return contribution;
}

QString contributionTitle(const QJsonObject& object, const QString& fallback = QStringLiteral("Extension"))
{
    const QString title = object.value(QStringLiteral("title")).toString(
        object.value(QStringLiteral("label")).toString(
            object.value(QStringLiteral("name")).toString())).trimmed();
    return title.isEmpty() ? fallback : title;
}

QString firstContributionText(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QString text = object.value(QString::fromLatin1(key)).toString().trimmed();
        if (!text.isEmpty()) {
            return text;
        }
    }
    return QString();
}

QString extensionShortcutIdForCommand(const QString& command)
{
    QString normalized = command.trimmed();
    normalized.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.:-]+")), QStringLiteral("_"));
    if (normalized.isEmpty()) {
        normalized = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    return normalized.startsWith(QStringLiteral("extension."))
        ? normalized
        : QStringLiteral("extension.%1").arg(normalized);
}

QString shortcutDefinitionDisplayName(const ShortcutRegistry::ShortcutDefinition& definition)
{
    if (UiText::isChineseUi() && !definition.labelZh.trimmed().isEmpty()) {
        return definition.labelZh;
    }
    if (!UiText::isChineseUi() && !definition.labelEn.trimmed().isEmpty()) {
        return definition.labelEn;
    }
    return definition.id;
}

bool gestureModifiersMatch(const QJsonArray& expected, Qt::KeyboardModifiers actual)
{
    if (expected.isEmpty()) {
        return true;
    }
    for (const QJsonValue& value : expected) {
        const QString modifier = value.toString().trimmed().toLower();
        if ((modifier == QStringLiteral("ctrl") || modifier == QStringLiteral("control"))
            && !actual.testFlag(Qt::ControlModifier)) {
            return false;
        }
        if (modifier == QStringLiteral("alt") && !actual.testFlag(Qt::AltModifier)) {
            return false;
        }
        if (modifier == QStringLiteral("shift") && !actual.testFlag(Qt::ShiftModifier)) {
            return false;
        }
        if ((modifier == QStringLiteral("meta") || modifier == QStringLiteral("cmd"))
            && !actual.testFlag(Qt::MetaModifier)) {
            return false;
        }
    }
    return true;
}

bool gestureTargetMatches(const QString& expected, const QString& actual)
{
    const QString normalized = expected.trimmed().toLower();
    return normalized.isEmpty()
        || normalized == QStringLiteral("any")
        || normalized == actual.trimmed().toLower();
}

bool gesturePhaseMatches(const QString& expected, const QString& actual)
{
    const QString normalized = expected.trimmed().toLower();
    return normalized.isEmpty()
        || normalized == QStringLiteral("any")
        || normalized == actual.trimmed().toLower();
}

bool gestureStringMatches(const QJsonObject& gesture, const QString& key, const QString& actual)
{
    const QString expected = gesture.value(key).toString().trimmed();
    return expected.isEmpty()
        || expected.compare(QStringLiteral("any"), Qt::CaseInsensitive) == 0
        || expected.compare(actual, Qt::CaseInsensitive) == 0;
}

QString providerDisplayText(const QJsonObject& provider)
{
    return firstContributionText(provider, {"markdown", "contents", "content", "message", "detail", "title", "label"});
}

bool providerMatchesContext(const QJsonObject& provider, const QJsonObject& context)
{
    const QString token = context.value(QStringLiteral("token")).toString(
        context.value(QStringLiteral("text")).toString()).trimmed();
    const QString pattern = provider.value(QStringLiteral("pattern")).toString(
        provider.value(QStringLiteral("match")).toString()).trimmed();
    if (!pattern.isEmpty() && !token.contains(pattern, Qt::CaseInsensitive)) {
        return false;
    }

    const QString trigger = context.value(QStringLiteral("trigger")).toString(
        context.value(QStringLiteral("triggerCharacter")).toString()).trimmed();
    const QJsonArray triggers = provider.value(QStringLiteral("triggerCharacters")).toArray();
    if (!triggers.isEmpty() && !trigger.isEmpty()) {
        bool matched = false;
        for (const QJsonValue& value : triggers) {
            if (value.toString() == trigger) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

double contributionNumber(const QJsonObject& object, const QString& key, double fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toDouble() : fallback;
}

QStringList resolvedPetOverlayFrames(const QJsonObject& overlay)
{
    QStringList frames;
    for (const QJsonValue& value : overlay.value(QStringLiteral("resolvedFrames")).toArray()) {
        const QString path = value.isObject()
            ? value.toObject().value(QStringLiteral("resolvedPath")).toString()
            : value.toString();
        if (!path.trimmed().isEmpty()) {
            frames.append(path);
        }
    }
    const QString imagePath = overlay.value(QStringLiteral("resolvedImagePath")).toString();
    if (frames.isEmpty() && !imagePath.trimmed().isEmpty()) {
        frames.append(imagePath);
    }
    return frames;
}

QRect petOverlayGeometry(const QJsonObject& overlay, const QSize& hostSize, const QSize& pixmapSize)
{
    const QJsonObject position = overlay.value(QStringLiteral("position")).toObject();
    const double requestedSize = contributionNumber(overlay, QStringLiteral("size"), 96.0);
    const int fallbackWidth = qBound(24, static_cast<int>(requestedSize), qMax(24, hostSize.width()));
    int width = qBound(16, static_cast<int>(contributionNumber(overlay, QStringLiteral("width"), fallbackWidth)), qMax(16, hostSize.width()));
    int height = static_cast<int>(contributionNumber(overlay, QStringLiteral("height"), 0.0));
    if (height <= 0) {
        height = pixmapSize.isValid() && pixmapSize.width() > 0
            ? qMax(16, qRound(static_cast<double>(width) * pixmapSize.height() / pixmapSize.width()))
            : width;
    }
    height = qBound(16, height, qMax(16, hostSize.height()));

    const QString anchor = overlay.value(QStringLiteral("anchor")).toString(
        position.value(QStringLiteral("anchor")).toString(QStringLiteral("bottomRight")));
    const int margin = qBound(0, static_cast<int>(contributionNumber(overlay, QStringLiteral("margin"), 16.0)), 120);
    int x = hostSize.width() - width - margin;
    int y = hostSize.height() - height - margin;
    if (anchor == QStringLiteral("topLeft")) {
        x = margin;
        y = margin;
    } else if (anchor == QStringLiteral("topRight")) {
        x = hostSize.width() - width - margin;
        y = margin;
    } else if (anchor == QStringLiteral("bottomLeft")) {
        x = margin;
        y = hostSize.height() - height - margin;
    } else if (anchor == QStringLiteral("center")) {
        x = (hostSize.width() - width) / 2;
        y = (hostSize.height() - height) / 2;
    }

    const QJsonValue xValue = position.contains(QStringLiteral("x")) ? position.value(QStringLiteral("x")) : overlay.value(QStringLiteral("x"));
    const QJsonValue yValue = position.contains(QStringLiteral("y")) ? position.value(QStringLiteral("y")) : overlay.value(QStringLiteral("y"));
    if (xValue.isDouble()) {
        const double value = xValue.toDouble();
        x = value >= 0.0 && value <= 1.0 ? qRound(value * qMax(0, hostSize.width() - width)) : qRound(value);
    }
    if (yValue.isDouble()) {
        const double value = yValue.toDouble();
        y = value >= 0.0 && value <= 1.0 ? qRound(value * qMax(0, hostSize.height() - height)) : qRound(value);
    }

    return QRect(
        qBound(0, x, qMax(0, hostSize.width() - width)),
        qBound(0, y, qMax(0, hostSize.height() - height)),
        width,
        height);
}

class ExtensionPetOverlayWidget final : public QLabel {
public:
    ExtensionPetOverlayWidget(
        const QJsonObject& overlay,
        QWidget* parent,
        std::function<void(const QString&)> runCommand)
        : QLabel(parent)
        , overlay_(overlay)
        , runCommand_(std::move(runCommand))
    {
        setObjectName(QStringLiteral("ExtensionPetOverlay"));
        setProperty("miacode.extension.overlay.id", overlay.value(QStringLiteral("id")).toString());
        setProperty("miacode.extension.overlay.ownerId", overlay.value(QStringLiteral("ownerId")).toString());
        setProperty("miacode.extension.overlay.kind", overlay.value(QStringLiteral("kind")).toString());
        setAlignment(Qt::AlignCenter);
        setScaledContents(true);
        setCursor(overlay.value(QStringLiteral("draggable")).toBool(false) ? Qt::OpenHandCursor : Qt::ArrowCursor);

        frames_ = resolvedPetOverlayFrames(overlay);
        if (frames_.isEmpty()) {
            setText(firstContributionText(overlay, {"text", "title", "label", "message"}));
        } else {
            setFrameIndex(0);
        }

        const double opacity = qBound(0.0, contributionNumber(overlay, QStringLiteral("opacity"), 1.0), 1.0);
        if (opacity < 1.0) {
            auto* effect = new QGraphicsOpacityEffect(this);
            effect->setOpacity(opacity);
            setGraphicsEffect(effect);
        }

        const int intervalMs = frameIntervalMs(overlay);
        if (frames_.size() > 1 && intervalMs > 0) {
            frameTimer_.setInterval(intervalMs);
            connect(&frameTimer_, &QTimer::timeout, this, [this]() {
                setFrameIndex((frameIndex_ + 1) % frames_.size());
            });
            frameTimer_.start();
        }
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QLabel::mousePressEvent(event);
            return;
        }
        pressed_ = true;
        dragged_ = false;
        pressGlobalPos_ = event->globalPosition().toPoint();
        dragStartTopLeft_ = geometry().topLeft();
        if (overlay_.value(QStringLiteral("draggable")).toBool(false)) {
            setCursor(Qt::ClosedHandCursor);
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!pressed_ || !overlay_.value(QStringLiteral("draggable")).toBool(false) || parentWidget() == nullptr) {
            QLabel::mouseMoveEvent(event);
            return;
        }
        const QPoint delta = event->globalPosition().toPoint() - pressGlobalPos_;
        if (delta.manhattanLength() > 3) {
            dragged_ = true;
        }
        const QRect parentRect = parentWidget()->contentsRect();
        const QPoint next(
            qBound(parentRect.left(), dragStartTopLeft_.x() + delta.x(), qMax(parentRect.left(), parentRect.right() - width() + 1)),
            qBound(parentRect.top(), dragStartTopLeft_.y() + delta.y(), qMax(parentRect.top(), parentRect.bottom() - height() + 1)));
        move(next);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !pressed_) {
            QLabel::mouseReleaseEvent(event);
            return;
        }
        pressed_ = false;
        setCursor(overlay_.value(QStringLiteral("draggable")).toBool(false) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        const QString command = dragged_
            ? overlay_.value(QStringLiteral("onDragEndCommand")).toString()
            : overlay_.value(QStringLiteral("onClickCommand")).toString(
                  overlay_.value(QStringLiteral("command")).toString());
        if (!command.trimmed().isEmpty() && runCommand_) {
            runCommand_(command);
        }
        event->accept();
    }

private:
    static int frameIntervalMs(const QJsonObject& overlay)
    {
        const QJsonObject sprite = overlay.value(QStringLiteral("sprite")).toObject();
        const double fps = contributionNumber(sprite, QStringLiteral("fps"), contributionNumber(overlay, QStringLiteral("fps"), 0.0));
        if (fps > 0.0) {
            return qBound(16, qRound(1000.0 / fps), 5000);
        }
        return qBound(16,
                      static_cast<int>(contributionNumber(sprite, QStringLiteral("frameDurationMs"),
                                                          contributionNumber(overlay, QStringLiteral("frameDurationMs"), 250.0))),
                      5000);
    }

    void setFrameIndex(int index)
    {
        if (frames_.isEmpty()) {
            return;
        }
        frameIndex_ = qBound(0, index, frames_.size() - 1);
        const QPixmap pixmap(frames_.at(frameIndex_));
        if (!pixmap.isNull()) {
            setPixmap(pixmap);
        }
    }

    QJsonObject overlay_;
    QStringList frames_;
    int frameIndex_ = 0;
    QTimer frameTimer_;
    bool pressed_ = false;
    bool dragged_ = false;
    QPoint pressGlobalPos_;
    QPoint dragStartTopLeft_;
    std::function<void(const QString&)> runCommand_;
};

QJsonArray firstContributionArray(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    for (const char* key : keys) {
        const QJsonValue value = object.value(QString::fromLatin1(key));
        if (value.isArray()) {
            return value.toArray();
        }
    }
    return {};
}

QString extensionOverlayStyleSheet()
{
    const UiTheme::Colors& colors = UiTheme::colors();
    QColor bg = colors.cardBg;
    bg.setAlpha(colors.dark ? 226 : 236);
    return QStringLiteral(
        "QLabel#ExtensionPreviewOverlayLabel {"
        "background:%1;"
        "color:%2;"
        "border:1px solid %3;"
        "border-radius:6px;"
        "padding:6px 8px;"
        "}")
        .arg(bg.name(QColor::HexArgb),
             colors.textPrimary.name(QColor::HexRgb),
             colors.border.name(QColor::HexRgb));
}

QLabel* makeExtensionTextLabel(const QString& text, QWidget* parent, bool heading = false)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    if (heading) {
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
    }
    return label;
}

class ExtensionCanvasWidget final : public QWidget {
public:
    ExtensionCanvasWidget(
        QJsonObject spec,
        QWidget* parent,
        std::function<void(const QString&)> runCommand)
        : QWidget(parent)
        , spec_(std::move(spec))
        , runCommand_(std::move(runCommand))
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setMouseTracking(true);
        setMinimumSize(
            qBound(120, spec_.value(QStringLiteral("width")).toInt(320), 1600),
            qBound(90, spec_.value(QStringLiteral("height")).toInt(220), 1200));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor background = colorFromJsonValue(spec_.value(QStringLiteral("background")), Qt::transparent);
        if (background.isValid() && background.alpha() > 0) {
            painter.fillRect(rect(), background);
        }
        const QJsonArray nodes = spec_.value(QStringLiteral("nodes")).toArray(
            spec_.value(QStringLiteral("shapes")).toArray(spec_.value(QStringLiteral("items")).toArray()));
        for (const QJsonValue& value : nodes) {
            const QJsonObject node = value.toObject();
            const QString type = node.value(QStringLiteral("type")).toString(QStringLiteral("rect")).trimmed().toLower();
            const QRectF r(
                contributionNumber(node, QStringLiteral("x"), 0.0),
                contributionNumber(node, QStringLiteral("y"), 0.0),
                contributionNumber(node, QStringLiteral("width"), contributionNumber(node, QStringLiteral("w"), 80.0)),
                contributionNumber(node, QStringLiteral("height"), contributionNumber(node, QStringLiteral("h"), 40.0)));
            const QColor fill = colorFromJsonValue(node.value(QStringLiteral("fill")), QColor(66, 153, 225, 96));
            const QColor stroke = colorFromJsonValue(node.value(QStringLiteral("stroke")), QColor(66, 153, 225, 220));
            painter.setPen(QPen(stroke, qMax(0.0, contributionNumber(node, QStringLiteral("strokeWidth"), 1.0))));
            painter.setBrush(fill);
            if (type == QStringLiteral("ellipse") || type == QStringLiteral("circle")) {
                painter.drawEllipse(r);
            } else if (type == QStringLiteral("line")) {
                painter.drawLine(
                    QPointF(contributionNumber(node, QStringLiteral("x1"), r.left()), contributionNumber(node, QStringLiteral("y1"), r.top())),
                    QPointF(contributionNumber(node, QStringLiteral("x2"), r.right()), contributionNumber(node, QStringLiteral("y2"), r.bottom())));
            } else if (type == QStringLiteral("text")) {
                painter.setPen(stroke);
                painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, firstContributionText(node, {"text", "label", "title"}));
            } else if (type == QStringLiteral("image")) {
                const QString path = node.value(QStringLiteral("resolvedPath")).toString(node.value(QStringLiteral("src")).toString());
                const QPixmap pixmap(path);
                if (!pixmap.isNull()) {
                    painter.drawPixmap(r.toRect(), pixmap);
                }
            } else {
                painter.drawRoundedRect(r, contributionNumber(node, QStringLiteral("radius"), 4.0), contributionNumber(node, QStringLiteral("radius"), 4.0));
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        const QString command = spec_.value(QStringLiteral("command")).toString(
            spec_.value(QStringLiteral("onClickCommand")).toString()).trimmed();
        if (!command.isEmpty() && runCommand_) {
            runCommand_(command);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    QJsonObject spec_;
    std::function<void(const QString&)> runCommand_;
};

QWidget* buildExtensionDeclarativeWidget(
    const QJsonObject& contribution,
    QWidget* parent,
    const std::function<void(const QString&)>& runCommand)
{
    QJsonObject spec = contribution.value(QStringLiteral("view")).toObject();
    if (spec.isEmpty()) {
        spec = contribution.value(QStringLiteral("content")).toObject();
    }
    if (spec.isEmpty()) {
        spec = contribution;
    }

    const QString type = spec.value(QStringLiteral("type")).toString(
        contribution.value(QStringLiteral("type")).toString()).trimmed().toLower();
    const QString html = spec.value(QStringLiteral("html")).toString(
        contribution.value(QStringLiteral("html")).toString()).trimmed();
    if (!html.isEmpty() || type == QStringLiteral("webview") || type == QStringLiteral("html")) {
        auto* browser = new QTextBrowser(parent);
        browser->setOpenExternalLinks(false);
        browser->setHtml(html.isEmpty() ? firstContributionText(spec, {"text", "body", "markdown"}).toHtmlEscaped() : html);
        browser->setMinimumSize(
            qBound(220, spec.value(QStringLiteral("width")).toInt(420), 1200),
            qBound(160, spec.value(QStringLiteral("height")).toInt(320), 900));
        return browser;
    }
    if (type == QStringLiteral("canvas") || type == QStringLiteral("scene")) {
        return new ExtensionCanvasWidget(spec, parent, runCommand);
    }

    auto* scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    const QString title = contributionTitle(spec, contributionTitle(contribution));
    if (!title.isEmpty()) {
        layout->addWidget(makeExtensionTextLabel(title, body, true));
    }

    const QString summary = firstContributionText(spec, {"description", "text", "markdown", "body"});
    if (!summary.isEmpty()) {
        layout->addWidget(makeExtensionTextLabel(summary, body));
    }

    const QJsonArray items = firstContributionArray(spec, {"items", "children"});
    for (const QJsonValue& value : items) {
        if (value.isString()) {
            layout->addWidget(makeExtensionTextLabel(QStringLiteral("- %1").arg(value.toString()), body));
            continue;
        }
        const QJsonObject item = value.toObject();
        const QString itemTitle = contributionTitle(item, QString());
        const QString itemText = firstContributionText(item, {"description", "text", "body"});
        if (!itemTitle.isEmpty()) {
            layout->addWidget(makeExtensionTextLabel(itemTitle, body, true));
        }
        if (!itemText.isEmpty()) {
            layout->addWidget(makeExtensionTextLabel(itemText, body));
        }
    }

    QJsonArray actions = firstContributionArray(spec, {"actions", "buttons"});
    const QString rootCommand = spec.value(QStringLiteral("command")).toString(
        contribution.value(QStringLiteral("command")).toString()).trimmed();
    if (!rootCommand.isEmpty()) {
        actions.append(QJsonObject{
            {QStringLiteral("title"), contributionTitle(spec, rootCommand)},
            {QStringLiteral("command"), rootCommand},
        });
    }
    if (!actions.isEmpty()) {
        auto* row = new QWidget(body);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        for (const QJsonValue& actionValue : actions) {
            const QJsonObject actionObject = actionValue.toObject();
            const QString command = actionObject.value(QStringLiteral("command")).toString().trimmed();
            const QString buttonText = contributionTitle(actionObject, command.isEmpty() ? QStringLiteral("Action") : command);
            auto* button = new QPushButton(buttonText, row);
            button->setEnabled(!command.isEmpty());
            QObject::connect(button, &QPushButton::clicked, row, [runCommand, command]() {
                if (!command.isEmpty()) {
                    runCommand(command);
                }
            });
            rowLayout->addWidget(button, 0);
        }
        rowLayout->addStretch(1);
        layout->addWidget(row);
    }

    layout->addStretch(1);
    scroll->setWidget(body);
    return scroll;
}


}  // namespace

QJsonObject MainWindow::handleExtensionHostRequest(const QString& method, const QJsonObject& params)
{
        const auto okValue = [](const QJsonValue& value) {
            return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("value"), value}};
        };
        const auto errorObject = [](const QString& error) {
            return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
        };
        const auto runExtensionCommand = [this](const QString& command) {
            if (extensionManager_ == nullptr) {
                return;
            }
            QString error;
            if (!extensionManager_->executeExtensionCommand(command, &error) && statusBar() != nullptr) {
                statusBar()->showMessage(error, 5000);
            }
        };
        const auto clearExtensionShortcuts = [this]() {
            for (const QPointer<QShortcut>& shortcut : std::as_const(state_.extensionShortcutObjects_)) {
                if (shortcut != nullptr) {
                    shortcut->deleteLater();
                }
            }
            state_.extensionShortcutObjects_.clear();
        };
        const auto rebuildExtensionShortcuts = [this, runExtensionCommand, clearExtensionShortcuts]() {
            clearExtensionShortcuts();
            for (auto it = state_.extensionShortcutContributions_.constBegin();
                 it != state_.extensionShortcutContributions_.constEnd();
                 ++it) {
                const QJsonObject contribution = it.value().toObject();
                const QString shortcutId = contribution.value(QStringLiteral("shortcutId")).toString(it.key());
                const QString command = contribution.value(QStringLiteral("command")).toString();
                const QList<QKeySequence> defaults{
                    QKeySequence(contribution.value(QStringLiteral("keybinding")).toString(
                        contribution.value(QStringLiteral("keys")).toString()))
                };
                const QString label = contribution.value(QStringLiteral("label")).toString(command);
                ShortcutRegistry::instance().registerExtensionShortcut(shortcutId, label, defaults);
                const QKeySequence sequence = ShortcutRegistry::instance().sequence(shortcutId, defaults.value(0));
                if (command.trimmed().isEmpty() || sequence.isEmpty()) {
                    continue;
                }
                auto* shortcut = new QShortcut(sequence, this);
                shortcut->setContext(Qt::ApplicationShortcut);
                QObject::connect(shortcut, &QShortcut::activated, this, [runExtensionCommand, command]() {
                    runExtensionCommand(command);
                });
                state_.extensionShortcutObjects_.append(shortcut);
            }
        };
        const auto clearRenderedExtensionUi = [this]() {
            for (const QPointer<QWidget>& widget : std::as_const(state_.extensionRenderedUiWidgets_)) {
                if (widget == nullptr) {
                    continue;
                }
                if (bottomTabs_ != nullptr) {
                    const int index = bottomTabs_->indexOf(widget);
                    if (index >= 0) {
                        bottomTabs_->removeTab(index);
                    }
                }
                if (quickShellBottomTabsProxy_ != nullptr) {
                    const int index = quickShellBottomTabsProxy_->indexOf(widget);
                    if (index >= 0) {
                        quickShellBottomTabsProxy_->removeTab(index);
                    }
                }
                widget->deleteLater();
            }
            state_.extensionRenderedUiWidgets_.clear();
            for (const QPointer<QAction>& action : std::as_const(state_.extensionToolbarActions_)) {
                if (action == nullptr) {
                    continue;
                }
                if (toolboxMenu_ != nullptr) {
                    toolboxMenu_->removeAction(action);
                }
                action->deleteLater();
            }
            state_.extensionToolbarActions_.clear();
        };
        const auto clearPreviewOverlayWidgets = [this]() {
            for (const QPointer<QWidget>& widget : std::as_const(state_.extensionPreviewOverlayWidgets_)) {
                if (widget != nullptr) {
                    widget->deleteLater();
                }
            }
            state_.extensionPreviewOverlayWidgets_.clear();
        };
        const auto renderPreviewOverlays = [this, clearPreviewOverlayWidgets, runExtensionCommand]() {
            clearPreviewOverlayWidgets();
            if (previewCanvasFrame_ == nullptr || state_.extensionPreviewOverlays_.isEmpty()) {
                return;
            }
            QWidget* textHost = nullptr;
            QVBoxLayout* textLayout = nullptr;
            const QString overlayStyle = extensionOverlayStyleSheet();
            for (const QJsonValue& value : state_.extensionPreviewOverlays_) {
                const QJsonObject overlay = value.toObject();
                if (overlay.value(QStringLiteral("kind")).toString() == QStringLiteral("ui/petOverlay")) {
                    const QStringList frames = resolvedPetOverlayFrames(overlay);
                    const QSize pixmapSize = frames.isEmpty() ? QSize() : QPixmap(frames.first()).size();
                    auto* pet = new ExtensionPetOverlayWidget(overlay, previewCanvasFrame_, runExtensionCommand);
                    pet->setGeometry(petOverlayGeometry(overlay, previewCanvasFrame_->contentsRect().size(), pixmapSize));
                    pet->show();
                    pet->raise();
                    state_.extensionPreviewOverlayWidgets_.append(pet);
                    continue;
                }
                if (overlay.value(QStringLiteral("kind")).toString() == QStringLiteral("ui/sceneOverlay")
                    || overlay.value(QStringLiteral("kind")).toString() == QStringLiteral("ui/canvasOverlay")) {
                    QJsonObject spec = overlay.value(QStringLiteral("view")).toObject(overlay);
                    if (!spec.contains(QStringLiteral("type"))) {
                        spec.insert(QStringLiteral("type"), QStringLiteral("scene"));
                    }
                    auto* scene = new ExtensionCanvasWidget(spec, previewCanvasFrame_, runExtensionCommand);
                    const QSize hostSize = previewCanvasFrame_->contentsRect().size();
                    const int width = qBound(80, overlay.value(QStringLiteral("width")).toInt(hostSize.width()), qMax(80, hostSize.width()));
                    const int height = qBound(60, overlay.value(QStringLiteral("height")).toInt(hostSize.height()), qMax(60, hostSize.height()));
                    const int x = qBound(0, overlay.value(QStringLiteral("x")).toInt(0), qMax(0, hostSize.width() - width));
                    const int y = qBound(0, overlay.value(QStringLiteral("y")).toInt(0), qMax(0, hostSize.height() - height));
                    scene->setGeometry(x, y, width, height);
                    scene->show();
                    scene->raise();
                    state_.extensionPreviewOverlayWidgets_.append(scene);
                    continue;
                }
                const QString text = firstContributionText(overlay, {"text", "title", "label", "message"});
                if (text.isEmpty()) {
                    continue;
                }
                if (textHost == nullptr) {
                    textHost = new QWidget(previewCanvasFrame_);
                    textHost->setObjectName(QStringLiteral("ExtensionPreviewOverlayLayer"));
                    textHost->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                    textHost->setGeometry(previewCanvasFrame_->contentsRect());
                    textLayout = new QVBoxLayout(textHost);
                    textLayout->setContentsMargins(10, 10, 10, 10);
                    textLayout->setSpacing(6);
                    textLayout->setAlignment(Qt::AlignTop | Qt::AlignRight);
                }
                auto* label = new QLabel(text, textHost);
                label->setObjectName(QStringLiteral("ExtensionPreviewOverlayLabel"));
                label->setProperty("miacode.extension.overlay.id", overlay.value(QStringLiteral("id")).toString());
                label->setProperty("miacode.extension.overlay.ownerId", overlay.value(QStringLiteral("ownerId")).toString());
                label->setWordWrap(true);
                label->setMaximumWidth(qMax(180, previewCanvasFrame_->width() / 2));
                label->setStyleSheet(overlayStyle);
                textLayout->addWidget(label, 0, Qt::AlignRight);
            }
            if (textHost != nullptr && textLayout != nullptr) {
                textLayout->addStretch(1);
                textHost->show();
                textHost->raise();
                state_.extensionPreviewOverlayWidgets_.append(textHost);
            }
        };
        const auto renderUiContribution = [this, runExtensionCommand](const QJsonObject& contribution) {
            const QString kind = contribution.value(QStringLiteral("kind")).toString();
            const QString title = contributionTitle(contribution, QStringLiteral("Extension"));
            const QString id = contribution.value(QStringLiteral("id")).toString();
            const QString ownerId = contribution.value(QStringLiteral("ownerId")).toString();
            if (kind == QStringLiteral("ui/toolbarButton")) {
                if (toolboxMenu_ == nullptr) {
                    return false;
                }
                const QString command = contribution.value(QStringLiteral("command")).toString().trimmed();
                QAction* action = toolboxMenu_->addAction(title);
                action->setProperty("miacode.extension.kind", kind);
                action->setProperty("miacode.extension.id", id);
                action->setProperty("miacode.extension.ownerId", ownerId);
                action->setProperty("miacode.extension.title", title);
                action->setProperty("miacode.extension.command", command);
                action->setEnabled(!command.isEmpty());
                QObject::connect(action, &QAction::triggered, toolboxMenu_, [runExtensionCommand, command]() {
                    if (!command.isEmpty()) {
                        runExtensionCommand(command);
                    }
                });
                state_.extensionToolbarActions_.append(action);
                return true;
            }
            if (kind == QStringLiteral("ui/sidebarView")) {
                auto* dock = new QDockWidget(title, this);
                dock->setObjectName(QStringLiteral("ExtensionSidebar_%1").arg(id));
                dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
                dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
                QWidget* body = buildExtensionDeclarativeWidget(contribution, dock, runExtensionCommand);
                dock->setWidget(body);
                dock->setProperty("miacode.extension.kind", kind);
                dock->setProperty("miacode.extension.id", id);
                dock->setProperty("miacode.extension.ownerId", ownerId);
                dock->setProperty("miacode.extension.title", title);
                addDockWidget(Qt::RightDockWidgetArea, dock);
                dock->show();
                state_.extensionRenderedUiWidgets_.append(dock);
                return true;
            }
            if (kind == QStringLiteral("ui/preferencesPage")) {
                auto* dialog = new QDialog(this);
                dialog->setAttribute(Qt::WA_DeleteOnClose, false);
                dialog->setWindowTitle(QStringLiteral("%1 - %2").arg(UiText::text(QStringLiteral("action.preferences")), title));
                dialog->setProperty("miacode.extension.kind", kind);
                dialog->setProperty("miacode.extension.id", id);
                dialog->setProperty("miacode.extension.ownerId", ownerId);
                dialog->setProperty("miacode.extension.title", title);
                auto* layout = new QVBoxLayout(dialog);
                layout->setContentsMargins(12, 12, 12, 12);
                layout->setSpacing(8);
                layout->addWidget(buildExtensionDeclarativeWidget(contribution, dialog, runExtensionCommand));
                dialog->resize(
                    qBound(320, contribution.value(QStringLiteral("width")).toInt(520), 1000),
                    qBound(240, contribution.value(QStringLiteral("height")).toInt(420), 800));
                dialog->show();
                dialog->raise();
                state_.extensionRenderedUiWidgets_.append(dialog);
                return true;
            }
            if (kind == QStringLiteral("ui/floatingPanel")) {
                auto* dialog = new QDialog(this);
                dialog->setAttribute(Qt::WA_DeleteOnClose, false);
                dialog->setWindowTitle(title);
                dialog->setProperty("miacode.extension.kind", kind);
                dialog->setProperty("miacode.extension.id", id);
                dialog->setProperty("miacode.extension.ownerId", ownerId);
                dialog->setProperty("miacode.extension.title", title);
                auto* layout = new QVBoxLayout(dialog);
                layout->setContentsMargins(12, 12, 12, 12);
                layout->setSpacing(8);
                QWidget* body = buildExtensionDeclarativeWidget(contribution, dialog, runExtensionCommand);
                layout->addWidget(body);
                dialog->resize(
                    qBound(260, contribution.value(QStringLiteral("width")).toInt(420), 900),
                    qBound(180, contribution.value(QStringLiteral("height")).toInt(320), 700));
                dialog->show();
                dialog->raise();
                state_.extensionRenderedUiWidgets_.append(dialog);
                return true;
            }
            if (!kind.startsWith(QStringLiteral("ui/")) || bottomTabs_ == nullptr) {
                return false;
            }
            QWidget* view = buildExtensionDeclarativeWidget(contribution, bottomTabs_, runExtensionCommand);
            view->setProperty("miacode.extension.kind", kind);
            view->setProperty("miacode.extension.id", id);
            view->setProperty("miacode.extension.ownerId", ownerId);
            view->setProperty("miacode.extension.title", title);
            const int index = bottomTabs_->addTab(view, title);
            bottomTabs_->setTabToolTip(index, QStringLiteral("%1\n%2").arg(title, ownerId));
            state_.extensionRenderedUiWidgets_.append(view);
            return true;
        };
        const auto rebuildRenderedExtensionUi = [this, clearRenderedExtensionUi, renderUiContribution]() {
            clearRenderedExtensionUi();
            for (const QJsonValue& value : state_.extensionUiContributions_) {
                renderUiContribution(value.toObject());
            }
        };
        const auto renderedViewsArray = [this]() {
            QJsonArray views;
            for (const QPointer<QWidget>& widget : std::as_const(state_.extensionRenderedUiWidgets_)) {
                if (widget == nullptr) {
                    continue;
                }
                views.append(QJsonObject{
                    {QStringLiteral("id"), widget->property("miacode.extension.id").toString()},
                    {QStringLiteral("ownerId"), widget->property("miacode.extension.ownerId").toString()},
                    {QStringLiteral("kind"), widget->property("miacode.extension.kind").toString()},
                    {QStringLiteral("hostSlot"), widget->property("miacode.extension.kind").toString().section(QLatin1Char('/'), 1, 1)},
                    {QStringLiteral("title"), widget->property("miacode.extension.title").toString()},
                    {QStringLiteral("visible"), widget->isVisible()},
                });
            }
            for (const QPointer<QAction>& action : std::as_const(state_.extensionToolbarActions_)) {
                if (action == nullptr) {
                    continue;
                }
                views.append(QJsonObject{
                    {QStringLiteral("id"), action->property("miacode.extension.id").toString()},
                    {QStringLiteral("ownerId"), action->property("miacode.extension.ownerId").toString()},
                    {QStringLiteral("kind"), action->property("miacode.extension.kind").toString()},
                    {QStringLiteral("hostSlot"), action->property("miacode.extension.kind").toString().section(QLatin1Char('/'), 1, 1)},
                    {QStringLiteral("title"), action->property("miacode.extension.title").toString()},
                    {QStringLiteral("command"), action->property("miacode.extension.command").toString()},
                    {QStringLiteral("visible"), action->isVisible()},
                });
            }
            return views;
        };
        const auto unregisterExtensionView = [this, clearRenderedExtensionUi, renderUiContribution](const QString& id, const QString& ownerId) {
            QJsonArray kept;
            int removed = 0;
            for (const QJsonValue& value : state_.extensionUiContributions_) {
                const QJsonObject contribution = value.toObject();
                const bool idMatches = id.isEmpty() || contribution.value(QStringLiteral("id")).toString() == id;
                const bool ownerMatches = ownerId.isEmpty() || contribution.value(QStringLiteral("ownerId")).toString() == ownerId;
                if (idMatches && ownerMatches) {
                    ++removed;
                    continue;
                }
                kept.append(contribution);
            }
            state_.extensionUiContributions_ = kept;
            clearRenderedExtensionUi();
            for (const QJsonValue& value : state_.extensionUiContributions_) {
                renderUiContribution(value.toObject());
            }
            return removed;
        };
        const auto collectProviders = [this](const QString& kind, QJsonObject context) {
            if (!context.contains(QStringLiteral("token")) && !context.contains(QStringLiteral("text"))) {
                auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
                if (editor != nullptr) {
                    const QTextCursor cursor = editor->textCursor();
                    const QString lineText = cursor.block().text();
                    int start = qBound(0, cursor.positionInBlock(), lineText.size());
                    int end = start;
                    const auto isTokenChar = [](QChar ch) {
                        return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('#') || ch == QLatin1Char('&');
                    };
                    while (start > 0 && isTokenChar(lineText.at(start - 1))) {
                        --start;
                    }
                    while (end < lineText.size() && isTokenChar(lineText.at(end))) {
                        ++end;
                    }
                    context.insert(QStringLiteral("token"), lineText.mid(start, end - start));
                    context.insert(QStringLiteral("line"), cursor.blockNumber() + 1);
                    context.insert(QStringLiteral("startCol"), start + 1);
                    context.insert(QStringLiteral("endCol"), end + 1);
                }
            }

            QJsonArray items;
            const QJsonArray providers = state_.extensionRegistrationsByKind_.value(kind);
            for (const QJsonValue& value : providers) {
                QJsonObject provider = value.toObject();
                if (!providerMatchesContext(provider, context)) {
                    continue;
                }
                provider.insert(QStringLiteral("providerKind"), kind);
                provider.insert(QStringLiteral("displayText"), providerDisplayText(provider));
                items.append(provider);
            }
            return QJsonObject{
                {QStringLiteral("kind"), kind},
                {QStringLiteral("context"), context},
                {QStringLiteral("items"), items},
            };
        };
        const auto addExtensionDiagnosticToPanel = [this](const ExtensionDiagnosticEntry& diagnostic) {
            const QString normalizedSeverity = diagnostic.severity.trimmed().toLower();
            const bool warning = normalizedSeverity == QStringLiteral("warning") || normalizedSeverity == QStringLiteral("warn");
            const QString prefix = warning ? QStringLiteral("[WARNING] ") : QStringLiteral("[ERROR] ");
            const QString message = diagnostic.message.trimmed().isEmpty()
                ? QStringLiteral("Extension diagnostic")
                : diagnostic.message.trimmed();
            addValidationError(
                diagnostic.line,
                diagnostic.col,
                message,
                prefix + message,
                QStringLiteral("extension.%1").arg(diagnostic.ownerId),
                diagnostic.source.trimmed().isEmpty() ? QStringLiteral("Extension") : diagnostic.source,
                false);
            addValidationDecoration(diagnostic.line, diagnostic.col, prefix + message, diagnostic.endCol);
        };
        const auto replayExtensionDiagnostics = [this, addExtensionDiagnosticToPanel]() {
            for (const QVector<ExtensionDiagnosticEntry>& entries : std::as_const(state_.extensionDiagnosticsByOwner_)) {
                for (const ExtensionDiagnosticEntry& diagnostic : entries) {
                    addExtensionDiagnosticToPanel(diagnostic);
                }
            }
            refreshEditorExtraSelections();
            updateEditorValidationSummary();
        };
        const auto activeDifficultyJson = [this]() {
            const SimaiDifficultyData* difficulty = document_.difficulty(activeDifficultyId_);
            return QJsonObject{
                {QStringLiteral("id"), activeDifficultyId_},
                {QStringLiteral("name"), SimaiDocument::difficultyName(activeDifficultyId_)},
                {QStringLiteral("shortName"), SimaiDocument::difficultyShortName(activeDifficultyId_)},
                {QStringLiteral("level"), difficulty != nullptr ? difficulty->level : QString()},
                {QStringLiteral("designer"), difficulty != nullptr ? difficulty->designer : QString()},
                {QStringLiteral("text"), hasActiveDifficulty() ? editorText() : QString()},
            };
        };
        const auto metadataJson = [this]() {
            return QJsonObject{
                {QStringLiteral("path"), currentFilePath_},
                {QStringLiteral("chartFolder"), currentFilePath_.isEmpty() ? QString() : QFileInfo(currentFilePath_).absolutePath()},
                {QStringLiteral("title"), titleEdit_ != nullptr ? titleEdit_->text() : QString()},
                {QStringLiteral("artist"), artistEdit_ != nullptr ? artistEdit_->text() : QString()},
                {QStringLiteral("designer"), designerEdit_ != nullptr ? designerEdit_->text() : QString()},
                {QStringLiteral("first"), firstEdit_ != nullptr ? firstEdit_->text() : QString()},
                {QStringLiteral("activeDifficultyId"), activeDifficultyId_},
                {QStringLiteral("dirty"), documentDirty_ || currentFieldDirty_},
            };
        };
        const auto difficultiesJson = [this]() {
            QJsonArray difficulties;
            for (int id : document_.difficultyIds()) {
                const SimaiDifficultyData* difficulty = document_.difficulty(id);
                if (difficulty == nullptr) {
                    continue;
                }
                difficulties.append(QJsonObject{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("name"), SimaiDocument::difficultyName(id)},
                    {QStringLiteral("shortName"), SimaiDocument::difficultyShortName(id)},
                    {QStringLiteral("level"), difficulty->level},
                    {QStringLiteral("designer"), difficulty->designer},
                    {QStringLiteral("active"), id == activeDifficultyId_},
                });
            }
            return difficulties;
        };
        const auto parsedMarkersJson = [this]() {
            QJsonArray markers;
            for (const TimelineNoteMarker& marker : std::as_const(state_.latestTimelineNoteMarkers_)) {
                markers.append(timelineNoteMarkerToJson(marker));
            }
            return markers;
        };
        const auto internalCommandsJson = []() {
            const auto item = [](const QString& id, const QString& title, const QString& permission, const QString& risk = QStringLiteral("medium")) {
                return QJsonObject{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("permission"), permission},
                    {QStringLiteral("risk"), risk},
                    {QStringLiteral("implemented"), true},
                };
            };
            return QJsonArray{
                item(QStringLiteral("app.openPreferences"), QStringLiteral("Open Preferences"), QStringLiteral("ui.prompt"), QStringLiteral("low")),
                item(QStringLiteral("app.openAboutDialog"), QStringLiteral("Open About Dialog"), QStringLiteral("ui.prompt"), QStringLiteral("low")),
                item(QStringLiteral("workspace.save"), QStringLiteral("Save current chart"), QStringLiteral("workspace.write"), QStringLiteral("high")),
                item(QStringLiteral("workspace.saveAs"), QStringLiteral("Save current chart as path"), QStringLiteral("workspace.write"), QStringLiteral("high")),
                item(QStringLiteral("document.setActiveDifficulty"), QStringLiteral("Set active difficulty"), QStringLiteral("document.edit")),
                item(QStringLiteral("document.edit"), QStringLiteral("Apply document edit operations"), QStringLiteral("document.edit")),
                item(QStringLiteral("document.formatActiveDifficulty"), QStringLiteral("Format active difficulty"), QStringLiteral("document.edit")),
                item(QStringLiteral("editor.undo"), QStringLiteral("Editor undo"), QStringLiteral("editor.edit")),
                item(QStringLiteral("editor.redo"), QStringLiteral("Editor redo"), QStringLiteral("editor.edit")),
                item(QStringLiteral("editor.cut"), QStringLiteral("Editor cut"), QStringLiteral("editor.edit")),
                item(QStringLiteral("editor.copy"), QStringLiteral("Editor copy"), QStringLiteral("editor.read"), QStringLiteral("low")),
                item(QStringLiteral("editor.paste"), QStringLiteral("Editor paste"), QStringLiteral("editor.edit")),
                item(QStringLiteral("editor.selectAll"), QStringLiteral("Editor select all"), QStringLiteral("editor.edit"), QStringLiteral("low")),
                item(QStringLiteral("preview.play"), QStringLiteral("Play preview"), QStringLiteral("preview.control")),
                item(QStringLiteral("preview.pause"), QStringLiteral("Pause preview"), QStringLiteral("preview.control")),
                item(QStringLiteral("preview.stop"), QStringLiteral("Stop preview"), QStringLiteral("preview.control")),
                item(QStringLiteral("preview.seek"), QStringLiteral("Seek preview"), QStringLiteral("preview.control")),
                item(QStringLiteral("preview.setSpeed"), QStringLiteral("Set preview speed"), QStringLiteral("preview.control")),
                item(QStringLiteral("timeline.seek"), QStringLiteral("Seek timeline"), QStringLiteral("timeline.control")),
                item(QStringLiteral("timeline.zoomIn"), QStringLiteral("Zoom timeline in"), QStringLiteral("timeline.control")),
                item(QStringLiteral("timeline.zoomOut"), QStringLiteral("Zoom timeline out"), QStringLiteral("timeline.control")),
                item(QStringLiteral("timeline.stepZoomPreset"), QStringLiteral("Step timeline zoom preset"), QStringLiteral("timeline.control")),
                item(QStringLiteral("timeline.setZoomScale"), QStringLiteral("Set timeline zoom scale"), QStringLiteral("timeline.control")),
                item(QStringLiteral("timeline.setFollowPreview"), QStringLiteral("Set timeline follow-preview"), QStringLiteral("timeline.control")),
                item(QStringLiteral("timeline.setFollowProgress"), QStringLiteral("Set timeline follow-progress"), QStringLiteral("timeline.control")),
                item(QStringLiteral("validation.run"), QStringLiteral("Run validation"), QStringLiteral("diagnostics.run")),
                item(QStringLiteral("analysis.runMuriAnalysis"), QStringLiteral("Run Muri analysis"), QStringLiteral("analysis.run")),
                item(QStringLiteral("export.video.start"), QStringLiteral("Start video export"), QStringLiteral("export.write"), QStringLiteral("high")),
                item(QStringLiteral("export.cover.start"), QStringLiteral("Start cover export"), QStringLiteral("export.write"), QStringLiteral("high")),
                item(QStringLiteral("extensions.all"), QStringLiteral("List extensions"), QStringLiteral("extensions.manage"), QStringLiteral("high")),
                item(QStringLiteral("extensions.reload"), QStringLiteral("Reload extensions"), QStringLiteral("extensions.manage"), QStringLiteral("high")),
            };
        };
        const auto documentQuery = [this, okValue, metadataJson, activeDifficultyJson, difficultiesJson, parsedMarkersJson](const QJsonObject& query) {
            QSet<QString> select;
            for (const QJsonValue& value : query.value(QStringLiteral("select")).toArray()) {
                const QString key = value.toString().trimmed();
                if (!key.isEmpty()) {
                    select.insert(key);
                }
            }
            const bool all = select.isEmpty() || select.contains(QStringLiteral("*"));
            QJsonObject result;
            if (all || select.contains(QStringLiteral("metadata"))) {
                result.insert(QStringLiteral("metadata"), metadataJson());
            }
            if (all || select.contains(QStringLiteral("activeDifficulty"))) {
                result.insert(QStringLiteral("activeDifficulty"), activeDifficultyJson());
            }
            if (all || select.contains(QStringLiteral("difficulties"))) {
                result.insert(QStringLiteral("difficulties"), difficultiesJson());
            }
            if (all || select.contains(QStringLiteral("text"))) {
                result.insert(QStringLiteral("text"), hasActiveDifficulty() ? editorText() : QString());
            }
            if (all || select.contains(QStringLiteral("lines"))) {
                result.insert(QStringLiteral("lines"), QJsonArray::fromStringList(hasActiveDifficulty() ? editorText().split(QLatin1Char('\n')) : QStringList{}));
            }
            if (all || select.contains(QStringLiteral("notes")) || select.contains(QStringLiteral("markers"))) {
                result.insert(QStringLiteral("notes"), parsedMarkersJson());
            }
            if (all || select.contains(QStringLiteral("timeline"))) {
                result.insert(QStringLiteral("timeline"), QJsonObject{
                    {QStringLiteral("currentSecond"), qtPreviewPauseSecond_},
                    {QStringLiteral("noteMarkerCount"), state_.latestTimelineNoteMarkers_.size()},
                    {QStringLiteral("noteMarkerSignature"), QString::fromLatin1(state_.latestTimelineNoteMarkerSignature_.toHex())},
                });
            }
            return okValue(result);
        };
        const auto documentEdit = [this, okValue, errorObject](const QJsonObject& request) {
            if (!hasActiveDifficulty()) {
                return errorObject(QStringLiteral("No active difficulty."));
            }
            QJsonArray ops = request.value(QStringLiteral("ops")).toArray(request.value(QStringLiteral("operations")).toArray());
            if (ops.isEmpty() && request.contains(QStringLiteral("op"))) {
                ops.append(request);
            }
            int applied = 0;
            for (const QJsonValue& value : ops) {
                const QJsonObject op = value.toObject();
                const QString kind = op.value(QStringLiteral("op")).toString().trimmed();
                const QString path = op.value(QStringLiteral("path")).toString().trimmed();
                if (kind == QStringLiteral("replaceText") || path == QStringLiteral("/text") || path == QStringLiteral("/difficulty/text")) {
                    setEditorText(op.value(QStringLiteral("value")).toString(op.value(QStringLiteral("text")).toString()));
                    ++applied;
                } else if (kind == QStringLiteral("insertText")) {
                    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
                    if (editor == nullptr) {
                        return errorObject(QStringLiteral("No active editor."));
                    }
                    QTextCursor cursor = editor->textCursor();
                    if (op.contains(QStringLiteral("position"))) {
                        cursor.setPosition(qBound(0, op.value(QStringLiteral("position")).toInt(), editor->toPlainText().size()));
                    }
                    cursor.insertText(op.value(QStringLiteral("text")).toString(op.value(QStringLiteral("value")).toString()));
                    editor->setTextCursor(cursor);
                    ++applied;
                } else if (kind == QStringLiteral("applyTextEdits")) {
                    const QString original = editorText();
                    struct TextEdit {
                        int start = 0;
                        int end = 0;
                        QString text;
                    };
                    QVector<TextEdit> edits;
                    for (const QJsonValue& editValue : op.value(QStringLiteral("edits")).toArray()) {
                        const QJsonObject object = editValue.toObject();
                        TextEdit edit;
                        edit.start = qBound(0, object.value(QStringLiteral("start")).toInt(), original.size());
                        edit.end = qBound(edit.start, object.value(QStringLiteral("end")).toInt(edit.start), original.size());
                        edit.text = object.value(QStringLiteral("text")).toString();
                        edits.append(edit);
                    }
                    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
                        return a.start > b.start;
                    });
                    QString next = original;
                    for (const TextEdit& edit : edits) {
                        next.replace(edit.start, edit.end - edit.start, edit.text);
                    }
                    setEditorText(next);
                    applied += edits.size();
                } else if (path == QStringLiteral("/metadata/title")) {
                    if (titleEdit_ != nullptr) {
                        titleEdit_->setText(op.value(QStringLiteral("value")).toString());
                    }
                    ++applied;
                } else if (path == QStringLiteral("/metadata/artist")) {
                    if (artistEdit_ != nullptr) {
                        artistEdit_->setText(op.value(QStringLiteral("value")).toString());
                    }
                    ++applied;
                } else if (path == QStringLiteral("/metadata/first")) {
                    if (firstEdit_ != nullptr) {
                        firstEdit_->setText(op.value(QStringLiteral("value")).toString());
                    }
                    ++applied;
                } else if (path == QStringLiteral("/difficulty/level")) {
                    if (difficultyLevelEdit_ != nullptr) {
                        difficultyLevelEdit_->setText(op.value(QStringLiteral("value")).toString());
                    }
                    ++applied;
                } else if (path == QStringLiteral("/difficulty/designer")) {
                    if (difficultyDesignerEdit_ != nullptr) {
                        difficultyDesignerEdit_->setText(op.value(QStringLiteral("value")).toString());
                    }
                    ++applied;
                } else {
                    return errorObject(QStringLiteral("Unsupported document edit operation: %1 %2").arg(kind, path));
                }
            }
            if (applied > 0) {
                markCurrentFieldDirty();
                refreshTimelineMetadata();
            }
            return okValue(QJsonObject{{QStringLiteral("applied"), applied}});
        };
        const auto executeInternalCommand = [this, okValue, errorObject, documentEdit](const QString& command, const QJsonObject& args) {
            const QString id = command.trimmed();
            if (id == QStringLiteral("app.openPreferences")) {
                onPreferences();
            } else if (id == QStringLiteral("app.openAboutDialog")) {
                onAbout();
            } else if (id == QStringLiteral("workspace.save")) {
                if (currentFilePath_.isEmpty()) {
                    return errorObject(QStringLiteral("No current file path."));
                }
                return QJsonObject{{QStringLiteral("ok"), saveToPath(currentFilePath_)}};
            } else if (id == QStringLiteral("workspace.saveAs")) {
                const QString path = args.value(QStringLiteral("path")).toString();
                return path.isEmpty() ? errorObject(QStringLiteral("Missing path.")) : QJsonObject{{QStringLiteral("ok"), saveToPath(path)}};
            } else if (id == QStringLiteral("document.setActiveDifficulty")) {
                return QJsonObject{{QStringLiteral("ok"), switchToDifficultyField(args.value(QStringLiteral("id")).toInt(activeDifficultyId_))}};
            } else if (id == QStringLiteral("document.edit")) {
                return documentEdit(args);
            } else if (id == QStringLiteral("document.formatActiveDifficulty")) {
                QStringList lines = editorText().split(QLatin1Char('\n'));
                for (QString& line : lines) {
                    while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t'))) {
                        line.chop(1);
                    }
                }
                setEditorText(lines.join(QLatin1Char('\n')));
                markCurrentFieldDirty();
                refreshTimelineMetadata();
            } else if (id == QStringLiteral("editor.undo") && undoAction_ != nullptr) {
                undoAction_->trigger();
            } else if (id == QStringLiteral("editor.redo") && redoAction_ != nullptr) {
                redoAction_->trigger();
            } else if (id == QStringLiteral("editor.cut") || id == QStringLiteral("editor.copy") || id == QStringLiteral("editor.paste") || id == QStringLiteral("editor.selectAll")) {
                auto* editor = qobject_cast<QPlainTextEdit*>(editorWidget_);
                if (editor == nullptr) {
                    return errorObject(QStringLiteral("No active editor."));
                }
                if (id == QStringLiteral("editor.cut")) {
                    editor->cut();
                } else if (id == QStringLiteral("editor.copy")) {
                    editor->copy();
                } else if (id == QStringLiteral("editor.paste")) {
                    editor->paste();
                } else {
                    editor->selectAll();
                }
            } else if (id == QStringLiteral("preview.play")) {
                toggleShellPreviewPlayback();
            } else if (id == QStringLiteral("preview.pause")) {
                pauseQtPreviewPlaybackExact();
            } else if (id == QStringLiteral("preview.stop")) {
                stopQtPreviewPlayback(true);
            } else if (id == QStringLiteral("preview.seek") || id == QStringLiteral("timeline.seek")) {
                seekPreviewToSecond(args.value(QStringLiteral("second")).toDouble(qtPreviewPauseSecond_), true);
            } else if (id == QStringLiteral("preview.setSpeed")) {
                setShellPreviewRate(args.value(QStringLiteral("value")).toDouble(previewPlaybackRate_));
            } else if (id == QStringLiteral("timeline.zoomIn")) {
                return handleExtensionHostRequest(QStringLiteral("timeline/zoomIn"), args);
            } else if (id == QStringLiteral("timeline.zoomOut")) {
                return handleExtensionHostRequest(QStringLiteral("timeline/zoomOut"), args);
            } else if (id == QStringLiteral("timeline.stepZoomPreset")) {
                return handleExtensionHostRequest(QStringLiteral("timeline/stepZoomPreset"), args);
            } else if (id == QStringLiteral("timeline.setZoomScale")) {
                return handleExtensionHostRequest(QStringLiteral("timeline/setZoomScale"), args);
            } else if (id == QStringLiteral("timeline.setFollowPreview")) {
                return handleExtensionHostRequest(QStringLiteral("timeline/setFollowPreview"), args);
            } else if (id == QStringLiteral("timeline.setFollowProgress")) {
                return handleExtensionHostRequest(QStringLiteral("timeline/setFollowProgress"), args);
            } else if (id == QStringLiteral("validation.run")) {
                return QJsonObject{{QStringLiteral("ok"), runValidateSimaiSilently(false)}};
            } else if (id == QStringLiteral("analysis.runMuriAnalysis")) {
                if (state_.latestTimelineNoteMarkers_.isEmpty()) {
                    return errorObject(QStringLiteral("No parsed timeline markers are available for Muri analysis."));
                }
                state_.muriAnalysisReport_ = MuriAnalyzer::analyze(
                    state_.latestTimelineNoteMarkers_,
                    state_.muriRenderOptions_,
                    static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0);
                state_.muriAnalysisReport_.revision = ++state_.muriAnalysisReportRevisionCounter_;
                state_.muriAnalysisReportNoteMarkerSignature_ = state_.latestTimelineNoteMarkerSignature_;
                applyAlignedMuriAnalysisReportToViews();
                refreshMuriDiagnosticsPanel();
                return okValue(muriReportToJson(state_.muriAnalysisReport_));
            } else if (id == QStringLiteral("export.video.start")) {
                if (exportSection_ == nullptr) {
                    return errorObject(QStringLiteral("Export section is not available."));
                }
                exportSection_->onExportPreviewVideo(resolveToolsMenuExportDifficultyId());
            } else if (id == QStringLiteral("export.cover.start")) {
                onExportCover();
            } else if (id == QStringLiteral("net.batchUpload.open")) {
                onNetBatchUpload();
            } else if (id == QStringLiteral("extensions.all")) {
                if (extensionManager_ == nullptr) {
                    return errorObject(QStringLiteral("Extension manager is not available."));
                }
                QJsonArray array;
                for (const miacode::extensions::ExtensionRecord& record : extensionManager_->records()) {
                    array.append(QJsonObject{
                        {QStringLiteral("id"), record.valid ? record.manifest.qualifiedId() : record.sourcePath},
                        {QStringLiteral("name"), record.manifest.name},
                        {QStringLiteral("version"), record.manifest.version},
                        {QStringLiteral("enabled"), record.enabled},
                        {QStringLiteral("valid"), record.valid},
                        {QStringLiteral("permissions"), QJsonArray::fromStringList(record.manifest.permissions)},
                        {QStringLiteral("diagnostic"), record.diagnostic},
                    });
                }
                return okValue(array);
            } else if (id == QStringLiteral("extensions.reload")) {
                if (extensionManager_ == nullptr) {
                    return errorObject(QStringLiteral("Extension manager is not available."));
                }
                extensionManager_->refreshExtensions();
            } else {
                return errorObject(QStringLiteral("Unknown internal command: %1").arg(id));
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        };
        const auto objectDescription = [](const QString& objectId) {
            return miacode::extensions::extensionOpenBridgeDescribeObject(objectId);
        };
        const auto objectIds = []() {
            return miacode::extensions::extensionOpenBridgeObjectIds();
        };
        if (method == QStringLiteral("commands/executeInternal")) {
            const QJsonObject args = params.value(QStringLiteral("args")).toObject(params.value(QStringLiteral("params")).toObject());
            const QString command = params.value(QStringLiteral("command")).toString(params.value(QStringLiteral("id")).toString());
            return executeInternalCommand(command, args);
        }
        if (method == QStringLiteral("commands/getInternalCommands")) {
            return okValue(internalCommandsJson());
        }
        if (method == QStringLiteral("document/query")) {
            return documentQuery(params);
        }
        if (method == QStringLiteral("document/edit")) {
            return documentEdit(params);
        }
        if (method == QStringLiteral("objects/list")) {
            return okValue(objectIds());
        }
        if (method == QStringLiteral("objects/describe")) {
            const QString id = params.value(QStringLiteral("object")).toString(params.value(QStringLiteral("id")).toString());
            if (id.trimmed().isEmpty()) {
                return errorObject(QStringLiteral("Object id is required."));
            }
            const QJsonObject description = objectDescription(id);
            if (description.value(QStringLiteral("methods")).toArray().isEmpty()) {
                return errorObject(QStringLiteral("Unknown object proxy: %1").arg(id));
            }
            return okValue(description);
        }
        if (method == QStringLiteral("objects/inspect")) {
            const QString id = params.value(QStringLiteral("object")).toString(params.value(QStringLiteral("id")).toString());
            QJsonObject result = objectDescription(id);
            if (id == QStringLiteral("document")) {
                result.insert(QStringLiteral("state"), documentQuery(QJsonObject{{QStringLiteral("select"), QJsonArray{QStringLiteral("metadata"), QStringLiteral("activeDifficulty")}}}).value(QStringLiteral("value")));
            } else if (id == QStringLiteral("timeline")) {
                result.insert(QStringLiteral("state"), QJsonObject{{QStringLiteral("currentSecond"), qtPreviewPauseSecond_}, {QStringLiteral("markerCount"), state_.latestTimelineNoteMarkers_.size()}});
            } else if (id == QStringLiteral("preview")) {
                result.insert(QStringLiteral("state"), QJsonObject{{QStringLiteral("currentSecond"), qtPreviewPauseSecond_}, {QStringLiteral("rate"), previewPlaybackRate_}, {QStringLiteral("overlays"), state_.extensionPreviewOverlays_}});
            } else if (id == QStringLiteral("ui")) {
                result.insert(QStringLiteral("state"), QJsonObject{{QStringLiteral("views"), renderedViewsArray()}});
            } else if (id == QStringLiteral("extensions")) {
                result.insert(QStringLiteral("state"), QJsonObject{{QStringLiteral("enabledCount"), extensionManager_ != nullptr ? extensionManager_->manifests().size() : 0}});
            }
            return okValue(result);
        }
        if (method == QStringLiteral("objects/call")) {
            const QString objectId = params.value(QStringLiteral("object")).toString(params.value(QStringLiteral("id")).toString());
            const QString member = params.value(QStringLiteral("member")).toString(params.value(QStringLiteral("method")).toString());
            const QJsonObject args = params.value(QStringLiteral("args")).toObject(params.value(QStringLiteral("params")).toObject());
            const QJsonObject item = miacode::extensions::extensionOpenBridgeDescribeMethod(objectId, member);
            if (item.isEmpty()) {
                return errorObject(QStringLiteral("Unsupported object call: %1.%2").arg(objectId, member));
            }
            if (item.value(QStringLiteral("status")).toString() != QStringLiteral("implemented")) {
                return errorObject(QStringLiteral("Open object method is %1: %2.%3")
                                       .arg(item.value(QStringLiteral("status")).toString(), objectId, member));
            }
            const QString hostMethod = item.value(QStringLiteral("hostMethod")).toString();
            if (!hostMethod.isEmpty()) {
                QJsonObject forwarded = args;
                if (params.contains(QStringLiteral("extensionId"))) {
                    forwarded.insert(QStringLiteral("extensionId"), params.value(QStringLiteral("extensionId")));
                }
                return handleExtensionHostRequest(hostMethod, forwarded);
            }
            const QString command = item.value(QStringLiteral("command")).toString();
            if (!command.isEmpty()) {
                return executeInternalCommand(command, args);
            }
            return errorObject(QStringLiteral("Unsupported object call: %1.%2").arg(objectId, member));
        }
        if (method == QStringLiteral("experimental/invoke")) {
            const QString id = params.value(QStringLiteral("id")).toString(params.value(QStringLiteral("capability")).toString());
            const QJsonObject args = params.value(QStringLiteral("args")).toObject(params.value(QStringLiteral("params")).toObject(params));
            if (id == QStringLiteral("preview/internal/setOverlayLayer")) {
                if (args.value(QStringLiteral("clear")).toBool(false)) {
                    state_.extensionPreviewOverlays_ = QJsonArray();
                    renderPreviewOverlays();
                    return QJsonObject{{QStringLiteral("ok"), true}};
                }
                QJsonObject overlay = registeredContribution(args, QStringLiteral("preview/addOverlay"));
                overlay.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
                state_.extensionPreviewOverlays_.append(overlay);
                renderPreviewOverlays();
                return okValue(overlay);
            }
            if (id == QStringLiteral("ui/internal/renderView")) {
                QJsonObject contribution = registeredContribution(args, args.value(QStringLiteral("kind")).toString(QStringLiteral("ui/bottomTabView")));
                contribution.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
                if (!renderUiContribution(contribution)) {
                    return errorObject(QStringLiteral("UI host is not available for experimental view."));
                }
                state_.extensionUiContributions_.append(contribution);
                return okValue(contribution);
            }
            if (id == QStringLiteral("document/internal/editOps")) {
                return documentEdit(args);
            }
            return errorObject(QStringLiteral("Unknown experimental capability: %1. Use miacode.api.request() to report the missing capability.").arg(id));
        }
        if (method == QStringLiteral("extensions/clearRuntimeContributions")) {
            clearRenderedExtensionUi();
            clearPreviewOverlayWidgets();
            clearExtensionShortcuts();
            state_.extensionUiContributions_ = QJsonArray();
            state_.extensionPreviewOverlays_ = QJsonArray();
            state_.extensionExportHooks_ = QJsonArray();
            state_.extensionInputGestures_ = QJsonArray();
            state_.extensionShortcutContributions_ = QJsonObject();
            state_.extensionRegistrationsByKind_.clear();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("input/registerWheelGesture")) {
            QJsonObject gesture = registeredContribution(params, QStringLiteral("input/wheelGesture"));
            gesture.insert(QStringLiteral("kind"), QStringLiteral("input/wheelGesture"));
            gesture.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            if (gesture.value(QStringLiteral("command")).toString().trimmed().isEmpty()) {
                return errorObject(QStringLiteral("input.registerWheelGesture requires a command."));
            }
            state_.extensionInputGestures_.append(gesture);
            state_.extensionRegistrationsByKind_[QStringLiteral("input/wheelGesture")].append(gesture);
            return okValue(gesture);
        }
        if (method == QStringLiteral("input/registerKeyGesture")
            || method == QStringLiteral("input/registerMouseGesture")) {
            const QString kind = method == QStringLiteral("input/registerKeyGesture")
                ? QStringLiteral("input/keyGesture")
                : QStringLiteral("input/mouseGesture");
            QJsonObject gesture = registeredContribution(params, kind);
            gesture.insert(QStringLiteral("kind"), kind);
            gesture.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            if (gesture.value(QStringLiteral("command")).toString().trimmed().isEmpty()) {
                return errorObject(QStringLiteral("%1 requires a command.").arg(method));
            }
            state_.extensionInputGestures_.append(gesture);
            state_.extensionRegistrationsByKind_[kind].append(gesture);
            return okValue(gesture);
        }
        if (method == QStringLiteral("input/getGestures")) {
            return okValue(state_.extensionInputGestures_);
        }
        if (method == QStringLiteral("input/dispatch")) {
            const QString target = params.value(QStringLiteral("target")).toString(QStringLiteral("any"));
            const QString type = params.value(QStringLiteral("type")).toString();
            const QString phase = params.value(QStringLiteral("phase")).toString();
            const Qt::KeyboardModifiers modifiers = Qt::KeyboardModifiers(params.value(QStringLiteral("modifiers")).toInt());
            for (const QJsonValue& value : state_.extensionInputGestures_) {
                const QJsonObject gesture = value.toObject();
                const QString kind = gesture.value(QStringLiteral("kind")).toString();
                if ((type == QStringLiteral("key") && kind != QStringLiteral("input/keyGesture"))
                    || (type == QStringLiteral("mouse") && kind != QStringLiteral("input/mouseGesture"))) {
                    continue;
                }
                if (!gestureTargetMatches(gesture.value(QStringLiteral("target")).toString(QStringLiteral("any")), target)
                    || !gestureModifiersMatch(gesture.value(QStringLiteral("modifiers")).toArray(), modifiers)
                    || !gesturePhaseMatches(gesture.value(QStringLiteral("phase")).toString(QStringLiteral("any")), phase)) {
                    continue;
                }
                if (type == QStringLiteral("key")
                    && !gestureStringMatches(gesture, QStringLiteral("key"), params.value(QStringLiteral("key")).toString())) {
                    continue;
                }
                if (type == QStringLiteral("mouse")
                    && !gestureStringMatches(gesture, QStringLiteral("button"), params.value(QStringLiteral("button")).toString())) {
                    continue;
                }
                runExtensionCommand(gesture.value(QStringLiteral("command")).toString());
                return okValue(QJsonObject{
                    {QStringLiteral("handled"), true},
                    {QStringLiteral("gestureId"), gesture.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("command"), gesture.value(QStringLiteral("command")).toString()},
                });
            }
            return okValue(QJsonObject{{QStringLiteral("handled"), false}});
        }
        if (method == QStringLiteral("input/dispatchWheel")) {
            const QString target = params.value(QStringLiteral("target")).toString(QStringLiteral("any"));
            const int delta = params.value(QStringLiteral("delta")).toInt();
            const Qt::KeyboardModifiers modifiers = Qt::KeyboardModifiers(params.value(QStringLiteral("modifiers")).toInt());
            for (const QJsonValue& value : state_.extensionInputGestures_) {
                const QJsonObject gesture = value.toObject();
                if (gesture.value(QStringLiteral("kind")).toString() != QStringLiteral("input/wheelGesture")) {
                    continue;
                }
                if (!gestureTargetMatches(gesture.value(QStringLiteral("target")).toString(QStringLiteral("any")), target)) {
                    continue;
                }
                if (!gestureModifiersMatch(gesture.value(QStringLiteral("modifiers")).toArray(), modifiers)) {
                    continue;
                }
                const QString direction = gesture.value(QStringLiteral("direction")).toString(QStringLiteral("any")).trimmed().toLower();
                if ((direction == QStringLiteral("up") && delta <= 0)
                    || (direction == QStringLiteral("down") && delta >= 0)) {
                    continue;
                }
                runExtensionCommand(gesture.value(QStringLiteral("command")).toString());
                return okValue(QJsonObject{
                    {QStringLiteral("handled"), true},
                    {QStringLiteral("gestureId"), gesture.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("command"), gesture.value(QStringLiteral("command")).toString()},
                });
            }
            return okValue(QJsonObject{{QStringLiteral("handled"), false}});
        }
        if (method == QStringLiteral("app/openPreferences")) {
            onPreferences();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("app/openAboutDialog")) {
            onAbout();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("window/showInputBox")) {
            bool ok = false;
            const QString title = params.value(QStringLiteral("title")).toString(UiText::text(QStringLiteral("extension.dialog.input_title")));
            const QString label = params.value(QStringLiteral("prompt")).toString();
            const QString text = QInputDialog::getText(this, title, label, QLineEdit::Normal, params.value(QStringLiteral("value")).toString(), &ok);
            return ok ? okValue(text) : errorObject(UiText::text(QStringLiteral("extension.dialog.canceled")));
        }
        if (method == QStringLiteral("window/showQuickPick")) {
            QStringList items;
            for (const QJsonValue& value : params.value(QStringLiteral("items")).toArray()) {
                items.append(value.toString());
            }
            bool ok = false;
            const QString title = params.value(QStringLiteral("title")).toString(UiText::text(QStringLiteral("extension.dialog.quick_pick_title")));
            const QString item = QInputDialog::getItem(this, title, params.value(QStringLiteral("placeHolder")).toString(), items, 0, false, &ok);
            return ok ? okValue(item) : errorObject(UiText::text(QStringLiteral("extension.dialog.canceled")));
        }
        if (method == QStringLiteral("window/showOpenDialog") || method == QStringLiteral("nativeDialogs/openFile")) {
            const QString title = params.value(QStringLiteral("title")).toString(UiText::text(QStringLiteral("action.open")));
            const QString dir = params.value(QStringLiteral("defaultPath")).toString(QDir::homePath());
            const QString filter = params.value(QStringLiteral("filter")).toString(QStringLiteral("All Files (*)"));
            const QString path = QFileDialog::getOpenFileName(this, title, dir, filter);
            return path.isEmpty() ? errorObject(UiText::text(QStringLiteral("extension.dialog.canceled"))) : okValue(path);
        }
        if (method == QStringLiteral("window/showSaveDialog") || method == QStringLiteral("nativeDialogs/saveFile")) {
            const QString title = params.value(QStringLiteral("title")).toString(UiText::text(QStringLiteral("action.save_as")));
            const QString dir = params.value(QStringLiteral("defaultPath")).toString(QDir::homePath());
            const QString filter = params.value(QStringLiteral("filter")).toString(QStringLiteral("All Files (*)"));
            const QString path = QFileDialog::getSaveFileName(this, title, dir, filter);
            return path.isEmpty() ? errorObject(UiText::text(QStringLiteral("extension.dialog.canceled"))) : okValue(path);
        }
        if (method == QStringLiteral("window/showSelectFolderDialog") || method == QStringLiteral("nativeDialogs/selectFolder")) {
            const QString title = params.value(QStringLiteral("title")).toString(UiText::text(QStringLiteral("dialog.batch_export.select_folder")));
            const QString dir = params.value(QStringLiteral("defaultPath")).toString(QDir::homePath());
            const QString path = QFileDialog::getExistingDirectory(this, title, dir);
            return path.isEmpty() ? errorObject(UiText::text(QStringLiteral("extension.dialog.canceled"))) : okValue(path);
        }
        if (method == QStringLiteral("window/createStatusBarItem")) {
            const QString text = params.value(QStringLiteral("text")).toString();
            if (statusBar() != nullptr && !text.isEmpty()) {
                statusBar()->showMessage(text, params.value(QStringLiteral("timeoutMs")).toInt(5000));
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("window/clearStatusBarMessage")) {
            if (statusBar() != nullptr) {
                statusBar()->clearMessage();
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("window/openExternalUrl")) {
            const QUrl url(params.value(QStringLiteral("url")).toString());
            return QJsonObject{{QStringLiteral("ok"), url.isValid() && QDesktopServices::openUrl(url)}};
        }
        if (method == QStringLiteral("window/focusEditor")) {
            if (editorWidget_ != nullptr) {
                editorWidget_->setFocus();
            }
            return QJsonObject{{QStringLiteral("ok"), editorWidget_ != nullptr}};
        }
        if (method == QStringLiteral("window/focusMetadataPanel")) {
            const bool ok = switchToMetadataField();
            if (titleEdit_ != nullptr) {
                titleEdit_->setFocus();
            }
            return QJsonObject{{QStringLiteral("ok"), ok}};
        }
        if (method == QStringLiteral("window/focusPreview")
            || method == QStringLiteral("window/focusTimeline")
            || method == QStringLiteral("window/focusValidationPanel")) {
            if (bottomTabs_ != nullptr) {
                bottomTabs_->setFocus();
            }
            return QJsonObject{{QStringLiteral("ok"), bottomTabs_ != nullptr}};
        }
        if (method == QStringLiteral("workspace/getChartFolder")) {
            return okValue(currentFilePath_.isEmpty() ? QString() : QFileInfo(currentFilePath_).absolutePath());
        }
        if (method == QStringLiteral("workspace/getCurrentFilePath")) {
            return okValue(currentFilePath_);
        }
        if (method == QStringLiteral("workspace/getDirtyState") || method == QStringLiteral("workspace/isDirty")) {
            return okValue(QJsonObject{
                {QStringLiteral("dirty"), documentDirty_ || currentFieldDirty_},
                {QStringLiteral("documentDirty"), documentDirty_},
                {QStringLiteral("currentFieldDirty"), currentFieldDirty_},
            });
        }
        if (method == QStringLiteral("workspace/getMediaFiles")) {
            QJsonArray files;
            if (!currentFilePath_.isEmpty()) {
                const QDir dir(QFileInfo(currentFilePath_).absolutePath());
                QStringList names = miacode::chart_assets::trackCandidateFileNames();
                names << miacode::chart_assets::backgroundMediaCandidateFileNames(/*includeVideoCandidates=*/true);
                for (const QString& name : names) {
                    const QString path = dir.filePath(name);
                    if (QFileInfo::exists(path)) {
                        files.append(path);
                    }
                }
            }
            return okValue(files);
        }
        if (method == QStringLiteral("workspace/getChartMetadata")) {
            return okValue(QJsonObject{
                {QStringLiteral("path"), currentFilePath_},
                {QStringLiteral("title"), titleEdit_ != nullptr ? titleEdit_->text() : QString()},
                {QStringLiteral("artist"), artistEdit_ != nullptr ? artistEdit_->text() : QString()},
                {QStringLiteral("first"), firstEdit_ != nullptr ? firstEdit_->text() : QString()},
                {QStringLiteral("activeDifficultyId"), activeDifficultyId_},
            });
        }
        if (method == QStringLiteral("workspace/updateChartMetadata")) {
            if (titleEdit_ != nullptr && params.contains(QStringLiteral("title"))) {
                titleEdit_->setText(params.value(QStringLiteral("title")).toString());
            }
            if (artistEdit_ != nullptr && params.contains(QStringLiteral("artist"))) {
                artistEdit_->setText(params.value(QStringLiteral("artist")).toString());
            }
            if (firstEdit_ != nullptr && params.contains(QStringLiteral("first"))) {
                firstEdit_->setText(params.value(QStringLiteral("first")).toString());
            }
            markCurrentFieldDirty();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("workspace/save")) {
            if (currentFilePath_.isEmpty()) {
                return errorObject(QStringLiteral("No current file path."));
            }
            return QJsonObject{{QStringLiteral("ok"), saveToPath(currentFilePath_)}};
        }
        if (method == QStringLiteral("workspace/saveAs")) {
            const QString path = params.value(QStringLiteral("path")).toString();
            return path.isEmpty() ? errorObject(QStringLiteral("Missing path.")) : QJsonObject{{QStringLiteral("ok"), saveToPath(path)}};
        }
        if (method == QStringLiteral("workspace/getRecentFiles")) {
            return okValue(QJsonArray::fromStringList(recentFilePaths_));
        }
        if (method == QStringLiteral("workspace/getProjectData")) {
            const QString key = params.value(QStringLiteral("key")).toString();
            return key.trimmed().isEmpty() ? okValue(state_.extensionProjectData_) : okValue(state_.extensionProjectData_.value(key));
        }
        if (method == QStringLiteral("workspace/setProjectData")) {
            const QString key = params.value(QStringLiteral("key")).toString();
            if (key.trimmed().isEmpty()) {
                return errorObject(QStringLiteral("Project data key is required."));
            }
            state_.extensionProjectData_.insert(key, params.value(QStringLiteral("value")));
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("workspace/scanChartFolders")) {
            const QString rootPath = params.value(QStringLiteral("rootPath")).toString();
            QDirIterator iterator(rootPath, QStringList{QStringLiteral("maidata.txt")},
                                  QDir::Files, QDirIterator::Subdirectories);
            QSet<QString> folders;
            while (iterator.hasNext()) {
                iterator.next();
                const QString path = iterator.fileInfo().absolutePath();
                if (!path.contains(QStringLiteral("/.miacode/"), Qt::CaseInsensitive)
                    && !path.contains(QStringLiteral("\\.miacode\\"), Qt::CaseInsensitive)) {
                    folders.insert(path);
                }
            }
            QStringList sorted = folders.values();
            sorted.sort(Qt::CaseInsensitive);
            return okValue(QJsonArray::fromStringList(sorted));
        }
        if (method == QStringLiteral("document/getDifficulties")) {
            QJsonArray difficulties;
            for (int id : document_.difficultyIds()) {
                const SimaiDifficultyData* difficulty = document_.difficulty(id);
                if (difficulty == nullptr) {
                    continue;
                }
                difficulties.append(QJsonObject{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("name"), SimaiDocument::difficultyName(id)},
                    {QStringLiteral("shortName"), SimaiDocument::difficultyShortName(id)},
                    {QStringLiteral("level"), difficulty->level},
                    {QStringLiteral("designer"), difficulty->designer},
                    {QStringLiteral("active"), id == activeDifficultyId_},
                });
            }
            return okValue(difficulties);
        }
        if (method == QStringLiteral("document/getActiveDifficulty")) {
            const SimaiDifficultyData* difficulty = document_.difficulty(activeDifficultyId_);
            if (difficulty == nullptr) {
                return errorObject(QStringLiteral("No active difficulty."));
            }
            return okValue(QJsonObject{
                {QStringLiteral("id"), activeDifficultyId_},
                {QStringLiteral("name"), SimaiDocument::difficultyName(activeDifficultyId_)},
                {QStringLiteral("level"), difficulty->level},
                {QStringLiteral("designer"), difficulty->designer},
                {QStringLiteral("text"), editorText()},
            });
        }
        if (method == QStringLiteral("document/setActiveDifficulty")) {
            const int id = params.value(QStringLiteral("id")).toInt();
            return QJsonObject{{QStringLiteral("ok"), switchToDifficultyField(id)}};
        }
        if (method == QStringLiteral("document/replaceActiveDifficultyText")) {
            if (!hasActiveDifficulty()) {
                return errorObject(QStringLiteral("No active difficulty."));
            }
            setEditorText(params.value(QStringLiteral("text")).toString());
            markCurrentFieldDirty();
            refreshTimelineMetadata();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("document/getParsedNoteMarkers")) {
            QJsonArray markers;
            for (const TimelineNoteMarker& marker : std::as_const(state_.latestTimelineNoteMarkers_)) {
                markers.append(timelineNoteMarkerToJson(marker));
            }
            return okValue(markers);
        }
        if (method == QStringLiteral("document/getTimingMetadata")) {
            return okValue(QJsonObject{
                {QStringLiteral("activeDifficultyId"), activeDifficultyId_},
                {QStringLiteral("currentSecond"), qtPreviewPauseSecond_},
                {QStringLiteral("noteMarkerCount"), state_.latestTimelineNoteMarkers_.size()},
                {QStringLiteral("noteMarkerSignature"), QString::fromLatin1(state_.latestTimelineNoteMarkerSignature_.toHex())},
                {QStringLiteral("first"), firstEdit_ != nullptr ? firstEdit_->text() : QString()},
            });
        }
        if (method == QStringLiteral("document/applyTextEdits")) {
            if (!hasActiveDifficulty()) {
                return errorObject(QStringLiteral("No active difficulty."));
            }
            struct TextEdit {
                int start = 0;
                int end = 0;
                QString text;
            };
            QVector<TextEdit> edits;
            const QString original = editorText();
            for (const QJsonValue& value : params.value(QStringLiteral("edits")).toArray()) {
                const QJsonObject object = value.toObject();
                TextEdit edit;
                edit.start = qBound(0, object.value(QStringLiteral("start")).toInt(), original.size());
                edit.end = qBound(edit.start, object.value(QStringLiteral("end")).toInt(edit.start), original.size());
                edit.text = object.value(QStringLiteral("text")).toString();
                edits.append(edit);
            }
            std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) {
                return a.start > b.start;
            });
            QString next = original;
            for (const TextEdit& edit : edits) {
                next.replace(edit.start, edit.end - edit.start, edit.text);
            }
            setEditorText(next);
            markCurrentFieldDirty();
            refreshTimelineMetadata();
            return okValue(QJsonObject{{QStringLiteral("applied"), edits.size()}});
        }
        if (method == QStringLiteral("document/format")) {
            if (!hasActiveDifficulty()) {
                return errorObject(QStringLiteral("No active difficulty."));
            }
            QStringList lines = editorText().split(QLatin1Char('\n'));
            for (QString& line : lines) {
                while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t'))) {
                    line.chop(1);
                }
            }
            setEditorText(lines.join(QLatin1Char('\n')));
            markCurrentFieldDirty();
            refreshTimelineMetadata();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("document/createDifficulty")) {
            const int id = params.value(QStringLiteral("id")).toInt(0);
            if (!SimaiDocument::isDifficultyId(id)) {
                return errorObject(QStringLiteral("Invalid difficulty id."));
            }
            SimaiDifficultyData& difficulty = document_.ensureDifficulty(id);
            difficulty.level = params.value(QStringLiteral("level")).toString(difficulty.level);
            difficulty.designer = params.value(QStringLiteral("designer")).toString(difficulty.designer);
            difficulty.chart = params.value(QStringLiteral("text")).toString(difficulty.chart);
            markCurrentFieldDirty();
            switchToDifficultyField(id);
            return okValue(QJsonObject{{QStringLiteral("id"), id}});
        }
        if (method == QStringLiteral("document/deleteDifficulty")) {
            const int id = params.value(QStringLiteral("id")).toInt(0);
            return QJsonObject{{QStringLiteral("ok"), deleteDifficultyField(id)}};
        }
        if (method == QStringLiteral("document/renameDifficulty")) {
            const int id = params.value(QStringLiteral("id")).toInt(0);
            SimaiDifficultyData* difficulty = document_.difficulty(id);
            if (difficulty == nullptr) {
                return errorObject(QStringLiteral("Difficulty not found."));
            }
            difficulty->level = params.value(QStringLiteral("label")).toString(difficulty->level);
            if (id == activeDifficultyId_ && ui_.difficultyLevelEdit_ != nullptr) {
                ui_.difficultyLevelEdit_->setText(difficulty->level);
            }
            markCurrentFieldDirty();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("editor/getCursor") || method == QStringLiteral("editor/getSelection")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            QTextCursor cursor = editor != nullptr ? editor->textCursor() : QTextCursor();
            return okValue(QJsonObject{
                {QStringLiteral("position"), cursor.position()},
                {QStringLiteral("anchor"), cursor.anchor()},
                {QStringLiteral("selectionStart"), cursor.selectionStart()},
                {QStringLiteral("selectionEnd"), cursor.selectionEnd()},
                {QStringLiteral("line"), cursor.blockNumber()},
                {QStringLiteral("column"), cursor.positionInBlock()},
            });
        }
        if (method == QStringLiteral("editor/getText")) {
            return okValue(hasActiveDifficulty() ? editorText() : QString());
        }
        if (method == QStringLiteral("editor/getVisibleRange")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr) {
                return errorObject(QStringLiteral("No active editor."));
            }
            const QTextCursor first = editor->cursorForPosition(QPoint(0, 0));
            const QTextCursor last = editor->cursorForPosition(QPoint(qMax(0, editor->viewport()->width() - 1), qMax(0, editor->viewport()->height() - 1)));
            return okValue(QJsonObject{
                {QStringLiteral("start"), first.position()},
                {QStringLiteral("end"), last.position()},
                {QStringLiteral("startLine"), first.blockNumber() + 1},
                {QStringLiteral("endLine"), last.blockNumber() + 1},
                {QStringLiteral("viewportWidth"), editor->viewport()->width()},
                {QStringLiteral("viewportHeight"), editor->viewport()->height()},
            });
        }
        if (method == QStringLiteral("editor/revealRange")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr) {
                return errorObject(QStringLiteral("No active editor."));
            }
            QTextCursor cursor = editor->textCursor();
            const int start = params.value(QStringLiteral("start")).toInt(-1);
            const int end = params.value(QStringLiteral("end")).toInt(start);
            if (start >= 0) {
                cursor.setPosition(start);
                if (end >= start) {
                    cursor.setPosition(end, QTextCursor::KeepAnchor);
                }
            } else if (!miacode::mainwindow::editor_selection::buildSelectionCursor(
                           editor,
                           qMax(1, params.value(QStringLiteral("line")).toInt(1)),
                           qMax(1, params.value(QStringLiteral("col")).toInt(1)),
                           qMax(1, params.value(QStringLiteral("endLine")).toInt(params.value(QStringLiteral("line")).toInt(1))),
                           qMax(1, params.value(QStringLiteral("endCol")).toInt(params.value(QStringLiteral("col")).toInt(1))),
                           &cursor)) {
                return errorObject(QStringLiteral("Invalid range."));
            }
            editor->setTextCursor(cursor);
            editor->ensureCursorVisible();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("editor/getParsedSnapshot")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            const QTextCursor cursor = editor != nullptr ? editor->textCursor() : QTextCursor();
            return okValue(QJsonObject{
                {QStringLiteral("activeDifficultyId"), activeDifficultyId_},
                {QStringLiteral("textLength"), hasActiveDifficulty() ? editorText().size() : 0},
                {QStringLiteral("lineCount"), editor != nullptr && editor->document() != nullptr ? editor->document()->blockCount() : 0},
                {QStringLiteral("cursor"), QJsonObject{
                     {QStringLiteral("position"), cursor.position()},
                     {QStringLiteral("line"), cursor.blockNumber() + 1},
                     {QStringLiteral("column"), cursor.positionInBlock() + 1},
                 }},
                {QStringLiteral("noteMarkerCount"), state_.latestTimelineNoteMarkers_.size()},
                {QStringLiteral("noteMarkerSignature"), QString::fromLatin1(state_.latestTimelineNoteMarkerSignature_.toHex())},
            });
        }
        if (method == QStringLiteral("editor/insertText") || method == QStringLiteral("editor/replaceSelection")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr || !hasActiveDifficulty()) {
                return errorObject(QStringLiteral("No active editor difficulty."));
            }
            QTextCursor cursor = editor->textCursor();
            cursor.insertText(params.value(QStringLiteral("text")).toString());
            editor->setTextCursor(cursor);
            markCurrentFieldDirty();
            refreshTimelineMetadata();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("editor/setSelection")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr) {
                return errorObject(QStringLiteral("No active editor."));
            }
            QTextCursor cursor = editor->textCursor();
            cursor.setPosition(qMax(0, params.value(QStringLiteral("start")).toInt()));
            cursor.setPosition(qMax(0, params.value(QStringLiteral("end")).toInt()), QTextCursor::KeepAnchor);
            editor->setTextCursor(cursor);
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("editor/addDecoration")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr) {
                return errorObject(QStringLiteral("No active editor."));
            }
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            const QJsonObject range = params.value(QStringLiteral("range")).toObject(params);
            const QJsonObject options = params.value(QStringLiteral("options")).toObject();
            const int start = range.value(QStringLiteral("start")).toInt(-1);
            const int end = range.value(QStringLiteral("end")).toInt(-1);
            QTextCursor cursor;
            if (start >= 0 && end >= start) {
                cursor = editor->textCursor();
                cursor.setPosition(start);
                cursor.setPosition(end, QTextCursor::KeepAnchor);
            } else if (!miacode::mainwindow::editor_selection::buildSelectionCursor(
                           editor,
                           qMax(1, range.value(QStringLiteral("line")).toInt(1)),
                           qMax(1, range.value(QStringLiteral("col")).toInt(1)),
                           qMax(1, range.value(QStringLiteral("endLine")).toInt(range.value(QStringLiteral("line")).toInt(1))),
                           qMax(1, range.value(QStringLiteral("endCol")).toInt(range.value(QStringLiteral("col")).toInt(1))),
                           &cursor)) {
                return errorObject(QStringLiteral("Invalid decoration range."));
            }
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            QColor background = colorFromJsonValue(options.value(QStringLiteral("backgroundColor")), QColor(255, 214, 102, 64));
            if (options.contains(QStringLiteral("backgroundColor")) || options.value(QStringLiteral("background")).toBool(true)) {
                background.setAlpha(options.value(QStringLiteral("backgroundAlpha")).toInt(background.alpha()));
                selection.format.setBackground(background);
            }
            const QString underlineStyle = options.value(QStringLiteral("underlineStyle")).toString(QStringLiteral("wave")).toLower();
            if (options.value(QStringLiteral("underline")).toBool(true)) {
                selection.format.setUnderlineStyle(underlineStyle == QStringLiteral("single")
                                                       ? QTextCharFormat::SingleUnderline
                                                       : QTextCharFormat::WaveUnderline);
                selection.format.setUnderlineColor(colorFromJsonValue(options.value(QStringLiteral("underlineColor")), UiTheme::colors().accent));
            }
            const QString toolTip = options.value(QStringLiteral("tooltip")).toString(options.value(QStringLiteral("message")).toString());
            if (!toolTip.isEmpty()) {
                selection.format.setToolTip(toolTip);
            }
            state_.extensionEditorExtraSelections_[ownerId].append(selection);
            state_.lastEditorExtraSelectionsSignature_.clear();
            refreshEditorExtraSelections();
            return okValue(QJsonObject{{QStringLiteral("ownerId"), ownerId}, {QStringLiteral("count"), state_.extensionEditorExtraSelections_.value(ownerId).size()}});
        }
        if (method == QStringLiteral("editor/clearDecorations")) {
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString();
            if (ownerId.trimmed().isEmpty()) {
                state_.extensionEditorExtraSelections_.clear();
            } else {
                state_.extensionEditorExtraSelections_.remove(ownerId);
            }
            state_.lastEditorExtraSelectionsSignature_.clear();
            refreshEditorExtraSelections();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("editor/getLine") || method == QStringLiteral("editor/getCurrentLine")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr) {
                return errorObject(QStringLiteral("No active editor."));
            }
            const int requestedLine = method == QStringLiteral("editor/getCurrentLine")
                ? editor->textCursor().blockNumber() + 1
                : params.value(QStringLiteral("line")).toInt(1);
            QTextBlock block = editor->document()->findBlockByNumber(qMax(1, requestedLine) - 1);
            if (!block.isValid()) {
                return errorObject(QStringLiteral("Line not found."));
            }
            return okValue(QJsonObject{
                {QStringLiteral("line"), requestedLine},
                {QStringLiteral("text"), block.text()},
                {QStringLiteral("position"), block.position()},
                {QStringLiteral("length"), block.length()},
            });
        }
        if (method == QStringLiteral("editor/getCurrentToken")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr) {
                return errorObject(QStringLiteral("No active editor."));
            }
            const QTextCursor cursor = editor->textCursor();
            const QString lineText = cursor.block().text();
            int start = qBound(0, cursor.positionInBlock(), lineText.size());
            int end = start;
            const auto isTokenChar = [](QChar ch) {
                return ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('#') || ch == QLatin1Char('&');
            };
            while (start > 0 && isTokenChar(lineText.at(start - 1))) {
                --start;
            }
            while (end < lineText.size() && isTokenChar(lineText.at(end))) {
                ++end;
            }
            return okValue(QJsonObject{
                {QStringLiteral("text"), lineText.mid(start, end - start)},
                {QStringLiteral("line"), cursor.blockNumber() + 1},
                {QStringLiteral("startCol"), start + 1},
                {QStringLiteral("endCol"), end + 1},
                {QStringLiteral("start"), cursor.block().position() + start},
                {QStringLiteral("end"), cursor.block().position() + end},
            });
        }
        if (method == QStringLiteral("editor/replaceRange")) {
            auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            if (editor == nullptr || !hasActiveDifficulty()) {
                return errorObject(QStringLiteral("No active editor difficulty."));
            }
            QTextCursor cursor;
            const int start = params.value(QStringLiteral("start")).toInt(-1);
            const int end = params.value(QStringLiteral("end")).toInt(-1);
            if (start >= 0 && end >= start) {
                cursor = editor->textCursor();
                cursor.setPosition(start);
                cursor.setPosition(end, QTextCursor::KeepAnchor);
            } else if (!miacode::mainwindow::editor_selection::buildSelectionCursor(
                           editor,
                           qMax(1, params.value(QStringLiteral("line")).toInt(1)),
                           qMax(1, params.value(QStringLiteral("col")).toInt(1)),
                           qMax(1, params.value(QStringLiteral("endLine")).toInt(params.value(QStringLiteral("line")).toInt(1))),
                           qMax(1, params.value(QStringLiteral("endCol")).toInt(params.value(QStringLiteral("col")).toInt(1))),
                           &cursor)) {
                return errorObject(QStringLiteral("Invalid range."));
            }
            cursor.insertText(params.value(QStringLiteral("text")).toString());
            editor->setTextCursor(cursor);
            markCurrentFieldDirty();
            refreshTimelineMetadata();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("editor/showHover")) {
            QString markdown = params.value(QStringLiteral("markdown")).toString();
            if (markdown.trimmed().isEmpty()) {
                const QJsonObject collected = collectProviders(QStringLiteral("providers/hover"), params);
                const QJsonArray items = collected.value(QStringLiteral("items")).toArray();
                if (!items.isEmpty()) {
                    markdown = items.first().toObject().value(QStringLiteral("displayText")).toString();
                }
            }
            if (statusBar() != nullptr && !markdown.trimmed().isEmpty()) {
                statusBar()->showMessage(markdown.trimmed(), 5000);
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("editor/showCompletions")) {
            return handleExtensionHostRequest(QStringLiteral("providers/showCompletions"), params);
        }
        if (method == QStringLiteral("editor/showCodeActions")) {
            return handleExtensionHostRequest(QStringLiteral("providers/showCodeActions"), params);
        }
        if (method == QStringLiteral("providers/getRegistered")) {
            const QString requestedKind = params.value(QStringLiteral("kind")).toString();
            QJsonArray registrations;
            for (auto it = state_.extensionRegistrationsByKind_.constBegin();
                 it != state_.extensionRegistrationsByKind_.constEnd();
                 ++it) {
                if (!requestedKind.isEmpty() && it.key() != requestedKind) {
                    continue;
                }
                if (!it.key().startsWith(QStringLiteral("providers/"))) {
                    continue;
                }
                for (const QJsonValue& value : it.value()) {
                    registrations.append(value);
                }
            }
            return okValue(registrations);
        }
        if (method == QStringLiteral("providers/collectHover")
            || method == QStringLiteral("providers/collectCompletions")
            || method == QStringLiteral("providers/collectCodeActions")) {
            const QString kind = method == QStringLiteral("providers/collectHover")
                ? QStringLiteral("providers/hover")
                : (method == QStringLiteral("providers/collectCompletions")
                       ? QStringLiteral("providers/completion")
                       : QStringLiteral("providers/codeAction"));
            QJsonObject collected = collectProviders(kind, params);
            if (method == QStringLiteral("providers/collectHover")
                && params.value(QStringLiteral("show")).toBool(false)
                && statusBar() != nullptr) {
                const QJsonArray items = collected.value(QStringLiteral("items")).toArray();
                if (!items.isEmpty()) {
                    statusBar()->showMessage(items.first().toObject().value(QStringLiteral("displayText")).toString(), 5000);
                }
            }
            return okValue(collected);
        }
        if (method == QStringLiteral("providers/showHover")) {
            QJsonObject collected = collectProviders(QStringLiteral("providers/hover"), params);
            const QJsonArray items = collected.value(QStringLiteral("items")).toArray();
            if (items.isEmpty()) {
                return okValue(QJsonObject{{QStringLiteral("shown"), false}});
            }
            const QString text = items.first().toObject().value(QStringLiteral("displayText")).toString();
            if (!text.trimmed().isEmpty()) {
                const QPoint globalPos(
                    params.value(QStringLiteral("globalX")).toInt(QCursor::pos().x()),
                    params.value(QStringLiteral("globalY")).toInt(QCursor::pos().y()));
                QToolTip::showText(globalPos, text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")), editorWidget_);
            }
            return okValue(QJsonObject{{QStringLiteral("shown"), true}, {QStringLiteral("item"), items.first()}});
        }
        if (method == QStringLiteral("providers/showCompletions")
            || method == QStringLiteral("providers/showCodeActions")) {
            const bool completions = method == QStringLiteral("providers/showCompletions");
            QJsonObject collected = collectProviders(
                completions ? QStringLiteral("providers/completion") : QStringLiteral("providers/codeAction"),
                params);
            const QJsonArray items = collected.value(QStringLiteral("items")).toArray();
            if (items.isEmpty()) {
                return okValue(QJsonObject{{QStringLiteral("shown"), false}});
            }
            QMenu menu(this);
            menu.setTitle(completions ? QStringLiteral("Completions") : QStringLiteral("Code Actions"));
            QHash<QAction*, QJsonObject> actionItems;
            for (const QJsonValue& value : items) {
                const QJsonObject item = value.toObject();
                const QString label = contributionTitle(item, item.value(QStringLiteral("displayText")).toString(QStringLiteral("Item")));
                QAction* action = menu.addAction(label);
                action->setToolTip(providerDisplayText(item));
                actionItems.insert(action, item);
            }
            const QPoint globalPos(
                params.value(QStringLiteral("globalX")).toInt(QCursor::pos().x()),
                params.value(QStringLiteral("globalY")).toInt(QCursor::pos().y()));
            QAction* selected = menu.exec(globalPos);
            if (selected == nullptr || !actionItems.contains(selected)) {
                return okValue(QJsonObject{{QStringLiteral("shown"), true}, {QStringLiteral("accepted"), false}});
            }
            const QJsonObject item = actionItems.value(selected);
            const QString command = item.value(QStringLiteral("command")).toString(
                item.value(QStringLiteral("onSelectCommand")).toString()).trimmed();
            const QString insertText = item.value(QStringLiteral("insertText")).toString(
                item.value(QStringLiteral("textEdit")).toObject().value(QStringLiteral("text")).toString()).trimmed();
            if (!insertText.isEmpty()) {
                handleExtensionHostRequest(QStringLiteral("editor/insertText"), QJsonObject{{QStringLiteral("text"), insertText}});
            }
            if (!command.isEmpty()) {
                runExtensionCommand(command);
            }
            return okValue(QJsonObject{{QStringLiteral("shown"), true}, {QStringLiteral("accepted"), true}, {QStringLiteral("item"), item}});
        }
        if (method == QStringLiteral("editor/addGutterIcon")
            || method == QStringLiteral("editor/clearGutterIcons")
            || method == QStringLiteral("editor/fold")
            || method == QStringLiteral("editor/unfold")) {
            QJsonObject contribution = registeredContribution(params, method);
            state_.extensionRegistrationsByKind_[method].append(contribution);
            return okValue(contribution);
        }
        if (method == QStringLiteral("validation/run") || method == QStringLiteral("diagnostics/validateDocument")) {
            const bool ok = runValidateSimaiSilently(false);
            replayExtensionDiagnostics();
            return QJsonObject{{QStringLiteral("ok"), ok}};
        }
        if (method == QStringLiteral("validation/getLastResult")) {
            const auto it = state_.validationCacheByDifficulty_.constFind(activeDifficultyId_);
            if (it == state_.validationCacheByDifficulty_.constEnd()) {
                return okValue(QJsonObject{{QStringLiteral("available"), false}});
            }
            QJsonArray issues;
            for (const ValidationCachedIssue& issue : it->issues) {
                issues.append(QJsonObject{
                    {QStringLiteral("line"), issue.line},
                    {QStringLiteral("col"), issue.col},
                    {QStringLiteral("endCol"), issue.endCol},
                    {QStringLiteral("rawMessage"), issue.rawMessage},
                    {QStringLiteral("displayMessage"), issue.displayMessage},
                    {QStringLiteral("issueTypeKey"), issue.issueTypeKey},
                    {QStringLiteral("issueTypeLabel"), issue.issueTypeLabel},
                });
            }
            return okValue(QJsonObject{
                {QStringLiteral("available"), true},
                {QStringLiteral("ok"), it->ok},
                {QStringLiteral("errorCount"), it->errorCount},
                {QStringLiteral("warningCount"), it->warningCount},
                {QStringLiteral("lenientNoteCount"), it->lenientNoteCount},
                {QStringLiteral("lenientErrorCount"), it->lenientErrorCount},
                {QStringLiteral("strictNoteCount"), it->strictNoteCount},
                {QStringLiteral("strictErrorCount"), it->strictErrorCount},
                {QStringLiteral("issues"), issues},
            });
        }
        if (method == QStringLiteral("validation/addDiagnostics")) {
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            QVector<ExtensionDiagnosticEntry>& entries = state_.extensionDiagnosticsByOwner_[ownerId];
            for (const QJsonValue& value : params.value(QStringLiteral("diagnostics")).toArray()) {
                const QJsonObject object = value.toObject();
                ExtensionDiagnosticEntry diagnostic;
                diagnostic.ownerId = ownerId;
                diagnostic.line = qMax(1, object.value(QStringLiteral("line")).toInt(1));
                diagnostic.col = qMax(1, object.value(QStringLiteral("col")).toInt(object.value(QStringLiteral("column")).toInt(1)));
                diagnostic.endCol = qMax(diagnostic.col, object.value(QStringLiteral("endCol")).toInt(object.value(QStringLiteral("endColumn")).toInt(diagnostic.col)));
                diagnostic.message = object.value(QStringLiteral("message")).toString();
                diagnostic.severity = object.value(QStringLiteral("severity")).toString(QStringLiteral("error"));
                diagnostic.source = object.value(QStringLiteral("source")).toString(QStringLiteral("Extension"));
                entries.append(diagnostic);
                addExtensionDiagnosticToPanel(diagnostic);
            }
            refreshEditorExtraSelections();
            updateEditorValidationSummary();
            return okValue(QJsonObject{{QStringLiteral("ownerId"), ownerId}, {QStringLiteral("count"), entries.size()}});
        }
        if (method == QStringLiteral("validation/clearDiagnostics")) {
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString();
            if (ownerId.trimmed().isEmpty()) {
                state_.extensionDiagnosticsByOwner_.clear();
            } else {
                state_.extensionDiagnosticsByOwner_.remove(ownerId);
            }
            refreshValidationPanelForActiveField();
            replayExtensionDiagnostics();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("analysis/runMuriAnalysis")) {
            if (state_.latestTimelineNoteMarkers_.isEmpty()) {
                return errorObject(QStringLiteral("No parsed timeline markers are available for Muri analysis."));
            }
            state_.muriAnalysisReport_ = MuriAnalyzer::analyze(
                state_.latestTimelineNoteMarkers_,
                state_.muriRenderOptions_,
                static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0);
            state_.muriAnalysisReport_.revision = ++state_.muriAnalysisReportRevisionCounter_;
            state_.muriAnalysisReportNoteMarkerSignature_ = state_.latestTimelineNoteMarkerSignature_;
            state_.muriStaticReferences_ = miacode::muri::buildStaticMuriReferences(
                state_.latestTimelineNoteMarkers_,
                static_cast<double>(state_.staticTapOnSlideThresholdMs_) / 1000.0);
            applyAlignedMuriAnalysisReportToViews();
            refreshMuriDiagnosticsPanel();
            return okValue(muriReportToJson(state_.muriAnalysisReport_));
        }
        if (method == QStringLiteral("analysis/getLastMuriResult")) {
            return okValue(muriReportToJson(state_.muriAnalysisReport_));
        }
        if (method == QStringLiteral("timeline/getCurrentSecond")) {
            return okValue(qtPreviewPauseSecond_);
        }
        if (method == QStringLiteral("timeline/getSnapshot")) {
            QJsonArray extensionMarkers;
            for (const QVector<ExtensionTimelineMarkerEntry>& markers : std::as_const(state_.extensionTimelineMarkersByOwner_)) {
                for (const ExtensionTimelineMarkerEntry& marker : markers) {
                    extensionMarkers.append(QJsonObject{
                        {QStringLiteral("ownerId"), marker.ownerId},
                        {QStringLiteral("id"), marker.id},
                        {QStringLiteral("second"), marker.second},
                        {QStringLiteral("endSecond"), marker.endSecond},
                        {QStringLiteral("label"), marker.label},
                        {QStringLiteral("color"), marker.color},
                    });
                }
            }
            return okValue(QJsonObject{
                {QStringLiteral("activeDifficultyId"), activeDifficultyId_},
                {QStringLiteral("currentSecond"), qtPreviewPauseSecond_},
                {QStringLiteral("zoomScale"), timelineQuickStateBridge_ != nullptr
                     ? timelineQuickStateBridge_->zoomScale()
                     : (timelineView_ != nullptr ? timelineView_->zoomScale() : 0.0)},
                // Rounded on purpose: the bridge scroll became sub-pixel, but this payload is
                // an extension-facing contract that has always carried a whole-pixel integer.
                {QStringLiteral("horizontalScrollValue"), timelineQuickStateBridge_ != nullptr
                     ? qRound(timelineQuickStateBridge_->horizontalScrollValue())
                     : (timelineView_ != nullptr ? timelineView_->horizontalScrollValue() : 0)},
                {QStringLiteral("viewportCenterSecond"), timelineQuickStateBridge_ != nullptr
                     ? timelineQuickStateBridge_->viewportCenterSecond()
                     : qtPreviewPauseSecond_},
                {QStringLiteral("followPreview"), timelineQuickStateBridge_ != nullptr
                     ? timelineQuickStateBridge_->followPreviewEnabled()
                     : (timelineView_ != nullptr && timelineView_->followPreviewEnabled())},
                {QStringLiteral("followProgress"), timelineQuickStateBridge_ == nullptr
                     ? (timelineView_ == nullptr || timelineView_->followProgressEnabled())
                     : timelineQuickStateBridge_->followProgressEnabled()},
                {QStringLiteral("textLength"), editorText().size()},
                {QStringLiteral("noteMarkerCount"), state_.latestTimelineNoteMarkers_.size()},
                {QStringLiteral("extensionMarkers"), extensionMarkers},
                {QStringLiteral("extensionVisuals"), state_.extensionTimelineVisuals_},
            });
        }
        if (method == QStringLiteral("timeline/getZoomState")) {
            return okValue(QJsonObject{
                {QStringLiteral("zoomScale"), timelineQuickStateBridge_ != nullptr
                     ? timelineQuickStateBridge_->zoomScale()
                     : (timelineView_ != nullptr ? timelineView_->zoomScale() : 0.0)},
                {QStringLiteral("presets"), [&]() {
                     QJsonArray presets;
                     if (timelineQuickStateBridge_ != nullptr) {
                         for (double preset : timelineQuickStateBridge_->zoomPresets()) {
                             presets.append(preset);
                         }
                     }
                     return presets;
                 }()},
            });
        }
        if (method == QStringLiteral("timeline/getVisibleRange")) {
            const double zoomScale = timelineQuickStateBridge_ != nullptr
                ? timelineQuickStateBridge_->zoomScale()
                : (timelineView_ != nullptr ? timelineView_->zoomScale() : 0.5);
            const double centerSecond = timelineQuickStateBridge_ != nullptr
                ? timelineQuickStateBridge_->viewportCenterSecond()
                : qtPreviewPauseSecond_;
            const int viewportWidth = timelineView_ != nullptr && timelineView_->viewport() != nullptr
                ? timelineView_->viewport()->width()
                : 0;
            const double pixelsPerSecond = qMax(1.0, 120.0 * zoomScale);
            const double halfSpan = viewportWidth > 0 ? (static_cast<double>(viewportWidth) / pixelsPerSecond) * 0.5 : 0.0;
            return okValue(QJsonObject{
                {QStringLiteral("startSecond"), qMax(0.0, centerSecond - halfSpan)},
                {QStringLiteral("endSecond"), centerSecond + halfSpan},
                {QStringLiteral("centerSecond"), centerSecond},
                {QStringLiteral("zoomScale"), zoomScale},
                // Rounded on purpose: the bridge scroll became sub-pixel, but this payload is
                // an extension-facing contract that has always carried a whole-pixel integer.
                {QStringLiteral("horizontalScrollValue"), timelineQuickStateBridge_ != nullptr
                     ? qRound(timelineQuickStateBridge_->horizontalScrollValue())
                     : (timelineView_ != nullptr ? timelineView_->horizontalScrollValue() : 0)},
                {QStringLiteral("viewportWidth"), viewportWidth},
            });
        }
        if (method == QStringLiteral("timeline/getMarkersAtSecond")) {
            const double second = params.value(QStringLiteral("second")).toDouble(qtPreviewPauseSecond_);
            const double tolerance = qMax(0.0, params.value(QStringLiteral("tolerance")).toDouble(0.02));
            QJsonArray markers;
            for (const TimelineNoteMarker& marker : std::as_const(state_.latestTimelineNoteMarkers_)) {
                const QJsonObject object = timelineNoteMarkerToJson(marker);
                const double markerSecond = object.value(QStringLiteral("second")).toDouble(object.value(QStringLiteral("time")).toDouble(-1.0));
                if (markerSecond >= 0.0 && qAbs(markerSecond - second) <= tolerance) {
                    markers.append(object);
                }
            }
            return okValue(markers);
        }
        if (method == QStringLiteral("timeline/seek") || method == QStringLiteral("preview/seek")) {
            seekPreviewToSecond(params.value(QStringLiteral("second")).toDouble(), true);
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/stepZoomPreset")
            || method == QStringLiteral("timeline/zoomIn")
            || method == QStringLiteral("timeline/zoomOut")) {
            int delta = params.value(QStringLiteral("delta")).toInt(0);
            if (method == QStringLiteral("timeline/zoomIn")) {
                delta = 1;
            } else if (method == QStringLiteral("timeline/zoomOut")) {
                delta = -1;
            }
            if (delta == 0) {
                return errorObject(QStringLiteral("timeline zoom delta must not be 0."));
            }
            const double anchorSecond = params.value(QStringLiteral("anchorSecond")).toDouble(
                timelineQuickStateBridge_ != nullptr
                    ? timelineQuickStateBridge_->viewportCenterSecond()
                    : qtPreviewPauseSecond_);
            if (timelineQuickStateBridge_ != nullptr) {
                timelineQuickStateBridge_->stepZoomPreset(delta, anchorSecond);
            } else if (timelineView_ != nullptr) {
                timelineView_->stepZoomPresetForQuickSurface(delta, anchorSecond);
            } else {
                return errorObject(QStringLiteral("Timeline host is not available."));
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/setZoomScale")) {
            const double scale = params.value(QStringLiteral("scale")).toDouble(params.value(QStringLiteral("value")).toDouble(0.5));
            const double anchorSecond = params.value(QStringLiteral("anchorSecond")).toDouble(
                timelineQuickStateBridge_ != nullptr
                    ? timelineQuickStateBridge_->viewportCenterSecond()
                    : qtPreviewPauseSecond_);
            if (timelineQuickStateBridge_ != nullptr) {
                timelineQuickStateBridge_->setZoomScaleAnchored(scale, anchorSecond);
            } else if (timelineView_ != nullptr) {
                timelineView_->stepZoomPresetForQuickSurface(scale > timelineView_->zoomScale() ? 1 : -1, anchorSecond);
            } else {
                return errorObject(QStringLiteral("Timeline host is not available."));
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/scrollToSecond")) {
            const double second = params.value(QStringLiteral("second")).toDouble(qtPreviewPauseSecond_);
            seekPreviewToSecond(second, true);
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/setFollowPreview")) {
            const bool enabled = params.value(QStringLiteral("enabled")).toBool(params.value(QStringLiteral("value")).toBool(false));
            state_.previewFollowEnabled_ = enabled;
            if (timelineQuickStateBridge_ != nullptr) {
                timelineQuickStateBridge_->setFollowPreviewEnabled(enabled);
            }
            if (timelineView_ != nullptr) {
                timelineView_->setFollowPreviewEnabled(enabled);
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/setFollowProgress")) {
            const bool enabled = params.value(QStringLiteral("enabled")).toBool(params.value(QStringLiteral("value")).toBool(true));
            if (timelineQuickStateBridge_ != nullptr) {
                timelineQuickStateBridge_->setFollowProgressEnabled(enabled);
            }
            if (timelineView_ != nullptr) {
                timelineView_->setFollowProgressEnabled(enabled);
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/addMarker")) {
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            ExtensionTimelineMarkerEntry marker;
            marker.ownerId = ownerId;
            marker.id = params.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
            marker.second = params.value(QStringLiteral("second")).toDouble(qtPreviewPauseSecond_);
            marker.endSecond = params.value(QStringLiteral("endSecond")).toDouble(-1.0);
            marker.label = params.value(QStringLiteral("label")).toString(params.value(QStringLiteral("title")).toString());
            marker.color = params.value(QStringLiteral("color")).toString();
            state_.extensionTimelineMarkersByOwner_[ownerId].append(marker);
            if (statusBar() != nullptr && !marker.label.isEmpty()) {
                statusBar()->showMessage(QStringLiteral("Timeline marker: %1 @ %2s").arg(marker.label).arg(marker.second, 0, 'f', 3), 3000);
            }
            return okValue(QJsonObject{{QStringLiteral("ownerId"), ownerId}, {QStringLiteral("id"), marker.id}});
        }
        if (method == QStringLiteral("timeline/clearMarkers")) {
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString();
            if (ownerId.trimmed().isEmpty()) {
                state_.extensionTimelineMarkersByOwner_.clear();
            } else {
                state_.extensionTimelineMarkersByOwner_.remove(ownerId);
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/addBand") || method == QStringLiteral("timeline/addVerticalLine")) {
            QJsonObject visual = registeredContribution(params, method);
            visual.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension"))));
            state_.extensionTimelineVisuals_.append(visual);
            return okValue(visual);
        }
        if (method == QStringLiteral("timeline/clearVisuals")) {
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString();
            if (ownerId.trimmed().isEmpty()) {
                state_.extensionTimelineVisuals_ = QJsonArray();
            } else {
                QJsonArray kept;
                for (const QJsonValue& value : state_.extensionTimelineVisuals_) {
                    if (value.toObject().value(QStringLiteral("ownerId")).toString() != ownerId) {
                        kept.append(value);
                    }
                }
                state_.extensionTimelineVisuals_ = kept;
            }
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("timeline/registerMarkerClickCommand")) {
            QJsonObject contribution = registeredContribution(params, method);
            state_.extensionRegistrationsByKind_[method].append(contribution);
            return okValue(contribution);
        }
        if (method == QStringLiteral("preview/play")) {
            toggleShellPreviewPlayback();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("preview/pause")) {
            pauseQtPreviewPlaybackExact();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("preview/stop")) {
            stopQtPreviewPlayback(true);
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("preview/getState")) {
            return okValue(QJsonObject{
                {QStringLiteral("currentSecond"), qtPreviewPauseSecond_},
                {QStringLiteral("rate"), previewPlaybackRate_},
                {QStringLiteral("activeDifficultyId"), activeDifficultyId_},
                {QStringLiteral("overlays"), state_.extensionPreviewOverlays_},
            });
        }
        if (method == QStringLiteral("preview/getRenderState")) {
            const QSize canvasSize = previewCanvasFrame_ != nullptr
                ? previewCanvasFrame_->contentsRect().size()
                : QSize();
            return okValue(QJsonObject{
                {QStringLiteral("currentSecond"), qtPreviewPauseSecond_},
                {QStringLiteral("playing"), qtPreviewPlaying_},
                {QStringLiteral("rate"), previewPlaybackRate_},
                {QStringLiteral("durationSeconds"), previewDurationSeconds()},
                {QStringLiteral("canvasWidth"), canvasSize.width()},
                {QStringLiteral("canvasHeight"), canvasSize.height()},
                {QStringLiteral("layoutSquareScale"), previewLayoutSquareScale_},
                {QStringLiteral("skinVariant"), static_cast<int>(previewSkinVariant_)},
                {QStringLiteral("mineSkinEnabled"), previewCanvas_ == nullptr || previewCanvas_->useMineSkin()},
                {QStringLiteral("mineSfxEnabled"), previewAudioSettings_.mineSfxEnabled},
                {QStringLiteral("outlineVariant"), static_cast<int>(previewOutlineVariant_)},
                {QStringLiteral("backgroundScaleMode"), static_cast<int>(previewBackgroundScaleMode_)},
                {QStringLiteral("tapFlowSpeed"), previewTapFlowSpeed_},
                {QStringLiteral("touchFlowSpeed"), previewTouchFlowSpeed_},
                {QStringLiteral("canvasFrameRateMode"), static_cast<int>(previewCanvasFrameRateMode_)},
                {QStringLiteral("stageMediaFrameRateMode"), static_cast<int>(state_.previewStageMediaFrameRateMode_)},
                {QStringLiteral("noteMarkerCount"), state_.latestTimelineNoteMarkers_.size()},
                {QStringLiteral("overlayCount"), state_.extensionPreviewOverlays_.size()},
            });
        }
        if (method == QStringLiteral("preview/getOverlays")) {
            return okValue(state_.extensionPreviewOverlays_);
        }
        if (method == QStringLiteral("preview/setSpeed")) {
            setShellPreviewRate(params.value(QStringLiteral("value")).toDouble(previewPlaybackRate_));
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("preview/setMineSkinEnabled")) {
            const bool enabled = params.value(QStringLiteral("enabled")).toBool(true);
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setUseMineSkin(enabled);
            }
            return okValue(QJsonObject{{QStringLiteral("enabled"), enabled}});
        }
        if (method == QStringLiteral("preview/setMineSfxEnabled")) {
            const bool enabled = params.value(QStringLiteral("enabled")).toBool(true);
            previewAudioSettings_.mineSfxEnabled = enabled;
            if (previewSfxRuntime_ != nullptr) {
                previewSfxRuntime_->applyLevels(previewAudioSettings_);
            }
            return okValue(QJsonObject{{QStringLiteral("enabled"), enabled}});
        }
        if (method == QStringLiteral("preview/addOverlay")) {
            QJsonObject overlay = registeredContribution(params, method);
            overlay.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension"))));
            state_.extensionPreviewOverlays_.append(overlay);
            if (statusBar() != nullptr && !overlay.value(QStringLiteral("text")).toString().isEmpty()) {
                statusBar()->showMessage(overlay.value(QStringLiteral("text")).toString(), overlay.value(QStringLiteral("timeoutMs")).toInt(3000));
            }
            renderPreviewOverlays();
            return okValue(overlay);
        }
        if (method == QStringLiteral("preview/updateOverlay")) {
            const QString id = params.value(QStringLiteral("id")).toString();
            if (id.trimmed().isEmpty()) {
                return errorObject(QStringLiteral("Overlay id is required."));
            }
            bool updated = false;
            QJsonArray next;
            for (const QJsonValue& value : state_.extensionPreviewOverlays_) {
                QJsonObject overlay = value.toObject();
                if (overlay.value(QStringLiteral("id")).toString() == id) {
                    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                        if (it.key() != QStringLiteral("extensionId")) {
                            overlay.insert(it.key(), it.value());
                        }
                    }
                    updated = true;
                }
                next.append(overlay);
            }
            state_.extensionPreviewOverlays_ = next;
            renderPreviewOverlays();
            return updated ? QJsonObject{{QStringLiteral("ok"), true}} : errorObject(QStringLiteral("Overlay not found: %1").arg(id));
        }
        if (method == QStringLiteral("preview/removeOverlay")) {
            const QString id = params.value(QStringLiteral("id")).toString();
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString());
            QJsonArray kept;
            int removed = 0;
            for (const QJsonValue& value : state_.extensionPreviewOverlays_) {
                const QJsonObject overlay = value.toObject();
                const bool idMatches = id.isEmpty() || overlay.value(QStringLiteral("id")).toString() == id;
                const bool ownerMatches = ownerId.isEmpty() || overlay.value(QStringLiteral("ownerId")).toString() == ownerId;
                if (idMatches && ownerMatches) {
                    ++removed;
                    continue;
                }
                kept.append(overlay);
            }
            state_.extensionPreviewOverlays_ = kept;
            renderPreviewOverlays();
            return okValue(QJsonObject{{QStringLiteral("removed"), removed}});
        }
        if (method == QStringLiteral("preview/clearOverlays")) {
            const QString ownerId = params.value(QStringLiteral("ownerId")).toString();
            if (ownerId.trimmed().isEmpty()) {
                state_.extensionPreviewOverlays_ = QJsonArray();
            } else {
                QJsonArray kept;
                for (const QJsonValue& value : state_.extensionPreviewOverlays_) {
                    if (value.toObject().value(QStringLiteral("ownerId")).toString() != ownerId) {
                        kept.append(value);
                    }
                }
                state_.extensionPreviewOverlays_ = kept;
            }
            renderPreviewOverlays();
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("preview/renderOverlayLayer")) {
            renderPreviewOverlays();
            return okValue(QJsonObject{{QStringLiteral("count"), state_.extensionPreviewOverlays_.size()}});
        }
        if (method == QStringLiteral("preview/hitTestOverlay")) {
            const QPoint point(params.value(QStringLiteral("x")).toInt(), params.value(QStringLiteral("y")).toInt());
            QJsonArray hits;
            for (const QPointer<QWidget>& widget : std::as_const(state_.extensionPreviewOverlayWidgets_)) {
                if (widget == nullptr) {
                    continue;
                }
                if (auto* directLabel = qobject_cast<QLabel*>(widget.data())) {
                    const QPoint directPoint = directLabel->mapFromParent(point);
                    if (directLabel->geometry().contains(point) || directLabel->rect().contains(directPoint)) {
                        const QString id = directLabel->property("miacode.extension.overlay.id").toString();
                        const QString ownerId = directLabel->property("miacode.extension.overlay.ownerId").toString();
                        for (const QJsonValue& value : state_.extensionPreviewOverlays_) {
                            const QJsonObject overlay = value.toObject();
                            if (overlay.value(QStringLiteral("id")).toString() == id
                                && overlay.value(QStringLiteral("ownerId")).toString() == ownerId) {
                                hits.append(overlay);
                            }
                        }
                    }
                    continue;
                }
                const QPoint hostPoint = widget->mapFromParent(point);
                const auto labels = widget->findChildren<QLabel*>(QStringLiteral("ExtensionPreviewOverlayLabel"));
                for (QLabel* label : labels) {
                    if (label == nullptr || !label->geometry().contains(hostPoint)) {
                        continue;
                    }
                    const QString id = label->property("miacode.extension.overlay.id").toString();
                    const QString ownerId = label->property("miacode.extension.overlay.ownerId").toString();
                    for (const QJsonValue& value : state_.extensionPreviewOverlays_) {
                        const QJsonObject overlay = value.toObject();
                        if (overlay.value(QStringLiteral("id")).toString() == id
                            && overlay.value(QStringLiteral("ownerId")).toString() == ownerId) {
                            hits.append(overlay);
                        }
                    }
                }
            }
            return okValue(hits);
        }
        if (method == QStringLiteral("media/getInfo")
            || method == QStringLiteral("media/info")
            || method == QStringLiteral("resources/getMediaInfo")) {
            const QString chartFolder = currentFilePath_.isEmpty() ? QString() : QFileInfo(currentFilePath_).absolutePath();
            const auto fileInfoObject = [](const QString& path) {
                const QFileInfo info(path);
                return QJsonObject{
                    {QStringLiteral("path"), path},
                    {QStringLiteral("exists"), info.exists()},
                    {QStringLiteral("size"), static_cast<double>(info.exists() ? info.size() : 0)},
                };
            };
            const QString resolvedTrackPath = miacode::chart_assets::resolveTrackPathForDirectory(chartFolder);
            return okValue(QJsonObject{
                {QStringLiteral("chartFolder"), chartFolder},
                {QStringLiteral("track"), fileInfoObject(resolvedTrackPath.isEmpty() && !chartFolder.isEmpty()
                    ? QDir(chartFolder).filePath(miacode::chart_assets::trackFileName())
                    : resolvedTrackPath)},
                {QStringLiteral("cover"), fileInfoObject(chartFolder.isEmpty() ? QString() : QDir(chartFolder).filePath(QStringLiteral("bg.jpg")))},
                {QStringLiteral("background"), fileInfoObject(chartFolder.isEmpty() ? QString() : QDir(chartFolder).filePath(QStringLiteral("bg.jpg")))},
                {QStringLiteral("durationSeconds"), previewTrackDurationSeconds_},
            });
        }
        if (method == QStringLiteral("media/list")) {
            QJsonArray files;
            if (!currentFilePath_.isEmpty()) {
                const QDir dir(QFileInfo(currentFilePath_).absolutePath());
                const QStringList filters{
                    QStringLiteral("*.mp3"),
                    QStringLiteral("*.wav"),
                    QStringLiteral("*.ogg"),
                    QStringLiteral("*.flac"),
                    QStringLiteral("*.jpg"),
                    QStringLiteral("*.jpeg"),
                    QStringLiteral("*.png"),
                    QStringLiteral("*.webp"),
                    QStringLiteral("*.mp4"),
                    QStringLiteral("*.mov"),
                    QStringLiteral("*.mkv"),
                };
                for (const QFileInfo& info : dir.entryInfoList(filters, QDir::Files, QDir::Name)) {
                    files.append(QJsonObject{
                        {QStringLiteral("name"), info.fileName()},
                        {QStringLiteral("path"), info.absoluteFilePath()},
                        {QStringLiteral("size"), static_cast<double>(info.size())},
                        {QStringLiteral("suffix"), info.suffix()},
                    });
                }
            }
            return okValue(files);
        }
        if (method == QStringLiteral("media/getAssetPath") || method == QStringLiteral("resources/getAssetPath")) {
            const QString id = params.value(QStringLiteral("id")).toString();
            if (id == QStringLiteral("cover")) {
                const QString chartFolder = currentFilePath_.isEmpty() ? QString() : QFileInfo(currentFilePath_).absolutePath();
                return okValue(chartFolder.isEmpty() ? QString() : QDir(chartFolder).filePath(QStringLiteral("bg.jpg")));
            }
            const QString chartFolder = currentFilePath_.isEmpty() ? QString() : QFileInfo(currentFilePath_).absolutePath();
            if (id == QStringLiteral("track")) {
                const QString resolvedTrackPath = miacode::chart_assets::resolveTrackPathForDirectory(chartFolder);
                return okValue(resolvedTrackPath.isEmpty() && !chartFolder.isEmpty()
                    ? QDir(chartFolder).filePath(miacode::chart_assets::trackFileName())
                    : resolvedTrackPath);
            }
            if (id == QStringLiteral("background")) {
                return okValue(chartFolder.isEmpty() ? QString() : QDir(chartFolder).filePath(QStringLiteral("bg.jpg")));
            }
            return errorObject(QStringLiteral("Unknown asset id: %1").arg(id));
        }
        if (method == QStringLiteral("resources/setAssetPath")) {
            const QString id = params.value(QStringLiteral("id")).toString();
            if (id == QStringLiteral("cover")) {
                const QString sourcePath = params.value(QStringLiteral("path")).toString().trimmed();
                const QString chartFolder = currentFilePath_.isEmpty() ? QString() : QFileInfo(currentFilePath_).absolutePath();
                if (sourcePath.isEmpty() || chartFolder.isEmpty()) {
                    return errorObject(QStringLiteral("Cover path and chart folder are required."));
                }
                const QString targetPath = QDir(chartFolder).filePath(QStringLiteral("bg.jpg"));
                if (QDir::cleanPath(sourcePath) == QDir::cleanPath(targetPath)) {
                    return QJsonObject{{QStringLiteral("ok"), true}};
                }
                if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
                    return errorObject(QStringLiteral("Failed to replace existing bg.jpg."));
                }
                if (!QFile::copy(sourcePath, targetPath)) {
                    return errorObject(QStringLiteral("Failed to copy cover to bg.jpg."));
                }
                return okValue(targetPath);
            }
            return errorObject(QStringLiteral("Only cover asset path can be changed directly."));
        }
        if (method == QStringLiteral("export/getPresets")) {
            QJsonArray presets{
                QJsonObject{{QStringLiteral("id"), QStringLiteral("high-quality")}, {QStringLiteral("label"), QStringLiteral("High Quality")}},
                QJsonObject{{QStringLiteral("id"), QStringLiteral("fast")}, {QStringLiteral("label"), QStringLiteral("Fast")}},
            };
            for (const QJsonObject& preset : std::as_const(state_.extensionExportPresets_)) {
                presets.append(preset);
            }
            return okValue(presets);
        }
        if (method == QStringLiteral("export/registerPreset")) {
            QJsonObject preset = params;
            if (!preset.contains(QStringLiteral("id"))) {
                return errorObject(QStringLiteral("Export preset requires an id."));
            }
            state_.extensionExportPresets_.append(preset);
            return okValue(preset);
        }
        if (method == QStringLiteral("export/startVideoExport")) {
            if (exportSection_ == nullptr) {
                return errorObject(QStringLiteral("Export section is not available."));
            }
            exportSection_->onExportPreviewVideo(resolveToolsMenuExportDifficultyId());
            return okValue(QJsonObject{{QStringLiteral("started"), true}, {QStringLiteral("mode"), QStringLiteral("video")}});
        }
        if (method == QStringLiteral("export/startCoverExport")) {
            onExportCover();
            return okValue(QJsonObject{{QStringLiteral("started"), true}, {QStringLiteral("mode"), QStringLiteral("cover")}});
        }
        if (method == QStringLiteral("contributions/register")) {
            const QString kind = params.value(QStringLiteral("kind")).toString(QStringLiteral("contribution"));
            QJsonObject contribution = registeredContribution(params, kind);
            contribution.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            state_.extensionRegistrationsByKind_[kind].append(contribution);
            if (kind.startsWith(QStringLiteral("export/"))) {
                state_.extensionExportHooks_.append(contribution);
            }
            if (kind == QStringLiteral("ui/sceneOverlay") || kind == QStringLiteral("ui/canvasOverlay")) {
                state_.extensionPreviewOverlays_.append(contribution);
                renderPreviewOverlays();
                return okValue(contribution);
            }
            if (kind.startsWith(QStringLiteral("ui/"))) {
                state_.extensionUiContributions_.append(contribution);
                renderUiContribution(contribution);
            }
            return okValue(contribution);
        }
        if (method == QStringLiteral("events/register")) {
            QJsonObject contribution = registeredContribution(params, params.value(QStringLiteral("kind")).toString(QStringLiteral("events/event")));
            contribution.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            state_.extensionRegistrationsByKind_[contribution.value(QStringLiteral("kind")).toString()].append(contribution);
            return okValue(contribution);
        }
        if (method == QStringLiteral("ui/getContributions")) {
            return okValue(state_.extensionUiContributions_);
        }
        if (method == QStringLiteral("extensions/getRuntimeRegistrations")) {
            QJsonArray registrations;
            for (auto it = state_.extensionRegistrationsByKind_.constBegin();
                 it != state_.extensionRegistrationsByKind_.constEnd();
                 ++it) {
                for (const QJsonValue& value : it.value()) {
                    registrations.append(value);
                }
            }
            return okValue(registrations);
        }
        if (method == QStringLiteral("ui/getViews")) {
            return okValue(renderedViewsArray());
        }
        if (method == QStringLiteral("ui/unregisterView")) {
            const int removed = unregisterExtensionView(
                params.value(QStringLiteral("id")).toString(),
                params.value(QStringLiteral("ownerId")).toString(params.value(QStringLiteral("extensionId")).toString()));
            return okValue(QJsonObject{{QStringLiteral("removed"), removed}});
        }
        if (method == QStringLiteral("ui/refreshViews")) {
            rebuildRenderedExtensionUi();
            return okValue(QJsonObject{{QStringLiteral("count"), renderedViewsArray().size()}});
        }
        if (method == QStringLiteral("ui/registerPetOverlay")) {
            QJsonObject overlay = registeredContribution(params, QStringLiteral("ui/petOverlay"));
            overlay.insert(QStringLiteral("kind"), QStringLiteral("ui/petOverlay"));
            overlay.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension"))));
            state_.extensionPreviewOverlays_.append(overlay);
            renderPreviewOverlays();
            return okValue(overlay);
        }
        if (method == QStringLiteral("ui/registerSceneOverlay")
            || method == QStringLiteral("ui/renderSceneOverlay")) {
            QJsonObject overlay = registeredContribution(params, QStringLiteral("ui/sceneOverlay"));
            overlay.insert(QStringLiteral("kind"), params.value(QStringLiteral("kind")).toString(QStringLiteral("ui/sceneOverlay")));
            overlay.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("ownerId")).toString(
                params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension"))));
            state_.extensionPreviewOverlays_.append(overlay);
            renderPreviewOverlays();
            return okValue(overlay);
        }
        if (method == QStringLiteral("ui/renderDeclarativeView")
            || method == QStringLiteral("ui/renderSidebarView")
            || method == QStringLiteral("ui/renderBottomTabView")
            || method == QStringLiteral("ui/renderPreferencesPage")
            || method == QStringLiteral("ui/renderFloatingPanel")
            || method == QStringLiteral("ui/renderWebView")
            || method == QStringLiteral("ui/renderCanvasView")
            || method == QStringLiteral("ui/renderToolbarButton")) {
            QString kind = params.value(QStringLiteral("kind")).toString();
            if (kind.isEmpty()) {
                if (method == QStringLiteral("ui/renderToolbarButton")) {
                    kind = QStringLiteral("ui/toolbarButton");
                } else if (method == QStringLiteral("ui/renderFloatingPanel")) {
                    kind = QStringLiteral("ui/floatingPanel");
                } else if (method == QStringLiteral("ui/renderWebView")) {
                    kind = QStringLiteral("ui/webView");
                } else if (method == QStringLiteral("ui/renderCanvasView")) {
                    kind = QStringLiteral("ui/canvasView");
                } else if (method == QStringLiteral("ui/renderSidebarView")) {
                    kind = QStringLiteral("ui/sidebarView");
                } else if (method == QStringLiteral("ui/renderPreferencesPage")) {
                    kind = QStringLiteral("ui/preferencesPage");
                } else {
                    kind = QStringLiteral("ui/bottomTabView");
                }
            }
            QJsonObject contribution = registeredContribution(params, kind);
            contribution.insert(QStringLiteral("kind"), kind);
            contribution.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            if ((kind == QStringLiteral("ui/webView") || kind == QStringLiteral("ui/canvasView"))
                && !contribution.contains(QStringLiteral("type"))) {
                contribution.insert(QStringLiteral("type"), kind == QStringLiteral("ui/webView") ? QStringLiteral("webview") : QStringLiteral("canvas"));
            }
            if (!renderUiContribution(contribution)) {
                return errorObject(QStringLiteral("UI host is not available for %1.").arg(kind));
            }
            state_.extensionUiContributions_.append(contribution);
            return okValue(contribution);
        }
        if (method == QStringLiteral("shortcuts/list")) {
            QJsonObject values;
            for (auto it = state_.extensionShortcutContributions_.constBegin();
                 it != state_.extensionShortcutContributions_.constEnd();
                 ++it) {
                const QJsonObject contribution = it.value().toObject();
                const QString shortcutId = contribution.value(QStringLiteral("shortcutId")).toString(it.key());
                values.insert(
                    contribution.value(QStringLiteral("command")).toString(shortcutId),
                    ShortcutRegistry::instance().sequence(shortcutId).toString(QKeySequence::PortableText));
            }
            return okValue(values);
        }
        if (method == QStringLiteral("shortcuts/getEditable")) {
            QJsonArray shortcuts;
            for (const ShortcutRegistry::ShortcutDefinition& definition : ShortcutRegistry::instance().editableShortcuts()) {
                QJsonArray sequences;
                for (const QKeySequence& sequence : ShortcutRegistry::instance().sequences(definition.id, definition.defaultSequences)) {
                    if (!sequence.isEmpty()) {
                        sequences.append(sequence.toString(QKeySequence::PortableText));
                    }
                }
                shortcuts.append(QJsonObject{
                    {QStringLiteral("id"), definition.id},
                    {QStringLiteral("label"), shortcutDefinitionDisplayName(definition)},
                    {QStringLiteral("keybindings"), sequences},
                });
            }
            return okValue(shortcuts);
        }
        if (method == QStringLiteral("shortcuts/getKeybinding")) {
            const QString command = params.value(QStringLiteral("command")).toString(params.value(QStringLiteral("id")).toString());
            const QString shortcutId = params.value(QStringLiteral("shortcutId")).toString(extensionShortcutIdForCommand(command));
            return okValue(ShortcutRegistry::instance().sequence(shortcutId).toString(QKeySequence::PortableText));
        }
        if (method == QStringLiteral("shortcuts/register")) {
            const QString command = params.value(QStringLiteral("command")).toString(params.value(QStringLiteral("id")).toString()).trimmed();
            if (command.isEmpty()) {
                return errorObject(QStringLiteral("shortcuts.register requires a command."));
            }
            const QString shortcutId = params.value(QStringLiteral("shortcutId")).toString(extensionShortcutIdForCommand(command));
            const QString label = params.value(QStringLiteral("label")).toString(params.value(QStringLiteral("title")).toString(command));
            const QString keybinding = params.value(QStringLiteral("keybinding")).toString(params.value(QStringLiteral("keys")).toString());
            const QList<QKeySequence> defaults = keybinding.trimmed().isEmpty()
                ? QList<QKeySequence>{}
                : QList<QKeySequence>{QKeySequence(keybinding)};
            ShortcutRegistry::instance().registerExtensionShortcut(shortcutId, label, defaults);

            QJsonObject contribution = params;
            contribution.insert(QStringLiteral("kind"), QStringLiteral("shortcuts/command"));
            contribution.insert(QStringLiteral("command"), command);
            contribution.insert(QStringLiteral("shortcutId"), shortcutId);
            contribution.insert(QStringLiteral("label"), label);
            contribution.insert(QStringLiteral("keybinding"), keybinding);
            contribution.insert(QStringLiteral("ownerId"), params.value(QStringLiteral("extensionId")).toString(QStringLiteral("extension")));
            state_.extensionShortcutContributions_.insert(shortcutId, contribution);
            rebuildExtensionShortcuts();
            return okValue(QJsonObject{
                {QStringLiteral("shortcutId"), shortcutId},
                {QStringLiteral("command"), command},
                {QStringLiteral("keybinding"), ShortcutRegistry::instance().sequence(shortcutId).toString(QKeySequence::PortableText)},
            });
        }
        if (method == QStringLiteral("shortcuts/reloadRegistered")) {
            rebuildExtensionShortcuts();
            return okValue(QJsonObject{{QStringLiteral("count"), state_.extensionShortcutObjects_.size()}});
        }
        if (method == QStringLiteral("tasks/withProgress") || method == QStringLiteral("tasks/reportProgress")) {
            const QString message = params.value(QStringLiteral("message")).toString(params.value(QStringLiteral("title")).toString());
            const int percent = params.value(QStringLiteral("percent")).toInt(-1);
            if (statusBar() != nullptr) {
                statusBar()->showMessage(percent >= 0
                                             ? QStringLiteral("%1 (%2%)").arg(message).arg(percent)
                                             : message,
                                         params.value(QStringLiteral("timeoutMs")).toInt(5000));
            }
            return okValue(QJsonObject{{QStringLiteral("taskId"), params.value(QStringLiteral("taskId")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces))}});
        }
        if (method == QStringLiteral("clipboard/readText")) {
            QClipboard* clipboard = QGuiApplication::clipboard();
            return okValue(clipboard != nullptr ? clipboard->text() : QString());
        }
        if (method == QStringLiteral("clipboard/writeText")) {
            QClipboard* clipboard = QGuiApplication::clipboard();
            if (clipboard == nullptr) {
                return errorObject(QStringLiteral("Clipboard is unavailable."));
            }
            clipboard->setText(params.value(QStringLiteral("text")).toString());
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("logs/append")) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                params.value(QStringLiteral("channel")).toString(QStringLiteral("extensions")),
                params.value(QStringLiteral("message")).toString());
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("logs/getPath")) {
            const QString channel = params.value(QStringLiteral("channel")).toString(QStringLiteral("extensions"));
            const QString root = extensionManager_ != nullptr ? extensionManager_->extensionLogDirectory() : QCoreApplication::applicationDirPath();
            return okValue(QDir(root).filePath(channel + QStringLiteral(".log")));
        }
        if (method == QStringLiteral("logs/open")) {
            const QString root = extensionManager_ != nullptr ? extensionManager_->extensionLogDirectory() : QCoreApplication::applicationDirPath();
            QDesktopServices::openUrl(QUrl::fromLocalFile(root));
            return QJsonObject{{QStringLiteral("ok"), true}};
        }
        if (method == QStringLiteral("logs/readRecent")) {
            const QString channel = params.value(QStringLiteral("channel")).toString(QStringLiteral("extensions"));
            const QString root = extensionManager_ != nullptr ? extensionManager_->extensionLogDirectory() : QCoreApplication::applicationDirPath();
            QFile file(QDir(root).filePath(channel + QStringLiteral(".log")));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return errorObject(QStringLiteral("Cannot read log: %1").arg(file.errorString()));
            }
            const int maxBytes = qBound(1024, params.value(QStringLiteral("maxBytes")).toInt(65536), 1024 * 1024);
            if (file.size() > maxBytes) {
                file.seek(file.size() - maxBytes);
            }
            return okValue(QString::fromUtf8(file.readAll()));
        }
        return QJsonObject();

}
