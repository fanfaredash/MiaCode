#include "tools/video_export/CardFontSettings.h"

#include "UiComponents.h"
#include "UiText.h"
#include "tools/video_export/FontLibrary.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace miacode::video_export {

CardFontSelector createCardFontSelector(QWidget* parent, const std::function<void()>& onChanged)
{
    auto* root = new QWidget(parent);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // 标题字体 / 正文字体 rows.
    auto* form = new QFormLayout();
    form->setSpacing(10);
    form->setContentsMargins(0, 0, 0, 0);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    const QString defaultLabel = UiText::text(QStringLiteral("card_font.default"));
    auto* displayCombo = miacode::ui::createDialogComboBox(root, 12, Qt::AlignLeft | Qt::AlignVCenter);
    auto* bodyCombo = miacode::ui::createDialogComboBox(root, 12, Qt::AlignLeft | Qt::AlignVCenter);

    const auto repopulate = [displayCombo, bodyCombo, defaultLabel]() {
        populateFontCombo(displayCombo, displayCombo->currentData().toString(), true, defaultLabel);
        populateFontCombo(bodyCombo, bodyCombo->currentData().toString(), true, defaultLabel);
        miacode::ui::applyDialogComboBoxStyle(displayCombo, 12);
        miacode::ui::applyDialogComboBoxStyle(bodyCombo, 12);
    };
    populateFontCombo(displayCombo, QString(), true, defaultLabel);
    populateFontCombo(bodyCombo, QString(), true, defaultLabel);
    miacode::ui::applyDialogComboBoxStyle(displayCombo, 12);
    miacode::ui::applyDialogComboBoxStyle(bodyCombo, 12);

    form->addRow(UiText::text(QStringLiteral("card_font.title")), displayCombo);
    form->addRow(UiText::text(QStringLiteral("card_font.body")), bodyCombo);
    layout->addLayout(form);

    // Import / Reset row.
    auto* buttonRow = new QWidget(root);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    auto* importButton = miacode::ui::createDialogAuxiliaryButton(
        buttonRow, UiText::text(QStringLiteral("card_font.import")));
    auto* resetButton = miacode::ui::createDialogAuxiliaryButton(
        buttonRow, UiText::text(QStringLiteral("card_font.reset")));
    buttonLayout->addWidget(importButton, 0);
    buttonLayout->addWidget(resetButton, 0);
    buttonLayout->addStretch(1);
    layout->addWidget(buttonRow, 0);

    const auto notify = [onChanged]() {
        if (onChanged) {
            onChanged();
        }
    };

    QObject::connect(displayCombo, qOverload<int>(&QComboBox::currentIndexChanged), root,
                     [notify](int) { notify(); });
    QObject::connect(bodyCombo, qOverload<int>(&QComboBox::currentIndexChanged), root,
                     [notify](int) { notify(); });
    QObject::connect(importButton, &QPushButton::clicked, root,
                     [root, displayCombo, bodyCombo, defaultLabel, repopulate, notify]() {
        const QString importedPath = importFontIntoLibrary(root);
        if (importedPath.isEmpty()) {
            return;
        }
        // Make the freshly imported font the display selection (the most common
        // intent); the body combo just gains the new option, keeping its choice.
        {
            const QSignalBlocker blockBody(bodyCombo);
            populateFontCombo(bodyCombo, bodyCombo->currentData().toString(), true, defaultLabel);
            miacode::ui::applyDialogComboBoxStyle(bodyCombo, 12);
        }
        populateFontCombo(displayCombo, importedPath, true, defaultLabel);
        miacode::ui::applyDialogComboBoxStyle(displayCombo, 12);
        notify();
    });
    QObject::connect(resetButton, &QPushButton::clicked, root,
                     [displayCombo, bodyCombo, defaultLabel, notify]() {
        {
            const QSignalBlocker blockDisplay(displayCombo);
            const QSignalBlocker blockBody(bodyCombo);
            populateFontCombo(displayCombo, QString(), true, defaultLabel);
            populateFontCombo(bodyCombo, QString(), true, defaultLabel);
            miacode::ui::applyDialogComboBoxStyle(displayCombo, 12);
            miacode::ui::applyDialogComboBoxStyle(bodyCombo, 12);
        }
        notify();
    });

    CardFontSelector selector;
    selector.widget = root;
    selector.displayPath = [displayCombo]() { return displayCombo->currentData().toString(); };
    selector.bodyPath = [bodyCombo]() { return bodyCombo->currentData().toString(); };
    selector.setSelection = [displayCombo, bodyCombo, defaultLabel](const QString& displayPath, const QString& bodyPath) {
        const QSignalBlocker blockDisplay(displayCombo);
        const QSignalBlocker blockBody(bodyCombo);
        populateFontCombo(displayCombo, displayPath, true, defaultLabel);
        populateFontCombo(bodyCombo, bodyPath, true, defaultLabel);
        miacode::ui::applyDialogComboBoxStyle(displayCombo, 12);
        miacode::ui::applyDialogComboBoxStyle(bodyCombo, 12);
    };
    return selector;
}

}  // namespace miacode::video_export
