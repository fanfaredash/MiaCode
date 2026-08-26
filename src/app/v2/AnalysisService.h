#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

#include <optional>

#include "ChartWorkspace.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "core/chart/parser/SimaiNativeParser.h"
#include "timeline/TimelineData.h"

namespace miacode::v2 {

// Immutable, revision-stamped output of one workspace analysis transaction.
// A later async scheduler can discard this whole value when its revision no
// longer matches ChartWorkspace; it never needs to inspect a MainWindow cache.
struct AnalysisSnapshot {
    quint64 revision = 0;
    int difficultyId = 0;
    bool available = false;
    bool pending = false;
    SimaiNativeValidationLocale locale = SimaiNativeValidationLocale::English;
    SimaiNativeValidationReport validation;
    QVector<TimelineNoteMarker> noteMarkers;
    QByteArray noteMarkerSignature;
    MuriAnalysisReport muri;
    QVector<MuriStaticReference> muriStaticReferences;
};

class AnalysisService final : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisService(
        ChartWorkspace& workspace,
        SimaiNativeValidationLocale locale = SimaiNativeValidationLocale::English,
        const MuriRenderOptions& renderOptions = {},
        double staticTapOnSlideThresholdSeconds = -1.0,
        QObject* parent = nullptr);

    AnalysisSnapshot snapshot() const;
    void requestAnalysis();

    static AnalysisSnapshot analyze(
        const ChartWorkspace& workspace,
        SimaiNativeValidationLocale locale = SimaiNativeValidationLocale::English,
        const MuriRenderOptions& renderOptions = {},
        double staticTapOnSlideThresholdSeconds = -1.0);

signals:
    // The whole pending/available value is installed before this signal is
    // emitted. Consumers read it once and gate the complete package by the
    // workspace (difficultyId, revision) identity.
    void snapshotChanged(int difficultyId, quint64 revision);
    void analysisReady(int difficultyId, quint64 revision);

private:
    struct AnalysisRequest {
        ChartWorkspaceSnapshot workspace;
        SimaiDocument document;
        SimaiNativeValidationLocale locale = SimaiNativeValidationLocale::English;
        MuriRenderOptions renderOptions;
        double staticTapOnSlideThresholdSeconds = -1.0;
    };

    static AnalysisSnapshot analyzeRequest(AnalysisRequest request);
    void dispatchPendingRequest();
    bool identityIsCurrent(int difficultyId, quint64 revision) const;

    ChartWorkspace* workspace_ = nullptr;
    SimaiNativeValidationLocale locale_ = SimaiNativeValidationLocale::English;
    MuriRenderOptions renderOptions_;
    double staticTapOnSlideThresholdSeconds_ = -1.0;
    AnalysisSnapshot snapshot_;
    std::optional<AnalysisRequest> pendingRequest_;
    bool workerRunning_ = false;
};

}  // namespace miacode::v2
