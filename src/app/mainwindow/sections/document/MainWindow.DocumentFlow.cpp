#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
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

MainWindow::DocumentSection::DocumentSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

namespace {

enum class UnsavedChangesChoice {
    Save,
    Discard,
    Cancel,
};

QString unsavedChangesChoiceName(UnsavedChangesChoice choice)
{
    switch (choice) {
    case UnsavedChangesChoice::Save:
        return QStringLiteral("save");
    case UnsavedChangesChoice::Discard:
        return QStringLiteral("discard");
    case UnsavedChangesChoice::Cancel:
    default:
        return QStringLiteral("cancel");
    }
}

constexpr qint64 kAutosaveUnsetTimestampMs = -1;
constexpr char kAutosaveMetadataSchema[] = "miacode_autosave_v2";

struct PreparedDocumentOpenPayload {
    bool success = false;
    bool usedSystemEncoding = false;
    QString normalizedPath;
    SimaiDocument document;
    QString resolvedTrackPath;
    double trackDurationSeconds = 0.0;
    bool hasTrackDuration = false;
    qint64 readElapsedMs = 0;
    qint64 decodeElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 trackProbeElapsedMs = 0;
    qint64 totalElapsedMs = 0;
};

struct BackupRestoreEntry {
    QString filePath;
    QDateTime modifiedAt;
    int priority = 0;
};

PreparedDocumentOpenPayload prepareDocumentOpenPayload(const QString& path, bool probeTrackDuration)
{
    PreparedDocumentOpenPayload payload;
    payload.normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (payload.normalizedPath.isEmpty()) {
        return payload;
    }

    QFile file(payload.normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return payload;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    const QByteArray bytes = file.readAll();
    payload.readElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QString text;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            payload.usedSystemEncoding = true;
        }
    }
    payload.decodeElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    payload.document = SimaiDocument::fromText(text);
    payload.parseElapsedMs = phaseTimer.elapsed();

    if (probeTrackDuration) {
        payload.resolvedTrackPath = miacode::chart_assets::resolveTrackPath(payload.normalizedPath);
        phaseTimer.restart();
        if (!payload.resolvedTrackPath.isEmpty()) {
            payload.trackDurationSeconds = probeAudioDurationSeconds(payload.resolvedTrackPath);
            payload.hasTrackDuration = payload.trackDurationSeconds > 0.0;
        }
        payload.trackProbeElapsedMs = phaseTimer.elapsed();
    }

    payload.totalElapsedMs = totalTimer.elapsed();
    payload.success = true;
    return payload;
}

UnsavedChangesChoice showUnsavedChangesDialog(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox dialog(
        QMessageBox::Warning,
        title,
        text,
        QMessageBox::NoButton,
        UiDialogs::effectiveParentWidget(parent)
    );
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    UiDialogs::configureDialogPreviewShortcuts(&dialog);
    UiDialogs::applyDetachedParentBehavior(&dialog, parent);
    QPushButton* saveButton = dialog.addButton(uiText("action.save", "Save"), QMessageBox::AcceptRole);
    QPushButton* discardButton = dialog.addButton(uiText("action.discard", "Discard"), QMessageBox::DestructiveRole);
    QPushButton* cancelButton = dialog.addButton(uiText("action.cancel", "Cancel"), QMessageBox::RejectRole);
    dialog.setDefaultButton(saveButton);
    dialog.setEscapeButton(cancelButton);
    UiDialogs::localizeMessageBox(&dialog);
    centerDialogOnAnchor(&dialog, parent);
    dialog.exec();
    if (dialog.clickedButton() == saveButton) {
        return UnsavedChangesChoice::Save;
    }
    if (dialog.clickedButton() == discardButton) {
        return UnsavedChangesChoice::Discard;
    }
    return UnsavedChangesChoice::Cancel;
}

QString autosaveEntryDirectoryPathForFile(const QString& filePath)
{
    const QString projectDataDirectoryPath = resolveProjectDataDirectoryPath(filePath);
    if (projectDataDirectoryPath.isEmpty()) {
        return QString();
    }

    QString fileContainerName = QFileInfo(filePath).fileName().trimmed();
    if (fileContainerName.isEmpty()) {
        fileContainerName = QStringLiteral("maidata.txt");
    }
    return QDir(projectDataDirectoryPath).filePath(QStringLiteral(".autosave/%1").arg(fileContainerName));
}

QString autosaveLatestFilePath(const QString& autosaveDirectoryPath)
{
    QString baseName = QFileInfo(autosaveDirectoryPath).fileName().trimmed();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("latest");
    }
    return QDir(autosaveDirectoryPath).filePath(baseName + QStringLiteral(".bak"));
}

QString autosaveHistoryDirectoryPath(const QString& autosaveDirectoryPath)
{
    return QDir(autosaveDirectoryPath).filePath(QStringLiteral("history"));
}

QString autosaveMetadataFilePath(const QString& autosaveDirectoryPath)
{
    return QDir(autosaveDirectoryPath).filePath(QStringLiteral("autosave.json"));
}

QString autosaveTimestampStringUtc(qint64 msecsSinceEpoch)
{
    return QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch, Qt::UTC)
        .toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
}

QString autosaveTimestampStringUtcNow()
{
    return autosaveTimestampStringUtc(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
}

QString backupRestoreEntryLabel(const BackupRestoreEntry& entry)
{
    if (!entry.modifiedAt.isValid()) {
        return QFileInfo(entry.filePath).fileName();
    }
    return entry.modifiedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QList<BackupRestoreEntry> backupRestoreEntriesForAutosaveDirectory(const QString& autosaveDirectoryPath)
{
    QList<BackupRestoreEntry> entries;
    if (autosaveDirectoryPath.isEmpty()) {
        return entries;
    }

    const QString latestPath = autosaveLatestFilePath(autosaveDirectoryPath);
    const QFileInfo latestInfo(latestPath);
    if (latestInfo.exists() && latestInfo.isFile()) {
        entries.append(BackupRestoreEntry{latestInfo.absoluteFilePath(), latestInfo.lastModified(), 1});
    }

    const QString crashRecoveryPath = QDir(autosaveDirectoryPath).filePath(
        QFileInfo(autosaveDirectoryPath).fileName() + QStringLiteral(".crash_recovery")
    );
    const QFileInfo crashRecoveryInfo(crashRecoveryPath);
    if (crashRecoveryInfo.exists() && crashRecoveryInfo.isFile()) {
        entries.append(BackupRestoreEntry{crashRecoveryInfo.absoluteFilePath(), crashRecoveryInfo.lastModified(), 2});
    }

    QDir historyDir(autosaveHistoryDirectoryPath(autosaveDirectoryPath));
    const QFileInfoList historyFiles = historyDir.entryInfoList(
        QStringList{QStringLiteral("*.bak")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name
    );
    for (const QFileInfo& historyInfo : historyFiles) {
        entries.append(BackupRestoreEntry{historyInfo.absoluteFilePath(), historyInfo.lastModified(), 0});
    }

    std::sort(entries.begin(), entries.end(), [](const BackupRestoreEntry& lhs, const BackupRestoreEntry& rhs) {
        if (lhs.modifiedAt != rhs.modifiedAt) {
            return lhs.modifiedAt > rhs.modifiedAt;
        }
        return lhs.priority > rhs.priority;
    });
    return entries;
}

bool ensureDirectoryExists(const QString& directoryPath)
{
    if (directoryPath.isEmpty()) {
        return false;
    }
    QDir dir(directoryPath);
    return dir.exists() || QDir().mkpath(directoryPath);
}

}  // namespace

bool MainWindow::DocumentSection::maybeSaveBeforeContinue()
{
    QElapsedTimer totalTimer;
    totalTimer.start();

    QElapsedTimer autosaveTimer;
    autosaveTimer.start();
    runAutosaveCheck(false);
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("autosave_check"),
        autosaveTimer.elapsed(),
        QStringLiteral("trigger=maybe_save_before_continue allow_history=0")
    );

    if (!state_.documentDirty_ && !state_.currentFieldDirty_) {
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_before_continue"),
            totalTimer.elapsed(),
            QStringLiteral("result=clean_document")
        );
        return true;
    }

    QElapsedTimer dialogTimer;
    dialogTimer.start();
    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        &owner_,
        uiText("dialog.unsaved_changes.title", "Unsaved Changes"),
        uiText("dialog.unsaved_changes.message", "Current document has unsaved changes. Save before continue?")
    );
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("unsaved_document_dialog"),
        dialogTimer.elapsed(),
        QStringLiteral("choice=%1").arg(unsavedChangesChoiceName(choice))
    );
    if (choice == UnsavedChangesChoice::Save) {
        QElapsedTimer saveTimer;
        saveTimer.start();
        const bool saved = onSaveFile();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("on_save_file"),
            saveTimer.elapsed(),
            QStringLiteral("trigger=maybe_save_before_continue result=%1")
                .arg(saved ? QStringLiteral("saved") : QStringLiteral("failed"))
        );
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_before_continue"),
            totalTimer.elapsed(),
            QStringLiteral("result=%1").arg(saved ? QStringLiteral("saved") : QStringLiteral("save_failed"))
        );
        return saved;
    }
    const bool shouldContinue = choice == UnsavedChangesChoice::Discard;
    if (shouldContinue) {
        anchorCurrentFieldCleanState();
        state_.documentDirty_ = false;
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        owner_.updateWindowTitle();
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("maybe_save_before_continue"),
        totalTimer.elapsed(),
        QStringLiteral("result=%1").arg(shouldContinue ? QStringLiteral("discard") : QStringLiteral("cancel"))
    );
    return shouldContinue;
}

