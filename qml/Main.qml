import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PlainBuy.Core

ApplicationWindow {
    id: window
    property string cancelOrderId: ""
    // Auto fields follow the balance and order book until the user types in them.
    property bool amountAuto: true
    property bool priceAuto: true
    // Fields are shown when either value is custom, or on request.
    property bool editOpen: false
    readonly property bool editing: editOpen || !amountAuto || !priceAuto
    property bool historyOpen: false
    property var confirmQuote: ({})
    readonly property var quote: {
        Buyer.availableAud; Buyer.bestAsk
        return Buyer.quote(amountField.text, priceField.text)
    }
    function money(value) { return Number(value).toLocaleString(Qt.locale("en_AU"), "f", 2) }
    function btc(value) { return Number(value).toFixed(8) }
    function applyAuto() {
        if (amountAuto) amountField.text = Buyer.spendableAud
        if (priceAuto) priceField.text = Buyer.marketPriceFor(amountField.text)
    }
    width: 540
    height: 560
    minimumWidth: 400
    minimumHeight: 480
    visible: true
    title: "PlainBuy"
    color: Theme.background
    palette.window: Theme.background
    palette.windowText: Theme.text
    palette.base: Theme.input
    palette.text: Theme.text
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    readonly property bool unlocked: Buyer.connected && Buyer.readOnlyReady
    // New keys are offered for saving only after CoinSpot verifies both.
    property bool saveWhenVerified: false
    property bool saveSkipped: false
    function openKeys() {
        if (Buyer.hasSavedCredentials && !Buyer.connected) unlockDialog.open()
        else if (Buyer.connected && !Buyer.readOnlyReady) readOnlyDialog.open()
        else credentialsDialog.open()
    }
    Component.onCompleted: {
        Buyer.refreshBestAsk()
        // Ask for the passphrase (or first-run keys) once per launch.
        Qt.callLater(function() { if (!window.unlocked) window.openKeys() })
    }
    Connections {
        target: Buyer
        function onChanged() {
            if (!window.saveWhenVerified) return
            if (Buyer.keysVerified) {
                window.saveWhenVerified = false
                saveDialog.open()
            } else if (!Buyer.keysChecking) {
                window.saveWhenVerified = false
                window.saveSkipped = true
            }
        }
        function onMarketUpdated() { window.applyAuto() }
        function onPreviewReady() {
            window.confirmQuote = Buyer.preview()
            confirmDialog.open()
        }
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            Action {
                text: qsTr("&Refresh now")
                shortcut: StandardKey.Refresh
                enabled: !Buyer.previewLoading && !Buyer.busy
                onTriggered: { Buyer.refreshBestAsk(); Buyer.refreshBalance(); Buyer.refreshOrderStatus() }
            }
            Action { text: qsTr("&Quit"); shortcut: StandardKey.Quit; onTriggered: Qt.quit() }
        }
        Menu {
            title: qsTr("&Account")
            Action { text: qsTr("&Enter API keys…"); enabled: !Buyer.previewLoading && !Buyer.busy; onTriggered: credentialsDialog.open() }
            Action { text: qsTr("Set &Read Only key…"); enabled: Buyer.connected && !Buyer.busy && !Buyer.previewLoading; onTriggered: readOnlyDialog.open() }
            Action { text: qsTr("&Retry API checks"); enabled: (Buyer.connected || Buyer.readOnlyReady) && !Buyer.busy && !Buyer.previewLoading; onTriggered: Buyer.validateKeys() }
            Action { text: qsTr("&Unlock encrypted file…"); enabled: Buyer.hasSavedCredentials && !Buyer.busy && !Buyer.previewLoading; onTriggered: unlockDialog.open() }
            Action { text: qsTr("&Save encrypted file…"); enabled: Buyer.keysVerified && !Buyer.busy && !Buyer.previewLoading; onTriggered: saveDialog.open() }
            Action { text: qsTr("&Remove encrypted file"); enabled: Buyer.hasSavedCredentials && !Buyer.busy && !Buyer.previewLoading; onTriggered: Buyer.forgetSavedCredentials() }
            Action { text: qsTr("&Clear API keys"); enabled: (Buyer.connected || Buyer.readOnlyReady) && !Buyer.busy && !Buyer.previewLoading; onTriggered: Buyer.clearCredentials() }
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
        onOpened: onlyKeyField.forceActiveFocus()
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
        onOpened: keyField.forceActiveFocus()
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
                onAccepted: credentialsDialog.accept()
            }
            CheckBox {
                id: saveAfterEntry
                text: qsTr("Save encrypted copy")
                checked: true
            }
            Label {
                Layout.fillWidth: true
                visible: saveAfterEntry.checked && Buyer.hasSavedCredentials
                text: qsTr("Once CoinSpot verifies both keys, saving replaces your existing encrypted key file and its passphrase. Until then the old file is kept.")
                wrapMode: Text.Wrap
                color: Theme.text
            }
        }
        onAccepted: {
            Buyer.setCredentials(keyField.text, secretField.text)
            Buyer.setReadOnlyCredentials(readKeyField.text, readSecretField.text)
            window.saveSkipped = false
            window.saveWhenVerified = saveAfterEntry.checked && Buyer.connected && Buyer.readOnlyReady
        }
    }

    Dialog {
        id: saveDialog
        title: qsTr("Encrypt API credentials")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        function trySave() {
            const error = Buyer.saveCredentials(savePass.text, saveConfirm.text)
            if (error) { saveError.text = error; savePass.forceActiveFocus() }
            else saveDialog.close()
        }
        onOpened: savePass.forceActiveFocus()
        onClosed: { savePass.text = ""; saveConfirm.text = ""; saveError.text = "" }
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
                onAccepted: saveConfirm.forceActiveFocus()
            }
            TextField {
                id: saveConfirm
                Layout.fillWidth: true
                placeholderText: qsTr("Repeat passphrase")
                echoMode: TextInput.Password
                Accessible.name: qsTr("Confirm encryption passphrase")
                onAccepted: saveDialog.trySave()
            }
            Label {
                id: saveError
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.text
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); onClicked: saveDialog.close() }
            Button { text: qsTr("Save"); highlighted: true; onClicked: saveDialog.trySave() }
        }
    }

    Dialog {
        id: unlockDialog
        title: qsTr("Unlock PlainBuy")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        function tryUnlock() {
            const error = Buyer.unlockCredentials(unlockPass.text)
            if (error) { unlockError.text = error; unlockPass.text = ""; unlockPass.forceActiveFocus() }
            else unlockDialog.close()
        }
        onOpened: unlockPass.forceActiveFocus()
        onClosed: { unlockPass.text = ""; unlockError.text = "" }
        contentItem: ColumnLayout {
            spacing: 9
            Label {
                Layout.fillWidth: true
                text: qsTr("Enter the passphrase for your saved CoinSpot API keys.")
                color: Theme.muted
                wrapMode: Text.Wrap
            }
            TextField {
                id: unlockPass
                Layout.fillWidth: true
                placeholderText: qsTr("Passphrase")
                echoMode: TextInput.Password
                Accessible.name: qsTr("Encrypted file passphrase")
                onAccepted: unlockDialog.tryUnlock()
            }
            Label {
                id: unlockError
                Layout.fillWidth: true
                visible: text.length > 0
                color: Theme.text
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
        }
        footer: RowLayout {
            spacing: 8
            Button {
                Layout.leftMargin: 12
                Layout.bottomMargin: 12
                flat: true
                text: qsTr("Enter new keys…")
                onClicked: { unlockDialog.close(); credentialsDialog.open() }
            }
            Item { Layout.fillWidth: true }
            Button { Layout.bottomMargin: 12; text: qsTr("Cancel"); onClicked: unlockDialog.close() }
            Button {
                Layout.rightMargin: 12
                Layout.bottomMargin: 12
                text: qsTr("Unlock")
                highlighted: true
                onClicked: unlockDialog.tryUnlock()
            }
        }
    }

    Dialog {
        id: confirmDialog
        title: qsTr("Confirm BTC purchase")
        modal: true
        anchors.centerIn: parent
        width: Math.min(window.width - 32, 440)
        onAccepted: Buyer.submit()
        onOpened: confirmCancel.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: 10
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 16
                rowSpacing: 4
                Repeater {
                    // Label and value cells, row by row.
                    model: [
                        qsTr("Order quantity"), qsTr("%1 BTC").arg(window.btc(window.confirmQuote.btc)),
                        qsTr("At most"), qsTr("A$%1 per BTC").arg(window.money(window.confirmQuote.maxPrice)),
                        qsTr("Trade value up to"), "A$" + window.money(window.confirmQuote.tradeValue),
                        qsTr("CoinSpot fee (0.1%)"), qsTr("up to A$%1").arg(window.money(window.confirmQuote.fee)),
                        qsTr("Total up to"), qsTr("A$%1 of your A$%2").arg(window.money(window.confirmQuote.total)).arg(window.money(window.confirmQuote.limit))
                    ]
                    delegate: Label {
                        required property var modelData
                        required property int index
                        text: modelData
                        color: index % 2 ? Theme.text : Theme.muted
                        font.features: { "tnum": 1 }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: window.confirmQuote.marketContext || ""
                wrapMode: Text.Wrap
                color: Theme.text
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Available AUD is checked again before submission. The order may fill partly; CoinSpot's trade record shows the final fee and BTC.")
                wrapMode: Text.Wrap
                color: Theme.muted
            }
        }
        footer: DialogButtonBox {
            Button {
                id: confirmCancel
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
            Button {
                text: qsTr("Buy %1 BTC").arg(window.btc(window.confirmQuote.btc))
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
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
            spacing: 14
            Item { Layout.preferredHeight: 6 }
            // Market: one compact line; the book refreshes itself every 15 s.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                spacing: 8
                Label { text: qsTr("BTC/AUD ask"); color: Theme.muted }
                Label {
                    text: Buyer.bestAsk ? "A$" + window.money(Buyer.bestAsk) : qsTr("unavailable")
                    color: Theme.text
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                }
                Label { visible: Buyer.marketSpread !== ""; text: qsTr("gap %1").arg(Buyer.marketSpread); color: Theme.muted }
                Item { Layout.fillWidth: true }
                Label { visible: Buyer.bookUpdated !== ""; text: Buyer.bookUpdated; color: Theme.muted; font.features: { "tnum": 1 } }
            }
            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; implicitHeight: 1; color: Theme.border }

            // Locked: one action instead of the buy form.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                visible: !window.unlocked
                spacing: 10
                Label {
                    text: Buyer.hasSavedCredentials && !Buyer.connected ? qsTr("Locked") : qsTr("No API keys")
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: Buyer.hasSavedCredentials && !Buyer.connected
                          ? qsTr("Unlock your saved CoinSpot API keys to see your balance and buy.")
                          : Buyer.connected
                            ? qsTr("Add a Read Only API key for balance and order checks to start buying.")
                            : qsTr("Enter your CoinSpot API keys to see your balance and buy.")
                    color: Theme.muted
                    wrapMode: Text.Wrap
                }
                Button {
                    text: Buyer.hasSavedCredentials && !Buyer.connected ? qsTr("Unlock…")
                        : Buyer.connected ? qsTr("Add Read Only key…") : qsTr("Enter API keys…")
                    highlighted: true
                    onClicked: window.openKeys()
                }
                Label {
                    Layout.fillWidth: true
                    text: Buyer.status
                    visible: text.length > 0
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
            }

            // Problems that need attention, each with its action.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                visible: window.unlocked && (Buyer.journalProblem || Buyer.journalUnsaved || Buyer.uncertainBuy || window.saveWhenVerified || window.saveSkipped)
                implicitHeight: problems.implicitHeight + 20
                radius: 4
                color: Theme.surface
                border.color: Theme.accent
                ColumnLayout {
                    id: problems
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        visible: window.saveWhenVerified || window.saveSkipped
                        text: window.saveWhenVerified
                              ? qsTr("The new keys will be offered for saving once CoinSpot verifies both.")
                              : qsTr("The new keys were not saved because CoinSpot did not verify both. Any saved key file is unchanged.")
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: Buyer.journalProblem
                        text: qsTr("The local order journal cannot be read, so buying is blocked. Back up and inspect this file: %1. Restore a valid copy, or check CoinSpot's open and completed orders, move the damaged file aside, and reopen PlainBuy. An unknown earlier buy may have succeeded.").arg(Buyer.journalPath)
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: Buyer.journalUnsaved
                        text: qsTr("The latest order update is not saved in the local order journal (%1). New buys are blocked until it is saved.").arg(Buyer.journalPath)
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    Button {
                        visible: Buyer.journalUnsaved
                        text: qsTr("Retry saving")
                        onClicked: Buyer.retrySaveJournal()
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: Buyer.uncertainBuy
                        text: qsTr("A previous buy request has an unknown outcome. New buys are blocked until you refresh orders, check CoinSpot, and acknowledge the risk of a duplicate.")
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        visible: Buyer.uncertainBuy
                        Button {
                            text: qsTr("Refresh to review")
                            enabled: Buyer.readKeyVerified && !Buyer.busy
                            onClicked: Buyer.reviewUncertainBuy()
                        }
                        Button {
                            text: qsTr("Acknowledge…")
                            enabled: Buyer.uncertainRefreshed && !Buyer.busy
                            onClicked: acknowledgeDialog.open()
                        }
                    }
                }
            }

            // Buy: a one-line summary while both values are automatic, the fields otherwise.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                visible: window.unlocked
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: Buyer.availableAud
                          ? qsTr("Available A$%1 · %2").arg(window.money(Buyer.availableAud)).arg(Buyer.balanceUpdated)
                          : Buyer.readKeyVerified ? qsTr("Balance unavailable; it is retried automatically.") : qsTr("Balance loads once your keys are verified.")
                    color: Theme.muted
                    font.features: { "tnum": 1 }
                    wrapMode: Text.Wrap
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: !window.editing
                    spacing: 2
                    Label {
                        Layout.fillWidth: true
                        text: Buyer.spendableAud && Number(Buyer.spendableAud) > 0
                              ? qsTr("Spend all <b>A$%1</b>").arg(window.money(Buyer.spendableAud))
                              : Buyer.spendableAud ? qsTr("No AUD available to spend") : qsTr("Waiting for your balance…")
                        textFormat: Text.StyledText
                        color: Theme.text
                        font.pointSize: Theme.fontSize * 1.35
                        font.features: { "tnum": 1 }
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: priceField.text
                              ? qsTr("at up to <b>A$%1</b> per BTC, the current price").arg(window.money(priceField.text))
                              : qsTr("at the current price (loading…)")
                        textFormat: Text.StyledText
                        color: Theme.text
                        font.features: { "tnum": 1 }
                        wrapMode: Text.Wrap
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: window.editing
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 4
                    Label { text: qsTr("Spend (A$)"); color: Theme.text; font.weight: Font.DemiBold }
                    TextField {
                        id: amountField
                        objectName: "amountField"
                        Layout.fillWidth: true
                        enabled: !Buyer.previewLoading && !Buyer.busy
                        placeholderText: qsTr("AUD, fee included")
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        font.features: { "tnum": 1 }
                        Accessible.name: qsTr("Australian dollars to spend, including the fee")
                        onTextEdited: {
                            window.amountAuto = false
                            if (window.priceAuto) priceField.text = Buyer.marketPriceFor(text)
                        }
                        onAccepted: if (buyButton.enabled) buyButton.clicked()
                    }
                    Item { implicitWidth: 1 }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: window.amountAuto ? qsTr("Using available balance") : qsTr("Custom amount"); color: Theme.muted }
                        Button { visible: !window.amountAuto; flat: true; text: qsTr("Use all AUD"); onClicked: { window.amountAuto = true; window.applyAuto() } }
                    }
                    Label { text: qsTr("Max price (A$)"); color: Theme.text; font.weight: Font.DemiBold }
                    TextField {
                        id: priceField
                        objectName: "priceField"
                        Layout.fillWidth: true
                        enabled: !Buyer.previewLoading && !Buyer.busy
                        placeholderText: qsTr("per BTC")
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        font.features: { "tnum": 1 }
                        Accessible.name: qsTr("Maximum price per Bitcoin in Australian dollars")
                        onTextEdited: window.priceAuto = false
                        onAccepted: if (buyButton.enabled) buyButton.clicked()
                    }
                    Item { implicitWidth: 1 }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: window.priceAuto ? qsTr("Using current order-book price") : qsTr("Custom price")
                            color: Theme.muted
                            ToolTip.visible: priceHint.hovered
                            ToolTip.text: window.priceAuto
                                          ? qsTr("The lowest sell orders that cover this amount, rounded up to the cent. Recalculated when you review the buy.")
                                          : qsTr("If this is below the current ask, the order may stay open until a seller matches it.")
                            HoverHandler { id: priceHint }
                        }
                        Button { visible: !window.priceAuto; flat: true; text: qsTr("Use current price"); onClicked: { window.priceAuto = true; window.applyAuto() } }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: window.quote.btc !== undefined
                          ? qsTr("Order %1 BTC · fee up to A$%2").arg(window.btc(window.quote.btc)).arg(window.money(window.quote.fee))
                            + (window.quote.askKnown ? (window.quote.reachesAsk ? qsTr(" · should fill straight away") : qsTr(" · below the ask; may stay open")) : "")
                          : ""
                    visible: text.length > 0
                    color: Theme.muted
                    font.features: { "tnum": 1 }
                    wrapMode: Text.Wrap
                }
                Button {
                    id: buyButton
                    objectName: "buyButton"
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: Buyer.previewLoading ? qsTr("Checking price…") : Buyer.busy ? qsTr("Submitting…") : qsTr("Review buy")
                    enabled: Buyer.buyBlockedReason === "" && window.quote.valid && !Buyer.busy && !Buyer.previewLoading
                    onClicked: Buyer.prepare(amountField.text, priceField.text, window.priceAuto)
                    Accessible.name: text
                    contentItem: Label {
                        text: buyButton.text
                        color: Theme.onAccent
                        font.weight: Font.DemiBold
                        font.pointSize: Theme.fontSize * 1.15
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitHeight: 48
                        radius: 4
                        opacity: buyButton.enabled ? 1 : 0.45
                        color: buyButton.down ? Qt.darker(Theme.accent, 1.15) : Theme.accent
                        border.width: buyButton.visualFocus ? 2 : 0
                        border.color: Theme.text
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: Buyer.busy || Buyer.previewLoading ? "" : Buyer.buyBlockedReason || (window.quote.valid ? "" : window.quote.error || "")
                    visible: text.length > 0
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                Button {
                    flat: true
                    text: window.editing ? qsTr("Use all AUD at the current price") : qsTr("Change amount or price")
                    enabled: !Buyer.busy && !Buyer.previewLoading
                    onClicked: {
                        if (window.editing) {
                            window.amountAuto = true
                            window.priceAuto = true
                            window.editOpen = false
                            window.applyAuto()
                        } else {
                            window.editOpen = true
                        }
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

            // Orders: below the buy section; history starts collapsed.
            Rectangle { visible: window.unlocked; Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; implicitHeight: 1; color: Theme.border }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 28
                visible: window.unlocked
                spacing: 8
                Label { text: qsTr("Open BTC buy orders"); color: Theme.text; font.weight: Font.DemiBold }
                Label {
                    Layout.fillWidth: true
                    visible: Buyer.openOrders.length === 0
                    text: Buyer.ordersLoaded ? qsTr("None.") : qsTr("Not loaded yet; checked every 15 seconds.")
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
                            text: qsTr("%1 BTC at A$%2\nID: %3").arg(Number(modelData.amount).toFixed(8)).arg(window.money(modelData.rate)).arg(modelData.id)
                            color: Theme.text
                            wrapMode: Text.WrapAnywhere
                        }
                        Button {
                            text: qsTr("Cancel…")
                            enabled: Buyer.keysVerified && !Buyer.busy
                            onClicked: { window.cancelOrderId = modelData.id; cancelDialog.open() }
                        }
                    }
                }
                Button {
                    Layout.topMargin: 4
                    flat: true
                    text: (window.historyOpen ? "▾ " : "▸ ") + qsTr("Orders placed with PlainBuy (%1)").arg(Buyer.orderHistory.length)
                    onClicked: window.historyOpen = !window.historyOpen
                }
                Repeater {
                    model: window.historyOpen ? Buyer.orderHistory : []
                    Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: qsTr("%1 · %2 BTC at A$%3 · %4\n%5").arg(modelData.date || "").arg(Number(modelData.amount).toFixed(8)).arg(window.money(modelData.rate)).arg(modelData.state).arg(modelData.id ? qsTr("ID: %1").arg(modelData.id) : qsTr("No order ID returned"))
                              + (modelData.filledBtc !== undefined ? qsTr("\nFilled: %1 BTC · average execution: A$%2/BTC").arg(Number(modelData.filledBtc).toFixed(8)).arg(window.money(modelData.averageFillPrice)) : "")
                              + (modelData.reportedTradeTotalAud !== undefined ? qsTr("\nCoinSpot trade total: A$%1").arg(window.money(modelData.reportedTradeTotalAud)) : "")
                              + (modelData.reportedFeeAud !== undefined ? qsTr(" · reported AUD fee incl. GST: A$%1").arg(window.money(modelData.reportedFeeAud)) : "")
                        color: Theme.muted
                        wrapMode: Text.Wrap
                    }
                }
            }
            Item { Layout.preferredHeight: 6 }
        }
    }
}
