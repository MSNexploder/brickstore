// Copyright (C) 2004-2024 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

pragma ComponentBehavior: Bound

import Mobile
import BrickStore as BS
import "utils.js" as Utils


AutoSizingDialog {
    id: root
    title: qsTr("Error Log")
    keepPaddingInSmallMode: true

    onOpened: {
        Utils.flashScrollIndicators(log)
        Qt.callLater(function() { log.positionViewAtEnd() })
    }

    ListView {
        id: log
        anchors.fill: parent
        clip: true

        ScrollIndicator.vertical: ScrollIndicator { }

        readonly property var typeInfo: ({
                                             0: { name: "Debug",    fg: "#000000", bg: "#00ff00" },
                                             1: { name: "Warning",  fg: "#000000", bg: "#ffff00" },
                                             2: { name: "Critical", fg: "#ff0000", bg: "#000000" },
                                             3: { name: "Fatal",    fg: "#ffffff", bg: "#ff0000" },
                                             4: { name: "Info",     fg: "#ffffff", bg: "#0000ff" },
                                         })

        readonly property var categoryColors: [ "#e81717", "#e8e817", "#17e817", "#17e8e8", "#1717e8", "#e817e8" ]


        model: BS.BrickStore.debug.log
        delegate: ColumnLayout {
            id: delegate
            required property int type
            required property string category
            required property int line
            required property string file
            required property string message

            width: ListView.view.width
            RowLayout {
                Label {
                    id: typeLabel
                    property var info: log.typeInfo[delegate.type]

                    text: '  ' + info.name + '  '
                    color: info.fg
                    font.bold: true
                    background: Rectangle {
                        anchors.fill: parent
                        color: typeLabel.info.bg
                        radius: height / 2
                    }
                }
                Label {
                    text: '  '+ delegate.category + '  '
                    background: Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.width: 1
                        border.color: log.categoryColors[(delegate.category.length
                                                          * delegate.category.codePointAt(0))
                            % log.categoryColors.length]
                        radius: height / 2
                    }
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    font.italic: true
                    text: (delegate.file === '' || delegate.type === 4)
                          ? '' : delegate.file + ", line " + delegate.line
                    elide: Text.ElideMiddle
                }
            }
            Label {
                Layout.fillWidth: true
                text: delegate.message
                wrapMode: Text.Wrap
                font: Style.monospaceFont
            }
            Rectangle {
                z: 1
                antialiasing: true
                implicitHeight: 1 / Screen.devicePixelRatio
                color: Style.hintTextColor
                Layout.topMargin: 8
                Layout.fillWidth: true
                Layout.bottomMargin: 8
            }
        }
    }
}
