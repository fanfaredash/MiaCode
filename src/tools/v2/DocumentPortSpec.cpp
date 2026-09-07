// Contract regression for stage 4.9d-4b-2c's third narrow port.
//
// PlaybackDocumentPort is the coordinator's seam onto document state: dirty-
// flag recomputation, committing whatever field the QML editor is currently
// holding, editor-navigation requests, and a read-only query for the applied
// workspace revision. Like PlaybackValidationPort, this one is cut by host —
// all four methods already belong to DocumentSessionHost.
//
// This target links Qt6::Core + Qt6::Test only. If the port ever grows a
// method that needs Session or a window to implement, FakeDocuments below
// fails to compile it — and if the header itself ever pulls in Session.h or
// a Widgets/QML type, this whole target fails to LINK, which is a stronger
// guarantee than grepping for the forbidden names.

#include "app/v2/PlaybackDocumentPort.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

// A stand-in document presenter. Its only job is to prove the contract can
// be implemented with no Session and no window; the production implementer
// is DocumentSessionHost.
class FakeDocuments final : public miacode::v2::PlaybackDocumentPort
{
public:
    void updateDirtyState() override { ++updateDirtyStateCount; }

    bool applyCurrentFieldToDocument() override
    {
        ++applyCurrentFieldToDocumentCount;
        return applyCurrentFieldResult;
    }

    bool requestEditorNavigation(int line, int column, int endLine, int endColumn,
                                 bool selectToken, bool focusEditor, bool centerView) override
    {
        lastLine = line;
        lastColumn = column;
        lastEndLine = endLine;
        lastEndColumn = endColumn;
        lastSelectToken = selectToken;
        lastFocusEditor = focusEditor;
        lastCenterView = centerView;
        ++requestEditorNavigationCount;
        return requestEditorNavigationResult;
    }

    quint64 appliedWorkspaceRevision() const override { return revision; }

    int updateDirtyStateCount = 0;
    int applyCurrentFieldToDocumentCount = 0;
    bool applyCurrentFieldResult = true;
    int requestEditorNavigationCount = 0;
    bool requestEditorNavigationResult = true;
    int lastLine = 0;
    int lastColumn = 0;
    int lastEndLine = 0;
    int lastEndColumn = 0;
    bool lastSelectToken = false;
    bool lastFocusEditor = false;
    bool lastCenterView = false;
    quint64 revision = 0;
};

bool verifyImplementableWithoutSessionOrAWindow(QTextStream& err)
{
    FakeDocuments documents;
    miacode::v2::PlaybackDocumentPort& contract = documents;

    contract.updateDirtyState();
    contract.updateDirtyState();
    bool ok = require(documents.updateDirtyStateCount == 2,
                      QStringLiteral("updateDirtyState reaches the implementation each call"), err);

    documents.applyCurrentFieldResult = false;
    ok &= require(contract.applyCurrentFieldToDocument() == false,
                  QStringLiteral("applyCurrentFieldToDocument's return value reaches the caller"), err);
    ok &= require(documents.applyCurrentFieldToDocumentCount == 1,
                  QStringLiteral("applyCurrentFieldToDocument reaches the implementation"), err);

    documents.requestEditorNavigationResult = true;
    ok &= require(contract.requestEditorNavigation(3, 5, 3, 9, true, false, true) == true,
                  QStringLiteral("requestEditorNavigation's return value reaches the caller"), err);
    ok &= require(documents.lastLine == 3 && documents.lastColumn == 5
                      && documents.lastEndLine == 3 && documents.lastEndColumn == 9
                      && documents.lastSelectToken && !documents.lastFocusEditor
                      && documents.lastCenterView,
                  QStringLiteral("requestEditorNavigation's arguments reach the implementation"), err);

    documents.revision = 42;
    ok &= require(contract.appliedWorkspaceRevision() == 42,
                  QStringLiteral("appliedWorkspaceRevision reaches the implementation"), err);

    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = verifyImplementableWithoutSessionOrAWindow(err);

    if (ok) {
        QTextStream(stdout) << "document_port_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
