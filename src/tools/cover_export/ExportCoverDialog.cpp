#include "tools/cover_export/ExportCoverDialog.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/SceneFrameRenderer.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantMap>

namespace {

struct CoverResolutionPreset {
    int width;
    int height;
    const char* label;
};

// Mirrors VideoExportDialog's resolution presets so the cover size matches the
// video-size choices (tracked independently here).
constexpr CoverResolutionPreset kCoverResolutionPresets[] = {
    {720, 720, "720x720 (1:1)"},
    {1024, 1024, "1024x1024 (1:1)"},
    {960, 720, "960x720 (4:3)"},
    {1280, 720, "1280x720 (16:9)"},
    {1080, 1080, "1080x1080 (1:1)"},
    {1440, 1080, "1440x1080 (4:3)"},
    {1920, 1080, "1920x1080 (16:9)"},
    {1440, 1440, "1440x1440 (1:1)"},
    {1920, 1440, "1920x1440 (4:3)"},
    {2560, 1440, "2560x1440 (16:9)"},
};

// Longest side of the live-preview pane in logical px; the preview letterboxes
// to the chosen output aspect so the layout matches the export exactly.
constexpr int kPreviewBox = 400;

// Debounce for chart-frame re-grabs while dragging the slider / scale handle.
constexpr int kChartFrameDebounceMs = 70;

// Chart-frame square render-size clamp (px); the natural size is the layer's
// export pixel height, but keep it sane for tiny / huge canvases.
constexpr int kChartFrameMinPx = 384;
constexpr int kChartFrameMaxPx = 2048;

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

// mm:ss.cs (centiseconds) for the frame-time readout.
QString formatFrameTime(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int totalCs = qRound(seconds * 100.0);
    const int cs = totalCs % 100;
    const int totalSec = totalCs / 100;
    const int s = totalSec % 60;
    const int m = totalSec / 60;
    return QStringLiteral("%1:%2.%3")
        .arg(m)
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(cs, 2, 10, QLatin1Char('0'));
}

QVariantMap loadBannerTemplate()
{
    QVariantMap templateMap;
    QFile templateFile(QStringLiteral(":/intro/templates/maimai_banner.json"));
    if (templateFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(templateFile.readAll());
        if (doc.isObject()) {
            templateMap = doc.object().toVariantMap();
        }
    }
    return templateMap;
}

}  // namespace

