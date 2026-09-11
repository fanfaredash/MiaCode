#pragma once

class QQmlEngine;

namespace miacode::ui {

class PreviewModel;

void registerNoteImageProvider(QQmlEngine* engine, PreviewModel* model);

} // namespace miacode::ui
