import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: settings
    objectName: "settingsDialog"
    anchors.centerIn: Overlay.overlay
    width: Math.min(600, parent.width-32); height: Math.min(640,parent.height-32)
    modal: true; focus: true; title: "Workstation settings"
    closePolicy: Popup.CloseOnEscape
    property var values: ({})
    property string failure: ""
    property var fields: [
        {id:"font_size",label:"Font size (10–24)"},
        {id:"background",label:"Background color"},{id:"text",label:"Text color"},
        {id:"muted",label:"Secondary text color"},{id:"accent",label:"Selection color"},
        {id:"menu_key",label:"Menu key (Esc always works)"},{id:"pause_key",label:"Pause / resume"},
        {id:"save_key",label:"Save"},{id:"next_tab_key",label:"Next workspace"},{id:"previous_tab_key",label:"Previous workspace"}]
    onAboutToShow: { values=Object.assign({},session.preferences);failure="";session.setMenuOpen(true) }
    onClosed: session.setMenuOpen(false)
    contentItem: ColumnLayout {
        Label { text: "Colors accept names or #RRGGBB. Keybindings use Qt notation, for example Ctrl+S."; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ScrollView {
            id: scroll; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            ColumnLayout {
                width: scroll.availableWidth; spacing: 10
                Repeater {
                    model: settings.fields
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: modelData.label }
                        TextField { Layout.fillWidth: true; text: settings.values[modelData.id] || ""; onTextEdited: settings.values[modelData.id]=text; selectByMouse: true }
                    }
                }
            }
        }
        Label { text: settings.failure; visible: text.length>0; color: "#ef8585"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: settings.close() }
            Button { text: "Save settings"; onClicked: { if(session.applyPreferences(settings.values))settings.close();else settings.failure=session.notice } }
        }
    }
}
