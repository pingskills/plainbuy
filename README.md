# PlainBuy

A small native Linux app for one-off BTC purchases through CoinSpot Markets. PlainBuy fills in your available AUD and the current price, so buying takes **Buy** and a confirmation; either value can be changed first.

PlainBuy uses CoinSpot API V2's `/my/buy` endpoint, which places an order on the BTC/AUD Markets book. CoinSpot currently lists a 0.1% Markets fee. The order may fill immediately, fill in parts, or remain open. The displayed AUD amount is a ceiling; the actual spend can be lower. PlainBuy does not sell, schedule trades, or withdraw coins.

## Build

Requires CMake, a C++20 compiler, libsodium, and Qt 6.5+ with Core, Gui, Network, Qml, Quick and QuickControls2.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/plainbuy
```

## Install

On Arch Linux / Omarchy, install the build dependencies and build a local pacman package:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf libsodium qt6-base qt6-declarative hicolor-icon-theme
git clone https://github.com/pingskills/plainbuy.git
cd plainbuy/packaging/arch
makepkg -si
```

This installs PlainBuy system-wide through pacman, so `sudo pacman -R plainbuy` removes it later. The package is built from the fixed `v0.1.4` source archive on GitHub; it is not published to the AUR. When building from an existing checkout, start with `cd packaging/arch`.

On other Linux distributions, configure with `-DCMAKE_INSTALL_PREFIX=/usr/local`, build, then run `sudo cmake --install build`. The install step places the executable, desktop entry, icon, and license. It does not install API keys or order history.

## Use

Create two CoinSpot API keys in My Account → API: a **Full Access** key for buying and a separate **Read Only** key for balance and order status checks. App-based 2FA is required. Do not enable coin withdrawals for the Full Access key. Enter both keys and their one-time secrets in the **CoinSpot API keys** dialog, which opens on first launch (or later from **Account → Enter API keys**). Do not share either key with another person or service. Consider CoinSpot's account-level withdrawal restriction if you do not need withdrawals.

PlainBuy checks each key with CoinSpot's authenticated `/status` endpoint after entry or unlock. The Account menu has **Retry API checks** for temporary connection failures. Both checks must succeed before a buy can be submitted. A status check verifies that a key works; it does not prove that the two keys belong to the same CoinSpot account, so create both from the same account.

Choose **Save encrypted copy** and set a passphrase to avoid entering the API details again. On later launches, PlainBuy asks for that passphrase as it opens; **Enter new keys…** in the same dialog replaces the saved keys instead. If you cancel, the order book stays visible and the buy form is replaced by an **Unlock…** button. PlainBuy does not store the passphrase. The encrypted file is `$XDG_DATA_HOME/plainbuy/credentials.enc` (normally `~/.local/share/plainbuy/credentials.enc`). You can copy it to the corresponding location on another Linux machine and unlock it with the same passphrase. The app creates the directory with owner-only access and writes the file atomically with owner-only permissions. **Account → Remove encrypted file** deletes the saved copy; **Clear API keys** removes the active keys from this session.
If you saved a file with an earlier version of PlainBuy, unlock it, use **Account → Set Read Only key** to add the second key, then use **Account → Save encrypted file** to update the file.

The file stores both key pairs using libsodium Argon2id with the interactive work profile and XChaCha20-Poly1305 authenticated encryption. A strong passphrase matters because anyone with a copy of the file can try guesses offline. If you lose the passphrase, create new CoinSpot API keys and replace the encrypted file.

Once both API keys are verified, PlainBuy fills in the order for you and shows it as one line, such as *Spend all A$1,234.56 at up to A$120,050.00 per BTC*. The amount is your whole available AUD balance (rounded down to the cent), and the price is the current price for that amount. That is the lowest level in CoinSpot's visible sell orders with enough cumulative BTC to cover the order, rounded up to the cent. The order book is refreshed every 15 seconds and the balance with each order check, and each shows the time it was last updated. **Change amount or price** shows the two fields; typing in either switches it to a custom value, and **Use all AUD**, **Use current price**, or **Use all AUD at the current price** switch back. This is a snapshot of CoinSpot's order book, not a valuation of Bitcoin or a promise that the order will fill. Other exchanges are not queried.

Under the order, PlainBuy shows the BTC order quantity, the most the fee can be, and whether the price reaches the current ask. The BTC quantity is the spend amount divided by the price plus CoinSpot's listed 0.1% Markets fee, rounded down to eight decimal places, so the trade value and a fee charged on top both fit within the amount. If buying is not possible, a line under **Review buy** says why. CoinSpot's published market-buy API does not specify whether the fee comes from AUD or from the BTC received, so an order may leave a small AUD remainder.

Select **Review buy** to request a fresh order book. While the price is automatic, PlainBuy recalculates it and the BTC quantity from that book; if the visible sell orders no longer cover the amount, no order is prepared. The confirmation lists the BTC order quantity, maximum price, trade value, fee, total, and whether the price reaches the freshly observed ask. **Cancel** has the default focus. CoinSpot's final trade record determines the fee and net BTC. If the new book request fails with a custom price, the confirmation says the quote is unavailable. A quote cannot guarantee an immediate fill, and an order at a higher price than needed can fill at a lower price and spend less.

Immediately before each buy, PlainBuy requests a fresh available balance and submits the order only if it covers the BTC quantity at the maximum price plus the fee. CoinSpot still makes the final funds check. If the balance request fails, PlainBuy does not submit an order. **Unavailable** means no verified balance is currently displayed, such as before the Read Only key is verified or when a balance request fails; it does not mean your balance is zero.

PlainBuy displays all open BTC/AUD buy orders returned by CoinSpot, including orders placed elsewhere. It polls open orders and completed market order history every 15 seconds; **File → Refresh now** (F5) checks immediately. Orders submitted through PlainBuy are recorded in a local journal and rechecked after a restart. Matching completed records add the reported BTC fill amount, weighted average execution price, AUD trade total, and AUD fee including GST where CoinSpot supplies those fields. The journal is stored in `$XDG_DATA_HOME/plainbuy/orders-<key fingerprint>.json` with owner-only permissions. It contains order IDs and details but no API secrets. Copy it separately if you want that history on another machine. CoinSpot remains the authoritative record; old fills can be absent from the API's limited history response.

An open order has a **Cancel** button. Confirming sends CoinSpot a cancellation request and then refreshes the order list. CoinSpot says an accepted cancellation request can still fail if the order has already started filling, so verify that the order has disappeared and check completed orders. If an order is absent from both API lists, PlainBuy reports that its state is uncertain.

Before sending a buy request, PlainBuy saves a pending attempt to the local journal. If the request times out, returns an unclear response, or the app exits before a response arrives, another buy is blocked after restart. Use **Refresh to review** to load both open and completed orders, inspect the results in CoinSpot, then explicitly acknowledge that the earlier buy may have succeeded. The acknowledgement is saved before the app allows another buy. PlainBuy cannot automatically reconcile an attempt that never returned an order ID, and it never retries a buy automatically. This guard is tied to the Full Access API key used for the attempt; use the same key when reviewing it.

If the journal exists but is damaged or unreadable, PlainBuy blocks buying and shows its path. Back up the file first. Restore a valid copy if you have one. If it cannot be restored, review CoinSpot's open and completed orders, move the damaged file aside, and restart PlainBuy. A missing journal is treated as a fresh start only after the file has been deliberately moved or removed.

CoinSpot fees and API behavior can change. Refer to [CoinSpot fees](https://www.coinspot.com.au/fees) and [API V2 documentation](https://www.coinspot.com.au/v2/api).
