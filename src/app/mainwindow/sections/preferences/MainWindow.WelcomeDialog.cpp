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
                 uiText("dialog.welcome.preview.editor", "Editor"), false);
        // textSecondary (not accentText) so the label stays readable — the
        // pane fill is panelBg, on which a white accentText would vanish.
        drawPane(painter, previewRect, colors.panelBg, colors.accent, colors.textSecondary,
                 uiText("dialog.welcome.preview.preview", "Preview"), true);
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
    dialog.setWindowTitle(uiText("dialog.welcome.title", "Welcome to MiaCode"));
    dialog.setModal(true);
    dialog.setMinimumWidth(460);
    dialog.setStyleSheet(UiTheme::preferencesDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(14);
    root->setSizeConstraint(QLayout::SetFixedSize);

    auto* heading = new QLabel(uiText("dialog.welcome.heading", "Welcome to MiaCode!"), &dialog);
    heading->setFont(uiAccentFont(15, QFont::DemiBold));
    root->addWidget(heading);

    auto* subtitle = new QLabel(
        uiText("dialog.welcome.subtitle",
               "Choose how your workspace looks. You can change these anytime later in "
               "Preferences (the gear icon in the top menu)."),
        &dialog);
    subtitle->setWordWrap(true);
    root->addWidget(subtitle);

    auto* schematic = new WelcomeLayoutPreview(&dialog);
    schematic->setPreviewOnLeft(owner_.workspacePanelsSwapped_);
    root->addWidget(schematic, 0, Qt::AlignHCenter);

    // --- Preview-side group -------------------------------------------------
    auto* sideLabel = new QLabel(uiText("dialog.welcome.preview_side", "Preview pane"), &dialog);
    sideLabel->setFont(uiAccentFont(10, QFont::DemiBold));
    root->addWidget(sideLabel);

    auto* sideRow = new QWidget(&dialog);
    auto* sideRowLayout = new QHBoxLayout(sideRow);
    sideRowLayout->setContentsMargins(0, 0, 0, 0);
    sideRowLayout->setSpacing(18);
    auto* previewRightRadio = new QRadioButton(
        uiText("dialog.welcome.preview_side.right", "On the right"), sideRow);
    auto* previewLeftRadio = new QRadioButton(
        uiText("dialog.welcome.preview_side.left", "On the left"), sideRow);
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
    auto* themeHeading = new QLabel(uiText("dialog.welcome.theme", "Color theme"), &dialog);
    themeHeading->setFont(uiAccentFont(10, QFont::DemiBold));
    root->addWidget(themeHeading);

    auto* themeRow = new QWidget(&dialog);
    auto* themeRowLayout = new QHBoxLayout(themeRow);
    themeRowLayout->setContentsMargins(0, 0, 0, 0);
    themeRowLayout->setSpacing(18);
    auto* lightRadio = new QRadioButton(uiText("dialog.welcome.theme.light", "Light"), themeRow);
    auto* darkRadio = new QRadioButton(uiText("dialog.welcome.theme.dark", "Dark"), themeRow);
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

    // --- Confirm ------------------------------------------------------------
    auto* getStarted = new QPushButton(uiText("dialog.welcome.get_started", "Get Started"), &dialog);
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

    // Persist the final choices even if the user never toggled a radio, so
    // the explicit selection sticks (preference would otherwise stay
    // "Follow System" / the prior swap state). The setters are idempotent.
    UiText::setPreferredTheme(
        darkRadio->isChecked() ? UiText::ThemePreference::Dark : UiText::ThemePreference::Light);
    owner_.windowSection_->applyUiTheme();
    owner_.setWorkspacePanelsSwapped(previewLeftRadio->isChecked(), true);
}

void MainWindow::showWelcomeDialog()
{
    preferencesSection_->showWelcomeDialog();
}
