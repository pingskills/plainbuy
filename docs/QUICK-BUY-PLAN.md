# Quick buy: plan

Goal: once the keys are unlocked and verified, buying all available AUD at the
current price takes one click on **Buy** and one on the confirmation. The user
can still override either field.

## Assumptions

- **Amount**: the whole available AUD balance, rounded down to the cent. The
  existing fee allowance in `coinAmount()` keeps the trade value plus CoinSpot's
  0.1% fee within that amount.
- **Price**: the "current price" for this amount. That is the lowest visible
  ask level whose cumulative depth covers the order (the existing
  `recommendedCap()`), rounded *up* to the cent so the order still reaches that
  level. An order at this price normally fills straight away. If the book has
  moved, part of it may stay open.

## Behaviour

1. **Autofill**
   - Both fields start in *auto* mode. The amount follows the latest balance and
     the price follows the latest order book for that amount.
   - Typing in a field switches it to *custom*. A "Use all AUD" or "Use current
     price" link puts it back in auto mode.
   - A custom amount with an auto price keeps the price following the book for
     the custom amount.
2. **Keep data fresh**
   - Fetch the public order book every 15 s while idle, plus on launch.
   - Fetch the balance after the read-only key is verified, on each order poll
     (15 s), and straight after an order is accepted.
   - Show when the data was last updated.
3. **Live summary** under the fields, in plain words, e.g. *≈ 0.01234567 BTC ·
   trade up to A$1,233.33 + A$1.23 fee*. It also says whether the price reaches
   the best ask. Problems show inline, e.g. invalid input, more than the
   available AUD, or a book too thin to cover the order.
4. **One-click Buy** labelled with what it does (*Buy ≈0.0123 BTC*). When it is
   disabled, a line says why (keys, unknown buy, journal, no balance, invalid
   input).
5. **Re-price at confirmation**
   - Buy already fetches a fresh book. In auto-price mode the price and BTC
     quantity are recalculated from that fresh book before the confirmation
     opens, so the confirmation shows a current quote.
   - If the book no longer covers the amount, no confirmation opens and the
     reason is shown.
6. **Clearer confirmation**: labelled rows (BTC, max price, trade value, fee,
   total, comparison with the ask). The confirm button is labelled
   *Buy … BTC*. Cancel has the default focus, so pressing Enter twice cannot
   buy.
7. **After an order**: the balance refreshes and the amount (still in auto mode)
   follows the remaining balance. Usually that is ~A$0, which leaves Buy
   disabled with the reason "No AUD available".

Unchanged safety checks: the fresh balance check before submission, the journal
and unknown-buy blocks, and the confirmation dialog.

## Code changes

`Buyer` (C++):
- Keep the last ask levels (`m_askLevels`), the numeric available balance, and
  the time of the last book and balance update.
- `static double currentPrice(asks, budget)`: `recommendedCap` rounded up to
  the cent.
- `Q_INVOKABLE QString maxSpend()`: the whole-cent available balance.
- `Q_INVOKABLE QString marketPriceFor(aud)`: the current price for that amount
  from the cached book.
- `Q_INVOKABLE QVariantMap quote(aud, price)`: wraps the static, unit-tested
  `quoteFor(...)`. Returns validity, a field error, BTC, trade value, fee,
  total, the comparison with the ask, and an over-balance flag.
- `Q_INVOKABLE QString buyBlockedReason()`.
- `prepare(aud, price, followMarket)`: when `followMarket` is set, re-price from
  the fresh book before `previewReady`.
- Book poll timer (15 s). A `marketUpdated()` signal after each book or
  balance update, which QML uses to refresh auto fields.
- Remove `recommend()`, `suggestAvailableSpend()`, `recommendedPrice` and
  `suggestedSpendReady`. Auto mode replaces them.
- `preview()` returns structured rows (`QVariantMap`) instead of one string.

QML (`Main.qml`):
- Reorder the form: amount first, then price, each with a mode hint or reset
  link, then the live summary, then Buy with its blocked reason.
- Rebuild the confirmation dialog from the structured preview.
- Show update times beside Best ask and Available AUD.

Tests: `currentPrice` rounding and coverage, `quoteFor` (valid, invalid input,
over balance, fee totals), and `wholeCents` (already exists).

Docs: update the README usage section.
