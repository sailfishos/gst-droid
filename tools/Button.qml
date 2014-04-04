/*
 * gst-droid
 *
 * Copyright (C) 2014 Mohammed Sameer <msameer@foolab.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

import QtQuick 2.0

Rectangle {
        id: rect
        signal clicked
        property alias text: label.text
        width: 200
        height: 150
        color: mouse.pressed ? "gray" : "white"

        Text {
                id: label
                anchors.fill: parent
                color: mouse.pressed ? "white" : "black"
                font.pixelSize: 34
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
        }

        MouseArea {
                id: mouse
                anchors.fill: parent
                onClicked: rect.clicked()
        }

}