bool MainWindow::DocumentSection::maybeSaveCurrentFieldChanges()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (!state_.currentFieldDirty_) {
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_current_field_changes"),
            totalTimer.elapsed(),
            QStringLiteral("result=clean_field")
        );
        return true;
    }

    const QString fieldName = owner_.hasActiveDifficulty()
        ? SimaiDocument::difficultyName(state_.activeDifficultyId_)
        : uiText("dialog.unsaved_field_changes.field.metadata", "Metadata");
    QElapsedTimer dialogTimer;
    dialogTimer.start();
    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        &owner_,
        uiText("dialog.unsaved_field_changes.title", "Unsaved Field Changes"),
        uiText("dialog.unsaved_field_changes.message", "%1 has unsaved changes. Save before switch?").arg(fieldName)
    );
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("unsaved_field_changes_dialog"),
        dialogTimer.elapsed(),
        QStringLiteral("choice=%1 field=%2")
            .arg(unsavedChangesChoiceName(choice), fieldName)
    );
    if (choice == UnsavedChangesChoice::Save) {
        QElapsedTimer applyTimer;
        applyTimer.start();
        const bool wasDocumentDirty = state_.documentDirty_;
        const bool applied = applyCurrentFieldToDocument();
        if (applied) {
            state_.documentDirty_ = wasDocumentDirty;
            updateDirtyState();
            owner_.updateWindowTitle();
        }
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("apply_current_field_to_document"),
            applyTimer.elapsed(),
            QStringLiteral("trigger=unsaved_field_changes result=%1 field=%2")
                .arg(applied ? QStringLiteral("applied") : QStringLiteral("failed"), fieldName)
        );
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_current_field_changes"),
            totalTimer.elapsed(),
            QStringLiteral("result=%1 field=%2")
                .arg(applied ? QStringLiteral("saved") : QStringLiteral("save_failed"), fieldName)
        );
        return applied;
    }
    if (choice == UnsavedChangesChoice::Discard) {
        if (owner_.hasActiveDifficulty()) {
            populateDifficultyPage(state_.activeDifficultyId_);
        } else if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
            populateMetadataPage();
        }
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_current_field_changes"),
            totalTimer.elapsed(),
            QStringLiteral("result=discard field=%1").arg(fieldName)
        );
        return true;
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("maybe_save_current_field_changes"),
        totalTimer.elapsed(),
        QStringLiteral("result=cancel field=%1").arg(fieldName)
    );
    return false;
}

