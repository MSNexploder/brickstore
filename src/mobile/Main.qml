import Mobile
import QtQuick.Window


ApplicationWindow {
    id: root
    visible: true
    title: "BrickStore"
    width: 800
    height: 600

    // ios: colored statusbar background
    flags: Qt.Window | (Style.isIOS ? Qt.MaximizeUsingFullscreenGeometryHint : 0)

    Binding { // used to apply the dark/light theme for the complete app (Style is a singleton)
        target: Style
        property: "rootWindow"
        value: root
    }

    Loader {
        id: loader
        asynchronous: true
        anchors.fill: parent
        active: true
        source: "MainWindow.qml"
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: loader.status !== Loader.Ready
        visible: loader.status !== Loader.Ready
    }
}
