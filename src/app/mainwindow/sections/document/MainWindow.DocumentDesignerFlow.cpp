#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/ProjectPreferences.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <algorithm>

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;
#include "MainWindow.DocumentFlow.Internal.h"

using namespace miacode::mainwindow::documentflow_detail;

namespace {

constexpr const char* kUnifiedDesignerPrefKey = "unified_designer_enabled";

// Collect every distinct non-empty designer name across the top &des and
// each per-difficulty &des_N. Used by the OFF→ON popup flows so the user
// can pick which name to canonicalize, or so we know whether anything is
// at risk of being overwritten silently.
struct DesignerSurvey {
    QString topDesigner;
    QVector<QPair<int, QString>> perDifficulty;  // (id, designer)
    QStringList distinctNonEmpty;                // de-duped, preserves insert order
};

DesignerSurvey surveyDesigners(const SimaiDocument& doc)
{
    DesignerSurvey survey;
    survey.topDesigner = doc.designer;
    if (!doc.designer.isEmpty()) {
        survey.distinctNonEmpty.append(doc.designer);
    }
    // Includes chart-less standalone &des_N so the unify picker can offer (and
    // overwrite) names that belong to slots without a chart.
    survey.perDifficulty = doc.perDifficultyDesigners();
    for (const QPair<int, QString>& slot : survey.perDifficulty) {
        if (!slot.second.isEmpty() && !survey.distinctNonEmpty.contains(slot.second)) {
            survey.distinctNonEmpty.append(slot.second);
        }
    }
    return survey;
}

void writeUnifiedDesignerPreference(const QString& chartPath, bool enabled)
{
    if (chartPath.isEmpty()) {
        return;
    }
    QJsonObject prefs = miacode::project_preferences::load(chartPath);
    prefs[QLatin1String(kUnifiedDesignerPrefKey)] = enabled;
    miacode::project_preferences::save(chartPath, prefs);
}

}  // namespace

void MainWindow::DocumentSection::refreshUnifiedDesignerStateForLoadedDocument()
{
    bool enabled = false;
    const QString chartPath = state_.currentFilePath_;
    bool decisionFromPreference = false;
    if (!chartPath.isEmpty()) {
        const QJsonObject prefs = miacode::project_preferences::load(chartPath);
        const QJsonValue stored = prefs.value(QLatin1String(kUnifiedDesignerPrefKey));
        if (stored.isBool()) {
            enabled = stored.toBool();
            decisionFromPreference = true;
        }
    }
    if (!decisionFromPreference) {
        enabled = state_.document_.inferUnifiedDesignerDefault();
    }
    state_.unifiedDesignerEnabled_ = enabled;

    // The preference claims "all difficulties share one designer", but the file
    // we just loaded may disagree (it could have been hand-edited, merged, or
    // touched by another tool since unified mode was last on). Without this,
    // the box would read as "synced" while &des and the &des_N silently
    // diverge until the next manual designer edit broadcasts. Reconcile now so
    // the on-screen promise holds the moment the document opens.
    if (!enabled) {
        return;
    }
    const DesignerSurvey survey = surveyDesigners(state_.document_);
    // Only auto-reconcile when there's an unambiguous canonical name:
    //   - 0/1 distinct non-empty name → fill blanks, no data loss; or
    //   - &des is non-empty → it is the declared source of truth.
    // When &des is empty AND the per-difficulty names genuinely conflict
    // there is no winner to pick without asking, and silently guessing on
    // load would destroy data with no undo — so leave it for the user.
    if (survey.distinctNonEmpty.size() >= 2 && survey.topDesigner.isEmpty()) {
        return;
    }
    QString canonical = survey.topDesigner;
    if (canonical.isEmpty() && !survey.distinctNonEmpty.isEmpty()) {
        canonical = survey.distinctNonEmpty.first();
    }
    // applyUnifiedDesignerName is a no-op (no dirty, no UI churn) when the
    // document is already consistent, which is the common case.
    applyUnifiedDesignerName(canonical);
}

void MainWindow::DocumentSection::enableUnifiedDocumentDesigner(const QString& canonicalName)
{
    state_.unifiedDesignerEnabled_ = true;
    writeUnifiedDesignerPreference(state_.currentFilePath_, true);
    applyUnifiedDesignerName(canonicalName);
}

