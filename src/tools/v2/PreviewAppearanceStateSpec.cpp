// Contract regression for the preview appearance settings owner.
//
// Stage 3.5 item 2: these eight values used to live as MainWindow private
// members, read and written straight through `friend` access by
// QmlExportSession and QmlPreviewSettingsModel — two copies of the same
// read/write logic against one hidden window. This target links Qt6::Core /
// Qt6::Gui / Qt6::Test only, so if the appearance state ever reaches back for
// QtWidgets or Qt Quick the spec fails to LINK.
//
// The behaviour worth pinning is the part the two duplicated copies used to
// re-derive by hand: every setter reports whether the value actually moved, and
// `changed` fires only on a real transition. Callers use that answer to skip
// reloading a skin and rewriting the settings file.

#include "app/v2/PreviewAppearanceState.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyDefaultsMatchTheShippedPreview(QTextStream& err)
{
    const miacode::v2::PreviewAppearanceState state;
    return require(state.skinDirectoryName() == QStringLiteral("skinSD")
                       && state.skinVariant() == PreviewSkinVariant::Standard
                       && state.judgeEffectStyle() == PreviewJudgeEffectStyle::Standard
                       && state.outlineVariant() == PreviewOutlineVariant::Line
                       && state.slideEarlierSecondAndTextOnTop()
                           == miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop
                       && state.tapJudgeTextDistance() == PreviewTapJudgeTextDistance::Inner
                       && state.centerDisplayMode()
                           == miacode::preview_gameplay::kDefaultCenterDisplayMode
                       && state.introSoundFileName().isEmpty(),
                   QStringLiteral("defaults match what the preview shipped with"), err);
}

bool verifySettersReportRealTransitionsOnly(QTextStream& err)
{
    miacode::v2::PreviewAppearanceState state;
    QSignalSpy changed(&state, &miacode::v2::PreviewAppearanceState::changed);

    bool ok = require(state.setJudgeEffectStyle(PreviewJudgeEffectStyle::Starry)
                          && changed.count() == 1,
                      QStringLiteral("a real change returns true and publishes once"), err);
    ok &= require(!state.setJudgeEffectStyle(PreviewJudgeEffectStyle::Starry)
                      && changed.count() == 1,
                  QStringLiteral("re-setting the same value neither reports a change nor "
                                 "republishes"), err);

    ok &= require(state.setOutlineVariant(PreviewOutlineVariant::JudgeAreaLabeled)
                      && !state.setOutlineVariant(PreviewOutlineVariant::JudgeAreaLabeled),
                  QStringLiteral("outline variant reports only real transitions"), err);
    ok &= require(state.setTapJudgeTextDistance(PreviewTapJudgeTextDistance::Outer)
                      && !state.setTapJudgeTextDistance(PreviewTapJudgeTextDistance::Outer),
                  QStringLiteral("tap judge distance reports only real transitions"), err);
    ok &= require(state.setCenterDisplayMode(
                      miacode::preview_gameplay::CenterDisplayMode::DxScorePlus)
                      && !state.setCenterDisplayMode(
                          miacode::preview_gameplay::CenterDisplayMode::DxScorePlus),
                  QStringLiteral("center display mode reports only real transitions"), err);
    ok &= require(state.setIntroSoundFileName(QStringLiteral("intro.wav"))
                      && !state.setIntroSoundFileName(QStringLiteral("intro.wav")),
                  QStringLiteral("intro sound file reports only real transitions"), err);
    ok &= require(state.setSlideEarlierSecondAndTextOnTop(
                      !miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop)
                      && !state.setSlideEarlierSecondAndTextOnTop(
                          !miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop),
                  QStringLiteral("slide-earlier flag reports only real transitions"), err);

    ok &= require(state.judgeEffectStyle() == PreviewJudgeEffectStyle::Starry
                      && state.outlineVariant() == PreviewOutlineVariant::JudgeAreaLabeled
                      && state.tapJudgeTextDistance() == PreviewTapJudgeTextDistance::Outer
                      && state.introSoundFileName() == QStringLiteral("intro.wav"),
                  QStringLiteral("every accepted value is readable afterwards"), err);
    return ok;
}

