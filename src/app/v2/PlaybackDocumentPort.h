#pragma once

#include <QtGlobal>

namespace miacode::v2 {

// The playback coordinator's third narrow port: a seam onto document state —
// dirty-flag recomputation, committing whatever field the QML editor is
// currently holding, and editor-navigation requests. Like PlaybackValidationPort
// (cut by host) and unlike PlaybackPreferencesPort (cut by capability, split
// across three hosts), all four methods here already belong to exactly one
// owner — DocumentSessionHost — so cutting by host is the honest shape: there
// is nothing left to split.
//
// appliedWorkspaceRevision() is a read-only query, not a relocation of the
// appliedQmlWorkspaceRevision_ field. That field is semantically document
// domain (written at document/DocumentFileFlow.cpp:748, read there and at
// document/DocumentBridge.cpp:504) but still lives on Session — a leftover
// from before the domain split. Two text-scanning specs
// (QmlDocumentLifecycleContractSpec.cpp, QmlEditorControllerSpec.cpp) pin its
// literal spelling as a Session member, so moving the field is the document
// domain's own cleanup and out of scope here. This port only adds a query
// DocumentSessionHost can answer from the Session& it already holds.
//
// Deliberately free of Session, QWidget, and QML/QSG types: DocumentPortSpec
// proves that at link time, by implementing this port with a fake that pulls
// in neither.
class PlaybackDocumentPort
{
public:
    virtual ~PlaybackDocumentPort() = default;

    virtual void updateDirtyState() = 0;
    virtual bool applyCurrentFieldToDocument() = 0;
    virtual bool requestEditorNavigation(int line, int column, int endLine, int endColumn,
                                         bool selectToken, bool focusEditor, bool centerView) = 0;
    virtual quint64 appliedWorkspaceRevision() const = 0;
};

}  // namespace miacode::v2
