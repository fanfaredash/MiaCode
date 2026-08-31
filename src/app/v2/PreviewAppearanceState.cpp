#include "PreviewAppearanceState.h"

namespace miacode::v2 {

namespace {

// Every setter is the same shape: compare, store, publish once. Writing it once
// keeps a future field from quietly forgetting the signal.
template <typename T>
bool assign(T& target, const T& value, PreviewAppearanceState& owner,
            void (PreviewAppearanceState::*changed)())
{
    if (target == value) {
        return false;
    }
    target = value;
    (owner.*changed)();
    return true;
}

}  // namespace

PreviewAppearanceState::PreviewAppearanceState(QObject* parent)
    : QObject(parent)
{
}

bool PreviewAppearanceState::setJudgeEffectStyle(PreviewJudgeEffectStyle value)
{
    if (!assign(values_.judgeEffectStyle, value, *this, &PreviewAppearanceState::changed)) {
        return false;
    }
    emit judgeEffectStyleChanged();
    return true;
}

bool PreviewAppearanceState::setOutlineVariant(PreviewOutlineVariant value)
{
    return assign(values_.outlineVariant, value, *this, &PreviewAppearanceState::changed);
}

bool PreviewAppearanceState::setSlideEarlierSecondAndTextOnTop(bool value)
{
    return assign(values_.slideEarlierSecondAndTextOnTop, value, *this,
                  &PreviewAppearanceState::changed);
}

bool PreviewAppearanceState::setTapJudgeTextDistance(PreviewTapJudgeTextDistance value)
{
    return assign(values_.tapJudgeTextDistance, value, *this, &PreviewAppearanceState::changed);
}

bool PreviewAppearanceState::setCenterDisplayMode(
    miacode::preview_gameplay::CenterDisplayMode value)
{
    return assign(values_.centerDisplayMode, value, *this, &PreviewAppearanceState::changed);
}

bool PreviewAppearanceState::setIntroSoundFileName(const QString& value)
{
    if (!assign(values_.introSoundFileName, value, *this, &PreviewAppearanceState::changed)) {
        return false;
    }
    emit introSoundChanged();
    return true;
}

PreviewSkinVariant PreviewAppearanceState::variantForDirectory(const QString& directoryName)
{
    return directoryName.compare(QStringLiteral("skinDX"), Qt::CaseInsensitive) == 0
        ? PreviewSkinVariant::Dx
        : PreviewSkinVariant::Standard;
}

bool PreviewAppearanceState::setSkinDirectory(const QString& directoryName)
{
    const PreviewSkinVariant variant = variantForDirectory(directoryName);
    // Case-insensitive on the directory to match how the skin folders have
    // always been compared. The variant is checked too so a state that was
    // restored with a mismatched pair still gets corrected.
    if (values_.skinDirectoryName.compare(directoryName, Qt::CaseInsensitive) == 0
        && values_.skinVariant == variant) {
        return false;
    }
    values_.skinDirectoryName = directoryName;
    values_.skinVariant = variant;
    emit changed();
    emit skinChanged();
    return true;
}

}  // namespace miacode::v2
