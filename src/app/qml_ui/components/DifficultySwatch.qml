import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    property int difficultyId: 0

    implicitWidth: Theme.difficultySwatchSize
    implicitHeight: Theme.difficultySwatchSize
    width: implicitWidth
    height: implicitHeight
    radius: Theme.difficultySwatchRadius
    color: Theme.difficultyColor(root.difficultyId)
}
