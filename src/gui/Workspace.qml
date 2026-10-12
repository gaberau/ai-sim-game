import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: workspace
    property color ink: session.preferences.text
    property color muted: session.preferences.muted
    property color accent: session.preferences.accent
    property alias editing: editor.visible
    function closeDialog() { editor.close() }
    property string search: ""
    property string group: "All groups"
    property string lastTab: session.selectedTab
    property string lastItem: session.selectedItem
    property bool mapVisible: session.selectedTab === "network" || session.selectedTab === "people"
    property var groups: {
        let result = ["All groups"]
        for (const item of session.workspace) if (item.group && result.indexOf(item.group) < 0) result.push(item.group)
        return result
    }
    property var entries: {
        return session.workspace.filter(function(item) {
            return (group === "All groups" || item.group === group) &&
                (item.title + " " + item.subtitle).toLowerCase().indexOf(search.toLowerCase()) >= 0
        })
    }
    component Heading: Label { color: workspace.ink; textFormat: Text.PlainText; wrapMode: Text.Wrap }
    component Small: Label { color: workspace.muted; textFormat: Text.PlainText; wrapMode: Text.Wrap }
    component Command: Button {
        hoverEnabled: true
        ToolTip.visible: hovered && !enabled
        ToolTip.text: ""
        padding: 12
        contentItem: Text {
            text: parent.text; font: parent.font
            color: parent.enabled ? workspace.ink : workspace.muted
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 5; color: parent.down ? "#3e4651" : parent.hovered ? "#303b49" : "#222c38"
            border.color: parent.activeFocus ? workspace.accent : "#3a4655"
        }
    }
    RowLayout {
        anchors.fill: parent
        spacing: 1
        Rectangle {
            color: "#151d26"
            Layout.preferredWidth: Math.min(300, workspace.width * 0.30)
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12
                TextField { Layout.fillWidth: true; placeholderText: "Search this workspace"; placeholderTextColor: workspace.muted; text: workspace.search; onTextEdited: workspace.search = text }
                ComboBox {
                    Layout.fillWidth: true; visible: workspace.groups.length > 1
                    model: workspace.groups
                    onActivated: workspace.group = currentText
                }
                Small { visible: session.location.length > 0; text: session.location; Layout.fillWidth: true }
                ListView {
                    id: list
                    objectName: "workspaceList"
                    model: workspace.entries
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        required property var modelData
                        width: ListView.view.width
                        highlighted: session.selectedItem === modelData.id
                        onClicked: session.selectItem(modelData.id)
                        contentItem: ColumnLayout {
                            Heading { text: modelData.title || modelData.name || modelData.id; Layout.fillWidth: true }
                            Small { text: modelData.subtitle || modelData.status || ""; Layout.fillWidth: true; color: modelData.warning ? "#ef8585" : workspace.muted }
                        }
                        background: Rectangle { radius: 5; color: parent.highlighted ? "#2c3744" : "transparent"; border.color: parent.highlighted ? workspace.accent : "transparent" }
                    }
                }
                Small { text: workspace.entries.length + " items" }
                CheckBox { visible: session.selectedTab === "network" || session.selectedTab === "people"; text: "Show connection map"; checked: workspace.mapVisible; onToggled: workspace.mapVisible = checked }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 20; spacing: 12
            Heading { text: session.detail.title || session.detail.name || "Select an item"; font.pixelSize: 23; Layout.fillWidth: true }
            Small { text: session.detail.subtitle || ""; Layout.fillWidth: true }
            ConnectionMap {
                visible: workspace.mapVisible
                Layout.fillWidth: true; Layout.preferredHeight: Math.max(180, workspace.height * 0.42)
            }
            ScrollView {
                id: detailScroll
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                ColumnLayout {
                    width: detailScroll.availableWidth; spacing: 16
                    Canvas {
                        id: chart
                        visible: session.detail.chart !== undefined && session.detail.chart.length > 0
                        Layout.fillWidth: true; Layout.preferredHeight: visible ? 150 : 0
                        property var prices: session.detail.chart || []
                        onPricesChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset()
                            if (!prices.length) return
                            let lo = Math.min.apply(null, prices), hi = Math.max.apply(null, prices)
                            if (hi === lo) { lo -= 1; hi += 1 }
                            ctx.strokeStyle = workspace.accent; ctx.lineWidth = 2; ctx.beginPath()
                            for (let i = 0; i < prices.length; i++) {
                                let x = 40 + i * (width - 60) / Math.max(1, prices.length - 1)
                                let y = 20 + (hi - prices[i]) / (hi - lo) * (height - 45)
                                if (i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y)
                            }
                            ctx.stroke(); ctx.fillStyle = workspace.muted
                            ctx.fillText("$" + hi.toFixed(2), 0, 12); ctx.fillText("$" + lo.toFixed(2), 0, height - 3)
                        }
                    }
                    Heading {
                        text: session.detail.body || (session.detail.cpu !== undefined ? "CPU: " + session.detail.cpu.toFixed(2) + " cores\nRAM: " + session.detail.ram.toFixed(0) + " MB\n" + session.detail.status : "")
                        Layout.fillWidth: true
                        font.pixelSize: session.preferences.font_size
                    }
                }
            }
            ScrollView {
                id: actionScroll
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(190, commands.implicitHeight); clip: true
                Flow {
                    id: commands
                    width: actionScroll.availableWidth; spacing: 8
                    Repeater {
                        model: session.detail.actions || []
                        delegate: Command {
                            required property var modelData
                            objectName: "workspaceAction" + modelData.id
                            text: modelData.label; enabled: modelData.enabled
                            ToolTip.text: modelData.reason
                            onClicked: editor.begin(modelData)
                        }
                    }
                }
            }
            Small {
                text: {
                    let reasons = []
                    for (const action of session.detail.actions || []) if (action.reason && reasons.indexOf(action.reason) < 0) reasons.push(action.reason)
                    return reasons.join("\n")
                }
                visible: text.length > 0; Layout.fillWidth: true
            }
        }
    }
    Connections {
        target: session
        function onStateChanged() {
            if (workspace.lastTab !== session.selectedTab) {
                workspace.lastTab = session.selectedTab; workspace.search = ""; workspace.group = "All groups"
            }
            if (workspace.lastItem !== session.selectedItem) {
                workspace.lastItem = session.selectedItem
                detailScroll.ScrollBar.vertical.position = 0
            }
            if (workspace.groups.indexOf(workspace.group) < 0) workspace.group = "All groups"
        }
    }
    Dialog {
        id: editor
        objectName: "actionDialog"
        anchors.centerIn: Overlay.overlay
        width: Math.min(720, workspace.width - 32)
        height: Math.min(580, workspace.height - 24)
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape
        property var command: ({})
        property var values: ({})
        property string targetItem
        property string targetTab
        property string failure: ""
        function begin(action) {
            command = action; values = {}; failure = ""
            targetItem = session.selectedItem; targetTab = session.selectedTab
            for (const f of action.fields) values[f.id] = f.value
            if (!action.fields.length && !action.confirm) { session.act(action.id, values); return }
            title = action.label; open()
        }
        onOpened: session.setMenuOpen(true)
        onClosed: session.setMenuOpen(false)
        contentItem: ColumnLayout {
            Small { visible: editor.command.confirm || false; text: "This changes or removes saved progress. Continue?"; Layout.fillWidth: true }
            ScrollView {
                id: formScroll
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                ColumnLayout {
                    width: formScroll.availableWidth; spacing: 12
                    Repeater {
                        model: editor.command.fields || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Heading { text: modelData.label; Layout.fillWidth: true }
                            ComboBox {
                                Layout.fillWidth: true
                                visible: modelData.options !== undefined && modelData.options.length > 0
                                model: modelData.options || []
                                textRole: "label"; valueRole: "id"
                                currentIndex: modelData.options ? modelData.options.findIndex(option => option.id === modelData.value) : -1
                                onActivated: editor.values[modelData.id] = currentValue
                            }
                            TextArea {
                                visible: modelData.options === undefined || modelData.options.length === 0
                                objectName: "actionField" + modelData.id
                                Layout.fillWidth: true
                                Layout.preferredHeight: modelData.multiline ? 260 : implicitHeight
                                text: modelData.value
                                wrapMode: TextEdit.Wrap
                                selectByMouse: true
                                onTextChanged: editor.values[modelData.id] = text
                                background: Rectangle { color: "#10151c"; border.color: parent.activeFocus ? workspace.accent : "#536071"; radius: 4 }
                            }
                        }
                    }
                }
            }
            Small { text: editor.failure; color: "#ef8585"; Layout.fillWidth: true; visible: text.length > 0 }
            RowLayout {
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: editor.close() }
                Button {
                    objectName: "applyActionButton"
                    text: editor.command.confirm ? "Confirm" : "Apply"
                    onClicked: {
                        if (editor.targetTab !== session.selectedTab || editor.targetItem !== session.selectedItem) { editor.failure = "The selection changed. Close this form and try again."; return }
                        if (session.act(editor.command.id, editor.values)) editor.close()
                        else editor.failure = session.notice
                    }
                }
            }
        }
    }
}
