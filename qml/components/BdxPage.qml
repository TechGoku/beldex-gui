import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BeldexGui

// Scrollable page body, centred with a max width, starting with the big title.
Flickable {
    id: root
    property alias title: heading.title
    property alias subtitle: heading.subtitle
    property alias headerRight: headerRight.data
    default property alias content: column.data

    contentWidth: width
    contentHeight: column.implicitHeight + 120
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        width: Math.min(root.width - Theme.gutter * 2, Theme.maxContentWidth)
        x: (root.width - width) / 2
        y: 44
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            spacing: 16
            PageTitle { id: heading; Layout.fillWidth: true }
            RowLayout { id: headerRight; spacing: 10; Layout.alignment: Qt.AlignBottom }
        }
    }
}
