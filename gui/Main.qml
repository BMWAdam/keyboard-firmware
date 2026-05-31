import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qcm.Material as MD

Window {
    id: root
    width: 800
    height: 650
    visible: true
    title: qsTr("Pico 2 W keyboard configurator")

    property bool darkMode: SerialMonitor.isDarkTheme

    property color colSurface:        darkMode ? AppColors.surface        : AppColors.surfaceLight
    property color colSurfaceVariant: darkMode ? AppColors.surfaceVariant : AppColors.surfaceVariantLight
    property color colOnSurface:      darkMode ? AppColors.onSurface      : AppColors.onSurfaceLight
    property color colOnSurfaceVar:   darkMode ? AppColors.onSurfaceVar   : AppColors.onSurfaceVarLight
    property color colPrimary:        darkMode ? AppColors.primary        : AppColors.primaryLight
    property color colSecondary:      darkMode ? AppColors.secondary      : AppColors.secondaryLight
    property color colOutline:        darkMode ? AppColors.outline        : AppColors.outlineLight

    Component.onCompleted: {
        MD.Token.themeMode = darkMode ? MD.Enum.Dark : MD.Enum.Light
    }

    Rectangle {
        anchors.fill: parent
        color: root.colSurface

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            RowLayout {
                Layout.fillWidth: true

                Column {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Pico 2 W Keyboard Controller"
                        font.pixelSize: 24
                        font.bold: true
                        color: root.colOnSurface
                    }

                    Text {
                        text: SerialMonitor.isConnected ? "Status: Connected" : "Status: Disconnected"
                        font.pixelSize: 14
                        color: SerialMonitor.isConnected ? root.colPrimary : root.colSecondary
                    }
                }

                RowLayout {
                    spacing: 8

                    Text {
                        text: root.darkMode ? "🌙" : "☀️"
                        font.pixelSize: 18
                    }

                    Switch {
                        checked: root.darkMode
                        onToggled: {
                            SerialMonitor.isDarkTheme = checked
                            root.darkMode = checked
                            MD.Token.themeMode = checked ? MD.Enum.Dark : MD.Enum.Light
                        }
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12

                ComboBox {
                    id: portSelector
                    model: SerialMonitor.availablePorts
                    Layout.preferredWidth: 200
                    enabled: !SerialMonitor.isConnected
                }

                MD.Button {
                    text: "Refresh"
                    onClicked: SerialMonitor.refreshPorts()
                }

                MD.Button {
                    text: SerialMonitor.isConnected ? "Disconnect" : "Connect"
                    onClicked: {
                        if (SerialMonitor.isConnected)
                            SerialMonitor.disconnectPort()
                        else if (portSelector.currentText !== "")
                            SerialMonitor.connectToPort(portSelector.currentText)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.colSurfaceVariant
                radius: 8

                ScrollView {
                    id: logScrollView
                    anchors.fill: parent
                    anchors.margins: 12
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOn

                    TextEdit {
                        text: SerialMonitor.logText
                        color: root.colOnSurfaceVar
                        font.family: "monospace"
                        wrapMode: Text.Wrap
                        width: logScrollView.width - 20
                        readOnly: true
                        selectByMouse: true

                        onTextChanged: {
                            Qt.callLater(() => {
                                logScrollView.ScrollBar.vertical.position =
                                    1.0 - logScrollView.ScrollBar.vertical.size
                            })
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                enabled: SerialMonitor.isConnected

                TextField {
                    id: serialInput
                    Layout.fillWidth: true
                    placeholderText: "Type a number (0-255) or text to send..."
                    placeholderTextColor: root.colOutline
                    color: root.colOnSurface

                    background: Rectangle {
                        color: root.colSurfaceVariant
                        radius: 4
                        border.color: serialInput.activeFocus ? root.colPrimary : root.colOutline
                        border.width: 1
                    }

                    onAccepted: {
                        if (text.length > 0) {
                            SerialMonitor.sendCommand(text)
                            text = ""
                        }
                    }
                }

                MD.Button {
                    text: "Send"
                    onClicked: {
                        if (serialInput.text.length > 0) {
                            SerialMonitor.sendCommand(serialInput.text)
                            serialInput.text = ""
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                enabled: SerialMonitor.isConnected
                spacing: 12

                MD.Button {
                    text: "Upload Layout Config"
                    onClicked: SerialMonitor.pickAndLoadConfig()
                }

                MD.Button {
                    text: "Upload Underglow Config"
                    onClicked: SerialMonitor.pickAndLoadUnderglowConfig()
                }

                MD.Button {
                    text: "Read Layout from Pico"
                    onClicked: SerialMonitor.readConfigFromPico()
                }

                MD.Button {
                    text: "Read Underglow from Pico"
                    onClicked: SerialMonitor.readUnderglowConfigFromPico()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                enabled: SerialMonitor.isConnected
                spacing: 12

                Text {
                    text: "Underglow:"
                    font.bold: true
                    color: root.colOnSurface
                    Layout.rightMargin: 8
                }

                MD.Button {
                    text: "Toggle"
                    onClicked: SerialMonitor.sendCommand("UG_TOGGLE")
                }

                MD.Button {
                    text: "Next Mode"
                    onClicked: SerialMonitor.sendCommand("UG_MODE_NEXT")
                }

                MD.Button {
                    text: "Dim -"
                    onClicked: SerialMonitor.sendCommand("UG_VAL_DOWN")
                }

                MD.Button {
                    text: "Bright +"
                    onClicked: SerialMonitor.sendCommand("UG_VAL_UP")
                }
            }
        }
    }
}
