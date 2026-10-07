# PlainBuy

A small native Linux app for one-off BTC purchases through CoinSpot Markets. Enter a maximum AUD price per BTC, an AUD spending ceiling, and press **Buy BTC**. PlainBuy asks for confirmation before placing the order.

PlainBuy uses CoinSpot API V2's `/my/buy` endpoint, which places an order on the BTC/AUD Markets book. CoinSpot currently lists a 0.1% Markets fee. The order may fill immediately, fill in parts, or remain open. The displayed AUD amount is a ceiling; the actual spend can be lower. PlainBuy does not sell, schedule trades, or withdraw coins.

## Build

Requires CMake, a C++20 compiler, libsodium, and Qt 6.5+ with Core, Gui, Network, Qml, Quick and QuickControls2.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/plainbuy
```

To add a launcher on Linux, configure with your chosen `CMAKE_INSTALL_PREFIX`, build, and run `cmake --install build`. The install step places only the executable, desktop entry, and icon.

## Use

Create two CoinSpot API keys in My Account → API: a **Full Access** key for buying and a separate **Read Only** key for balance and order status checks. App-based 2FA is required. Do not enable coin withdrawals for the Full Access key. Enter both keys and their one-time secrets in PlainBuy's **Account → Enter API keys** dialog. Do not share either key with another person or service. Consider CoinSpot's account-level withdrawal restriction if you do not need withdrawals.

PlainBuy checks each key with CoinSpot's authenticated `/status` endpoint after entry or unlock. The Account menu has **Retry API checks** for temporary connection failures. Both checks must succeed before a buy can be submitted. A status check verifies that a key works; it does not prove that the two keys belong to the same CoinSpot account, so create both from the same account.

Choose **Save encrypted copy** and set a passphrase to avoid entering the API details again. On later launches, use **Unlock** and enter that passphrase. PlainBuy does not store the passphrase. The encrypted file is `$XDG_DATA_HOME/plainbuy/credentials.enc` (normally `~/.local/share/plainbuy/credentials.enc`). You can copy it to the corresponding location on another Linux machine and unlock it with the same passphrase. The app creates the directory with owner-only access and writes the file atomically with owner-only permissions. **Account → Remove encrypted file** deletes the saved copy; **Clear API keys** removes the active keys from this session.
If you saved a file with an earlier version of PlainBuy, unlock it, use **Account → Set Read Only key** to add the second key, then use **Account → Save encrypted file** to update the file.

The file stores both key pairs using libsodium Argon2id with the interactive work profile and XChaCha20-Poly1305 authenticated encryption. A strong passphrase matters because anyone with a copy of the file can try guesses offline. If you lose the passphrase, create new CoinSpot API keys and replace the encrypted file.

Use **Refresh** to see the best available BTC/AUD ask and bid–ask gap. After entering an AUD amount, **Suggest max price** checks CoinSpot's visible sell orders and fills the price field with the lowest price level that currently has enough cumulative BTC to cover the quantity implied by your amount. This is a snapshot of CoinSpot's order book, not a valuation of Bitcoin or a promise that the order will fill. Other exchanges are not queried.

Enter the most you will pay per BTC and your maximum AUD spend, then select **Buy BTC** and review the confirmation. The app derives the BTC quantity by dividing the AUD ceiling by the maximum price, rounded down to eight decimal places. The confirmation shows the maximum spend, an approximate BTC amount after CoinSpot's 0.1% fee, and whether your price reaches the last observed ask. The quote is treated as stale after 30 seconds; it is a snapshot and cannot guarantee an immediate fill. A higher maximum price can cause the order to spend less than the AUD ceiling when it fills at a lower price.

PlainBuy shows your available AUD balance when the Read Only key is ready. Immediately before each buy, it requests a fresh available balance and submits the order only if that covers the requested BTC quantity at the maximum price. CoinSpot still makes the final funds check. If the balance request fails, PlainBuy does not submit an order.

PlainBuy displays all open BTC/AUD buy orders returned by CoinSpot, including orders placed elsewhere. It polls open orders and completed market order history every 15 seconds, and **Refresh orders** requests an immediate check. Orders submitted through PlainBuy are recorded in a local journal and rechecked after a restart. Matching completed records add the reported BTC fill amount, weighted average execution price, AUD trade total, and AUD fee including GST where CoinSpot supplies those fields. The journal is stored in `$XDG_DATA_HOME/plainbuy/orders-<key fingerprint>.json` with owner-only permissions. It contains order IDs and details but no API secrets. Copy it separately if you want that history on another machine. CoinSpot remains the authoritative record; old fills can be absent from the API's limited history response.

An open order has a **Cancel** button. Confirming sends CoinSpot a cancellation request and then refreshes the order list. CoinSpot says an accepted cancellation request can still fail if the order has already started filling, so verify that the order has disappeared and check completed orders. If an order is absent from both API lists, PlainBuy reports that its state is uncertain.

Before sending a buy request, PlainBuy saves a pending attempt to the local journal. If the request times out, returns an unclear response, or the app exits before a response arrives, another buy is blocked after restart. Use **Refresh to review** to load both open and completed orders, inspect the results in CoinSpot, then explicitly acknowledge that the earlier buy may have succeeded. The acknowledgement is saved before the app allows another buy. PlainBuy cannot automatically reconcile an attempt that never returned an order ID, and it never retries a buy automatically. This guard is tied to the Full Access API key used for the attempt; use the same key when reviewing it.

CoinSpot fees and API behavior can change. Refer to [CoinSpot fees](https://www.coinspot.com.au/fees) and [API V2 documentation](https://www.coinspot.com.au/v2/api).