ExportCoverDialog::ExportCoverDialog(const VideoExportTask& task, const QSize& initialSize, QWidget* parent)
    : QDialog(parent)
    , banner_(task.intro)
{
    setWindowTitle(l10n(QStringLiteral("Export Cover"), QStringLiteral("导出封面")));
    setModal(true);
    setStyleSheet(UiTheme::exportDialogStyleSheet());

    const QSize seededSize = initialSize.isValid() ? initialSize : QSize(1080, 1080);

    model_ = new miacode::cover_export::CoverLayoutModel(this);
    composerView_ = new miacode::cover_export::CoverComposerView(model_, this);
    cachedTemplate_ = loadBannerTemplate();

    // Bootstrap the in-process chart-frame renderer once from the export task.
    // Unavailable (→ the toggle stays disabled) when the difficulty has no notes
    // or the skin can't load.
    contentDurationSeconds_ = qMax(0.0, task.contentDurationSeconds);
    sceneFrameRenderer_ = std::make_unique<miacode::cover_export::SceneFrameRenderer>();
    if (!task.noteMarkers.isEmpty()) {
        chartFrameAvailable_ = sceneFrameRenderer_->bootstrap(task);
    }

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(16);

    // ---- Left: live composer preview ----
    auto* previewColumn = new QVBoxLayout();
    previewColumn->setSpacing(8);

    auto* previewFrame = new QFrame(this);
    previewFrame->setFrameShape(QFrame::StyledPanel);
    previewFrame->setStyleSheet(QStringLiteral(
        "QFrame { background: #14141C; border: 1px solid rgba(255,255,255,40); border-radius: 6px; }"));
    auto* previewFrameLayout = new QVBoxLayout(previewFrame);
    previewFrameLayout->setContentsMargins(1, 1, 1, 1);

    QWidget* container = composerView_->createContainer(previewFrame);
    if (container != nullptr) {
        previewContainer_ = container;
        previewFrameLayout->addWidget(previewContainer_);
    } else {
        auto* failed = new QLabel(
            l10n(QStringLiteral("Failed to start the composer:\n%1"),
                 QStringLiteral("合成器启动失败：\n%1")).arg(composerView_->lastError()),
            previewFrame);
        failed->setWordWrap(true);
        previewFrameLayout->addWidget(failed);
    }
    previewColumn->addWidget(previewFrame, 0, Qt::AlignTop);

    auto* previewHint = new QLabel(
        l10n(QStringLiteral("Drag the card to place it; drag the corner to resize. Lines snap to centre/edges."),
             QStringLiteral("拖动卡片摆放，拖角缩放；松手会吸附到中线/边缘。")),
        this);
    previewHint->setWordWrap(true);
    previewHint->setStyleSheet(QStringLiteral("color: %1;")
        .arg(UiTheme::colors().textSecondary.name(QColor::HexRgb)));
    previewColumn->addWidget(previewHint, 0, Qt::AlignHCenter);

    resetLayoutButton_ = new QPushButton(
        l10n(QStringLiteral("Reset layout"), QStringLiteral("重置布局")), this);
    resetLayoutButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    previewColumn->addWidget(resetLayoutButton_, 0, Qt::AlignHCenter);

    previewColumn->addStretch(1);
    rootLayout->addLayout(previewColumn, 0);

    // ---- Right: controls ----
    auto* controlsColumn = new QVBoxLayout();
    controlsColumn->setSpacing(10);
    auto* form = new QFormLayout();
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Size.
    sizeCombo_ = new QComboBox(this);
    int selectedIndex = -1;
    for (const CoverResolutionPreset& preset : kCoverResolutionPresets) {
        const QSize size(preset.width, preset.height);
        sizeCombo_->addItem(QString::fromLatin1(preset.label), size);
        if (size == seededSize) {
            selectedIndex = sizeCombo_->count() - 1;
        }
    }
    if (selectedIndex < 0) {
        sizeCombo_->addItem(
            QStringLiteral("%1x%2").arg(seededSize.width()).arg(seededSize.height()), seededSize);
        selectedIndex = sizeCombo_->count() - 1;
    }
    sizeCombo_->setCurrentIndex(selectedIndex);
    form->addRow(l10n(QStringLiteral("Size"), QStringLiteral("尺寸")), sizeCombo_);

    // Background source.
    backgroundCombo_ = new QComboBox(this);
    backgroundCombo_->addItem(l10n(QStringLiteral("Chart jacket (曲绘)"), QStringLiteral("曲绘")),
                              QStringLiteral("jacket"));
    backgroundCombo_->addItem(l10n(QStringLiteral("Custom image"), QStringLiteral("自定义图片")),
                              QStringLiteral("custom"));
    backgroundCombo_->addItem(l10n(QStringLiteral("Transparent"), QStringLiteral("透明")),
                              QStringLiteral("transparent"));
    form->addRow(l10n(QStringLiteral("Background"), QStringLiteral("背景")), backgroundCombo_);

    // Custom background path + browse.
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    backgroundPathEdit_ = new QLineEdit(pathRow);
    backgroundPathEdit_->setPlaceholderText(
        l10n(QStringLiteral("Custom background image path"), QStringLiteral("自定义背景图片路径")));
    backgroundBrowse_ = new QPushButton(l10n(QStringLiteral("Browse…"), QStringLiteral("浏览…")), pathRow);
    backgroundBrowse_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    pathLayout->addWidget(backgroundPathEdit_, 1);
    pathLayout->addWidget(backgroundBrowse_, 0);
    form->addRow(QString(), pathRow);

    // Backdrop blur.
    blurCheck_ = new QCheckBox(l10n(QStringLiteral("Blur background"), QStringLiteral("背景虚化")), this);
    blurCheck_->setChecked(true);
    form->addRow(QString(), blurCheck_);

    // Card drop shadow.
    cardShadowCheck_ = new QCheckBox(l10n(QStringLiteral("Card drop shadow"), QStringLiteral("难度卡阴影")), this);
    cardShadowCheck_->setChecked(false);
    form->addRow(QString(), cardShadowCheck_);

    // Level text render.
    levelTextRenderCheck_ = new QCheckBox(
        l10n(QStringLiteral("Render level as text"), QStringLiteral("等级文本渲染")), this);
    levelTextRenderCheck_->setChecked(false);
    form->addRow(QString(), levelTextRenderCheck_);

    // Long-text overflow.
    textOverflowCombo_ = new QComboBox(this);
    textOverflowCombo_->addItem(
        l10n(QStringLiteral("Shrink to fit"), QStringLiteral("缩小字体以放入全部")), QStringLiteral("shrink"));
    textOverflowCombo_->addItem(
        l10n(QStringLiteral("Keep size, ellipsis (…)"), QStringLiteral("保持字号，省略号(…)截断")),
        QStringLiteral("ellipsis"));
    form->addRow(l10n(QStringLiteral("Long text"), QStringLiteral("文字超长")), textOverflowCombo_);

    // Chart frame (a square playfield grab at a picked time, added as a layer).
    chartFrameCheck_ = new QCheckBox(
        l10n(QStringLiteral("Add chart frame"), QStringLiteral("添加谱面帧")), this);
    chartFrameCheck_->setChecked(false);
    chartFrameCheck_->setEnabled(chartFrameAvailable_);
    if (!chartFrameAvailable_) {
        chartFrameCheck_->setToolTip(
            l10n(QStringLiteral("This difficulty has no chart notes to render."),
                 QStringLiteral("当前难度没有可渲染的谱面音符。")));
    }
    form->addRow(QString(), chartFrameCheck_);

    // Frame-time picker (slider over the chart content + a mm:ss.cs readout).
    auto* frameRow = new QWidget(this);
    auto* frameLayout = new QHBoxLayout(frameRow);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(8);
    frameSlider_ = new QSlider(Qt::Horizontal, frameRow);
    frameSlider_->setMinimum(0);
    frameSlider_->setMaximum(qMax(1, qRound(contentDurationSeconds_ * 1000.0)));
    frameSlider_->setValue(0);
    frameSlider_->setEnabled(false);
    frameTimeLabel_ = new QLabel(formatFrameTime(0.0), frameRow);
    frameTimeLabel_->setMinimumWidth(56);
    frameTimeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    frameLayout->addWidget(frameSlider_, 1);
    frameLayout->addWidget(frameTimeLabel_, 0);
    form->addRow(l10n(QStringLiteral("Frame time"), QStringLiteral("谱面时间")), frameRow);

    frameDebounce_ = new QTimer(this);
    frameDebounce_->setSingleShot(true);
    frameDebounce_->setInterval(kChartFrameDebounceMs);
    connect(frameDebounce_, &QTimer::timeout, this, &ExportCoverDialog::renderChartFrameNow);

    controlsColumn->addLayout(form);
    controlsColumn->addStretch(1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* exportButton = buttonBox->addButton(
        l10n(QStringLiteral("Export"), QStringLiteral("导出")), QDialogButtonBox::AcceptRole);
    exportButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    if (QPushButton* cancelButton = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelButton->setText(l10n(QStringLiteral("Cancel"), QStringLiteral("取消")));
        cancelButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    controlsColumn->addWidget(buttonBox, 0);

    rootLayout->addLayout(controlsColumn, 1);

    // ---- Wiring ----
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        resizePreviewToAspect();
        pushInputs();
        // The chart-frame render resolution tracks the output height.
        if (chartFrameCheck_ != nullptr && chartFrameCheck_->isChecked()) {
            scheduleChartFrameRender();
        }
    });
    connect(backgroundCombo_, &QComboBox::currentIndexChanged, this, [this] {
        syncControlEnabled();
        pushInputs();
    });
    connect(backgroundPathEdit_, &QLineEdit::textChanged, this, [this] { pushInputs(); });
    connect(backgroundBrowse_, &QPushButton::clicked, this, &ExportCoverDialog::browseBackground);
    connect(blurCheck_, &QCheckBox::toggled, this, [this] { pushInputs(); });
    connect(cardShadowCheck_, &QCheckBox::toggled, this, [this] { pushInputs(); });
    connect(levelTextRenderCheck_, &QCheckBox::toggled, this, [this] { pushInputs(); });
    connect(textOverflowCombo_, &QComboBox::currentIndexChanged, this, [this] { pushInputs(); });
    if (resetLayoutButton_ != nullptr) {
        connect(resetLayoutButton_, &QPushButton::clicked, this, [this] {
            if (model_ != nullptr) model_->resetLayout();
            // resetLayout hides the chart frame; keep the toggle in sync.
            if (chartFrameCheck_ != nullptr) chartFrameCheck_->setChecked(false);
        });
    }

    connect(chartFrameCheck_, &QCheckBox::toggled, this, &ExportCoverDialog::onChartFrameToggled);
    connect(frameSlider_, &QSlider::valueChanged, this, [this](int valueMs) {
        if (frameTimeLabel_ != nullptr) {
            frameTimeLabel_->setText(formatFrameTime(valueMs / 1000.0));
        }
        if (chartFrameCheck_ != nullptr && chartFrameCheck_->isChecked()) {
            scheduleChartFrameRender();
        }
    });
    // Re-grab at the new resolution when the output size changes, and re-grab at a
    // crisper resolution when the chart-frame layer is scaled up.
    if (miacode::cover_export::CoverLayer* chartFrameLayer =
            model_ != nullptr ? model_->layer(QStringLiteral("chartFrame")) : nullptr) {
        connect(chartFrameLayer, &miacode::cover_export::CoverLayer::sizeFractionChanged, this, [this] {
            if (chartFrameCheck_ != nullptr && chartFrameCheck_->isChecked()) {
                scheduleChartFrameRender();
            }
        });
    }

    syncControlEnabled();
    resizePreviewToAspect();
    pushInputs();
}

