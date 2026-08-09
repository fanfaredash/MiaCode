#pragma once

#include <QStringView>

namespace miacode::editor {

bool isBookmarkCommentMarker(QStringView lineText, int markerPosition);

} // namespace miacode::editor
