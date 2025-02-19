// Copyright (C) 2004-2024 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

import Mobile
import Qt5Compat.GraphicalEffects
import QtQuick.Controls.Basic as Basic
import BrickStore as BS
import BrickLink as BL
import LDraw


Control {
    id: root
    property BS.Document document
    property BL.Lot lot

    property bool is3D: true
    property bool prefer3D: true
    property BL.Picture picture
    property bool isUpdating: (picture && (picture.updateStatus === BL.BrickLink.UpdateStatus.Updating))

    onLotChanged: { updateInfo() }

    function updateInfo() {
        if (picture)
            picture.release()
        picture = null

        picture = BL.BrickLink.picture(lot.item, lot.color, true)
        if (picture)
            picture.addRef()

        info3D.renderController.setItemAndColor(lot.item, lot.color)
        root.is3D = prefer3D && info3D.renderController.canRender
    }

    Component.onDestruction: {
        if (picture)
            picture.release()
    }

    StackLayout {
        anchors.fill: parent
        clip: true
        currentIndex: root.is3D ? 1 : 0

        QImageItem {
            id: infoImage
            fillColor: "white"
            image: root.picture && root.picture.isValid ? root.picture.image : noImage
            property var noImage: BL.BrickLink.noImage(0, 0)

            Text {
                id: infoNoImage
                anchors.fill: parent
                visible: !root.picture || !root.picture.isValid
                color: "#DA4453"
                fontSizeMode: Text.Fit
                font.pointSize: root.font.pointSize * 3
                font.italic: true
                minimumPointSize: root.font.pointSize
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: qsTr("No image available")
            }

            Text {
                id: infoImageUpdating
                anchors.fill: parent
                visible: root.isUpdating
                color: "black"
                fontSizeMode: Text.Fit
                font.pointSize: root.font.pointSize * 3
                font.italic: true
                minimumPointSize: root.font.pointSize
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Please wait... updating")
            }

            Glow {
                anchors.fill: infoImageUpdating
                visible: root.isUpdating
                radius: 8
                spread: 0.5
                color: "white"
                source: infoImageUpdating
            }
        }

        PartRenderer {
            id: info3D
            Connections {
                target: info3D.renderController
                function onCanRenderChanged(canRender : bool) {
                    if (!root.is3D && root.prefer3D && canRender)
                        root.is3D = true;
                    else if (root.is3D && !canRender)
                        root.is3D = false;
                }
            }
        }
    }
    RowLayout {
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        Basic.Button {
            flat: true
            font.bold: true
            palette.windowText: "black"
            palette.buttonText: "black"
            leftPadding: 8
            bottomPadding: 8
            topPadding: 16
            rightPadding: 16
            text: root.is3D ? "2D" : "3D"
            onClicked: { root.is3D = !root.is3D; root.prefer3D = root.is3D }
        }
        Item { Layout.fillWidth: true }
        Basic.Button {
            flat: true
            palette.windowText: "black"
            palette.buttonText: "black"
            icon.name: root.is3D ? "zoom-fit-best" : "view-refresh"
            rightPadding: 8
            bottomPadding: 8
            topPadding: 16
            leftPadding: 16
            onClicked: {
                if (root.is3D)
                    info3D.renderController.resetCamera();
                else if (root.picture)
                    root.picture.update(true);
            }
        }
    }
}