ExportCoverDialog::~ExportCoverDialog() = default;

void ExportCoverDialog::onChartFrameToggled(bool on)
{
    if (model_ != nullptr) {
        model_->setChartFrameEnabled(on);
    }
    if (frameSlider_ != nullptr) {
        frameSlider_->setEnabled(on && chartFrameAvailable_);
    }
    if (!on) {
        return;
    }
    // Immediate first grab; later scrubs are debounced. The first grab cold-settles
    // the offscreen scene (~0.3 s), so flag the GUI as busy. If it fails (e.g. the
    // offscreen RHI couldn't render), don't leave the layer enabled-but-blank — it
    // would silently drop from the export — so revert the toggle and tell the user.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool produced = renderChartFrameNow();
    QApplication::restoreOverrideCursor();
    if (!produced) {
        const QSignalBlocker block(chartFrameCheck_);   // avoid recursive toggled()
        chartFrameCheck_->setChecked(false);
        if (model_ != nullptr) {
            model_->setChartFrameEnabled(false);
        }
        if (frameSlider_ != nullptr) {
            frameSlider_->setEnabled(false);
        }
        QMessageBox::warning(
            this,
            l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧")),
            l10n(QStringLiteral("Could not render the chart frame."),
                 QStringLiteral("无法渲染谱面帧。")));
    }
}

