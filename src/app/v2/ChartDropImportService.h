#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

#include <functional>

namespace miacode::v2 {

struct ChartDropCandidate {
    QString sourcePath;
    QString sourceDirectory;
    QString extension;
    QString targetDirectory;
};

struct ChartDropCreateResult {
    int createdCount = 0;
    int failedCount = 0;
    QString targetPath;
};

struct ChartDropImportResult {
    quint64 requestId = 0;
    quint64 generation = 0;
    bool accepted = false;
    bool completed = false;
    bool cancelled = false;
    int createdCount = 0;
    int failedCount = 0;
    QString targetPath;
};

// Document-specific UI and file operations stay behind this narrow adapter.
// The service owns the request and every continuation; DocumentSection only
// answers one operation at a time and never retains the dropped paths.
struct DocumentImportAdapter {
    using Validate = std::function<QList<ChartDropCandidate>(const QStringList&, QString* error)>;
    using Confirmation = std::function<void(const QList<ChartDropCandidate>&, std::function<void(bool)>)>;
    using LeaveDocument = std::function<void(std::function<void(bool)>)>;
    using CreateCharts = std::function<void(const QList<ChartDropCandidate>&,
                                            std::function<void(const ChartDropCreateResult&)>)>;
    using FinalSwitch = std::function<void(const QString&, std::function<void(bool)>)>;

    Validate validate;
    Confirmation requestFirstConfirmation;
    LeaveDocument requestLeaveDocument;
    CreateCharts createCharts;
    FinalSwitch requestFinalSwitch;
};

class ChartDropImportService final : public QObject
{
public:
    using Completion = std::function<void(const ChartDropImportResult&)>;

    explicit ChartDropImportService(QObject* parent = nullptr);

    // Returns false when another request is already waiting for a UI answer or
    // chart creation. An accepted request always resolves its completion once,
    // including every validation/confirmation failure path.
    bool submit(const QStringList& paths,
                quint64 requestId,
                quint64 generation,
                DocumentImportAdapter adapter,
                Completion completion);
    void release();
    bool busy() const { return pending_; }
    quint64 generation() const { return generation_; }

private:
    enum class Phase {
        None,
        FirstConfirmation,
        LeaveDocument,
        CreateCharts,
        FinalSwitch,
    };

    bool isCurrent(quint64 requestId, quint64 generation) const;
    void complete(const ChartDropImportResult& result);
    void completeCancelled();
    void beginLeaveDocument();
    void beginCreateCharts();
    void beginFinalSwitch(const ChartDropCreateResult& result);

    bool pending_ = false;
    bool released_ = false;
    quint64 generation_ = 0;
    quint64 requestId_ = 0;
    Phase phase_ = Phase::None;
    QList<ChartDropCandidate> candidates_;
    DocumentImportAdapter adapter_;
    Completion completion_;
    ChartDropImportResult result_;
};

} // namespace miacode::v2