bool MainWindow::DocumentSection::applyCurrentFieldToDocument()
{
    bool changed = false;
    bool metadataTimingChanged = false;
    bool designerBroadcastNeeded = false;
    QString broadcastDesignerValue;
    if (owner_.hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = state_.document_.ensureDifficulty(state_.activeDifficultyId_);
        const QString newLevel = ui_.difficultyLevelEdit_ != nullptr ? ui_.difficultyLevelEdit_->text() : QString();
        const QString newDesigner = ui_.difficultyDesignerEdit_ != nullptr ? ui_.difficultyDesignerEdit_->text() : QString();
        const QString newChart = owner_.editorText();
        const bool designerEdited = (difficultyData.designer != newDesigner);
        if (difficultyData.level != newLevel || designerEdited || difficultyData.chart != newChart) {
            difficultyData.level = newLevel;
            difficultyData.designer = newDesigner;
            difficultyData.chart = newChart;
            changed = true;
        }
        if (state_.unifiedDesignerEnabled_ && designerEdited) {
            designerBroadcastNeeded = true;
            broadcastDesignerValue = newDesigner;
        }
    } else {
        const QString newTitle = ui_.titleEdit_ != nullptr ? ui_.titleEdit_->text() : QString();
        const QString newArtist = ui_.artistEdit_ != nullptr ? ui_.artistEdit_->text() : QString();
        const QString newFirst = ui_.firstEdit_ != nullptr ? ui_.firstEdit_->text() : QString();
        const QString newDesigner = ui_.designerEdit_ != nullptr ? ui_.designerEdit_->text() : QString();
        const QVector<SimaiRawField> newExtraFields = SimaiDocument::parseRawFields(
            ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
            true
        );
        const bool designerEdited = (state_.document_.designer != newDesigner);
        if (state_.document_.title != newTitle
            || state_.document_.artist != newArtist
            || state_.document_.first != newFirst
            || designerEdited
            || state_.document_.extraFields != newExtraFields) {
            metadataTimingChanged = (state_.document_.first != newFirst);
            state_.document_.title = newTitle;
            state_.document_.artist = newArtist;
            state_.document_.first = newFirst;
            state_.document_.designer = newDesigner;
            state_.document_.extraFields = newExtraFields;
            changed = true;
        }
        if (state_.unifiedDesignerEnabled_ && designerEdited) {
            designerBroadcastNeeded = true;
            broadcastDesignerValue = newDesigner;
        }
    }

    // Broadcast designer name to every other field when the "unified" option
    // is enabled. We do this *after* the just-edited field has been applied
    // so the value being broadcast is always the new one.
    if (designerBroadcastNeeded) {
        if (state_.document_.designer != broadcastDesignerValue) {
            state_.document_.designer = broadcastDesignerValue;
            changed = true;
        }
        const QVector<int> diffIds = state_.document_.difficultyIds();
        for (int diffId : diffIds) {
            SimaiDifficultyData* diff = state_.document_.difficulty(diffId);
            if (diff == nullptr) {
                continue;
            }
            if (diff->designer != broadcastDesignerValue) {
                diff->designer = broadcastDesignerValue;
                changed = true;
            }
        }
        // Reflect the freshly-broadcast value into the inactive editor's UI
        // so users see consistent state when they switch fields. We only
        // touch the editor that's NOT the active source.
        if (owner_.hasActiveDifficulty()) {
            if (ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != broadcastDesignerValue) {
                QSignalBlocker block(ui_.designerEdit_);
                ui_.designerEdit_->setText(broadcastDesignerValue);
            }
        } else {
            if (ui_.difficultyDesignerEdit_ != nullptr && ui_.difficultyDesignerEdit_->text() != broadcastDesignerValue) {
                QSignalBlocker block(ui_.difficultyDesignerEdit_);
                ui_.difficultyDesignerEdit_->setText(broadcastDesignerValue);
            }
        }
    }

    anchorCurrentFieldCleanState();
    state_.currentFieldDirty_ = false;
    if (changed) {
        state_.documentDirty_ = true;
    }
    updateDirtyState();
    owner_.updateWindowTitle();
    rebuildFieldSidebar();
    if (metadataTimingChanged) {
        owner_.refreshWaveformCache();
        owner_.refreshTimelineMetadata();
    }
    return true;
}

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
    const QVector<int> diffIds = doc.difficultyIds();
    survey.perDifficulty.reserve(diffIds.size());
    for (int id : diffIds) {
        const SimaiDifficultyData* diff = doc.difficulty(id);
        if (diff == nullptr) {
            continue;
        }
        survey.perDifficulty.append(qMakePair(id, diff->designer));
        if (!diff->designer.isEmpty() && !survey.distinctNonEmpty.contains(diff->designer)) {
            survey.distinctNonEmpty.append(diff->designer);
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
    if (ui_.unifiedDesignerCheckbox_ != nullptr) {
        QSignalBlocker block(ui_.unifiedDesignerCheckbox_);
        ui_.unifiedDesignerCheckbox_->setChecked(enabled);
    }
}

void MainWindow::DocumentSection::onUnifiedDesignerToggled(bool checked)
{
    MC_OP("MainWindow::DocumentSection::onUnifiedDesignerToggled");
    // Flush any pending in-progress edit so the document state we inspect
    // for the popup logic is the same as what the user can see in the UI.
    applyCurrentFieldToDocument();

    if (!checked) {
        // ON → OFF: just persist the new preference. No content edits, no
        // popup. Per-difficulty designers stay exactly as they were.
        state_.unifiedDesignerEnabled_ = false;
        writeUnifiedDesignerPreference(state_.currentFilePath_, false);
        return;
    }

    // OFF → ON: figure out whether we can flip silently, need to confirm an
    // overwrite, or need to ask the user to pick a canonical name.
    const DesignerSurvey survey = surveyDesigners(state_.document_);

    // Case A: zero or one distinct non-empty designer — nothing to disambiguate.
    //   - No designers at all → silently enable.
    //   - One designer, also matches top &des → silently enable.
    //   - One designer only on per-diff side, top &des empty → silently
    //     adopt the per-diff name as canonical (no destructive overwrite).
    if (survey.distinctNonEmpty.size() <= 1) {
        QString canonical = survey.topDesigner;
        if (canonical.isEmpty() && !survey.distinctNonEmpty.isEmpty()) {
            canonical = survey.distinctNonEmpty.first();
        }
        applyUnifiedDesignerName(canonical);
        state_.unifiedDesignerEnabled_ = true;
        writeUnifiedDesignerPreference(state_.currentFilePath_, true);
        return;
    }

    // Case B: top &des is non-empty AND some per-difficulty designer differs
    // → ask the user to confirm overwriting the differing per-diff names.
    const bool topNonEmpty = !survey.topDesigner.isEmpty();
    bool someDiffDiffers = false;
    for (const auto& pair : survey.perDifficulty) {
        if (!pair.second.isEmpty() && pair.second != survey.topDesigner) {
            someDiffDiffers = true;
            break;
        }
    }
    if (topNonEmpty && someDiffDiffers) {
        QStringList affected;
        for (const auto& pair : survey.perDifficulty) {
            if (!pair.second.isEmpty() && pair.second != survey.topDesigner) {
                affected.append(
                    QStringLiteral("&des_%1 (%2)").arg(pair.first).arg(pair.second));
            }
        }
        const QString detail = affected.join(QStringLiteral("\n"));
        // Top-level line uses the same "&des (name)" shape as the per-diff
        // entries below so the list reads as a single column.
        const QString promptBody = UiText::isChineseUi()
            ? QStringLiteral(
                  "启用「所有难度采用相同名义」后，下列难度的谱师名将被 &des (%1) 覆盖：\n\n%2\n\n"
                  "是否继续？")
                  .arg(survey.topDesigner, detail)
            : QStringLiteral(
                  "Enabling \"All difficulties share this designer\" will overwrite the "
                  "designer of the following difficulties with &des (%1):\n\n%2\n\n"
                  "Continue?")
                  .arg(survey.topDesigner, detail);
        // Manually construct the QMessageBox so we can apply the
        // dark-aware system backdrop (UiDialogs::showMessageBox doesn't
        // expose a hook for that). The dialog still uses the localized
        // Yes/No labels from UiDialogs::execMessageBox.
        QMessageBox confirmDialog(
            QMessageBox::Question,
            UiText::isChineseUi()
                ? QStringLiteral("覆盖各难度谱师名？")
                : QStringLiteral("Overwrite per-difficulty designers?"),
            promptBody,
            QMessageBox::NoButton,
            UiDialogs::effectiveParentWidget(&owner_));
        confirmDialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        confirmDialog.setDefaultButton(QMessageBox::No);
        owner_.windowSection_->applySystemWindowBackdrop(&confirmDialog);
        UiDialogs::prepareDialogWindow(&confirmDialog, &owner_);
        const auto choice = UiDialogs::execMessageBox(&confirmDialog);
        if (choice != QMessageBox::Yes) {
            // User declined → revert checkbox UI to OFF and don't persist.
            if (ui_.unifiedDesignerCheckbox_ != nullptr) {
                QSignalBlocker block(ui_.unifiedDesignerCheckbox_);
                ui_.unifiedDesignerCheckbox_->setChecked(false);
            }
            state_.unifiedDesignerEnabled_ = false;
            return;
        }
        applyUnifiedDesignerName(survey.topDesigner);
        state_.unifiedDesignerEnabled_ = true;
        writeUnifiedDesignerPreference(state_.currentFilePath_, true);
        return;
    }

    // Case C: top &des is empty (or matches everything except some per-diff
    // designers) AND multiple distinct per-difficulty designers exist →
    // show a picker so the user can choose which name becomes canonical.
    QString picked;
    bool pickerAccepted = promptCanonicalDesignerName(survey.distinctNonEmpty, &picked);
    if (!pickerAccepted) {
        if (ui_.unifiedDesignerCheckbox_ != nullptr) {
            QSignalBlocker block(ui_.unifiedDesignerCheckbox_);
            ui_.unifiedDesignerCheckbox_->setChecked(false);
        }
        state_.unifiedDesignerEnabled_ = false;
        return;
    }
    applyUnifiedDesignerName(picked);
    state_.unifiedDesignerEnabled_ = true;
    writeUnifiedDesignerPreference(state_.currentFilePath_, true);
}

void MainWindow::DocumentSection::applyUnifiedDesignerName(const QString& canonicalName)
{
    bool changed = false;
    if (state_.document_.designer != canonicalName) {
        state_.document_.designer = canonicalName;
        changed = true;
    }
    const QVector<int> diffIds = state_.document_.difficultyIds();
    for (int id : diffIds) {
        SimaiDifficultyData* diff = state_.document_.difficulty(id);
        if (diff != nullptr && diff->designer != canonicalName) {
            diff->designer = canonicalName;
            changed = true;
        }
    }
    if (ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != canonicalName) {
        QSignalBlocker block(ui_.designerEdit_);
        ui_.designerEdit_->setText(canonicalName);
    }
    if (ui_.difficultyDesignerEdit_ != nullptr && ui_.difficultyDesignerEdit_->text() != canonicalName) {
        QSignalBlocker block(ui_.difficultyDesignerEdit_);
        ui_.difficultyDesignerEdit_->setText(canonicalName);
    }
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
    dialog.setWindowTitle(UiText::isChineseUi()
        ? QStringLiteral("选择统一的谱师名")
        : QStringLiteral("Pick the canonical designer"));
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
        UiText::isChineseUi()
            ? QStringLiteral(
                  "当前谱面在 &des 和各 &des_N 中检测到多个不同的谱师名义。\n"
                  "请选择一个作为统一名义（将写入到所有难度），或选择「直接清除」让所有字段变为空。")
            : QStringLiteral(
                  "This chart has multiple distinct designer names across &des and the "
                  "per-difficulty &des_N fields. Pick one to use everywhere, or choose "
                  "\"Clear all\" to make every field empty."),
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
    // "Clear all" lives at the end of the list. Its chosen-id is the
    // sentinel kClearAllId so callers can tell it apart from real names.
    constexpr int kClearAllId = -1;
    auto* clearAllRadio = new QRadioButton(
        UiText::isChineseUi()
            ? QStringLiteral("直接清除")
            : QStringLiteral("Clear all"),
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

void MainWindow::DocumentSection::onNewFile()
{
    MC_OP("MainWindow::DocumentSection::onNewFile");
    if (!maybeSaveBeforeContinue()) {
        return;
    }

    const QString targetDirectory = QFileDialog::getExistingDirectory(
        &owner_,
        UiText::isChineseUi() ? QStringLiteral("选择谱面文件夹") : QStringLiteral("Select Chart Folder"),
        owner_.resolveInitialOpenDirectory(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (targetDirectory.isEmpty()) {
        return;
    }

    const QString normalizedDirectory = QDir::cleanPath(targetDirectory);
    const QString targetPath = QDir(normalizedDirectory).filePath(QStringLiteral("maidata.txt"));
    if (QFileInfo::exists(targetPath)) {
        const QMessageBox::StandardButton choice = UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            UiText::isChineseUi() ? QStringLiteral("文件已存在") : QStringLiteral("File Already Exists"),
            UiText::isChineseUi()
                ? QStringLiteral("所选文件夹下已存在 maidata.txt，是否覆盖？")
                : QStringLiteral("maidata.txt already exists in the selected folder. Overwrite it?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return;
        }
    }

    const SimaiDocument newDocument = SimaiDocument::createEmpty();
    QStringEncoder encoder(QStringConverter::Utf8);
    const QByteArray payload = encoder.encode(newDocument.toText());
    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Create Failed", "Cannot write file:\n" + targetPath);
        return;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Create Failed", "Write failed:\n" + targetPath);
        return;
    }

    cancelPendingStartupRestore();
    loadDocument(newDocument);
    owner_.clearValidationCache();
    state_.currentEncoding_ = TextEncoding::Utf8;
    owner_.setCurrentFilePath(targetPath);
    owner_.statusBar()->showMessage(QString("Created: %1").arg(targetPath));
}

void MainWindow::DocumentSection::onOpenFile()
{
    MC_OP("MainWindow::DocumentSection::onOpenFile");
    owner_.windowSection_->logTopLevelWindowSnapshot("open_file_flow/begin");
    const bool canContinue = maybeSaveBeforeContinue();
    if (!canContinue) {
        owner_.windowSection_->logTopLevelWindowSnapshot("open_file_flow/cancelled_before_dialog");
        return;
    }

    owner_.windowSection_->logWindowGeometryDebug("open_file_before_dialog");
    owner_.windowSection_->logTopLevelWindowSnapshot("open_file_before_dialog");
    const QString path = QFileDialog::getOpenFileName(
        &owner_,
        QStringLiteral("Open simai file"),
        owner_.resolveInitialOpenDirectory(),
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    owner_.windowSection_->logWindowGeometryDebug("open_file_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    owner_.windowSection_->logTopLevelWindowSnapshot("open_file_after_dialog");
    if (path.isEmpty()) {
        return;
    }
    openFileAtPath(path, true, true);
}

bool MainWindow::DocumentSection::openFileAtPath(const QString& path, bool showStatusMessage, bool showErrors)
{
    MC_OP("MainWindow::DocumentSection::openFileAtPath");
    _mc_op_.note(QStringLiteral("path=%1").arg(path));
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty path"));
        return false;
    }

    cancelPendingStartupRestore();
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
    if (!payload.success) {
        if (showErrors) {
            UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Open Failed", "Cannot open file:\n" + normalizedPath);
        }
        _mc_op_.fail(QStringLiteral("prepareDocumentOpenPayload failed"));
        return false;
    }

    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8,
        payload.document,
        showStatusMessage,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

bool MainWindow::DocumentSection::restoreLastSessionFile()
{
    MC_OP("MainWindow::DocumentSection::restoreLastSessionFile");
    _mc_op_.note(QStringLiteral("path=%1").arg(state_.lastSessionFilePath_));
    if (state_.lastSessionFilePath_.isEmpty()) {
        return false;  // not a failure — first run or cleared session
    }
    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        _mc_op_.fail(QStringLiteral("session_file_missing"));
        state_.lastSessionFilePath_.clear();
        return false;
    }
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(fileInfo.absoluteFilePath(), true);
    if (!payload.success) {
        _mc_op_.fail(QStringLiteral("prepareDocumentOpenPayload failed"));
        return false;
    }
    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8,
        payload.document,
        false,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

void MainWindow::DocumentSection::scheduleStartupRestoreLastSessionFile()
{
    if (!state_.autoRestoreLastSessionFile_ || state_.lastSessionFilePath_.isEmpty()) {
        state_.startupRestorePending_ = false;
        return;
    }

    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        state_.lastSessionFilePath_.clear();
        state_.startupRestorePending_ = false;
        return;
    }

    state_.startupRestorePending_ = true;
    const quint64 generation = ++state_.startupRestoreGeneration_;
    const QString normalizedPath = fileInfo.absoluteFilePath();
    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = state_.previewWarmupPool_ != nullptr
        ? state_.previewWarmupPool_
        : QThreadPool::globalInstance();
    pool->start([guard, generation, normalizedPath]() {
        const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, payload]() {
                if (guard.isNull() || generation != guard->state_.startupRestoreGeneration_ || !guard->state_.startupRestorePending_) {
                    return;
                }

                guard->state_.startupRestorePending_ = false;
                if (!payload.success) {
                    appendStartupTimingStage("mainwindow/startup_restore_prepare_failed", 0, 0);
                    guard->scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
                    return;
                }

                PreparedStartupRestoreDocument prepared;
                prepared.generation = generation;
                prepared.normalizedPath = payload.normalizedPath;
                prepared.document = payload.document;
                prepared.encodingUsed = payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8;
                prepared.resolvedTrackPath = payload.resolvedTrackPath;
                prepared.trackDurationSeconds = payload.trackDurationSeconds;
                prepared.hasTrackDuration = payload.hasTrackDuration;
                prepared.readElapsedMs = payload.readElapsedMs;
                prepared.decodeElapsedMs = payload.decodeElapsedMs;
                prepared.parseElapsedMs = payload.parseElapsedMs;
                prepared.trackProbeElapsedMs = payload.trackProbeElapsedMs;
                prepared.totalElapsedMs = payload.totalElapsedMs;
                guard->applyPreparedStartupRestoreDocument(prepared);
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::DocumentSection::cancelPendingStartupRestore()
{
    if (!state_.startupRestorePending_) {
        return;
    }
    state_.startupRestorePending_ = false;
    ++state_.startupRestoreGeneration_;
}

void MainWindow::DocumentSection::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
{
    if (prepared.generation != state_.startupRestoreGeneration_) {
        return;
    }

    appendStartupTimingStage("mainwindow/startup_restore_read_bytes", prepared.readElapsedMs, prepared.readElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_decode_text", prepared.decodeElapsedMs, prepared.decodeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_parse_document", prepared.parseElapsedMs, prepared.parseElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_probe_track_duration", prepared.trackProbeElapsedMs, prepared.trackProbeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_prepare_total", prepared.totalElapsedMs, prepared.totalElapsedMs);

    QElapsedTimer applyTimer;
    applyTimer.start();
    applyOpenedDocumentState(
        prepared.normalizedPath,
        prepared.encodingUsed,
        prepared.document,
        false,
        prepared.hasTrackDuration ? prepared.trackDurationSeconds : -1.0
    );
    const qint64 applyElapsedMs = applyTimer.elapsed();
    appendStartupTimingStage("mainwindow/startup_restore_apply_document_ui", applyElapsedMs, applyElapsedMs);
    appendStartupTimingStage(
        "mainwindow/restored_last_document_applied",
        prepared.totalElapsedMs + applyElapsedMs,
        prepared.totalElapsedMs + applyElapsedMs
    );
    owner_.scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::DocumentSection::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    bool showStatusMessage,
    double knownTrackDurationSeconds)
{
    MC_OP("MainWindow::DocumentSection::applyOpenedDocumentState");
    _mc_op_.note(QStringLiteral("path=%1 dur=%2")
                     .arg(normalizedPath)
                     .arg(knownTrackDurationSeconds, 0, 'f', 3));
    state_.currentEncoding_ = encodingUsed;
    owner_.applyWaveformData(
        miacode::waveform::makeWaveformPlaceholder(
            knownTrackDurationSeconds > 0.0 ? knownTrackDurationSeconds : 0.0));
    owner_.setCurrentFilePath(normalizedPath, true);
    owner_.addRecentFilePath(normalizedPath);

    // Eagerly create the crash-recovery directory BEFORE the user can
    // edit. Without this, a crash in the first ~1 ms after a keystroke
    // (before the lazy mkpath inside updateSnapshot has run) would find
    // the parent directory missing and fail CreateFileW. mkpath is
    // re-entrant and cheap on warm runs (one stat()).
    miacode::crash_recovery::prepareForChart(normalizedPath);

    // Crash-recovery prompt — if a recovery file exists for this chart
    // and its content differs from what's on disk, offer the user the
    // chance to restore the unsaved changes from before the crash.
    // Only fires on the path where the user actually opens a chart;
    // restoreLastSessionFile and other internal callers also funnel
    // through here so they get the prompt too.
    SimaiDocument documentToLoad = document;
    bool restoredFromCrash = false;
    {
        const QByteArray recoveryUtf8 =
            miacode::crash_recovery::readRecoveryFile(normalizedPath);
        if (!recoveryUtf8.isEmpty()) {
            const QString recoveryText = QString::fromUtf8(recoveryUtf8);
            const QString diskText = document.toText();
            if (!recoveryText.isEmpty() && recoveryText != diskText) {
                const auto choice = UiDialogs::showMessageBox(
                    QMessageBox::Question,
                    &owner_,
                    uiText("dialog.crash_recovery.title", "Recover Unsaved Changes"),
                    uiText(
                        "dialog.crash_recovery.message",
                        "MiaCode appears to have closed unexpectedly while editing this chart.\n\n"
                        "Recover the unsaved changes from the previous session?\n\n"
                        "Choosing No will discard the recovery file."),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes);
                if (choice == QMessageBox::Yes) {
                    documentToLoad = SimaiDocument::fromText(recoveryText);
                    restoredFromCrash = true;
                }
            }
            // Always delete the recovery file once it's been considered
            // — keeping it around would prompt repeatedly even after
            // the user declined or has resaved.
            miacode::crash_recovery::deleteRecoveryFile(normalizedPath);
        }
    }

    loadDocument(documentToLoad);
    owner_.refreshWaveformCache(knownTrackDurationSeconds);
    if (restoredFromCrash) {
        owner_.statusBar()->showMessage(
            uiText("status.crash_recovery.loaded", "Recovered unsaved changes from previous session. Save to keep them."),
            10000);
        // Mark the current text as a fresh edit baseline so the regular
        // autosave timers see it as dirty vs the on-disk content.
        markCurrentFieldDirty();
    } else if (showStatusMessage) {
        owner_.statusBar()->showMessage(
            QString("Opened: %1 (%2)")
                .arg(QFileInfo(normalizedPath).fileName())
                .arg(encodingUsed == TextEncoding::Utf8 ? "UTF-8" : "System encoding")
        );
    }
}

void MainWindow::DocumentSection::resetAutosaveState(const QString& referenceText)
{
    state_.autosaveReferenceContentSignature_ = autosaveContentSignature(referenceText);
    state_.autosaveLastLatestContentSignature_.clear();
    state_.autosaveLastHistoryContentSignature_.clear();
    state_.autosaveLastHistorySnapshotMs_ = kAutosaveUnsetTimestampMs;
    state_.autosaveDirtySinceMs_ = kAutosaveUnsetTimestampMs;

    // Crash-recovery: a fresh document state means the on-disk content
    // is now the truth — clear any stale crash-recovery file and the
    // in-memory snapshot. If the user was working on a different chart
    // or just saved, we don't want a future crash to write a recovery
    // file with the now-stale content. The next markCurrentFieldDirty
    // refills the snapshot.
    cleanupCrashRecoveryForCleanExit();
}

void MainWindow::DocumentSection::cleanupCrashRecoveryForCleanExit()
{
    if (!state_.currentFilePath_.isEmpty()) {
        miacode::crash_recovery::deleteRecoveryFile(state_.currentFilePath_);
    }
    miacode::crash_recovery::clearSnapshot();
}

QString MainWindow::DocumentSection::resolveAutosaveDirectoryPath() const
{
    return autosaveEntryDirectoryPathForFile(state_.currentFilePath_);
}

void MainWindow::DocumentSection::refreshRestoreBackupMenu(QMenu* restoreBackupMenu)
{
    if (restoreBackupMenu == nullptr) {
        return;
    }
    restoreBackupMenu->clear();

    const QString autosaveDirectoryPath = resolveAutosaveDirectoryPath();
    const QList<BackupRestoreEntry> entries = backupRestoreEntriesForAutosaveDirectory(autosaveDirectoryPath);
    if (entries.isEmpty()) {
        QAction* emptyAction = restoreBackupMenu->addAction(uiText("action.restore_backup.empty", "No Backups Available"));
        emptyAction->setEnabled(false);
        return;
    }

    QSet<QString> usedLabels;
    for (const BackupRestoreEntry& entry : entries) {
        QString label = backupRestoreEntryLabel(entry);
        if (usedLabels.contains(label)) {
            label = QStringLiteral("%1  %2").arg(label, QFileInfo(entry.filePath).fileName());
        }
        usedLabels.insert(label);

        QAction* action = restoreBackupMenu->addAction(label);
        action->setToolTip(QDir::toNativeSeparators(entry.filePath));
        action->setStatusTip(QDir::toNativeSeparators(entry.filePath));
        connect(action, &QAction::triggered, &owner_, [this, path = entry.filePath]() {
            restoreBackupFilePath(path);
        });
    }
}

void MainWindow::DocumentSection::restoreBackupFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const QFileInfo backupInfo(normalizedPath);
    if (normalizedPath.isEmpty() || !backupInfo.exists() || !backupInfo.isFile()) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            uiText("dialog.restore_backup.title", "Restore Backup"),
            uiText(
                "dialog.restore_backup.missing",
                QStringLiteral("Backup file does not exist:\n%1").arg(QDir::toNativeSeparators(normalizedPath)))
        );
        return;
    }

    const auto choice = UiDialogs::showMessageBox(
        QMessageBox::Question,
        &owner_,
        uiText("dialog.restore_backup.title", "Restore Backup"),
        uiText(
            "dialog.restore_backup.confirm",
            QStringLiteral("Restore this backup?\n\n%1\n\nThe current editor content will be replaced, but the original file will not be overwritten.")
                .arg(QDir::toNativeSeparators(normalizedPath))),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }

    QString diskReferenceText = state_.document_.toText();
    if (!state_.currentFilePath_.isEmpty()) {
        QFile currentFile(state_.currentFilePath_);
        if (currentFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray currentBytes = currentFile.readAll();
            if (currentBytes.startsWith("\xEF\xBB\xBF")) {
                diskReferenceText = QString::fromUtf8(currentBytes.mid(3));
            } else {
                QStringDecoder utf8Decoder(QStringConverter::Utf8);
                diskReferenceText = utf8Decoder.decode(currentBytes);
                if (utf8Decoder.hasError()) {
                    QStringDecoder systemDecoder(QStringConverter::System);
                    diskReferenceText = systemDecoder.decode(currentBytes);
                }
            }
        }
    }

    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            uiText("dialog.restore_backup.title", "Restore Backup"),
            uiText(
                "dialog.restore_backup.read_failed",
                QStringLiteral("Cannot read backup file:\n%1").arg(QDir::toNativeSeparators(normalizedPath)))
        );
        return;
    }

    QString backupText;
    const QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        backupText = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        backupText = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            backupText = systemDecoder.decode(bytes);
        }
    }

    loadDocument(SimaiDocument::fromText(backupText));
    state_.autosaveReferenceContentSignature_ = autosaveContentSignature(diskReferenceText);
    state_.autosaveLastLatestContentSignature_.clear();
    state_.autosaveLastHistoryContentSignature_.clear();
    state_.autosaveLastHistorySnapshotMs_ = kAutosaveUnsetTimestampMs;
    state_.documentDirty_ = true;
    state_.currentFieldDirty_ = true;
    updateDirtyState();
    owner_.scheduleTimelineRefresh();
    owner_.statusBar()->showMessage(
        uiText("status.restore_backup.loaded", "Restored from backup. Save to keep the changes."),
        10000
    );
}

QString MainWindow::DocumentSection::currentDocumentTextForAutosave() const
{
    SimaiDocument snapshot = state_.document_;
    if (owner_.hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = snapshot.ensureDifficulty(state_.activeDifficultyId_);
        difficultyData.level = ui_.difficultyLevelEdit_ != nullptr ? ui_.difficultyLevelEdit_->text() : QString();
        difficultyData.designer = ui_.difficultyDesignerEdit_ != nullptr ? ui_.difficultyDesignerEdit_->text() : QString();
        difficultyData.chart = owner_.editorText();
    } else if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        snapshot.title = ui_.titleEdit_ != nullptr ? ui_.titleEdit_->text() : QString();
        snapshot.artist = ui_.artistEdit_ != nullptr ? ui_.artistEdit_->text() : QString();
        snapshot.first = ui_.firstEdit_ != nullptr ? ui_.firstEdit_->text() : QString();
        snapshot.designer = ui_.designerEdit_ != nullptr ? ui_.designerEdit_->text() : QString();
        snapshot.extraFields = SimaiDocument::parseRawFields(
            ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
            true
        );
    }
    return snapshot.toText();
}

