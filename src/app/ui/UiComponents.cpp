#include "UiComponents.h"

#include "DialogLocalization.h"
#include "UiTheme.h"
#include "common/AdoptedWidgetCoordinates.h"
#include "mainwindow/MainWindowShared.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSize>
#include <QSlider>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWidget>
#include <QtGlobal>

namespace miacode::ui {

namespace {

QString dialogStyleSheetForChrome(SettingsDialogChrome chrome)
{
    switch (chrome) {
    case SettingsDialogChrome::Preferences:
        return UiTheme::preferencesDialogStyleSheet();
    case SettingsDialogChrome::Settings:
    default:
        return UiTheme::settingsDialogStyleSheet();
    }
}

void repolish(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// QuickShell on macOS adopts a bridge QWidget's NSView into a QQuickWindow,
// while Qt Widgets still maps the bridge through its former NSPanel. Let Qt
// choose the normal popup geometry (including screen-edge flipping), then
// translate that result by the adopted-surface coordinate delta on show.
class AdoptedAnchorPopupPositionFilter final : public QObject {
public:
    AdoptedAnchorPopupPositionFilter(QWidget* anchor, QWidget* popup)
        : QObject(popup)
        , anchor_(anchor)
        , popup_(popup)
    {
        if (popup_ != nullptr) {
            popup_->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event == nullptr || event->type() != QEvent::Show || watched != popup_ || anchor_.isNull()) {
            return false;
        }
        const AdoptedWidgetCoordinateRoute route = adoptedWidgetCoordinateRoute(anchor_, QPoint());
        if (route.window == nullptr) {
            return false;
        }
        const QPoint defaultAnchor = anchor_->mapToGlobal(QPoint());
        const QPoint adoptedAnchor = route.window->mapToGlobal(route.surfacePoint);
        popup_->move(popup_->pos() + adoptedAnchor - defaultAnchor);
        return false;
    }

private:
    QPointer<QWidget> anchor_;
    QPointer<QWidget> popup_;
};

void installAdoptedAnchorPopupPositioning(QWidget* anchor, QWidget* popup)
{
    if (anchor == nullptr || popup == nullptr
        || popup->property("miacode.adopted_popup_positioning").toBool()) {
        return;
    }
    popup->setProperty("miacode.adopted_popup_positioning", true);
    new AdoptedAnchorPopupPositionFilter(anchor, popup);
}

QSize dialogMenuRowSizeHint(const QWidget* widget, QSize baseSize)
{
    if (widget == nullptr) {
        return baseSize;
    }

    bool ok = false;
    const int rowWidth =
        widget->property("miacode.dialog_menu_row_width").toInt(&ok);
    if (ok && rowWidth > 0) {
        baseSize.setWidth(qMax(baseSize.width(), rowWidth));
    }
    return baseSize;
}

class DialogMenuCheckBox final : public QCheckBox {
public:
    using QCheckBox::QCheckBox;

    QSize sizeHint() const override
    {
        return dialogMenuRowSizeHint(this, QCheckBox::sizeHint());
    }

    QSize minimumSizeHint() const override
    {
        return dialogMenuRowSizeHint(this, QCheckBox::minimumSizeHint());
    }

protected:
    bool hitButton(const QPoint& pos) const override
    {
        return rect().contains(pos);
    }
};

QLabel* createDialogSliderValueLabel(QSlider* slider, const QString& suffix, QWidget* parent)
{
    const int value = slider != nullptr ? slider->value() : 0;
    auto* label = new QLabel(QString::number(value) + suffix, parent);
    label->setMinimumWidth(46);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (slider != nullptr) {
        QObject::connect(slider, &QSlider::valueChanged, label, [label, suffix](int v) {
            label->setText(QString::number(v) + suffix);
        });
    }
    return label;
}

}  // namespace

TabbedSettingsDialog::TabbedSettingsDialog(QWidget* parent,
                                           const QString& title,
                                           SettingsDialogChrome chrome)
    : QDialog(UiDialogs::effectiveParentWidget(parent))
    , chrome_(chrome)
{
    setWindowTitle(title);
    setModal(true);
    refreshStyleSheet();
    UiDialogs::prepareDialogWindow(this, parent);

    contentLayout_ = new QVBoxLayout(this);
    contentLayout_->setContentsMargins(12, 12, 12, 12);
    contentLayout_->setSpacing(10);
    contentLayout_->setSizeConstraint(QLayout::SetFixedSize);
}

QVBoxLayout* TabbedSettingsDialog::contentLayout() const
{
    return contentLayout_;
}

QTabWidget* TabbedSettingsDialog::createTabs(const QString& objectName)
{
    auto* tabs = new QTabWidget(this);
    if (!objectName.isEmpty()) {
        tabs->setObjectName(objectName);
    }
    contentLayout_->addWidget(tabs);
    return tabs;
}

QDialogButtonBox* TabbedSettingsDialog::buttonBox()
{
    if (buttonBox_ == nullptr) {
        buttonBox_ = new QDialogButtonBox(this);
        contentLayout_->addWidget(buttonBox_);
    }
    return buttonBox_;
}

QPushButton* TabbedSettingsDialog::addCloseButton(const QString& text, bool rejectMapsToAccept)
{
    QDialogButtonBox* buttons = buttonBox();
    auto* closeButton = createDialogPushButton(text, buttons);
    buttons->addButton(closeButton, QDialogButtonBox::RejectRole);
    QObject::connect(buttons,
                     &QDialogButtonBox::rejected,
                     this,
                     rejectMapsToAccept ? &QDialog::accept : &QDialog::reject);
    return closeButton;
}

void TabbedSettingsDialog::refreshStyleSheet()
{
    setStyleSheet(dialogStyleSheetForChrome(chrome_));
}

QFrame* createCard(const QString& titleText, QWidget* parent, QVBoxLayout** bodyLayoutOut)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("LatencyCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(titleText, card);
    title->setProperty("role", "cardTitle");
    title->setFont(miacode::mainwindow::shared::uiAccentFont(13, QFont::DemiBold));
    layout->addWidget(title);

    if (bodyLayoutOut != nullptr) {
        *bodyLayoutOut = layout;
    }
    return card;
}

QLabel* createFormLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "cardHint");
    label->setFont(miacode::mainwindow::shared::uiOutputFont());
    return label;
}

