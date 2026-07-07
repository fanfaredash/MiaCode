#include "MainWindow.PreferencesSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/OperationLog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

// Self-contained schematic that mirrors the live workspace split so the
// first-run dialog can preview the "preview pane on which side" choice
// even when the real window is hidden behind the modal. It paints two
// rounded panes — a wider editor pane and a narrower preview pane — in
// the selected order, using the *currently applied* theme colors (which
// the welcome dialog switches live), so it doubles as a theme preview.
class WelcomeLayoutPreview final : public QWidget {
public:
    explicit WelcomeLayoutPreview(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(320, 132);
    }

    void setPreviewOnLeft(bool previewOnLeft)
    {
        if (previewOnLeft_ == previewOnLeft) {
            return;
        }
        previewOnLeft_ = previewOnLeft;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const UiTheme::Colors& colors = UiTheme::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(colors.border, 1.0));
        painter.setBrush(colors.windowBg);
        painter.drawRoundedRect(frame, 10.0, 10.0);

        const qreal margin = 12.0;
        const qreal gap = 8.0;
        const QRectF inner = frame.adjusted(margin, margin, -margin, -margin);
        const qreal previewWidth = inner.width() * 0.38;
        const qreal editorWidth = inner.width() - previewWidth - gap;

        QRectF editorRect;
        QRectF previewRect;
        if (previewOnLeft_) {
            previewRect = QRectF(inner.left(), inner.top(), previewWidth, inner.height());
            editorRect = QRectF(previewRect.right() + gap, inner.top(), editorWidth, inner.height());
        } else {
            editorRect = QRectF(inner.left(), inner.top(), editorWidth, inner.height());
            previewRect = QRectF(editorRect.right() + gap, inner.top(), previewWidth, inner.height());
        }

        drawPane(painter, editorRect, colors.cardBg, colors.border, colors.textSecondary,
                 UiText::text(QStringLiteral("dialog.welcome.preview.editor")), false);
        // textSecondary (not accentText) so the label stays readable — the
        // pane fill is panelBg, on which a white accentText would vanish.
        drawPane(painter, previewRect, colors.panelBg, colors.accent, colors.textSecondary,
                 UiText::text(QStringLiteral("dialog.welcome.preview.preview")), true);
    }

private:
    void drawPane(QPainter& painter,
                  const QRectF& pane,
                  const QColor& fill,
                  const QColor& border,
                  const QColor& textColor,
                  const QString& label,
                  bool accentBadge)
    {
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(pane, 7.0, 7.0);

        if (accentBadge) {
            // A small accent disc to read unmistakably as "the preview".
            const qreal radius = qMin(pane.width(), pane.height()) * 0.18;
            const QPointF center(pane.center().x(), pane.top() + pane.height() * 0.4);
            painter.setPen(Qt::NoPen);
            painter.setBrush(UiTheme::colors().accent);
            painter.drawEllipse(center, radius, radius);
        }

        QFont labelFont = font();
        labelFont.setPointSizeF(labelFont.pointSizeF() - 0.5);
        painter.setFont(labelFont);
        painter.setPen(textColor);
        const QRectF labelRect(pane.left(), pane.bottom() - 22.0, pane.width(), 18.0);
        painter.drawText(labelRect, Qt::AlignCenter, label);
    }

    bool previewOnLeft_ = false;
};

}  // namespace

