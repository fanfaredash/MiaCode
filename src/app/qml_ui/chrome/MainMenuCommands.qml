pragma ComponentBehavior: Bound

import QtQuick

// Single command bus for MainMenu hosts

QtObject {
    id: root

    property bool canUndo: false
    property bool canRedo: false
    property bool commandsEnabled: true

    signal toggleSidebarRequested()
    signal toggleBottomPanelRequested()
    signal togglePreviewRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal exitRequested()
    signal undoRequested()
    signal redoRequested()
    signal selectAllRequested()
    signal findRequested()
    signal selectCurrentLineRequested()
    signal validateRequested()
    signal metadataRequested()
    signal chartTransformRequested(string opId)
    signal normalizeChartRequested()
    signal aboutRequested()
    signal openRecentRequested(string path)
    signal closeDocumentRequested()
    signal audioSettingsRequested()
    signal previewSettingsRequested()
    signal unavailableFeatureRequested(string featureName)
}