void MainWindow::DocumentSection::pruneAutosaveFiles(const QString& autosaveDirectoryPath) const
{
    QDir historyDir(autosaveHistoryDirectoryPath(autosaveDirectoryPath));
    QFileInfoList autosaveFiles = historyDir.entryInfoList(
        QStringList{QStringLiteral("*.bak")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name
    );
    if (autosaveFiles.size() <= kAutosaveHistoryMaxVersions) {
        return;
    }

    const int removeCount = autosaveFiles.size() - kAutosaveHistoryMaxVersions;
    for (int index = 0; index < removeCount; ++index) {
        historyDir.remove(autosaveFiles.at(index).fileName());
    }
}

void MainWindow::DocumentSection::rebuildAutosaveMetadata(const QString& autosaveDirectoryPath) const
{
    if (autosaveDirectoryPath.isEmpty() || state_.currentFilePath_.isEmpty() || !ensureDirectoryExists(autosaveDirectoryPath)) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(kAutosaveMetadataSchema));
    root.insert(QStringLiteral("source_path"), QDir::cleanPath(state_.currentFilePath_));
    root.insert(
        QStringLiteral("source_relative_path"),
        QFileInfo(state_.currentFilePath_).fileName()
    );
    root.insert(QStringLiteral("latest_file"), QFileInfo(autosaveLatestFilePath(autosaveDirectoryPath)).fileName());
    root.insert(QStringLiteral("history_dir"), QStringLiteral("history"));
    root.insert(QStringLiteral("history_limit"), kAutosaveHistoryMaxVersions);
    root.insert(QStringLiteral("history_interval_ms"), kAutosaveIntervalMs);
    root.insert(
        QStringLiteral("saved_reference_signature"),
        QString::fromLatin1(state_.autosaveReferenceContentSignature_.toHex())
    );

    const QString latestFilePath = autosaveLatestFilePath(autosaveDirectoryPath);
    const QFileInfo latestInfo(latestFilePath);
    if (latestInfo.exists() && latestInfo.isFile()) {
        QJsonObject latest;
        latest.insert(QStringLiteral("file"), latestInfo.fileName());
        latest.insert(QStringLiteral("size_bytes"), static_cast<qint64>(latestInfo.size()));
        latest.insert(QStringLiteral("modified_at"), latestInfo.lastModified().toUTC().toString(Qt::ISODateWithMs));
        root.insert(QStringLiteral("latest"), latest);
    }

    const QString historyDirectoryPath = autosaveHistoryDirectoryPath(autosaveDirectoryPath);
    QDir historyDir(historyDirectoryPath);
    QFileInfoList historyFiles = historyDir.entryInfoList(
        QStringList{QStringLiteral("*.bak")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name
    );
    QJsonArray historyArray;
    for (const QFileInfo& historyInfo : historyFiles) {
        QJsonObject entry;
        entry.insert(QStringLiteral("file"), QStringLiteral("history/%1").arg(historyInfo.fileName()));
        entry.insert(QStringLiteral("size_bytes"), static_cast<qint64>(historyInfo.size()));
        entry.insert(QStringLiteral("modified_at"), historyInfo.lastModified().toUTC().toString(Qt::ISODateWithMs));
        historyArray.append(entry);
    }
    root.insert(QStringLiteral("history"), historyArray);
    root.insert(QStringLiteral("history_count"), historyArray.size());
    root.insert(QStringLiteral("updated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QSaveFile metadataFile(autosaveMetadataFilePath(autosaveDirectoryPath));
    if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (metadataFile.write(payload) != payload.size()) {
        return;
    }
    metadataFile.commit();
}

void MainWindow::DocumentSection::runAutosaveCheck(bool allowHistory)
{
    if (state_.currentFilePath_.isEmpty()) {
        return;
    }

    const QString autosaveDirectoryPath = resolveAutosaveDirectoryPath();
    if (autosaveDirectoryPath.isEmpty()) {
        return;
    }

    const QFileInfo autosaveDirectoryInfo(autosaveDirectoryPath);
    const QString metadataFilePath = autosaveMetadataFilePath(autosaveDirectoryPath);
    if (!QFileInfo::exists(metadataFilePath) && autosaveDirectoryInfo.exists() && autosaveDirectoryInfo.isDir()) {
        rebuildAutosaveMetadata(autosaveDirectoryPath);
    }

    const QString snapshotText = currentDocumentTextForAutosave();
    const QByteArray snapshotSignature = autosaveContentSignature(snapshotText);
    if (snapshotSignature == state_.autosaveReferenceContentSignature_) {
        state_.autosaveDirtySinceMs_ = kAutosaveUnsetTimestampMs;
        return;
    }

    if (!ensureDirectoryExists(autosaveDirectoryPath)) {
        return;
    }

    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (state_.autosaveDirtySinceMs_ < 0) {
        state_.autosaveDirtySinceMs_ = nowMs;
    }

    const QString latestFilePath = autosaveLatestFilePath(autosaveDirectoryPath);
    const bool latestExists = QFileInfo::exists(latestFilePath);
    if (!latestExists || snapshotSignature != state_.autosaveLastLatestContentSignature_) {
        QSaveFile latestFile(latestFilePath);
        if (!latestFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }

        const QByteArray payload = snapshotText.toUtf8();
        if (latestFile.write(payload) != payload.size() || !latestFile.commit()) {
            return;
        }
        state_.autosaveLastLatestContentSignature_ = snapshotSignature;
    }

    const qint64 historyReferenceMs = state_.autosaveLastHistorySnapshotMs_ >= 0
        ? state_.autosaveLastHistorySnapshotMs_
        : state_.autosaveDirtySinceMs_;
    const bool historyDue = historyReferenceMs >= 0 && nowMs - historyReferenceMs >= kAutosaveIntervalMs;
    if (allowHistory && historyDue && snapshotSignature != state_.autosaveLastHistoryContentSignature_) {
        const QString historyDirectoryPath = autosaveHistoryDirectoryPath(autosaveDirectoryPath);
        if (!ensureDirectoryExists(historyDirectoryPath)) {
            return;
        }

        const QString historyFileName = autosaveTimestampStringUtcNow() + QStringLiteral(".bak");
        QSaveFile historyFile(QDir(historyDirectoryPath).filePath(historyFileName));
        if (!historyFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }

        const QByteArray payload = snapshotText.toUtf8();
        if (historyFile.write(payload) != payload.size() || !historyFile.commit()) {
            return;
        }
        state_.autosaveLastHistoryContentSignature_ = snapshotSignature;
        state_.autosaveLastHistorySnapshotMs_ = nowMs;
        pruneAutosaveFiles(autosaveDirectoryPath);
    }

    rebuildAutosaveMetadata(autosaveDirectoryPath);
}

bool MainWindow::DocumentSection::onSaveFile()
{
    MC_OP("MainWindow::DocumentSection::onSaveFile");
    if (state_.currentFilePath_.isEmpty()) {
        return onSaveFileAs();
    }
    return saveToPath(state_.currentFilePath_);
}

bool MainWindow::DocumentSection::onSaveFileAs()
{
    MC_OP("MainWindow::DocumentSection::onSaveFileAs");
    owner_.windowSection_->logTopLevelWindowSnapshot("save_file_as_dialog/begin");
    owner_.windowSection_->logWindowGeometryDebug("save_file_as_before_dialog");
    const QString path = QFileDialog::getSaveFileName(
        &owner_,
        QStringLiteral("Save simai file"),
        state_.currentFilePath_.isEmpty() ? QStringLiteral("chart.txt") : state_.currentFilePath_,
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    owner_.windowSection_->logWindowGeometryDebug("save_file_as_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    owner_.windowSection_->logTopLevelWindowSnapshot("save_file_as_dialog/after_dialog");
    if (path.isEmpty()) {
        return false;
    }
    owner_.setLastOpenDirectory(path);
    return saveToPath(path);
}

bool MainWindow::DocumentSection::saveToPath(const QString& path)
{
    MC_OP("MainWindow::DocumentSection::saveToPath");
    _mc_op_.note(QStringLiteral("path=%1").arg(path));
    QElapsedTimer totalTimer;
    totalTimer.start();
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const QString targetName = QFileInfo(normalizedPath.isEmpty() ? path : normalizedPath).fileName();

    QElapsedTimer applyTimer;
    applyTimer.start();
    const bool applied = applyCurrentFieldToDocument();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("apply_current_field_to_document"),
        applyTimer.elapsed(),
        QStringLiteral("trigger=save_to_path result=%1 target=%2")
            .arg(applied ? QStringLiteral("applied") : QStringLiteral("failed"), targetName)
    );
    if (!applied) {
        _mc_op_.fail(QStringLiteral("apply_current_field_to_document failed"));
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=apply_failed target=%1").arg(targetName)
        );
        return false;
    }
    bool firstOk = false;
    (void)owner_.parsedFirstSeconds(&firstOk);
    if (!firstOk) {
        _mc_op_.fail(QStringLiteral("invalid_first"));
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Save Failed", "&first must be a valid number of seconds.");
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=invalid_first target=%1").arg(targetName)
        );
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        _mc_op_.fail(QStringLiteral("QSaveFile::open: %1").arg(file.errorString()));
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Save Failed", "Cannot write file:\n" + path);
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=open_failed target=%1").arg(targetName)
        );
        return false;
    }
    QByteArray data;
    const QString serialized = state_.document_.toText();
    if (state_.currentEncoding_ == TextEncoding::System) {
        QStringEncoder encoder(QStringConverter::System);
        data = encoder.encode(serialized);
    } else {
        QStringEncoder encoder(QStringConverter::Utf8);
        data = encoder.encode(serialized);
    }
    if (file.write(data) != data.size() || !file.commit()) {
        _mc_op_.fail(QStringLiteral("write_or_commit_failed err=%1").arg(file.errorString()));
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Save Failed", "Write failed:\n" + path);
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=write_failed target=%1").arg(targetName)
        );
        return false;
    }
    if (normalizedPath != state_.currentFilePath_) {
        owner_.setCurrentFilePath(normalizedPath);
    }
    owner_.addRecentFilePath(normalizedPath);
    resetAutosaveState(serialized);
    const QString autosaveDirectoryPath = resolveAutosaveDirectoryPath();
    if (!autosaveDirectoryPath.isEmpty()) {
        rebuildAutosaveMetadata(autosaveDirectoryPath);
    }
    state_.documentDirty_ = false;
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    owner_.updateWindowTitle();
    owner_.statusBar()->showMessage("Saved: " + QFileInfo(path).fileName());
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("save_to_path"),
        totalTimer.elapsed(),
        QStringLiteral("result=saved target=%1").arg(targetName)
    );
    return true;
}