QWidget* createSliderValueRow(QSlider* slider,
                              QLabel** valueOut,
                              const QString& suffix,
                              QWidget* parent)
{
    applyDialogSliderStyle(slider);

    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* value = createDialogSliderValueLabel(slider, suffix, row);
    if (slider != nullptr) {
        layout->addWidget(slider, 1);
    }
    layout->addWidget(value, 0);

    if (valueOut != nullptr) {
        *valueOut = value;
    }
    return row;
}

void applyDialogSliderStyle(QSlider* slider)
{
    if (slider == nullptr) {
        return;
    }
    slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
    slider->ensurePolished();
    slider->setFixedHeight(qMax(slider->sizeHint().height(), 20) + 2);
}

QWidget* createDialogSliderOption(const QString& title,
                                  QSlider* slider,
                                  QLabel** valueOut,
                                  const QString& suffix,
                                  QWidget* parent,
                                  DialogSliderOptionLayout optionLayout)
{
    applyDialogSliderStyle(slider);

    auto* container = new QWidget(parent);
    auto* value = createDialogSliderValueLabel(slider, suffix, container);
    if (valueOut != nullptr) {
        *valueOut = value;
    }

    if (optionLayout == DialogSliderOptionLayout::Stacked) {
        auto* containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(3);

        auto* header = new QWidget(container);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(6);
        headerLayout->addWidget(new QLabel(title, header), 1);
        headerLayout->addWidget(value, 0);

        containerLayout->addWidget(header, 0);
        if (slider != nullptr) {
            containerLayout->addWidget(slider, 0);
        }
        return container;
    }

    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(new QLabel(title, container), 0);
    if (slider != nullptr) {
        layout->addWidget(slider, 1);
    }
    layout->addWidget(value, 0);
    return container;
}

QPushButton* createDialogPushButton(const QString& text, QWidget* parent, bool primary)
{
    auto* button = new QPushButton(text, parent);
    applyDialogPushButtonStyle(button, primary);
    return button;
}

void applyDialogPushButtonStyle(QPushButton* button, bool primary)
{
    if (button == nullptr) {
        return;
    }
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(primary));
}

void applyDialogAuxiliaryButtonStyle(QPushButton* button)
{
    if (button == nullptr) {
        return;
    }
    button->setObjectName(QStringLiteral("DialogAuxiliaryButton"));
    button->setProperty("miacodeAuxiliaryButton", true);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(UiTheme::dialogAuxiliaryButtonStyleSheet());
}

QComboBox* createDialogComboBox(QWidget* parent, int maxVisibleItems, Qt::Alignment textAlignment)
{
    auto* combo = new QComboBox(parent);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Consumed by UiTheme::styleDialogComboBox for padding (centred vs left),
    // per-item Qt::TextAlignmentRole, and the W1 fixed height.
    combo->setProperty("miacode.combo_text_alignment", static_cast<int>(textAlignment));
    applyDialogComboBoxStyle(combo, maxVisibleItems);
    return combo;
}

