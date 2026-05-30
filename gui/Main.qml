import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qcm.Material as MD

Window {
    id: root
    width: 800
    height: 600
    visible: true
    title: qsTr("Pico 2 W keyboard configurator")

    // --- Single source of truth for theme ---
    property bool darkMode: SerialMonitor.isDarkTheme

    // --- Color palette recomputes when darkMode flips ---
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

            // --- Header row ---
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

            // --- Port controls ---
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

            // --- Log area ---
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

            // --- Send row ---
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

            // --- Upload row ---
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                enabled: SerialMonitor.isConnected

                MD.Button {
                    text: "Upload Layout Config File"
                    onClicked: SerialMonitor.pickAndLoadConfig()
                }

                MD.Button {
                    text: "Upload Underglow Config File"
                    onClicked: SerialMonitor.pickAndLoadUnderglowConfig()
                }

                MD.Button {
                    text: "Read Config from Pico"
                    onClicked: SerialMonitor.readConfigFromPico()
                }
            }
        }
    }
}