bool MainWindow::DocumentSection::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    MC_OP("MainWindow::DocumentSection::applyBatchTransform");
    _mc_op_.note(QStringLiteral("op=%1").arg(opName));
    const QString original = owner_.editorText();
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int changed = 0;
    const QString transformed = transform(original, &changed);
    if (transformed == original) {
        owner_.statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.select(QTextCursor::Document);
    editCursor.insertText(transformed);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    const int restoredAnchor = qBound(0, oldCursor.anchor(), maxPos);
    const int restoredPosition = qBound(0, oldCursor.position(), maxPos);
    restoredCursor.setPosition(restoredAnchor);
    restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    editor->setTextCursor(restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()
        ));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()
        ));
    }

    markCurrentFieldDirty();
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();
    owner_.statusBar()->showMessage(QString("%1 applied: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::DocumentSection::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    MC_OP("MainWindow::DocumentSection::applySelectionBatchTransform");
    _mc_op_.note(QStringLiteral("op=%1").arg(opName));
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int startPos = -1;
    int endPos = -1;
    if (!currentSelectionRange(&startPos, &endPos)) {
        owner_.statusBar()->showMessage(QString("%1: no selection.").arg(opName));
        return false;
    }

    const QString original = owner_.editorText();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    if (begin < 0 || finish <= begin || finish > original.size()) {
        owner_.statusBar()->showMessage(QString("%1: invalid selection range.").arg(opName));
        return false;
    }

    const QString selected = original.mid(begin, finish - begin);
    int changed = 0;
    const QString transformed = transform(selected, &changed);
    if (transformed == selected) {
        owner_.statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    const bool forwardSelection = oldCursor.hasSelection()
        ? (oldCursor.position() >= oldCursor.anchor())
        : true;
    const int originalAnchor = forwardSelection ? begin : finish;
    const int originalPosition = forwardSelection ? finish : begin;

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.setPosition(begin);
    editCursor.setPosition(finish, QTextCursor::KeepAnchor);
    editCursor.insertText(transformed);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    const int transformedEnd = begin + transformed.size();
    const int restoredAnchor = qBound(0, forwardSelection ? begin : transformedEnd, maxPos);
    const int restoredPosition = qBound(0, forwardSelection ? transformedEnd : begin, maxPos);
    restoredCursor.setPosition(restoredAnchor);
    restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    editor->setTextCursor(restoredCursor);
    recordChartSelectionTransformUndoEntry(originalAnchor, originalPosition, restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()
        ));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()
        ));
    }

    markCurrentFieldDirty();
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();
    owner_.statusBar()->showMessage(QString("%1 applied on selection: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::maybeSaveBeforeContinue()
{
    return documentSection_->maybeSaveBeforeContinue();
}

bool MainWindow::maybeSaveCurrentFieldChanges()
{
    return documentSection_->maybeSaveCurrentFieldChanges();
}

bool MainWindow::applyCurrentFieldToDocument()
{
    return documentSection_->applyCurrentFieldToDocument();
}

void MainWindow::onUnifiedDesignerToggled(bool checked)
{
    documentSection_->onUnifiedDesignerToggled(checked);
}

void MainWindow::onNewFile()
{
    documentSection_->onNewFile();
}

void MainWindow::onOpenFile()
{
    documentSection_->onOpenFile();
}

void MainWindow::onOpenCurrentFolder()
{
    const QFileInfo fileInfo(currentFilePath_);
    const QString folderPath = currentFilePath_.isEmpty()
        ? QString()
        : fileInfo.absoluteDir().absolutePath();
    if (!folderPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
    }
}

void MainWindow::addRecentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return;
    }
    recentFilePaths_.removeAll(normalizedPath);
    recentFilePaths_.prepend(normalizedPath);
    while (recentFilePaths_.size() > 10) {
        recentFilePaths_.removeLast();
    }
    savePortableState();
}

