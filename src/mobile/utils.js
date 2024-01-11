// Copyright (C) 2004-2024 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

//.pragma library

function flashScrollIndicators(flickable) {
    if (flickable.ScrollIndicator.vertical)
        flickable.ScrollIndicator.vertical.active = true
    else if (flickable.ScrollBar.vertical)
        flickable.ScrollBar.vertical.active = true
    else
        return

    let timer = Qt.createQmlObject("import QtQuick;Timer{}", flickable)
    timer.interval = 1000
    timer.repeat = false
    timer.triggered.connect(function() {
        timer.destroy()
        if (!flickable.moving) {
            if (flickable.ScrollIndicator.vertical)
                flickable.ScrollIndicator.vertical.active = false
            else if (flickable.ScrollBar.vertical)
                flickable.ScrollBar.vertical.active = false
        }
    })
    timer.start()
}