void MainWindow::PreferencesSection::showWelcomeDialog()
{
    MC_OP("MainWindow::PreferencesSection::showWelcomeDialog");

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(UiText::text(QStringLiteral("dialog.welcome.title")));
    dialog.setModal(true);
    dialog.setMinimumWidth(460);
    dialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);
    root->setSizeConstraint(QLayout::SetFixedSize);

    auto* heading = new QLabel(UiText::text(QStringLiteral("dialog.welcome.heading")), &dialog);
    heading->setFont(uiAccentFont(15, QFont::DemiBold));
    root->addWidget(heading);

    auto* subtitle = new QLabel(
        UiText::text(QStringLiteral("dialog.welcome.subtitle")),
        &dialog);
    subtitle->setWordWrap(true);
    root->addWidget(subtitle);

    auto* schematic = new WelcomeLayoutPreview(&dialog);
    schematic->setPreviewOnLeft(owner_.workspacePanelsSwapped_);
    root->addWidget(schematic, 0, Qt::AlignHCenter);

    // --- Preview-side group -------------------------------------------------
    auto* sideLabel = new QLabel(UiText::text(QStringLiteral("dialog.welcome.preview_side")), &dialog);
    sideLabel->setFont(uiAccentFont(10, QFont::DemiBold));
    root->addWidget(sideLabel);

    auto* sideRow = new QWidget(&dialog);
    auto* sideRowLayout = new QHBoxLayout(sideRow);
    sideRowLayout->setContentsMargins(0, 0, 0, 0);
    sideRowLayout->setSpacing(18);
    auto* previewRightRadio = new QRadioButton(
        UiText::text(QStringLiteral("dialog.welcome.preview_side.right")), sideRow);
    auto* previewLeftRadio = new QRadioButton(
        UiText::text(QStringLiteral("dialog.welcome.preview_side.left")), sideRow);
    auto* sideGroup = new QButtonGroup(&dialog);
    sideGroup->addButton(previewRightRadio);
    sideGroup->addButton(previewLeftRadio);
    (owner_.workspacePanelsSwapped_ ? previewLeftRadio : previewRightRadio)->setChecked(true);
    sideRowLayout->addWidget(previewRightRadio, 0);
    sideRowLayout->addWidget(previewLeftRadio, 0);
    sideRowLayout->addStretch(1);
    root->addWidget(sideRow);

    const auto applyPreviewSide = [this, schematic](bool previewOnLeft) {
        schematic->setPreviewOnLeft(previewOnLeft);
        owner_.setWorkspacePanelsSwapped(previewOnLeft, true);
    };
    QObject::connect(previewLeftRadio, &QRadioButton::toggled, &dialog, [applyPreviewSide](bool checked) {
        if (checked) {
            applyPreviewSide(true);
        }
    });
    QObject::connect(previewRightRadio, &QRadioButton::toggled, &dialog, [applyPreviewSide](bool checked) {
        if (checked) {
            applyPreviewSide(false);
        }
    });

    // --- Theme group --------------------------------------------------------
    auto* themeHeading = new QLabel(UiText::text(QStringLiteral("dialog.welcome.theme")), &dialog);
    themeHeading->setFont(uiAccentFont(10, QFont::DemiBold));
    root->addWidget(themeHeading);

    auto* themeRow = new QWidget(&dialog);
    auto* themeRowLayout = new QHBoxLayout(themeRow);
    themeRowLayout->setContentsMargins(0, 0, 0, 0);
    themeRowLayout->setSpacing(18);
    auto* lightRadio = new QRadioButton(UiText::text(QStringLiteral("dialog.welcome.theme.light")), themeRow);
    auto* darkRadio = new QRadioButton(UiText::text(QStringLiteral("dialog.welcome.theme.dark")), themeRow);
    auto* themeGroup = new QButtonGroup(&dialog);
    themeGroup->addButton(lightRadio);
    themeGroup->addButton(darkRadio);
    (UiTheme::isDarkTheme() ? darkRadio : lightRadio)->setChecked(true);
    themeRowLayout->addWidget(lightRadio, 0);
    themeRowLayout->addWidget(darkRadio, 0);
    themeRowLayout->addStretch(1);
    root->addWidget(themeRow);

    QPointer<QDialog> dialogGuard(&dialog);
    const auto applyTheme = [this, dialogGuard, subtitle, schematic](UiText::ThemePreference theme) {
        UiText::setPreferredTheme(theme);
        owner_.windowSection_->applyUiTheme();
        if (!dialogGuard.isNull()) {
            // Re-applying the sheet also re-themes the "?" help badge
            // (QLabel#WelcomeHelpBadge) for the freshly chosen theme.
            dialogGuard->setStyleSheet(UiTheme::preferencesDialogStyleSheet());
            owner_.windowSection_->applySystemWindowBackdrop(dialogGuard.data());
        }
        subtitle->setStyleSheet(
            QStringLiteral("color: %1;").arg(UiTheme::colors().textSecondary.name(QColor::HexRgb)));
        schematic->update();
    };
    // Seed the secondary-text color for the current theme.
    subtitle->setStyleSheet(
        QStringLiteral("color: %1;").arg(UiTheme::colors().textSecondary.name(QColor::HexRgb)));
    QObject::connect(darkRadio, &QRadioButton::toggled, &dialog, [applyTheme](bool checked) {
        if (checked) {
            applyTheme(UiText::ThemePreference::Dark);
        }
    });
    QObject::connect(lightRadio, &QRadioButton::toggled, &dialog, [applyTheme](bool checked) {
        if (checked) {
            applyTheme(UiText::ThemePreference::Light);
        }
    });

    // --- Chinese-input group ------------------------------------------------
    // The same three graduated levels as Preferences' 中文输入 combo, surfaced on
    // first run (default 关闭输入法). The two underlying editor preferences are:
    //   关闭输入法   imeDisabled=ON  halfWidth=ON  (block the IME entirely)
    //   开启输入法   imeDisabled=OFF halfWidth=OFF (plain IME, no filtering)
    //   转换全角字符 imeDisabled=OFF halfWidth=ON  (IME on, normalize full-width)
    // The "?" badge next to the group title explains the choice.
    auto* chineseInputHeaderRow = new QWidget(&dialog);
    auto* chineseInputHeaderLayout = new QHBoxLayout(chineseInputHeaderRow);
    chineseInputHeaderLayout->setContentsMargins(0, 0, 0, 0);
    chineseInputHeaderLayout->setSpacing(6);
    auto* chineseInputLabel = new QLabel(
        UiText::text(QStringLiteral("dialog.welcome.chinese_input")), chineseInputHeaderRow);
    chineseInputLabel->setFont(uiAccentFont(10, QFont::DemiBold));
    // Round "?" help badge. Styled via QLabel#WelcomeHelpBadge in
    // preferencesDialogStyleSheet (theme-safe). Opt past the app-wide tooltip
    // suppression (MainWindow's global event filter hides tooltips outside the
    // preview area) with the miacodeAllowTooltip property.
    auto* chineseInputHelp = new QLabel(QStringLiteral("?"), chineseInputHeaderRow);
    chineseInputHelp->setObjectName(QStringLiteral("WelcomeHelpBadge"));
    chineseInputHelp->setAlignment(Qt::AlignCenter);
    chineseInputHelp->setFixedSize(16, 16);
    chineseInputHelp->setProperty("miacodeAllowTooltip", true);
    chineseInputHelp->setCursor(Qt::WhatsThisCursor);
    chineseInputHelp->setToolTip(UiText::text(QStringLiteral("dialog.welcome.chinese_input.hint")));
    chineseInputHeaderLayout->addWidget(chineseInputLabel, 0);
    chineseInputHeaderLayout->addWidget(chineseInputHelp, 0, Qt::AlignVCenter);
    chineseInputHeaderLayout->addStretch(1);
    root->addWidget(chineseInputHeaderRow);

    auto* imeRow = new QWidget(&dialog);
    auto* imeRowLayout = new QHBoxLayout(imeRow);
    imeRowLayout->setContentsMargins(0, 0, 0, 0);
    imeRowLayout->setSpacing(18);
    auto* imeDisableRadio = new QRadioButton(
        UiText::text(QStringLiteral("dialog.welcome.chinese_input.disable")), imeRow);
    auto* imeEnableRadio = new QRadioButton(
        UiText::text(QStringLiteral("dialog.welcome.chinese_input.enable")), imeRow);
    auto* imeFullWidthRadio = new QRadioButton(
        UiText::text(QStringLiteral("dialog.welcome.chinese_input.fullwidth")), imeRow);
    auto* imeGroup = new QButtonGroup(&dialog);
    imeGroup->addButton(imeDisableRadio);
    imeGroup->addButton(imeEnableRadio);
    imeGroup->addButton(imeFullWidthRadio);
    // Reflect the current stored state (default imeDisabled=ON => 关闭输入法).
    if (!state_.editorImeInputDisabled_ && state_.editorHalfWidthInputEnabled_) {
        imeFullWidthRadio->setChecked(true);
    } else if (!state_.editorImeInputDisabled_) {
        imeEnableRadio->setChecked(true);
    } else {
        imeDisableRadio->setChecked(true);
    }
    imeRowLayout->addWidget(imeDisableRadio, 0);
    imeRowLayout->addWidget(imeFullWidthRadio, 0);
    imeRowLayout->addWidget(imeEnableRadio, 0);
    imeRowLayout->addStretch(1);
    root->addWidget(imeRow);

    // imeDisabled, halfWidth. halfWidth persists last so the pair is written once.
    const auto applyChineseInput = [this](bool imeDisabled, bool halfWidth) {
        owner_.applyEditorHalfWidthInputEnabled(halfWidth, false);
        owner_.applyEditorImeInputDisabled(imeDisabled, true);
    };
    QObject::connect(imeDisableRadio, &QRadioButton::toggled, &dialog, [applyChineseInput](bool checked) {
        if (checked) {
            applyChineseInput(true, true);
        }
    });
    QObject::connect(imeEnableRadio, &QRadioButton::toggled, &dialog, [applyChineseInput](bool checked) {
        if (checked) {
            applyChineseInput(false, false);
        }
    });
    QObject::connect(imeFullWidthRadio, &QRadioButton::toggled, &dialog, [applyChineseInput](bool checked) {
        if (checked) {
            applyChineseInput(false, true);
        }
    });

    // --- Confirm ------------------------------------------------------------
    auto* getStarted = new QPushButton(UiText::text(QStringLiteral("dialog.welcome.get_started")), &dialog);
    getStarted->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    getStarted->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    getStarted->setDefault(true);
    QObject::connect(getStarted, &QPushButton::clicked, &dialog, &QDialog::accept);
    root->addSpacing(2);
    root->addWidget(getStarted, 0, Qt::AlignRight);

    // Shown programmatically at startup, so the QuickShell main window can win
    // the z-order race and occlude this modal. Raise + activate once the modal
    // loop starts so the welcome reliably lands in front of the main window.
    QTimer::singleShot(0, &dialog, [dlg = &dialog]() {
        dlg->raise();
        dlg->activateWindow();
    });
    dialog.exec();

    // Persist the final choices even if the user never toggled a radio, so the
    // explicit selection sticks AND the preferences file is rewritten under the
    // current schema (which is what stops the welcome dialog re-appearing next
    // launch). The setters are idempotent.
    UiText::setPreferredTheme(
        darkRadio->isChecked() ? UiText::ThemePreference::Dark : UiText::ThemePreference::Light);
    owner_.windowSection_->applyUiTheme();
    owner_.setWorkspacePanelsSwapped(previewLeftRadio->isChecked(), true);
    if (imeEnableRadio->isChecked()) {
        applyChineseInput(false, false);
    } else if (imeFullWidthRadio->isChecked()) {
        applyChineseInput(false, true);
    } else {
        applyChineseInput(true, true);   // 关闭输入法 (default)
    }
}

void MainWindow::showWelcomeDialog()
{
    preferencesSection_->showWelcomeDialog();
}