void MainWindow::openRecentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return;
    }
    const QFileInfo fileInfo(normalizedPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        recentFilePaths_.removeAll(normalizedPath);
        savePortableState();
        UiDialogs::showMessageBox(QMessageBox::Warning, this, uiText("action.open_recent", "Open Recent"), "File no longer exists:\n" + normalizedPath);
        return;
    }
    openFileAtPath(normalizedPath, true, true);
}

void MainWindow::restoreBackupFilePath(const QString& path)
{
    documentSection_->restoreBackupFilePath(path);
}

void MainWindow::refreshRecentFilesMenu(QMenu* recentFilesMenu)
{
    if (recentFilesMenu == nullptr) {
        return;
    }
    recentFilesMenu->clear();
    QStringList existingPaths;
    QSet<QString> seenPaths;
    for (const QString& path : recentFilePaths_) {
        const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath)) {
            continue;
        }
        const QFileInfo fileInfo(normalizedPath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }
        seenPaths.insert(normalizedPath);
        existingPaths.append(normalizedPath);
    }
    if (existingPaths != recentFilePaths_) {
        recentFilePaths_ = existingPaths;
        savePortableState();
    }
    if (existingPaths.isEmpty()) {
        QAction* emptyAction = recentFilesMenu->addAction(uiText("action.open_recent.empty", "No Recent Files"));
        emptyAction->setEnabled(false);
        return;
    }
    for (const QString& path : existingPaths) {
        const QFileInfo fileInfo(path);
        const QString folderName = fileInfo.absoluteDir().dirName().trimmed();
        const QString displayName = folderName.isEmpty()
            ? QDir::toNativeSeparators(fileInfo.absoluteFilePath())
            : folderName;
        QAction* action = recentFilesMenu->addAction(displayName);
        action->setToolTip(QDir::toNativeSeparators(path));
        action->setStatusTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path]() {
            openRecentFilePath(path);
        });
    }
}

