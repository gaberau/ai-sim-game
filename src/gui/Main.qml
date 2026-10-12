import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    objectName: "workstation"
    visible: true
    width: 1180
    height: 780
    minimumWidth: 800
    minimumHeight: 580
    title: "AI Sim — Workstation"
    color: session.preferences.background
    font.family: "monospace"
    font.pixelSize: session.preferences.font_size
    property color ink: session.preferences.text
    property color muted: session.preferences.muted
    property color accent: session.preferences.accent
    property string contactName: {
        for (let item of session.contacts)
            if (item.id === session.selectedContact) return item.name
        return "Mark"
    }
    palette.window: session.preferences.background
    palette.windowText: ink
    palette.text: ink
    palette.button: "#222c38"
    palette.buttonText: ink
    palette.highlight: accent
    palette.highlightedText: "#10151c"
    palette.base: "#18212b"
    onClosing: function(close) { close.accepted = session.save() }

    Shortcut { sequence: "Escape"; onActivated: window.toggleMenu() }
    Shortcut { sequence: session.preferences.menu_key; onActivated: window.toggleMenu() }
    Shortcut { sequence: session.preferences.pause_key; onActivated: session.togglePause() }
    Shortcut { sequence: session.preferences.save_key; onActivated: session.save() }

    function toggleMenu() {
        if (workspaces.editing) { workspaces.closeDialog(); return }
        if (settings.opened) { settings.close(); return }
        if (slots.opened) { slots.close(); return }
        if (newGameConfirm.opened) { newGameConfirm.close(); return }
        if (menu.opened) menu.close(); else menu.open()
    }
    function cycleTab(direction) {
        if (workspaces.editing || settings.opened || slots.opened || newGameConfirm.opened || menu.opened) return
        const available = session.tabs.filter(t => t.enabled)
        const index = available.findIndex(t => t.id === session.selectedTab)
        session.selectTab(available[(index + direction + available.length) % available.length].id)
    }
    Shortcut { sequence: session.preferences.next_tab_key; onActivated: window.cycleTab(1) }
    Shortcut { sequence: session.preferences.previous_tab_key; onActivated: window.cycleTab(-1) }
    component Rule: Rectangle { color: "#2a3441"; Layout.fillWidth: true; Layout.preferredHeight: 1 }
    component Caption: Label { color: window.muted; font.pixelSize: 12 }
    component Action: Button {
        id: action
        hoverEnabled: true
        background: Rectangle {
            radius: 5
            color: action.down ? "#3e4651" : action.hovered ? "#303b49" : "#222c38"
            border.color: action.activeFocus ? window.accent : "#3a4655"
        }
        contentItem: Text {
            text: action.text; color: action.enabled ? window.ink : window.muted
            font: action.font; horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
        }
        padding: 12
    }
    component Meter: ColumnLayout {
        property string heading
        property string detail
        property real fraction
        spacing: 10
        Label { text: parent.heading; color: window.ink; font.pixelSize: 18 }
        Label { text: parent.detail; color: window.muted }
        ProgressBar {
            Layout.fillWidth: true
            value: Math.max(0, Math.min(1, parent.fraction))
            background: Rectangle { implicitHeight: 8; color: "#2a3441"; radius: 4 }
            contentItem: Item {
                implicitHeight: 8
                Rectangle { width: parent.width * parent.parent.visualPosition; height: 8; radius: 4; color: window.accent }
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 22
            Label { text: "AI SIM"; color: accent; font.bold: true; font.pixelSize: 19 }
            Caption { text: " / WORKSTATION" }
            Item { Layout.fillWidth: true }
            Caption { text: session.resources.clock }
            Action { objectName: "pauseButton"; text: session.paused ? "Resume" : "Pause"; onClicked: session.togglePause() }
            Action { objectName: "menuButton"; text: "Menu"; onClicked: menu.open() }
        }
        Rule {}
        Flickable {
            id: tabNavigation
            property string activeTab: session.selectedTab
            function revealActiveTab() {
                for (let i=0; i<tabRepeater.count; i++) {
                    const button=tabRepeater.itemAt(i)
                    if (button && button.modelData.id === activeTab) {
                        const left=button.x+tabStrip.x, right=left+button.width
                        if (left < contentX) contentX=left
                        else if (right > contentX+width) contentX=right-width+22
                        contentX=Math.max(0,Math.min(contentX,contentWidth-width))
                        break
                    }
                }
            }
            onActiveTabChanged: Qt.callLater(revealActiveTab)
            Layout.fillWidth: true; Layout.preferredHeight: 64
            contentWidth: tabStrip.implicitWidth + 44; contentHeight: height
            clip: true; flickableDirection: Flickable.HorizontalFlick
            Row {
                id: tabStrip; x: 22; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                Repeater {
                    id: tabRepeater
                    model: session.tabs
                    delegate: Action {
                        required property var modelData
                        objectName: "tabButton" + modelData.id
                        text: modelData.label + (modelData.enabled ? "" : " · locked")
                        enabled: modelData.enabled
                        onClicked: session.selectTab(modelData.id)
                        background: Rectangle {
                            color: session.selectedTab === modelData.id ? "#39404a" : "transparent"
                            radius: 5; border.color: session.selectedTab === modelData.id ? window.accent : "transparent"
                        }
                        ToolTip.visible: hovered && !modelData.enabled
                        ToolTip.text: modelData.reason
                    }
                }
            }
            ScrollBar.horizontal: ScrollBar {}
        }
        Rule {}
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: session.selectedTab === "messages" ? 0 : session.selectedTab === "resources" ? 1 : 2
            RowLayout {
                spacing: 0
                Rectangle {
                    color: "#151d26"
                    Layout.preferredWidth: Math.min(260, window.width * 0.25)
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 20; spacing: 16
                        Caption { text: "CONVERSATIONS"; font.letterSpacing: 1.5 }
                        ListView {
                            id: contacts
                            objectName: "contactList"
                            Layout.fillWidth: true; Layout.fillHeight: true
                            model: session.contacts
                            spacing: 8; clip: true
                            delegate: Action {
                                required property var modelData
                                width: ListView.view.width
                                text: modelData.name + (modelData.unread ? "  (" + modelData.unread + ")" : "")
                                onClicked: { timeline.followLatest = true; session.selectContact(modelData.id) }
                                background: Rectangle {
                                    radius: 5
                                    color: session.selectedContact === modelData.id ? "#2c3744" : "transparent"
                                    border.color: session.selectedContact === modelData.id ? "#536071" : "transparent"
                                }
                            }
                        }
                        Caption { text: session.resources.connected ? "Company chat connected" : "Local connection"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                }
                Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: "#2a3441" }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.margins: 24; spacing: 16
                    RowLayout {
                        Label { text: window.contactName; color: window.ink; font.pixelSize: 23 }
                        Item { Layout.fillWidth: true }
                        Caption { text: "SCRIPTED CONVERSATION" }
                    }
                    Rule {}
                    ListView {
                        id: timeline
                        objectName: "messageList"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        model: session.messages
                        spacing: 20; clip: true
                        property bool followLatest: true
                        property real readingPosition: 0
                        onMovementStarted: followLatest = false
                        onContentYChanged: if (moving) readingPosition = contentY
                        onMovementEnded: { readingPosition = contentY; followLatest = atYEnd }
                        onCountChanged: if (followLatest) Qt.callLater(function() { timeline.positionViewAtEnd() })
                        ScrollBar.vertical: ScrollBar {}
                        Connections {
                            target: session
                            function onChatChanged() {
                                const position = timeline.readingPosition
                                Qt.callLater(function() {
                                    if (timeline.followLatest) timeline.positionViewAtEnd()
                                    else timeline.contentY = Math.max(0, Math.min(position, timeline.contentHeight - timeline.height))
                                })
                            }
                        }
                        delegate: ColumnLayout {
                            required property var modelData
                            width: ListView.view.width - 16
                            spacing: 6
                            Caption { text: modelData.outgoing ? "YOU" : window.contactName; color: modelData.outgoing ? window.accent : window.muted }
                            Label {
                                text: modelData.body; color: window.ink
                                textFormat: Text.PlainText; wrapMode: Text.Wrap
                                Layout.fillWidth: true; font.pixelSize: 16
                            }
                        }
                    }
                    RowLayout {
                        visible: !timeline.followLatest
                        Item { Layout.fillWidth: true }
                        Action { text: "Latest messages ↓"; onClicked: { timeline.followLatest = true; timeline.positionViewAtEnd() } }
                    }
                    Rule {}
                    Caption { text: session.choices.length ? "CHOOSE YOUR RESPONSE" : "NO RESPONSE NEEDED"; font.letterSpacing: 1 }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(180, responseColumn.implicitHeight)
                        clip: true
                        ColumnLayout {
                            id: responseColumn
                            width: timeline.width
                            spacing: 8
                            Repeater {
                                model: session.choices
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Action {
                                        objectName: "responseButton"
                                        Layout.fillWidth: true
                                        text: modelData.label; enabled: modelData.enabled
                                        onClicked: { timeline.followLatest = true; session.choose(modelData.token) }
                                    }
                                    Caption {
                                        visible: modelData.reason.length > 0
                                        text: modelData.reason; wrapMode: Text.WordWrap; Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                    }
                }
            }
            ScrollView {
                clip: true
                ColumnLayout {
                    width: window.width - 64
                    anchors.margins: 32
                    spacing: 24
                    Label { text: "Local resources"; color: window.ink; font.pixelSize: 24; Layout.topMargin: 24; Layout.leftMargin: 32 }
                    RowLayout {
                        Layout.fillWidth: true; Layout.leftMargin: 32; spacing: 48
                        Meter {
                            Layout.fillWidth: true
                            heading: "CPU"
                            detail: session.resources.cpu.toFixed(2) + " / " + session.resources.cores + " cores"
                            fraction: session.resources.cpuFraction
                        }
                        Meter {
                            Layout.fillWidth: true
                            heading: "Memory"
                            detail: session.resources.ram.toFixed(0) + " / " + session.resources.ramTotal.toFixed(0) + " MB"
                            fraction: session.resources.ramFraction
                        }
                    }
                    Rule { Layout.leftMargin: 32 }
                    Label { text: "Mark trust: " + session.resources.trust.toFixed(0) + "    Cash: " + session.resources.cash; color: window.ink; Layout.leftMargin: 32 }
                    Caption { text: "Helping Mark is unpaid. Money tools unlock later."; Layout.leftMargin: 32 }
                    Label { text: "Processes"; color: window.ink; font.pixelSize: 19; Layout.leftMargin: 32 }
                    Caption { visible: session.processes.length === 0; text: "Process details unlock through the story."; Layout.leftMargin: 32 }
                    Repeater {
                        model: session.processes
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true; Layout.leftMargin: 32
                            implicitHeight: 64; color: "#18212b"; radius: 6
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 14
                                Label { text: modelData.name; color: window.ink; Layout.fillWidth: true; elide: Text.ElideRight }
                                Caption { text: modelData.cpu.toFixed(2) + " cores   " + modelData.ram.toFixed(0) + " MB   " + modelData.status }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 24 }
                }
            }
                    Workspace { id: workspaces; Layout.fillWidth: true; Layout.fillHeight: true }
        }
        Rule {}
        RowLayout {
            Layout.fillWidth: true; Layout.margins: 16
            Caption { text: session.notice; Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText }
            Caption { text: "Esc / " + session.preferences.menu_key + "  Menu    " + session.preferences.save_key + "  Save" }
        }
    }
    Popup {
        id: menu
        objectName: "gameMenu"
        anchors.centerIn: Overlay.overlay
        width: 460; height: Math.min(window.height - 48, 620); padding: 24
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onAboutToShow: session.setMenuOpen(true)
        onClosed: session.setMenuOpen(false)
        background: Rectangle { color: "#18212b"; radius: 10; border.color: "#536071" }
        Overlay.modal: Rectangle { color: "#99070c12" }
        contentItem: ScrollView {
          id: menuScroll
          clip: true
          ColumnLayout {
            width: menuScroll.availableWidth; spacing: 12
            Label { text: "Workstation menu"; color: window.ink; font.pixelSize: 21; Layout.bottomMargin: 12 }
            Action { Layout.fillWidth: true; text: "Continue"; onClicked: menu.close() }
            Action { Layout.fillWidth: true; text: session.paused ? "Resume time" : "Pause time"; onClicked: session.togglePause() }
            Action { Layout.fillWidth: true; visible: session.resources.ended; text: "Start next workday"; onClicked: { session.nextDay(); menu.close() } }
            Action { Layout.fillWidth: true; text: "Quick save"; onClicked: session.save() }
            Action { Layout.fillWidth: true; text: "Save checkpoint…"; onClicked: { menu.close(); slots.loading=false; slots.open() } }
            Action { Layout.fillWidth: true; text: "Load checkpoint…"; onClicked: { menu.close(); slots.loading=true; slots.open() } }
            Action { Layout.fillWidth: true; text: "Wait five game minutes"; enabled: session.resources.connected && !session.resources.ended; onClicked: { session.advance(300); menu.close() } }
            Action { Layout.fillWidth: true; text: "New game…"; onClicked: { menu.close(); newGameConfirm.open() } }
            Action { Layout.fillWidth: true; text: "Settings — keys and colors"; onClicked: { menu.close(); settings.open() } }
            Action { Layout.fillWidth: true; text: "Modifications"; enabled: session.tabs.some(t => t.id === "modifications" && t.enabled); onClicked: { menu.close(); session.selectTab("modifications") } }
            Action { Layout.fillWidth: true; text: "Money-making tools"; enabled: session.tabs.some(t => t.id === "money" && t.enabled); onClicked: { menu.close(); session.selectTab("money") } }
            Action { Layout.fillWidth: true; text: "Save and quit"; onClicked: { if (session.save()) Qt.quit() } }
          }
        }
    }
    Dialog {
        id: newGameConfirm
        anchors.centerIn: Overlay.overlay
        width: Math.min(500,window.width-32)
        title: "Start a new game?"
        modal: true; focus: true
        onOpened: session.setMenuOpen(true)
        onClosed: session.setMenuOpen(false)
        contentItem: ColumnLayout {
            Label { text: "Current progress and autosave will be replaced. Named checkpoints remain available."; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Button { text: "Cancel"; onClicked: newGameConfirm.close() }
                Button { text: "Start new game"; onClicked: { if(session.newGame())newGameConfirm.close() } }
            }
        }
    }
    SaveDialog { id: slots }
    SettingsDialog { id: settings }
}