void ExportCoverDialog::scheduleChartFrameRender()
{
    if (frameDebounce_ != nullptr) {
        frameDebounce_->start();
    }
}

int ExportCoverDialog::chartFrameRenderPx() const
{
    qreal sizeFraction = 0.82;
    if (model_ != nullptr) {
        if (miacode::cover_export::CoverLayer* layer = model_->layer(QStringLiteral("chartFrame"))) {
            sizeFraction = layer->sizeFraction();
        }
    }
    const int outputHeight = qMax(1, currentSize().height());
    return qBound(kChartFrameMinPx, qRound(sizeFraction * outputHeight), kChartFrameMaxPx);
}

bool ExportCoverDialog::renderChartFrameNow()
{
    // renderAt() pumps the event loop (settle), which can re-deliver the debounce
    // mid-grab. Guard against re-entrancy; re-arm so the latest request still runs
    // once the in-flight grab finishes.
    if (rendering_) {
        scheduleChartFrameRender();
        return false;
    }
    if (!chartFrameAvailable_ || sceneFrameRenderer_ == nullptr || model_ == nullptr
        || !model_->chartFrameEnabled()) {
        return false;   // nothing to do (e.g. a debounce that survived a reset/disable)
    }
    rendering_ = true;
    const double seconds = frameSlider_ != nullptr ? frameSlider_->value() / 1000.0 : 0.0;
    QString error;
    const QImage frame = sceneFrameRenderer_->renderAt(seconds, chartFrameRenderPx(), &error);
    bool produced = false;
    if (!frame.isNull()) {
        if (miacode::cover_export::CoverLayer* layer = model_->layer(QStringLiteral("chartFrame"))) {
            layer->setFrameSeconds(seconds);
        }
        model_->setLayerImage(QStringLiteral("chartFrame"), frame);
        produced = true;
    }
    // else: leave the previous still (if any) in place rather than blanking the layer.
    rendering_ = false;
    return produced;
}