void MainWindow::refreshRestoreBackupMenu(QMenu* restoreBackupMenu)
{
    documentSection_->refreshRestoreBackupMenu(restoreBackupMenu);
}

bool MainWindow::openFileAtPath(const QString& path, bool showStatusMessage, bool showErrors)
{
    return documentSection_->openFileAtPath(path, showStatusMessage, showErrors);
}

bool MainWindow::openStartupTarget(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    const QFileInfo info(normalizedPath);
    if (info.isDir()) {
        const QString maidataPath = QDir(info.absoluteFilePath()).filePath(QStringLiteral("maidata.txt"));
        if (QFileInfo::exists(maidataPath) && QFileInfo(maidataPath).isFile()) {
            return openFileAtPath(maidataPath, true, true);
        }

        setCurrentFilePath(QString(), true);
        loadDocument(SimaiDocument::createEmpty());
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.open_startup_folder.missing_maidata.title", "maidata.txt Not Found"),
            uiText(
                "dialog.open_startup_folder.missing_maidata.message",
                QStringLiteral("No maidata.txt was found in the dropped folder:\n%1")
                    .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
            )
        );
        return false;
    }

    if (info.exists() && info.isFile()) {
        return openFileAtPath(info.absoluteFilePath(), true, true);
    }

    UiDialogs::showMessageBox(
        QMessageBox::Warning,
        this,
        uiText("dialog.open_startup_target.missing.title", "Open Failed"),
        uiText(
            "dialog.open_startup_target.missing.message",
            QStringLiteral("The dropped file or folder does not exist:\n%1")
                .arg(QDir::toNativeSeparators(normalizedPath))
        )
    );
    return false;
}

