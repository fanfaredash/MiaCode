pragma ComponentBehavior: Bound

import QtQuick

// Single command bus for MainMenu hosts

QtObject {
    id: root

    property bool canUndo: false
    property bool canRedo: false
    property bool canCut: false
    property bool canCopy: false
    property bool canPaste: false
    property bool commandsEnabled: true

    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal exitRequested()
    signal undoRequested()
    signal redoRequested()
    signal cutRequested()
    signal copyRequested()
    signal pasteRequested()
    signal selectAllRequested()
    signal findRequested()
    signal selectCurrentLineRequested()
    signal validateRequested()
    signal metadataRequested()
    signal chartTransformRequested(string opId)
    signal normalizeChartRequested()
    signal aboutRequested()
    signal preferencesRequested()
    signal newDocumentRequested()
    signal openRecentRequested(string path)
    signal restoreBackupRequested(string path)
    signal closeDocumentRequested()
    signal audioSettingsRequested()
    signal previewSettingsRequested()
    signal unavailableFeatureRequested(string featureName)
}
