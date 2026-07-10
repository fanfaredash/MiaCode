#include "UiComponents.h"

#include "DialogLocalization.h"
#include "EditableValueLabel.h"
#include "UiTheme.h"
#include "mainwindow/MainWindowShared.h"

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
                              EditableValueLabel** valueOut,
                              const QString& suffix,
                              QWidget* parent)
{
    if (slider != nullptr) {
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        slider->ensurePolished();
        slider->setFixedHeight(qMax(slider->sizeHint().height(), 20) + 2);
    }

    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* value = new EditableValueLabel(QStringLiteral("0") + suffix, row);
    value->setMinimumWidth(46);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (slider != nullptr) {
        value->bindSlider(slider);
        layout->addWidget(slider, 1);
        QObject::connect(slider, &QSlider::valueChanged, value, [value, suffix](int v) {
            value->setText(QString::number(v) + suffix);
        });
    }
    layout->addWidget(value, 0);

    if (valueOut != nullptr) {
        *valueOut = value;
    }
    return row;
}

QPushButton* createDialogPushButton(const QString& text, QWidget* parent, bool primary)
{
    auto* button = new QPushButton(text, parent);
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(primary));
    return button;
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
    button->setObjectName(QStringLiteral("DialogAuxiliaryButton"));
    button->setProperty("miacodeAuxiliaryButton", true);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(UiTheme::dialogAuxiliaryButtonStyleSheet());
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
        metrics.tabBarWidth = tabBar->sizeHint().width();
    }
    metrics.pinnedWidth = qMax(metrics.maxPageWidth, metrics.tabBarWidth) + metrics.tabChrome;
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