void MainWindow::DocumentSection::disableUnifiedDocumentDesigner()
{
    if (!state_.unifiedDesignerEnabled_) {
        return;
    }
    state_.unifiedDesignerEnabled_ = false;
    writeUnifiedDesignerPreference(state_.currentFilePath_, false);
}

void MainWindow::DocumentSection::openPerDifficultyDesignerDialog()
{
    MC_OP("MainWindow::DocumentSection::openPerDifficultyDesignerDialog");
    // Flush the top &des (and any other active metadata edit) so the dialog
    // opens on the state the user can see and the unify survey is accurate.
    applyCurrentFieldToDocument();

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setModal(true);
    dialog.setWindowTitle(UiText::text(QStringLiteral("document.designer_management")));
    // Reuse the picker dialog's themed chrome (QLabel/QPushButton), then layer on:
    //  - a dark outer dialog (windowAltBg) so the content reads as a card on a
    //    dark page — matching the audio-settings dialog;
    //  - a QGroupBox "card" (cardBg) whose title sits top-left like "音频";
    //  - themed QLineEdit inputs (inputBg/textPrimary/borderSoft), same as the
    //    metadata page, so the inside colors are unchanged from before.
    const UiTheme::Colors& tc = UiTheme::colors();
    const auto hex = [](const QColor& col) { return col.name(QColor::HexRgb); };
    dialog.setStyleSheet(UiTheme::designerPickerDialogStyleSheet()
        + QStringLiteral(
              "QDialog { background: %1; }"
              "QGroupBox { background: %2; border: 1px solid %3; border-radius: 10px;"
              " margin-top: 12px; padding-top: 10px; }"
              "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: %4; }")
              .arg(hex(tc.windowAltBg), hex(tc.cardBg), hex(tc.border), hex(tc.textPrimary))
        + QStringLiteral(
              "QLineEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
              " padding: 5px 8px; selection-background-color: %4; selection-color: %5; }"
              "QLineEdit:focus { border-color: %6; }"
              "QLineEdit:disabled { background: %7; color: %8; }")
              .arg(hex(tc.inputBg), hex(tc.textPrimary), hex(tc.borderSoft),
                   hex(tc.selection), hex(tc.selectionText), hex(tc.accent),
                   hex(tc.inputDisabledBg), hex(tc.textMuted)));
    dialog.resize(460, 440);

    auto* outerLayout = new QVBoxLayout(&dialog);
    outerLayout->setContentsMargins(14, 14, 14, 12);
    outerLayout->setSpacing(10);

    // The rows + checkbox live inside a titled card (like the "音频" group in
    // the audio-settings dialog); the OK/Cancel buttons stay outside it.
    auto* designerGroup = new QGroupBox(
        UiText::text(QStringLiteral("document.designers")),
        &dialog);
    auto* groupLayout = new QVBoxLayout(designerGroup);
    groupLayout->setContentsMargins(12, 6, 12, 12);
    groupLayout->setSpacing(10);

    // Working copy of the seven names + the unified flag (index 1..7). Nothing
    // touches the document until OK, so Cancel is a clean no-op.
    QVector<QString> slotNames(8);
    for (int id = 1; id <= 7; ++id) {
        slotNames[id] = state_.document_.designerForSlot(id);
    }
    QString topDesigner = state_.document_.designer;
    bool localUnified = state_.unifiedDesignerEnabled_;

    auto* unifiedBox = new QCheckBox(
        UiText::text(QStringLiteral("document.all_difficulties_share_this_designer")),
        &dialog);
    unifiedBox->setToolTip(UiText::text(QStringLiteral("document.when_checked_des_and_every")));
    unifiedBox->setStyleSheet(UiTheme::darkAwareCheckBoxStyleSheet());
    {
        QSignalBlocker block(unifiedBox);
        unifiedBox->setChecked(localUnified);
    }
    // Added to the layout after the per-difficulty rows (below) so it sits at
    // the bottom, just above the OK/Cancel buttons.

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);
    QVector<QLineEdit*> rowEdits(8, nullptr);
    for (int id = 1; id <= 7; ++id) {
        auto* edit = new QLineEdit(&dialog);
        edit->setPlaceholderText(QStringLiteral("&des_%1=").arg(id));
        edit->setText(slotNames[id]);
        rowEdits[id] = edit;
        connect(edit, &QLineEdit::textChanged, &dialog, [&slotNames, id](const QString& text) {
            slotNames[id] = text;
        });
        auto* label = new QLabel(SimaiDocument::difficultyName(id), &dialog);
        // A small marker shows which slots already have a chart, so a name on a
        // chart-less slot is an obvious "designer-only" entry.
        if (state_.document_.difficulty(id) == nullptr) {
            label->setToolTip(UiText::text(QStringLiteral("document.no_chart_yet_records_des")).arg(id));
        }
        form->addRow(label, edit);
    }
    groupLayout->addLayout(form);
    groupLayout->addWidget(unifiedBox);
    outerLayout->addWidget(designerGroup);

    const auto refreshRowEnabled = [&]() {
        for (int id = 1; id <= 7; ++id) {
            rowEdits[id]->setEnabled(!localUnified);
        }
    };
    const auto syncRowsFromNames = [&]() {
        for (int id = 1; id <= 7; ++id) {
            QSignalBlocker block(rowEdits[id]);
            rowEdits[id]->setText(slotNames[id]);
        }
    };
    refreshRowEnabled();

    connect(unifiedBox, &QCheckBox::toggled, &dialog, [&](bool checked) {
        if (!checked) {
            localUnified = false;
            refreshRowEnabled();
            return;
        }
        // Turning ON: collect distinct non-empty names across top &des + the
        // seven slots. <=1 unifies silently; otherwise the user picks the
        // canonical name (or "clear all"). Mirrors the old checkbox flow.
        QStringList distinct;
        const auto consider = [&distinct](const QString& name) {
            if (!name.isEmpty() && !distinct.contains(name)) {
                distinct.append(name);
            }
        };
        consider(topDesigner);
        for (int id = 1; id <= 7; ++id) {
            consider(slotNames[id]);
        }
        QString canonical;
        if (distinct.size() <= 1) {
            canonical = distinct.isEmpty() ? QString() : distinct.first();
        } else {
            QString picked;
            if (!promptCanonicalDesignerName(distinct, &picked)) {
                QSignalBlocker block(unifiedBox);
                unifiedBox->setChecked(false);
                localUnified = false;
                refreshRowEnabled();
                return;
            }
            canonical = picked;
        }
        localUnified = true;
        topDesigner = canonical;
        // Every charted difficulty (and any slot the user already named) takes
        // the canonical name; chart-less unnamed slots stay empty so unifying
        // never materializes a `&des_N` for a slot nobody touched.
        for (int id = 1; id <= 7; ++id) {
            const bool slotGetsName =
                state_.document_.difficulty(id) != nullptr || !slotNames[id].isEmpty();
            slotNames[id] = slotGetsName ? canonical : QString();
        }
        syncRowsFromNames();
        refreshRowEnabled();
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    UiDialogs::localizeButtonBox(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    outerLayout->addWidget(buttons);

    // Themed (dark/light) title bar + center on the main window, matching every
    // other app dialog and the canonical-name picker below.
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Commit the working copy. setDesignerForSlot writes a chart-less name as a
    // standalone &des_N (no phantom difficulty) and clears the entry on "".
    bool changed = false;
    for (int id = 1; id <= 7; ++id) {
        if (state_.document_.designerForSlot(id) != slotNames[id]) {
            state_.document_.setDesignerForSlot(id, slotNames[id]);
            changed = true;
        }
    }
    // Under unified mode the top &des is the shared name too.
    if (localUnified && state_.document_.designer != topDesigner) {
        state_.document_.designer = topDesigner;
        if (ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != topDesigner) {
            QSignalBlocker block(ui_.designerEdit_);
            ui_.designerEdit_->setText(topDesigner);
        }
        changed = true;
    }
    if (state_.unifiedDesignerEnabled_ != localUnified) {
        state_.unifiedDesignerEnabled_ = localUnified;
        changed = true;
    }
    writeUnifiedDesignerPreference(state_.currentFilePath_, localUnified);
    // The dialog rewrites &des_N behind the header's back; refresh the header
    // designer edit so a later field commit can't restore pre-dialog names.
    syncHeaderDesignerEditFromModel();

    if (changed) {
        state_.documentDirty_ = true;
        anchorCurrentFieldCleanState();
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        owner_.updateWindowTitle();
        rebuildFieldSidebar();
    }
}

void MainWindow::DocumentSection::applyUnifiedDesignerName(const QString& canonicalName)
{
    bool changed = false;
    if (state_.document_.designer != canonicalName) {
        state_.document_.designer = canonicalName;
        changed = true;
    }
    // Charted difficulties AND chart-less standalone &des_N alike. An empty
    // canonical clears standalone entries (setDesignerForSlot("")).
    const QVector<QPair<int, QString>> designerSlots = state_.document_.perDifficultyDesigners();
    for (const QPair<int, QString>& slot : designerSlots) {
        if (slot.second != canonicalName) {
            state_.document_.setDesignerForSlot(slot.first, canonicalName);
            changed = true;
        }
    }
    if (ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != canonicalName) {
        QSignalBlocker block(ui_.designerEdit_);
        ui_.designerEdit_->setText(canonicalName);
    }
    syncHeaderDesignerEditFromModel();
    if (changed) {
        state_.documentDirty_ = true;
        anchorCurrentFieldCleanState();
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        owner_.updateWindowTitle();
        rebuildFieldSidebar();
    }
}

bool MainWindow::DocumentSection::promptCanonicalDesignerName(const QStringList& candidates, QString* out)
{
    if (out == nullptr) {
        return false;
    }
    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setModal(true);
    dialog.setWindowTitle(UiText::text(QStringLiteral("document.pick_the_canonical_designer")));
    // Themed background and visible radio indicators (the Windows default
    // disc is nearly invisible against the dark card background).
    dialog.setStyleSheet(UiTheme::designerPickerDialogStyleSheet());
    // Size hint generous enough for long Chinese designer names so the
    // radio labels aren't cut off. The scroll area lets the dialog stay
    // bounded even when there are many candidates.
    dialog.resize(520, 420);

    auto* outerLayout = new QVBoxLayout(&dialog);
    outerLayout->setContentsMargins(14, 14, 14, 12);
    outerLayout->setSpacing(10);
    auto* prompt = new QLabel(
        UiText::text(QStringLiteral("document.multiple_distinct_designer_names_were_detected")),
        &dialog);
    prompt->setWordWrap(true);
    outerLayout->addWidget(prompt);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* listHost = new QWidget(scroll);
    auto* listLayout = new QVBoxLayout(listHost);
    listLayout->setContentsMargins(8, 8, 8, 8);
    listLayout->setSpacing(2);
    auto* group = new QButtonGroup(&dialog);
    group->setExclusive(true);

    // Every radio button (regular candidate or "Clear all") gets the same
    // base widget styling from the dialog stylesheet — no per-row override
    // here, so vertical alignment between rows stays exact.
    int radioIndex = 0;
    for (const QString& name : candidates) {
        auto* radio = new QRadioButton(name, listHost);
        radio->setToolTip(name);
        if (radioIndex == 0) {
            radio->setChecked(true);
        }
        group->addButton(radio, radioIndex);
        listLayout->addWidget(radio);
        ++radioIndex;
    }
    // "Clear all" lives at the end of the list. Its chosen-id is a sentinel
    // past the last candidate index so callers can tell it apart from real
    // names. It must NOT be -1: QButtonGroup::addButton(button, -1) means
    // "auto-assign an id" (and -1 is also what checkedId() returns for "no
    // selection"), so -1 would never round-trip back as the clear-all choice.
    const int kClearAllId = candidates.size();
    auto* clearAllRadio = new QRadioButton(
        UiText::text(QStringLiteral("document.clear_all")),
        listHost);
    group->addButton(clearAllRadio, kClearAllId);
    listLayout->addWidget(clearAllRadio);
    listLayout->addStretch(1);
    scroll->setWidget(listHost);
    outerLayout->addWidget(scroll, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    outerLayout->addWidget(buttonBox);

    // Apply the dark-mode title bar + center the dialog on the main window
    // (or whatever the active anchor widget is). Without these the dialog
    // would inherit a light title bar and spawn at an arbitrary location.
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    const int selectedId = group->checkedId();
    if (selectedId == kClearAllId) {
        *out = QString();
    } else if (selectedId >= 0 && selectedId < candidates.size()) {
        *out = candidates.at(selectedId);
    } else {
        // No selection somehow → treat as cancel.
        return false;
    }
    return true;
}