void applyDialogComboBoxStyle(QComboBox* combo, int maxVisibleItems)
{
    UiTheme::styleDialogComboBox(combo, maxVisibleItems);
    if (QAbstractItemView* popupView = combo != nullptr ? combo->view() : nullptr) {
        installAdoptedAnchorPopupPositioning(combo, popupView->window());
    }
}

QToolButton* createDialogDropdownButton(QWidget* parent,
                                        const QString& text,
                                        QMenu** menuOut,
                                        Qt::Alignment textAlignment)
{
    // Styled to match createDialogComboBox's closed box (same background /
    // border / radius / height), so a multi-select pseudo-dropdown sits next
    // to real combos as one consistent control. Same W1 fixed-height formula.
    auto* button = new QToolButton(parent);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(UiTheme::dialogComboLikeButtonStyleSheet(textAlignment));
    button->setText(text);
    // A QToolButton's sizeHint runs taller than a QComboBox for the same styled
    // box, so measure a same-font, populated combo probe and pin to that.
    QComboBox heightProbe;
    heightProbe.setFont(button->font());
    heightProbe.setProperty("miacode.combo_text_alignment", static_cast<int>(textAlignment));
    heightProbe.addItem(text.isEmpty() ? QStringLiteral(" ") : text);
    UiTheme::styleDialogComboBox(&heightProbe, 0);
    button->setFixedHeight(heightProbe.minimumHeight());

    auto* menu = new QMenu(button);
    UiTheme::styleDialogDropdownMenu(*menu);
    installAdoptedAnchorPopupPositioning(button, menu);
    UiTheme::bindDialogMenuButtonPopupState(button, menu);
    button->setMenu(menu);
    if (menuOut != nullptr) {
        *menuOut = menu;
    }
    return button;
}

QToolButton* createDialogMenuButton(QWidget* parent,
                                    const QString& text,
                                    QToolButton::ToolButtonPopupMode popupMode)
{
    auto* button = new QToolButton(parent);
    button->setPopupMode(popupMode);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
    button->setText(text);
    button->ensurePolished();
    button->setFixedHeight(qMax(button->sizeHint().height(), 30) + 4);
    return button;
}

QToolButton* addDialogMenuChoice(QMenu* menu,
                                 const QString& text,
                                 const std::function<void()>& onTriggered)
{
    if (menu == nullptr) {
        return nullptr;
    }
    auto* action = new QWidgetAction(menu);
    auto* button = new QToolButton(menu);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setText(text);
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(UiTheme::dialogDropdownItemButtonStyleSheet());
    QObject::connect(button, &QToolButton::clicked, menu, [action, menu, onTriggered]() {
        if (onTriggered) {
            onTriggered();
        }
        action->trigger();
        menu->close();
    });
    action->setDefaultWidget(button);
    menu->addAction(action);
    return button;
}

QCheckBox* addDialogMenuCheckChoice(QMenu* menu,
                                    const QString& text,
                                    bool checked,
                                    QObject* context,
                                    const std::function<void(QCheckBox*, bool)>& onToggled)
{
    if (menu == nullptr) {
        return nullptr;
    }
    auto* action = new QWidgetAction(menu);
    auto* checkbox = new DialogMenuCheckBox(text, menu);
    checkbox->setChecked(checked);
    checkbox->setCursor(Qt::PointingHandCursor);
    checkbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    checkbox->setStyleSheet(UiTheme::dialogDropdownCheckBoxStyleSheet());
    QObject::connect(checkbox,
                     &QCheckBox::toggled,
                     context != nullptr ? context : checkbox,
                     [checkbox, onToggled](bool isChecked) {
                         if (onToggled) {
                             onToggled(checkbox, isChecked);
                         }
                     });
    action->setDefaultWidget(checkbox);
    menu->addAction(action);
    return checkbox;
}

QPushButton* createDialogAuxiliaryButton(QWidget* parent, const QString& text)
{
    auto* button = new QPushButton(text, parent);
    applyDialogAuxiliaryButtonStyle(button);
    return button;
}

QFormLayout* createFormGroup(const QString& title,
                             QWidget* parent,
                             QVBoxLayout* hostLayout,
                             const QMargins& margins,
                             int horizontalSpacing,
                             int verticalSpacing)
{
    auto* group = new QGroupBox(title, parent);
    auto* form = new QFormLayout(group);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setHorizontalSpacing(horizontalSpacing);
    form->setVerticalSpacing(verticalSpacing);
    form->setContentsMargins(margins);
    if (hostLayout != nullptr) {
        hostLayout->addWidget(group);
    }
    return form;
}

void flattenGroupForTabPage(QGroupBox* group)
{
    if (group == nullptr) {
        return;
    }
    group->setTitle(QString());
    group->setFlat(true);
    group->setStyleSheet(QStringLiteral(
        "QGroupBox { border: none; margin-top: 0; padding-top: 0; }"));
}

