#pragma once

#include "common/PreviewGameplayConfig.h"
#include "core/video/PreviewRenderSettings.h"

#include <QObject>
#include <QString>

namespace miacode::v2 {

// The preview's appearance settings, with an owner that is not a window.
//
// These eight values decide how both the on-screen preview and the exported
// video are drawn, so three callers need them: the preview settings page, the
// export page, and the export task builder. Until now all three reached
// MainWindow's private members through `friend` access, and QmlExportSession
// and QmlPreviewSettingsModel carried two copies of the same read/write logic —
// which is how a "skin picker" and an "export skin picker" drift apart.
//
// This type holds the values and nothing else. It deliberately does NOT know
// about PreviewRuntime, the SFX runtime or the settings file: applying a change
// to the live surfaces and persisting it stay with whoever owns those. That is
// what keeps this header out of Qt Quick and Qt Widgets, so a Core+Gui-only
// spec can link it.
//
// Owned by ApplicationServices.
class PreviewAppearanceState final : public QObject
{
    Q_OBJECT

public:
    // The stored values, exposed as data so MainWindow can bind its same-named
    // members to them by reference and keep reading and writing the single copy
    // without changing a line on its restore/apply paths.
    //
    // That direct door is a transitional concession, not the interface: a write
    // through `values()` does not emit `changed()`. Everything outside the
    // window uses the typed setters below. The door closes with stage 4, when
    // the window that needs it is gone.
    struct Values {
        QString skinDirectoryName = QStringLiteral("skinSD");
        PreviewSkinVariant skinVariant = PreviewSkinVariant::Standard;
        PreviewJudgeEffectStyle judgeEffectStyle = PreviewJudgeEffectStyle::Standard;
        PreviewOutlineVariant outlineVariant = PreviewOutlineVariant::Line;
        bool slideEarlierSecondAndTextOnTop =
            miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop;
        PreviewTapJudgeTextDistance tapJudgeTextDistance = PreviewTapJudgeTextDistance::Inner;
        miacode::preview_gameplay::CenterDisplayMode centerDisplayMode =
            miacode::preview_gameplay::kDefaultCenterDisplayMode;
        QString introSoundFileName;
    };

    explicit PreviewAppearanceState(QObject* parent = nullptr);

    Values& values() { return values_; }
    const Values& values() const { return values_; }

    QString skinDirectoryName() const { return values_.skinDirectoryName; }
    PreviewSkinVariant skinVariant() const { return values_.skinVariant; }
    PreviewJudgeEffectStyle judgeEffectStyle() const { return values_.judgeEffectStyle; }
    PreviewOutlineVariant outlineVariant() const { return values_.outlineVariant; }
    bool slideEarlierSecondAndTextOnTop() const
    {
        return values_.slideEarlierSecondAndTextOnTop;
    }
    PreviewTapJudgeTextDistance tapJudgeTextDistance() const
    {
        return values_.tapJudgeTextDistance;
    }
    miacode::preview_gameplay::CenterDisplayMode centerDisplayMode() const
    {
        return values_.centerDisplayMode;
    }
    QString introSoundFileName() const { return values_.introSoundFileName; }

    // Each setter reports whether the value actually moved, so a caller can
    // skip the expensive part — reloading a skin, re-applying it to every
    // surface, rewriting the settings file — instead of rediscovering that with
    // its own comparison. `changed` fires only on a real transition.
    bool setJudgeEffectStyle(PreviewJudgeEffectStyle value);
    bool setOutlineVariant(PreviewOutlineVariant value);
    bool setSlideEarlierSecondAndTextOnTop(bool value);
    bool setTapJudgeTextDistance(PreviewTapJudgeTextDistance value);
    bool setCenterDisplayMode(miacode::preview_gameplay::CenterDisplayMode value);
    bool setIntroSoundFileName(const QString& value);

    // The variant a skin directory implies: "skinDX" is the DX family and
    // everything else is Standard. Three call sites — the preview settings page,
    // the export page and the widget export dialog — each derived this on their
    // own, which is how a state where "skinDX" is still marked Standard becomes
    // possible.
    static PreviewSkinVariant variantForDirectory(const QString& directoryName);

    // Selecting a directory sets the variant with it, so the pair can never
    // disagree. Compared case-insensitively because that is how the skin folders
    // have always been matched.
    bool setSkinDirectory(const QString& directoryName);

signals:
    // `changed` is for anyone who just needs to re-read. The three specific
    // signals exist because the reactions differ in cost: a skin change reloads
    // and re-applies assets to every surface, a judge-effect change only pushes
    // one value to the live canvas, and an intro-sound change reloads the SFX
    // bank. Collapsing them into one handler would make every judge-effect
    // toggle reload the whole skin.
    void changed();
    void skinChanged();
    void judgeEffectStyleChanged();
    void introSoundChanged();

private:
    Values values_;
};

}  // namespace miacode::v2
