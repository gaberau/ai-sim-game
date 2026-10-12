import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "saveDialog"
    anchors.centerIn: Overlay.overlay
    width: Math.min(600,parent.width-32); height: Math.min(520,parent.height-32)
    property bool loading: false
    property string failure: ""
    title: loading ? "Load checkpoint" : "Save checkpoint"
    modal: true; focus: true
    onAboutToShow: { failure="";session.setMenuOpen(true) }
    onClosed: session.setMenuOpen(false)
    contentItem: ColumnLayout {
        Label { text: dialog.loading ? "Loading replaces your current progress with this checkpoint." : "Enter a slot name or select one to replace."; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: session.saveSlots
            delegate: ItemDelegate {
                required property var modelData
                width: ListView.view.width; text: modelData.label
                onClicked: name.text=modelData.id
            }
            ScrollBar.vertical: ScrollBar {}
        }
        TextField { id: name; Layout.fillWidth: true; placeholderText: "Slot name"; text: "checkpoint"; selectByMouse: true }
        Label { text: dialog.failure; color: "#ef8585"; wrapMode: Text.Wrap; Layout.fillWidth: true; visible: text.length>0 }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: dialog.close() }
            Button {
                text: dialog.loading ? "Load and replace progress" : "Save slot"
                onClicked: {
                    const ok=dialog.loading ? session.loadSlot(name.text) : session.saveSlot(name.text)
                    if(ok)dialog.close();else dialog.failure=session.notice
                }
            }
        }
    }
}