TabWidgetWidthMetrics pinTabWidgetToContentWidth(QTabWidget* tabs,
                                                 QDialog* dialog,
                                                 QLayout* rootLayout)
{
    TabWidgetWidthMetrics metrics;
    if (tabs == nullptr || dialog == nullptr) {
        return metrics;
    }

    for (int i = 0; i < tabs->count(); ++i) {
        QWidget* page = tabs->widget(i);
        if (page == nullptr) {
            continue;
        }
        if (page->layout() != nullptr) {
            page->layout()->activate();
        }
        metrics.maxPageWidth = qMax(metrics.maxPageWidth, page->sizeHint().width());
    }

    dialog->adjustSize();
    QCoreApplication::sendPostedEvents(tabs, QEvent::LayoutRequest);
    auto* innerStack = tabs->findChild<QStackedWidget*>();
    if (innerStack != nullptr && tabs->width() > innerStack->width()) {
        metrics.tabChrome = tabs->width() - innerStack->width();
    }

    if (auto* tabBar = tabs->findChild<QTabBar*>()) {
        if (tabs->objectName() == QLatin1String("PreferenceTabs")) {
            tabBar->setUsesScrollButtons(true);
            tabBar->setElideMode(Qt::ElideRight);
            tabBar->setExpanding(false);
        }
        metrics.tabBarWidth = tabBar->sizeHint().width();
    }
    const bool compactScrollableTabs = tabs->objectName() == QLatin1String("PreferenceTabs");
    metrics.pinnedWidth =
        (compactScrollableTabs ? metrics.maxPageWidth : qMax(metrics.maxPageWidth, metrics.tabBarWidth))
        + metrics.tabChrome;
    tabs->setFixedWidth(metrics.pinnedWidth);
    if (rootLayout != nullptr) {
        rootLayout->activate();
    }
    dialog->adjustSize();
    return metrics;
}

void pinTabWidgetToContentHeight(QTabWidget* tabs, QDialog* dialog, QLayout* rootLayout)
{
    if (tabs == nullptr || dialog == nullptr) {
        return;
    }

    int maxPageHeight = 0;
    for (int i = 0; i < tabs->count(); ++i) {
        QWidget* page = tabs->widget(i);
        if (page == nullptr) {
            continue;
        }
        if (page->layout() != nullptr) {
            page->layout()->activate();
        }
        maxPageHeight = qMax(maxPageHeight, page->sizeHint().height());
    }

    // Chrome = tab bar + pane frame/padding. QStyleSheetStyle omits the styled
    // QTabWidget::pane padding from the tab sizeHint (W2), so a SetFixedSize
    // dialog ends up a few px short and the TALLEST tab's bottom row clips its
    // border. Compute the chrome DETERMINISTICALLY: a live
    // `tabs->height() - innerStack->height()` is unreliable here because this
    // runs during construction (before show), where the inner stack can report
    // ~0 height and massively over-inflate the pin — which then stretches the
    // grid rows apart. kPaneChrome mirrors dialogTabStripStyleSheet:
    // ::pane padding 8+8 + border 1+1 (the 1px `top:-1px` lift is left as
    // headroom).
    constexpr int kPaneChrome = 8 + 8 + 1 + 1;
    int chrome = kPaneChrome;
    if (auto* tabBar = tabs->findChild<QTabBar*>(); tabBar != nullptr) {
        chrome += tabBar->sizeHint().height();
    }
    tabs->setMinimumHeight(maxPageHeight + chrome);

    if (rootLayout != nullptr) {
        rootLayout->activate();
    }
    dialog->adjustSize();
}

void applyCompactDialogButton(QPushButton* button, bool primary)
{
    if (button == nullptr) {
        return;
    }
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(primary)
        + QStringLiteral("QPushButton { min-width: 0px; min-height: 30px; padding: 0 12px; }"));
}

void applySmallDialogButton(QPushButton* button)
{
    if (button == nullptr) {
        return;
    }
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 28px; max-width: 30px;"
                         " min-height: 26px; max-height: 28px; padding: 0; border-radius: 6px; }"));
    button->setFixedSize(30, 28);
}

void applyScrollBarStyle(QAbstractScrollArea* area)
{
    if (area == nullptr) {
        return;
    }
    if (QScrollBar* vertical = area->verticalScrollBar(); vertical != nullptr) {
        vertical->setStyleSheet(UiTheme::scrollBarStyleSheet());
    }
    if (QScrollBar* horizontal = area->horizontalScrollBar(); horizontal != nullptr) {
        horizontal->setStyleSheet(UiTheme::scrollBarStyleSheet());
    }
}

}  // namespace miacode::ui
