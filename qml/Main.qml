import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PlainBuy.Core

ApplicationWindow {
    id: window
    property string cancelOrderId: ""
    width: 540
    height: 520
    minimumWidth: 400
    minimumHeight: 320
    visible: true
    title: "PlainBuy"
    color: Theme.background
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.input
    palette.text: Theme.text
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    Component.onCompleted: Buyer.refreshBestAsk()
    Connections {
        target: Buyer
        function onRecommendedPriceChanged() {
            if (Buyer.recommendedPrice)
                priceField.text = Buyer.recommendedPrice
        }
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action { text: qsTr("&Quit"); shortcut: StandardKey.Quit; onTriggered: Qt.quit() }
        }
        Menu {
            title: qsTr("&Account")
            Action { text: qsTr("&Enter API keys…"); onTriggered: credentialsDialog.open() }
            Action { text: qsTr("Set &Read Only key…"); enabled: Buyer.connected && !Buyer.busy; onTriggered: readOnlyDialog.open() }
            Action { text: qsTr("&Unlock encrypted file…"); enabled: Buyer.hasSavedCredentials && !Buyer.busy; onTriggered: unlockDialog.open() }
            Action { text: qsTr("&Save encrypted file…"); enabled: Buyer.connected && Buyer.readOnlyReady && !Buyer.busy; onTriggered: saveDialog.open() }
            Action { text: qsTr("&Remove encrypted file"); enabled: Buyer.hasSavedCredentials && !Buyer.busy; onTriggered: Buyer.forgetSavedCredentials() }
            Action { text: qsTr("&Clear API keys"); enabled: (Buyer.connected || Buyer.readOnlyReady) && !Buyer.busy; onTriggered: Buyer.clearCredentials() }
        }
    }

    Dialog {
        id: readOnlyDialog
        title: qsTr("CoinSpot Read Only API key")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: Buyer.setReadOnlyCredentials(onlyKeyField.text, onlySecretField.text)
        onClosed: { onlyKeyField.text = ""; onlySecretField.text = "" }
        contentItem: ColumnLayout {
            spacing: 9
            Label {
                Layout.fillWidth: true
                text: qsTr("Add a separate Read Only key for balance and order checks. Save the encrypted file again to keep it for future sessions.")
                wrapMode: Text.Wrap
                color: Theme.muted
            }
            TextField {
                id: onlyKeyField
                Layout.fillWidth: true
                placeholderText: qsTr("Read Only API key")
                Accessible.name: qsTr("CoinSpot Read Only API key")
            }
            TextField {
                id: onlySecretField
                Layout.fillWidth: true
                placeholderText: qsTr("Read Only API secret")
                echoMode: TextInput.Password
                Accessible.name: qsTr("CoinSpot Read Only API secret")
            }
        }
    }

    Dialog {
        id: credentialsDialog
        title: qsTr("CoinSpot API keys")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onClosed: { keyField.text = ""; secretField.text = ""; readKeyField.text = ""; readSecretField.text = "" }
        contentItem: ColumnLayout {
            spacing: 9
            Label {
                Layout.fillWidth: true
                text: qsTr("Enter a Full Access key for buying and a separate Read Only key for balance and order checks. Do not enable API withdrawals.")
                wrapMode: Text.Wrap
                color: Theme.muted
            }
            TextField {
                id: keyField
                Layout.fillWidth: true
                placeholderText: qsTr("Full Access API key")
                Accessible.name: qsTr("CoinSpot Full Access API key")
            }
            TextField {
                id: secretField
                Layout.fillWidth: true
                placeholderText: qsTr("Full Access API secret")
                echoMode: TextInput.Password
                Accessible.name: qsTr("CoinSpot API secret")
            }
            TextField {
                id: readKeyField
                Layout.fillWidth: true
                placeholderText: qsTr("Read Only API key")
                Accessible.name: qsTr("CoinSpot Read Only API key")
            }
            TextField {
                id: readSecretField
                Layout.fillWidth: true
                placeholderText: qsTr("Read Only API secret")
                echoMode: TextInput.Password
                Accessible.name: qsTr("CoinSpot Read Only API secret")
            }
            CheckBox {
                id: saveAfterEntry
                text: qsTr("Save encrypted copy")
                checked: true
            }
        }
        onAccepted: {
            Buyer.setCredentials(keyField.text, secretField.text)
            Buyer.setReadOnlyCredentials(readKeyField.text, readSecretField.text)
            if (saveAfterEntry.checked && Buyer.connected && Buyer.readOnlyReady)
                Qt.callLater(function() { saveDialog.open() })
        }
    }

    Dialog {
        id: saveDialog
        title: qsTr("Encrypt API credentials")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: Buyer.saveCredentials(savePass.text, saveConfirm.text)
        onClosed: { savePass.text = ""; saveConfirm.text = "" }
        contentItem: ColumnLayout {
            spacing: 9
            Label {
                Layout.fillWidth: true
                text: qsTr("Choose a passphrase of at least 12 characters. You will enter it each time you open PlainBuy. If you lose it, the saved API credentials cannot be recovered from this file.")
                color: Theme.muted
                wrapMode: Text.Wrap
            }
            TextField {
                id: savePass
                Layout.fillWidth: true
                placeholderText: qsTr("Passphrase")
                echoMode: TextInput.Password
                Accessible.name: qsTr("Encryption passphrase")
            }
            TextField {
                id: saveConfirm
                Layout.fillWidth: true
                placeholderText: qsTr("Repeat passphrase")
                echoMode: TextInput.Password
                Accessible.name: qsTr("Confirm encryption passphrase")
            }
        }
    }

    Dialog {
        id: unlockDialog
        title: qsTr("Unlock API credentials")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: Buyer.unlockCredentials(unlockPass.text)
        onClosed: unlockPass.text = ""
        contentItem: TextField {
            id: unlockPass
            placeholderText: qsTr("File passphrase")
            echoMode: TextInput.Password
            Accessible.name: qsTr("Encrypted file passphrase")
        }
    }

    Dialog {
        id: confirmDialog
        title: qsTr("Confirm BTC purchase")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: Buyer.submit()
        contentItem: Label {
            text: Buyer.preview()
            wrapMode: Text.Wrap
            color: Theme.text
        }
    }

    Dialog {
        id: cancelDialog
        title: qsTr("Confirm cancellation")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: Buyer.cancelOrder(window.cancelOrderId)
        contentItem: Label {
            text: qsTr("Ask CoinSpot to cancel order %1? A cancellation request may arrive after the order has already filled. PlainBuy will check its status afterward.").arg(window.cancelOrderId)
            wrapMode: Text.WrapAnywhere
            color: Theme.text
        }
    }

    Dialog {
        id: acknowledgeDialog
        title: qsTr("Acknowledge uncertain buy")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: Buyer.acknowledgeUncertainBuy()
        contentItem: Label {
            text: qsTr("Confirm that you checked CoinSpot's open and completed BTC orders. The earlier buy may have succeeded even without an API response. Proceeding can create a duplicate purchase.")
            wrapMode: Text.Wrap
            color: Theme.text
        }
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 18
            Item { Layout.preferredHeight: 4 }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 7
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("BTC / AUD"); color: Theme.muted; font.letterSpacing: 1.4 }
                    Item { Layout.fillWidth: true }
                    Label { text: Buyer.connected && Buyer.readOnlyReady ? qsTr("Keys ready") : Buyer.hasSavedCredentials && !Buyer.connected ? qsTr("Locked") : qsTr("Keys needed"); color: Theme.muted }
                    Button {
                        text: Buyer.hasSavedCredentials && !Buyer.connected ? qsTr("Unlock…") : qsTr("Set keys…")
                        onClicked: Buyer.hasSavedCredentials && !Buyer.connected ? unlockDialog.open() : credentialsDialog.open()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Best ask"); color: Theme.muted }
                    Label {
                        text: Buyer.bestAsk ? "A$" + Buyer.bestAsk : qsTr("Unavailable")
                        color: Theme.text
                        font.features: { "tnum": 1 }
                    }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Refresh"); onClicked: Buyer.refreshBestAsk() }
                }
                Label {
                    text: Buyer.marketSpread ? qsTr("Current bid–ask gap: %1").arg(Buyer.marketSpread) : ""
                    visible: text.length > 0
                    color: Theme.muted
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Available AUD"); color: Theme.muted }
                    Label { text: Buyer.availableAud ? "A$" + Buyer.availableAud : qsTr("Unavailable"); color: Theme.text }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Refresh"); enabled: Buyer.readOnlyReady && !Buyer.busy; onClicked: Buyer.refreshBalance() }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                implicitHeight: 1
                color: Theme.border
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 10
                Label { text: qsTr("Maximum price per BTC"); color: Theme.text; font.weight: Font.DemiBold }
                TextField {
                    id: priceField
                    objectName: "priceField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("AUD per BTC")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    font.features: { "tnum": 1 }
                    Accessible.name: qsTr("Maximum price per Bitcoin in Australian dollars")
                    onAccepted: amountField.forceActiveFocus()
                }
                Label { text: qsTr("Amount to spend, up to"); color: Theme.text; font.weight: Font.DemiBold }
                TextField {
                    id: amountField
                    objectName: "amountField"
                    Layout.fillWidth: true
                    placeholderText: qsTr("AUD")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    font.features: { "tnum": 1 }
                    Accessible.name: qsTr("Maximum Australian dollar amount to spend")
                    onAccepted: buyButton.clicked()
                }
                Button {
                    text: qsTr("Suggest max price")
                    onClicked: Buyer.recommend(amountField.text)
                    Accessible.name: text
                }
                Button {
                    id: buyButton
                    objectName: "buyButton"
                    text: Buyer.busy ? qsTr("Submitting…") : qsTr("Buy BTC")
                    enabled: !Buyer.busy && !Buyer.uncertainBuy
                    Layout.alignment: Qt.AlignLeft
                    onClicked: if (Buyer.prepare(amountField.text, priceField.text)) confirmDialog.open()
                    Accessible.name: text
                    contentItem: Label {
                        text: buyButton.text
                        color: Theme.onAccent
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitWidth: 112
                        implicitHeight: 34
                        radius: 3
                        color: buyButton.down ? Qt.darker(Theme.accent, 1.15) : Theme.accent
                        border.width: buyButton.visualFocus ? 2 : 0
                        border.color: Theme.text
                    }
                }
                Label { Layout.fillWidth: true; text: qsTr("Places a CoinSpot Markets buy. Check open orders before placing another."); color: Theme.muted; wrapMode: Text.Wrap }
                Label {
                    Layout.fillWidth: true
                    visible: Buyer.uncertainBuy
                    text: qsTr("A previous buy request has an unknown outcome. New buys are blocked until you refresh orders, check CoinSpot, and acknowledge the risk of a duplicate.")
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: Buyer.uncertainBuy
                    Button {
                        text: qsTr("Refresh to review")
                        enabled: Buyer.readOnlyReady && !Buyer.busy
                        onClicked: Buyer.reviewUncertainBuy()
                    }
                    Button {
                        text: qsTr("Acknowledge…")
                        enabled: Buyer.uncertainRefreshed && !Buyer.busy
                        onClicked: acknowledgeDialog.open()
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: Buyer.status
                    visible: text.length > 0
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; implicitHeight: 1; color: Theme.border }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Open BTC buy orders"); color: Theme.text; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Refresh orders"); enabled: Buyer.readOnlyReady; onClicked: Buyer.refreshOrderStatus() }
                }
                Label {
                    Layout.fillWidth: true
                    visible: Buyer.readOnlyReady && Buyer.openOrders.length === 0
                    text: Buyer.ordersLoaded ? qsTr("No open BTC buy orders.") : qsTr("Open orders unavailable. Refresh to try again.")
                    color: Theme.muted
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: Buyer.openOrders
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 BTC at A$%2\nID: %3").arg(Number(modelData.amount).toFixed(8)).arg(Number(modelData.rate).toFixed(2)).arg(modelData.id)
                            color: Theme.text
                            wrapMode: Text.WrapAnywhere
                        }
                        Button {
                            text: qsTr("Cancel…")
                            enabled: !Buyer.busy
                            onClicked: { window.cancelOrderId = modelData.id; cancelDialog.open() }
                        }
                    }
                }
                Label {
                    Layout.topMargin: 9
                    text: qsTr("Orders placed with PlainBuy")
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                Label { visible: Buyer.orderHistory.length === 0; text: qsTr("No locally recorded orders yet."); color: Theme.muted }
                Repeater {
                    model: Buyer.orderHistory
                    Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: qsTr("%1 BTC at A$%2 · %3\n%4").arg(Number(modelData.amount).toFixed(8)).arg(Number(modelData.rate).toFixed(2)).arg(modelData.state).arg(modelData.id ? qsTr("ID: %1").arg(modelData.id) : qsTr("No order ID returned"))
                              + (modelData.filledBtc !== undefined ? qsTr("\nFilled: %1 BTC · average execution: A$%2/BTC").arg(Number(modelData.filledBtc).toFixed(8)).arg(Number(modelData.averageFillPrice).toFixed(2)) : "")
                              + (modelData.reportedTradeTotalAud !== undefined ? qsTr("\nCoinSpot trade total: A$%1").arg(Number(modelData.reportedTradeTotalAud).toFixed(2)) : "")
                              + (modelData.reportedFeeAud !== undefined ? qsTr(" · reported AUD fee incl. GST: A$%1").arg(Number(modelData.reportedFeeAud).toFixed(2)) : "")
                        color: Theme.muted
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
            Item { Layout.preferredHeight: 4 }
        }
    }
}
