#include "tools/cover_export/ExportCoverDialog.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "UiText.h"
#include "UiTheme.h"

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
#include <QPushButton>
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

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
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

ExportCoverDialog::ExportCoverDialog(const IntroBannerSpec& banner, const QSize& initialSize, QWidget* parent)
    : QDialog(parent)
    , banner_(banner)
{
    setWindowTitle(l10n(QStringLiteral("Export Cover"), QStringLiteral("导出封面")));
    setModal(true);
    setStyleSheet(UiTheme::exportDialogStyleSheet());

    const QSize seededSize = initialSize.isValid() ? initialSize : QSize(1080, 1080);

    model_ = new miacode::cover_export::CoverLayoutModel(this);
    composerView_ = new miacode::cover_export::CoverComposerView(model_, this);
    cachedTemplate_ = loadBannerTemplate();

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
        });
    }

    syncControlEnabled();
    resizePreviewToAspect();
    pushInputs();
}

ExportCoverDialog::~ExportCoverDialog() = default;

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
