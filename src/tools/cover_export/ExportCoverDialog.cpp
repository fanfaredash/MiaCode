#include "tools/cover_export/ExportCoverDialog.h"

#include "UiText.h"
#include "UiTheme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

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

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

}  // namespace

ExportCoverDialog::ExportCoverDialog(const QSize& initialSize, QWidget* parent)
    : QDialog(parent)
    , selectedSize_(initialSize.isValid() ? initialSize : QSize(1080, 1080))
{
    setWindowTitle(l10n(QStringLiteral("Export Cover"), QStringLiteral("导出封面")));
    setModal(true);
    setStyleSheet(UiTheme::exportDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(12);

    // Size row.
    auto* sizeRow = new QWidget(this);
    auto* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(10);
    auto* sizeLabel = new QLabel(l10n(QStringLiteral("Size"), QStringLiteral("尺寸")), sizeRow);
    sizeCombo_ = new QComboBox(sizeRow);
    int selectedIndex = -1;
    for (const CoverResolutionPreset& preset : kCoverResolutionPresets) {
        const QSize size(preset.width, preset.height);
        sizeCombo_->addItem(QString::fromLatin1(preset.label), size);
        if (size == selectedSize_) {
            selectedIndex = sizeCombo_->count() - 1;
        }
    }
    if (selectedIndex < 0) {
        // The seeded video size has no matching preset — surface it as its own
        // entry so the cover defaults to the same size as the video export.
        sizeCombo_->addItem(
            QStringLiteral("%1x%2").arg(selectedSize_.width()).arg(selectedSize_.height()),
            selectedSize_
        );
        selectedIndex = sizeCombo_->count() - 1;
    }
    sizeCombo_->setCurrentIndex(selectedIndex);
    sizeLayout->addWidget(sizeLabel, 0);
    sizeLayout->addWidget(sizeCombo_, 1);
    rootLayout->addWidget(sizeRow, 0);

    // Transparent-background toggle.
    transparentCheck_ = new QCheckBox(
        l10n(QStringLiteral("Transparent background"), QStringLiteral("透明背景")),
        this
    );
    transparentCheck_->setChecked(false);
    rootLayout->addWidget(transparentCheck_, 0);

    auto* hint = new QLabel(
        l10n(
            QStringLiteral("Exports the difficulty card to card.jpg (card.png when transparent)."),
            QStringLiteral("将难度卡导出为 card.jpg（勾选透明背景时为 card.png）。")
        ),
        this
    );
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(UiTheme::colors().textSecondary.name(QColor::HexRgb)));
    rootLayout->addWidget(hint, 0);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* exportButton = buttonBox->addButton(
        l10n(QStringLiteral("Export"), QStringLiteral("导出")),
        QDialogButtonBox::AcceptRole
    );
    exportButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    if (QPushButton* cancelButton = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelButton->setText(l10n(QStringLiteral("Cancel"), QStringLiteral("取消")));
        cancelButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ExportCoverDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox, 0);
}

void ExportCoverDialog::accept()
{
    if (sizeCombo_ != nullptr) {
        const QVariant data = sizeCombo_->currentData();
        if (data.canConvert<QSize>()) {
            selectedSize_ = data.toSize();
        }
    }
    transparentBackground_ = transparentCheck_ != nullptr && transparentCheck_->isChecked();
    QDialog::accept();
}