// The variant is derived from the directory rather than passed alongside it, so
// a state where "skinDX" is still marked Standard cannot be expressed. Three
// call sites used to derive it independently.
bool verifySkinDirectoryCarriesItsVariant(QTextStream& err)
{
    bool ok = require(
        miacode::v2::PreviewAppearanceState::variantForDirectory(QStringLiteral("skinDX"))
                == PreviewSkinVariant::Dx
            && miacode::v2::PreviewAppearanceState::variantForDirectory(QStringLiteral("SKINDX"))
                == PreviewSkinVariant::Dx
            && miacode::v2::PreviewAppearanceState::variantForDirectory(QStringLiteral("skinSD"))
                == PreviewSkinVariant::Standard
            && miacode::v2::PreviewAppearanceState::variantForDirectory(QStringLiteral("myCustom"))
                == PreviewSkinVariant::Standard,
        QStringLiteral("only the DX folder maps to the DX family, case-insensitively"), err);

    miacode::v2::PreviewAppearanceState state;
    QSignalSpy changed(&state, &miacode::v2::PreviewAppearanceState::changed);
    QSignalSpy skinChanged(&state, &miacode::v2::PreviewAppearanceState::skinChanged);

    ok &= require(state.setSkinDirectory(QStringLiteral("skinDX")) && changed.count() == 1
                      && skinChanged.count() == 1
                      && state.skinDirectoryName() == QStringLiteral("skinDX")
                      && state.skinVariant() == PreviewSkinVariant::Dx,
                  QStringLiteral("one publish carries the directory and the variant it implies"),
                  err);

    // The skin folders have always been matched case-insensitively; a differently
    // cased spelling of the current folder is not a change.
    ok &= require(!state.setSkinDirectory(QStringLiteral("SKINDX")) && changed.count() == 1
                      && state.skinDirectoryName() == QStringLiteral("skinDX"),
                  QStringLiteral("a differently cased spelling of the same folder is not a "
                                 "change, and does not overwrite the stored name"), err);

    // A pair restored with a mismatched variant still gets corrected, because
    // the comparison checks the derived variant as well as the name.
    state.values().skinVariant = PreviewSkinVariant::Standard;
    ok &= require(state.setSkinDirectory(QStringLiteral("skinDX"))
                      && state.skinVariant() == PreviewSkinVariant::Dx,
                  QStringLiteral("a restored mismatch between folder and variant is corrected"),
                  err);
    return ok;
}

// The three specific signals exist so a judge-effect toggle does not trigger a
// full skin reload. Each one must fire only for its own field.
bool verifySpecificSignalsStayOnTheirOwnField(QTextStream& err)
{
    miacode::v2::PreviewAppearanceState state;
    QSignalSpy skinChanged(&state, &miacode::v2::PreviewAppearanceState::skinChanged);
    QSignalSpy judgeEffect(&state, &miacode::v2::PreviewAppearanceState::judgeEffectStyleChanged);
    QSignalSpy introSound(&state, &miacode::v2::PreviewAppearanceState::introSoundChanged);

    state.setJudgeEffectStyle(PreviewJudgeEffectStyle::Starry);
    bool ok = require(judgeEffect.count() == 1 && skinChanged.isEmpty() && introSound.isEmpty(),
                      QStringLiteral("a judge-effect change does not announce a skin reload"),
                      err);

    state.setIntroSoundFileName(QStringLiteral("intro.wav"));
    ok &= require(introSound.count() == 1 && skinChanged.isEmpty() && judgeEffect.count() == 1,
                  QStringLiteral("an intro-sound change stays on its own signal"), err);

    state.setSkinDirectory(QStringLiteral("skinDX"));
    ok &= require(skinChanged.count() == 1 && judgeEffect.count() == 1
                      && introSound.count() == 1,
                  QStringLiteral("a skin change stays on its own signal"), err);

    state.setOutlineVariant(PreviewOutlineVariant::Point);
    ok &= require(skinChanged.count() == 1 && judgeEffect.count() == 1
                      && introSound.count() == 1,
                  QStringLiteral("a field with no specific signal fires none of them"), err);
    return ok;
}

// MainWindow binds its own members to values() by reference and still writes
// them directly while restoring settings. That door has to exist and has to be
// the same storage the accessors read.
bool verifyDirectValueAccessSharesOneCopy(QTextStream& err)
{
    miacode::v2::PreviewAppearanceState state;
    QSignalSpy changed(&state, &miacode::v2::PreviewAppearanceState::changed);

    QString& boundDirectory = state.values().skinDirectoryName;
    boundDirectory = QStringLiteral("restoredSkin");

    bool ok = require(state.skinDirectoryName() == QStringLiteral("restoredSkin"),
                      QStringLiteral("a write through values() is visible to the accessors"),
                      err);
    ok &= require(changed.isEmpty(),
                  QStringLiteral("a direct write does not publish — restore paths must not "
                                 "look like a user edit"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;

    ok &= verifyDefaultsMatchTheShippedPreview(err);
    ok &= verifySettersReportRealTransitionsOnly(err);
    ok &= verifySkinDirectoryCarriesItsVariant(err);
    ok &= verifySpecificSignalsStayOnTheirOwnField(err);
    ok &= verifyDirectValueAccessSharesOneCopy(err);

    if (ok) {
        QTextStream(stdout) << "preview_appearance_state_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
