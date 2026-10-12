import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: map
    color: "#151d26"; radius: 6; clip: true
    property var graphData: session.graph
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property var positions: {
        let nodes = graphData.nodes || [], out = {}
        if (!nodes.length) return out
        let xs = nodes.map(n => n.x), ys = nodes.map(n => n.y)
        let left = Math.min.apply(null, xs), right = Math.max.apply(null, xs)
        let top = Math.min.apply(null, ys), bottom = Math.max.apply(null, ys)
        for (const n of nodes) out[n.id] = {x: 90 + (n.x-left)/Math.max(1,right-left)*(width-180), y: 55+(n.y-top)/Math.max(1,bottom-top)*(height-120)}
        return out
    }
    function point(id) {
        const p = positions[id] || {x: 0, y: 0}
        return {x: width/2 + (p.x-width/2)*zoom+panX, y: height/2+(p.y-height/2)*zoom+panY}
    }
    Canvas {
        id: lines; anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d"); ctx.reset()
            for (const link of map.graphData.links || []) {
                if (!map.positions[link.from] || !map.positions[link.to]) continue
                const a=map.point(link.from), b=map.point(link.to)
                const selected=link.from===session.selectedItem || link.to===session.selectedItem
                ctx.strokeStyle=selected ? session.preferences.accent : link.color; ctx.lineWidth=selected ? 2 : 1
                ctx.beginPath();ctx.moveTo(a.x,a.y);ctx.lineTo(b.x,b.y);ctx.stroke()
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        property real lastX
        property real lastY
        onPressed: function(mouse) { lastX=mouse.x;lastY=mouse.y }
        onPositionChanged: function(mouse) { if(pressed) { map.panX+=mouse.x-lastX;map.panY+=mouse.y-lastY;lastX=mouse.x;lastY=mouse.y } }
        onWheel: function(wheel) { map.zoom=Math.max(.5,Math.min(3,map.zoom*(wheel.angleDelta.y>0 ? 1.15 : 1/1.15))) }
    }
    Repeater {
        model: map.graphData.nodes || []
        delegate: Button {
            required property var modelData
            x: map.point(modelData.id).x-width/2; y: map.point(modelData.id).y-height/2
            text: modelData.label+(modelData.presence ? " ★" : "")
            hoverEnabled: true
            onClicked: session.selectItem(modelData.id)
            ToolTip.visible: hovered; ToolTip.text: modelData.hint
            contentItem: Label { text: parent.text; color: modelData.color; font.bold: session.selectedItem===modelData.id }
            background: Rectangle { color: "#18212b"; radius: 5; border.color: session.selectedItem===modelData.id ? session.preferences.accent : modelData.color }
        }
    }
    Row {
        anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 8; spacing: 6
        Button { text: "−"; onClicked: map.zoom=Math.max(.5,map.zoom/1.2) }
        Button { text: "+"; onClicked: map.zoom=Math.min(3,map.zoom*1.2) }
        Button { text: "Fit"; onClicked: {map.zoom=1;map.panX=0;map.panY=0} }
    }
    Label {
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 10
        width: parent.width-20; wrapMode: Text.Wrap; color: session.preferences.muted; font.pixelSize: 11
        text: session.selectedTab==="network" ? "Green: accessible · Amber: credentials · Red: offline · ★ AI present · Drag to pan, wheel to zoom" : "Blue: known person · Amber links: uncertain · Highlight: selected connections · Drag to pan, wheel to zoom"
    }
    onGraphDataChanged: lines.requestPaint()
    onPositionsChanged: lines.requestPaint()
    onZoomChanged: lines.requestPaint()
    onPanXChanged: lines.requestPaint()
    onPanYChanged: lines.requestPaint()
    Connections { target: session; function onStateChanged() {lines.requestPaint()} }
}