QSize ExportCoverDialog::currentSize() const
{
    const QVariant data = sizeCombo_ != nullptr ? sizeCombo_->currentData() : QVariant();
    return data.canConvert<QSize>() ? data.toSize() : QSize(1080, 1080);
}

miacode::cover_export::CoverComposerInputs ExportCoverDialog::buildInputs() const
{
    using miacode::cover_export::CoverBackgroundMode;
    miacode::cover_export::CoverComposerInputs in;
    in.templateMap = cachedTemplate_;

    QVariantMap track;
    track.insert(QStringLiteral("title"), banner_.title);
    track.insert(QStringLiteral("artist"), banner_.artist);
    track.insert(QStringLiteral("designer"), banner_.designer);
    track.insert(QStringLiteral("level"), banner_.level);
    track.insert(QStringLiteral("difficulty"), banner_.difficulty);
    track.insert(QStringLiteral("bpm"), banner_.bpm);
    track.insert(QStringLiteral("mode"), banner_.mode);
    track.insert(QStringLiteral("lvRenderMode"),
                 (levelTextRenderCheck_ != nullptr && levelTextRenderCheck_->isChecked())
                     ? QStringLiteral("text")
                     : QStringLiteral("atlas"));
    track.insert(QStringLiteral("stillTextMode"),
                 textOverflowCombo_ != nullptr ? textOverflowCombo_->currentData().toString()
                                               : QStringLiteral("shrink"));
    in.trackOverrides = track;

    in.jacketPath = banner_.jacketPath;

    const QString bg = backgroundCombo_ != nullptr ? backgroundCombo_->currentData().toString()
                                                   : QStringLiteral("jacket");
    if (bg == QStringLiteral("custom")) {
        in.backgroundMode = CoverBackgroundMode::Custom;
        in.backgroundPath = backgroundPathEdit_ != nullptr ? backgroundPathEdit_->text().trimmed() : QString();
    } else if (bg == QStringLiteral("transparent")) {
        in.backgroundMode = CoverBackgroundMode::Transparent;
    } else {
        in.backgroundMode = CoverBackgroundMode::Jacket;
    }

    in.blurBackground = blurCheck_ != nullptr && blurCheck_->isChecked();
    in.cardShadow = cardShadowCheck_ != nullptr && cardShadowCheck_->isChecked();
    return in;
}