bool MainWindow::restoreLastSessionFile()
{
    return documentSection_->restoreLastSessionFile();
}

void MainWindow::scheduleStartupRestoreLastSessionFile()
{
    documentSection_->scheduleStartupRestoreLastSessionFile();
}

void MainWindow::cancelPendingStartupRestore()
{
    documentSection_->cancelPendingStartupRestore();
}

void MainWindow::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
{
    documentSection_->applyPreparedStartupRestoreDocument(prepared);
}

void MainWindow::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    bool showStatusMessage,
    double knownTrackDurationSeconds)
{
    documentSection_->applyOpenedDocumentState(
        normalizedPath,
        encodingUsed,
        document,
        showStatusMessage,
        knownTrackDurationSeconds
    );
}

void MainWindow::resetAutosaveState(const QString& referenceText)
{
    documentSection_->resetAutosaveState(referenceText);
}

QString MainWindow::resolveAutosaveDirectoryPath() const
{
    return documentSection_->resolveAutosaveDirectoryPath();
}

QString MainWindow::currentDocumentTextForAutosave() const
{
    return documentSection_->currentDocumentTextForAutosave();
}

void MainWindow::pruneAutosaveFiles(const QString& autosaveDirectoryPath) const
{
    documentSection_->pruneAutosaveFiles(autosaveDirectoryPath);
}

void MainWindow::runAutosaveCheck(bool allowHistory)
{
    documentSection_->runAutosaveCheck(allowHistory);
}

bool MainWindow::onSaveFile()
{
    return documentSection_->onSaveFile();
}

bool MainWindow::onSaveFileAs()
{
    return documentSection_->onSaveFileAs();
}

bool MainWindow::saveToPath(const QString& path)
{
    return documentSection_->saveToPath(path);
}

bool MainWindow::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    return documentSection_->applyBatchTransform(opName, transform);
}

bool MainWindow::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    return documentSection_->applySelectionBatchTransform(opName, transform);
}

std::pair<int, int> MainWindow::currentCursorLineCol() const
{
    return documentSection_->currentCursorLineCol();
}

std::pair<int, int> MainWindow::currentSelectionOrCursorLineCol() const
{
    return documentSection_->currentSelectionOrCursorLineCol();
}

bool MainWindow::currentSelectionRange(int* startPos, int* endPos) const
{
    return documentSection_->currentSelectionRange(startPos, endPos);
}

void MainWindow::setMetadataExtraText(const QString& text)
{
    documentSection_->setMetadataExtraText(text);
}

void MainWindow::setEditorText(const QString& text)
{
    documentSection_->setEditorText(text);
}

void MainWindow::updatePauseButtonAppearance()
{
    documentSection_->updatePauseButtonAppearance();
}

void MainWindow::updateDirtyState()
{
    documentSection_->updateDirtyState();
}

bool MainWindow::currentFieldHasUndoChanges() const
{
    return documentSection_->currentFieldHasUndoChanges();
}

void MainWindow::refreshCurrentFieldDirtyState()
{
    documentSection_->refreshCurrentFieldDirtyState();
}

void MainWindow::markCurrentFieldDirty()
{
    documentSection_->markCurrentFieldDirty();
}

void MainWindow::clearDeletedDifficultyUndoState()
{
    documentSection_->clearDeletedDifficultyUndoState();
}

bool MainWindow::undoDeletedDifficultyField()
{
    return documentSection_->undoDeletedDifficultyField();
}

void MainWindow::updateEditorHeader()
{
    documentSection_->updateEditorHeader();
}

void MainWindow::updateDifficultyScopedActionStates()
{
    documentSection_->updateDifficultyScopedActionStates();
}

void MainWindow::updateEditorHeaderLayoutMode()
{
    documentSection_->updateEditorHeaderLayoutMode();
}

void MainWindow::syncEditorHeaderMinimumWidth()
{
    documentSection_->syncEditorHeaderMinimumWidth();
}

void MainWindow::updateEditorStatus()
{
    documentSection_->updateEditorStatus();
}

void MainWindow::updateEditorEmptyState()
{
    documentSection_->updateEditorEmptyState();
}

void MainWindow::updateMetadataPageMode()
{
    documentSection_->updateMetadataPageMode();
}

bool MainWindow::deleteDifficultyField(int difficultyId)
{
    return documentSection_->deleteDifficultyField(difficultyId);
}

void MainWindow::updateDifficultyDeleteButton(bool visible)
{
    documentSection_->updateDifficultyDeleteButton(visible);
}

void MainWindow::rebuildFieldSidebar()
{
    documentSection_->rebuildFieldSidebar();
}

void MainWindow::populateMetadataPage()
{
    documentSection_->populateMetadataPage();
}

void MainWindow::populateDifficultyPage(int difficultyId)
{
    documentSection_->populateDifficultyPage(difficultyId);
}

bool MainWindow::switchToMetadataField()
{
    return documentSection_->switchToMetadataField();
}

bool MainWindow::switchToWelcomePage()
{
    return documentSection_->switchToWelcomePage();
}

bool MainWindow::switchToDifficultyField(int difficultyId)
{
    return documentSection_->switchToDifficultyField(difficultyId);
}

void MainWindow::activateInitialField()
{
    documentSection_->activateInitialField();
}

void MainWindow::loadDocument(const SimaiDocument& document)
{
    documentSection_->loadDocument(document);
}

void MainWindow::clearTimelineAndPreview()
{
    documentSection_->clearTimelineAndPreview();
}
