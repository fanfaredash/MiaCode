#pragma once

#include <QDialog>
#include <QFrame>
#include <QMargins>
#include <QPointer>
#include <QString>
#include <QToolButton>

#include <functional>

class QLabel;
class QAbstractScrollArea;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QGroupBox;
class QHideEvent;
class QLayout;
class QMenu;
class QPushButton;
class QScrollArea;
class QSlider;
class QTabWidget;
class QVBoxLayout;
class QWidget;

namespace miacode::ui {

class EditableValueLabel;

enum class SettingsDialogChrome {
    Settings,
    Preferences,
};

struct TabWidgetWidthMetrics {
    int maxPageWidth = 0;
    int tabBarWidth = 0;
    int tabChrome = 18;
    int pinnedWidth = 0;
};

class TabbedSettingsDialog final : public QDialog {
public:
    explicit TabbedSettingsDialog(QWidget* parent,
                                  const QString& title,
                                  SettingsDialogChrome chrome);

    QVBoxLayout* contentLayout() const;
    QTabWidget* createTabs(const QString& objectName);
    QDialogButtonBox* buttonBox();
    QPushButton* addCloseButton(const QString& text, bool rejectMapsToAccept);
    void refreshStyleSheet();

private:
    SettingsDialogChrome chrome_;
    QVBoxLayout* contentLayout_ = nullptr;
    QDialogButtonBox* buttonBox_ = nullptr;
};

QFrame* createCard(const QString& titleText, QWidget* parent, QVBoxLayout** bodyLayoutOut = nullptr);
QLabel* createFormLabel(const QString& text, QWidget* parent);
QWidget* createSliderValueRow(QSlider* slider,
                              EditableValueLabel** valueOut,
                              const QString& suffix,
                              QWidget* parent);
QPushButton* createDialogPushButton(const QString& text, QWidget* parent, bool primary = false);

// The canonical dialog dropdown (the fixed 皮肤-popup combobox): rounded
// translucent popup panel, item hover rows, popup row limit and themed
// floating scrollbar. Use this factory for every real QComboBox in dialog
// pages; call applyDialogComboBoxStyle again after repopulating items or on
// a theme switch (it re-measures the popup row height with the final style
// and re-stamps the text alignment on the current items).
// textAlignment applies to the closed label AND the popup rows (via
// Qt::TextAlignmentRole): settings/export pages use the centered default,
// 皮肤/封面 pages pass Qt::AlignLeft | Qt::AlignVCenter.
QComboBox* createDialogComboBox(QWidget* parent,
                                int maxVisibleItems = 12,
                                Qt::Alignment textAlignment = Qt::AlignCenter);
void applyDialogComboBoxStyle(QComboBox* combo, int maxVisibleItems = 12);

// The canonical dialog pseudo-dropdown (QToolButton + QMenu) sharing the same
// popup panel treatment as createDialogComboBox. Creates the button, its
// menu (styled + popup-state bound + attached), and returns the button;
// populate via addDialogMenuChoice / addDialogMenuCheckChoice on *menuOut.
QToolButton* createDialogDropdownButton(QWidget* parent,
                                        const QString& text,
                                        QMenu** menuOut,
                                        Qt::Alignment textAlignment = Qt::AlignCenter);
QToolButton* createDialogMenuButton(QWidget* parent,
                                    const QString& text,
                                    QToolButton::ToolButtonPopupMode popupMode);
QToolButton* addDialogMenuChoice(QMenu* menu,
                                 const QString& text,
                                 const std::function<void()>& onTriggered);
QCheckBox* addDialogMenuCheckChoice(QMenu* menu,
                                    const QString& text,
                                    bool checked,
                                    QObject* context,
                                    const std::function<void(QCheckBox*, bool)>& onToggled);
QPushButton* createDialogAuxiliaryButton(QWidget* parent, const QString& text);
QFormLayout* createFormGroup(const QString& title,
                             QWidget* parent,
                             QVBoxLayout* hostLayout,
                             const QMargins& margins = QMargins(10, 8, 10, 8),
                             int horizontalSpacing = 10,
                             int verticalSpacing = 8);
void flattenGroupForTabPage(QGroupBox* group);
TabWidgetWidthMetrics pinTabWidgetToContentWidth(QTabWidget* tabs,
                                                 QDialog* dialog,
                                                 QLayout* rootLayout);
// Pins the tab widget's minimum HEIGHT to fit the tallest page plus the
// live-measured pane chrome, so a SetFixedSize dialog is tall enough and the
// tallest tab's bottom row doesn't clip (QSS pane padding is absent from the
// tab sizeHint — W2). Call after all addTab()s.
void pinTabWidgetToContentHeight(QTabWidget* tabs,
                                 QDialog* dialog,
                                 QLayout* rootLayout);
void applyCompactDialogButton(QPushButton* button, bool primary = false);
void applySmallDialogButton(QPushButton* button);
void applyScrollBarStyle(QAbstractScrollArea* area);

}  // namespace miacode::ui