void ExportCoverDialog::pushInputs()
{
    if (composerView_ != nullptr) {
        composerView_->setInputs(buildInputs());
    }
}

void ExportCoverDialog::resizePreviewToAspect()
{
    if (previewContainer_ == nullptr) {
        return;
    }
    const QSize sz = currentSize();
    int pw = kPreviewBox;
    int ph = kPreviewBox;
    if (sz.width() >= sz.height()) {
        pw = kPreviewBox;
        ph = qMax(1, qRound(static_cast<double>(pw) * sz.height() / sz.width()));
    } else {
        ph = kPreviewBox;
        pw = qMax(1, qRound(static_cast<double>(ph) * sz.width() / sz.height()));
    }
    previewContainer_->setFixedSize(pw, ph);
}

miacode::cover_export::CoverExportResult ExportCoverDialog::exportCover(const QString& outputDirectory)
{
    // Re-grab the chart frame at the exact export resolution (chartFrameRenderPx
    // tracks currentSize().height(), which the composite scales the layer to), so
    // the still is crisp in the export and any pending debounced grab is flushed.
    if (model_ != nullptr && model_->chartFrameEnabled() && chartFrameAvailable_) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        renderChartFrameNow();
        QApplication::restoreOverrideCursor();
        // Defensive: the toggle-revert keeps an enabled frame from ever sitting
        // image-less, but the export is the irreversible output — if somehow it
        // still has no still, tell the user rather than silently ship a cover
        // missing the frame they enabled.
        const miacode::cover_export::CoverLayer* chartFrameLayer =
            model_->layer(QStringLiteral("chartFrame"));
        if (chartFrameLayer != nullptr && chartFrameLayer->imageRevision() < 0) {
            QMessageBox::warning(
                this,
                l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧")),
                l10n(QStringLiteral("The chart frame could not be rendered; the cover will not include it."),
                     QStringLiteral("谱面帧无法渲染，封面将不包含它。")));
        }
    }
    return miacode::cover_export::exportCoverComposite(
        model_, buildInputs(), currentSize(), outputDirectory);
}

void ExportCoverDialog::syncControlEnabled()
{
    const QString bg = backgroundCombo_ != nullptr ? backgroundCombo_->currentData().toString()
                                                   : QStringLiteral("jacket");
    const bool isCustom = (bg == QStringLiteral("custom"));
    const bool isTransparent = (bg == QStringLiteral("transparent"));
    if (backgroundPathEdit_ != nullptr) backgroundPathEdit_->setEnabled(isCustom);
    if (backgroundBrowse_ != nullptr) backgroundBrowse_->setEnabled(isCustom);
    // Blur only applies when there is a backdrop behind the card. The card drop
    // shadow works in EVERY mode (in Transparent it casts a soft shadow onto the
    // alpha PNG), so it stays enabled regardless of the background source.
    if (blurCheck_ != nullptr) blurCheck_->setEnabled(!isTransparent);
    if (cardShadowCheck_ != nullptr) cardShadowCheck_->setEnabled(true);
}

void ExportCoverDialog::browseBackground()
{
    const QString start = backgroundPathEdit_ != nullptr && !backgroundPathEdit_->text().trimmed().isEmpty()
        ? QFileInfo(backgroundPathEdit_->text().trimmed()).absolutePath()
        : QString();
    const QString file = QFileDialog::getOpenFileName(
        this,
        l10n(QStringLiteral("Choose background image"), QStringLiteral("选择背景图片")),
        start,
        l10n(QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"),
             QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp)")));
    if (file.isEmpty()) {
        return;
    }
    if (backgroundCombo_ != nullptr) {
        const int customIndex = backgroundCombo_->findData(QStringLiteral("custom"));
        if (customIndex >= 0) backgroundCombo_->setCurrentIndex(customIndex);
    }
    if (backgroundPathEdit_ != nullptr) {
        backgroundPathEdit_->setText(file);
    }
}
