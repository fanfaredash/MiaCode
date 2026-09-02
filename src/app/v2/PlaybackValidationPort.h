#pragma once

#include "common/MuriRenderOptions.h"

namespace miacode::v2 {

// The playback coordinator's second narrow port: a seam onto muri
// validation/analysis presentation. Unlike PlaybackPreferencesPort (cut by
// capability, because its eight methods' eventual owners are split across
// three different hosts), all five methods here already have exactly one
// owner today — ValidationHost — so cutting by host is the honest shape of
// the contract: there is nothing left to split. A capability cut would just
// be an alias for "everything ValidationHost does that playback/ touches",
// with no independent boundary to justify it.
//
// Deliberately free of Session, QWidget, and QML/QSG types: ValidationPortSpec
// proves that at link time, by implementing this port with a fake that pulls
// in neither.
//
// No default arguments here (stage 3.5 precedent: a single-argument overload
// on the interface would make every one-argument call to setMuriRenderMode
// ambiguous against the two-argument virtual). ValidationHost keeps its own
// default argument on the override — a virtual function's default argument
// is resolved statically at the call site, not dispatched, so that default
// only affects callers that already hold a ValidationHost&.
class PlaybackValidationPort
{
public:
    virtual ~PlaybackValidationPort() = default;

    virtual void setMuriRenderMode(RenderMode mode, bool persistState) = 0;
    virtual void clearValidationCache() = 0;
    virtual void clearValidationDecorations() = 0;
    virtual void applyAlignedMuriAnalysisReportToViews() = 0;
    virtual void applyDeferredAnalysisUiUpdates() = 0;
};

}  // namespace miacode::v2
