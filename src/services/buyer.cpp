#include "services/buyer.h"
#include "services/credentialstore.h"
#include "services/orderstore.h"
#include "config.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <sodium.h>
#include <cmath>

namespace pb {
Buyer::Buyer(QObject *parent) : QObject(parent) {
  m_hasSavedCredentials = CredentialStore::exists(CredentialStore::defaultPath());
  m_orderPoll.setInterval(15000);
  connect(&m_orderPoll, &QTimer::timeout, this, [this] {
    refreshOrderStatus();
    refreshBalance();
  });
  m_bookPoll.setInterval(15000);
  connect(&m_bookPoll, &QTimer::timeout, this, [this] {
    if (!m_busy && !m_previewLoading) fetchBook();
  });
  m_bookPoll.start();
}
Buyer::~Buyer() {
  if (!m_key.isEmpty()) sodium_memzero(m_key.data(), static_cast<size_t>(m_key.size()));
  if (!m_secret.isEmpty()) sodium_memzero(m_secret.data(), static_cast<size_t>(m_secret.size()));
  if (!m_readKey.isEmpty()) sodium_memzero(m_readKey.data(), static_cast<size_t>(m_readKey.size()));
  if (!m_readSecret.isEmpty()) sodium_memzero(m_readSecret.data(), static_cast<size_t>(m_readSecret.size()));
}
QString Buyer::version() const { return QStringLiteral(PLAINBUY_VERSION); }

bool Buyer::parseMoney(const QString &text, int maxDecimals, double *value) {
  QString cleaned = text.trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
  const QRegularExpression format(QStringLiteral("^[0-9]+(?:\\.[0-9]{1,%1})?$").arg(maxDecimals));
  if (!format.match(cleaned).hasMatch()) return false;
  bool ok = false;
  const double parsed = cleaned.toDouble(&ok);
  if (!ok || !std::isfinite(parsed) || parsed <= 0) return false;
  *value = parsed;
  return true;
}

double Buyer::coinAmount(double audBudget, double maxPrice, double feeRate) {
  if (!std::isfinite(audBudget) || !std::isfinite(maxPrice) || !std::isfinite(feeRate) ||
      audBudget <= 0 || maxPrice <= 0 || feeRate < 0)
    return 0;
  // Calculate satoshis with extra precision. The final check prevents a
  // floating-point round-up from taking the trade value plus the fee
  // allowance across the user's AUD ceiling.
  const long double perBtc = static_cast<long double>(maxPrice) * (1.0L + feeRate);
  const long double raw = static_cast<long double>(audBudget) / perBtc * 100000000.0L;
  const long double nearest = std::round(raw);
  auto satoshis = std::floor(std::abs(raw - nearest) < 0.0000001L ? nearest : raw);
  while (satoshis > 0 && satoshis * perBtc / 100000000.0L > static_cast<long double>(audBudget))
    --satoshis;
  return static_cast<double>(satoshis / 100000000.0L);
}

double Buyer::wholeCents(double aud) {
  if (!std::isfinite(aud) || aud <= 0) return 0;
  return static_cast<double>(std::floor(static_cast<long double>(aud) * 100.0L + 0.0000001L) / 100.0L);
}

double Buyer::currentPrice(const QVector<AskLevel> &asks, double audBudget) {
  const double cap = recommendedCap(asks, audBudget);
  if (cap <= 0) return 0;
  // Round up to the cent so the order still reaches the covering ask level.
  return static_cast<double>(std::ceil(static_cast<long double>(cap) * 100.0L - 0.0000001L) / 100.0L);
}

QVariantMap Buyer::quoteFor(const QString &aud, const QString &price, double availableAud, double bestAsk) {
  QVariantMap quote{{QStringLiteral("valid"), false}};
  double budget = 0, maxPrice = 0;
  const QString amountText = aud.trimmed();
  if (amountText.isEmpty()) {
    quote.insert(QStringLiteral("error"), QStringLiteral("Enter an amount in AUD."));
    return quote;
  }
  if (!parseMoney(amountText, 2, &budget)) {
    bool numeric = false;
    const double value = QString(amountText).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&numeric);
    quote.insert(QStringLiteral("error"), numeric && value == 0
        ? QStringLiteral("No AUD to spend.")
        : QStringLiteral("Enter an AUD amount with up to two decimals."));
    return quote;
  }
  if (price.trimmed().isEmpty()) {
    quote.insert(QStringLiteral("error"), QStringLiteral("No current price for this amount yet."));
    return quote;
  }
  if (!parseMoney(price, 8, &maxPrice)) {
    quote.insert(QStringLiteral("error"), QStringLiteral("Enter a positive price per BTC in AUD."));
    return quote;
  }
  const double btc = coinAmount(budget, maxPrice, kMarketsFeeRate);
  if (btc < 0.00000001) {
    quote.insert(QStringLiteral("error"), QStringLiteral("The amount is too small for this price."));
    return quote;
  }
  const double tradeValue = btc * maxPrice;
  const double fee = tradeValue * kMarketsFeeRate;
  quote.insert(QStringLiteral("btc"), btc);
  quote.insert(QStringLiteral("tradeValue"), tradeValue);
  quote.insert(QStringLiteral("fee"), fee);
  quote.insert(QStringLiteral("total"), tradeValue + fee);
  quote.insert(QStringLiteral("askKnown"), bestAsk > 0);
  quote.insert(QStringLiteral("reachesAsk"), bestAsk > 0 && maxPrice >= bestAsk);
  if (availableAud >= 0 && budget > availableAud + 0.000001) {
    quote.insert(QStringLiteral("error"), QStringLiteral("More than your available A$%1.")
                                              .arg(QString::number(wholeCents(availableAud), 'f', 2)));
    return quote;
  }
  quote.insert(QStringLiteral("valid"), true);
  return quote;
}

double Buyer::recommendedCap(QVector<AskLevel> asks, double audBudget) {
  if (!std::isfinite(audBudget) || audBudget <= 0) return 0;
  std::sort(asks.begin(), asks.end(), [](const AskLevel &a, const AskLevel &b) {
    return a.rate < b.rate;
  });
  long double cumulativeBtc = 0;
  for (const auto &level : asks) {
    if (!std::isfinite(level.rate) || !std::isfinite(level.amount) ||
        level.rate <= 0 || level.amount <= 0) continue;
    cumulativeBtc += level.amount;
    const double needed = coinAmount(audBudget, level.rate, kMarketsFeeRate);
    if (needed > 0 && cumulativeBtc >= static_cast<long double>(needed)) return level.rate;
  }
  return 0;
}

void Buyer::setStatus(const QString &value) { m_status = value; emit changed(); }

bool Buyer::apiStatusOkay(const QJsonObject &response) {
  return response.value(QStringLiteral("status")).toString() == QLatin1String("ok");
}

void Buyer::validateFullKey() {
  const quint64 serial = ++m_fullValidationSerial;
  m_fullKeyVerified = false;
  m_fullKeyStatus = connected() ? QStringLiteral("Checking…") : QStringLiteral("Not entered");
  emit changed();
  if (!connected()) return;
  auto *reply = postPrivate(QStringLiteral("status"), {}, false);
  connect(reply, &QNetworkReply::finished, this, [this, reply, serial] {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    if (serial == m_fullValidationSerial) {
      m_fullKeyVerified = reply->error() == QNetworkReply::NoError && apiStatusOkay(response);
      const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      m_fullKeyStatus = m_fullKeyVerified ? QStringLiteral("Verified")
          : response.value(QStringLiteral("status")).toString() == QLatin1String("error") ||
            httpStatus == 401 || httpStatus == 403
              ? QStringLiteral("Rejected by CoinSpot")
              : QStringLiteral("Could not verify; retry");
      emit changed();
    }
    reply->deleteLater();
  });
}

void Buyer::validateReadKey() {
  const quint64 serial = ++m_readValidationSerial;
  m_readKeyVerified = false;
  m_orderPoll.stop();
  m_readKeyStatus = readOnlyReady() ? QStringLiteral("Checking…") : QStringLiteral("Not entered");
  emit changed();
  if (!readOnlyReady()) return;
  auto *reply = postPrivate(QStringLiteral("ro/status"), {}, true);
  connect(reply, &QNetworkReply::finished, this, [this, reply, serial] {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    if (serial == m_readValidationSerial) {
      m_readKeyVerified = reply->error() == QNetworkReply::NoError && apiStatusOkay(response);
      const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      m_readKeyStatus = m_readKeyVerified ? QStringLiteral("Verified")
          : response.value(QStringLiteral("status")).toString() == QLatin1String("error") ||
            httpStatus == 401 || httpStatus == 403
              ? QStringLiteral("Rejected by CoinSpot")
              : QStringLiteral("Could not verify; retry");
      emit changed();
      if (m_readKeyVerified) {
        m_orderPoll.start();
        refreshBalance();
        refreshOrderStatus();
      }
    }
    reply->deleteLater();
  });
}

void Buyer::validateKeys() {
  if (m_busy || m_previewLoading) return;
  validateFullKey();
  validateReadKey();
}

void Buyer::loadOrders() {
  m_orderPath.clear();
  m_orders = {};
  m_openOrders = {};
  m_ordersLoaded = false;
  m_journalProblem = false;
  m_journalUnsaved = false;
  m_uncertainBuy = false;
  m_uncertainRefreshed = false;
  m_reviewRequested = false;
  m_lastOrderId.clear(); m_lastOrderDate.clear(); m_orderState.clear();
  m_lastOrderAmount = 0;
  if (!connected()) { emit changed(); return; }
  m_orderPath = OrderStore::pathForKey(m_key);
  bool journalOkay = false;
  m_orders = OrderStore::load(m_orderPath, &journalOkay);
  if (!journalOkay) m_journalProblem = true;
  for (const auto &entry : m_orders)
    if (entry.toObject().value(QStringLiteral("pending")).toBool()) m_uncertainBuy = true;
  if (!m_orders.isEmpty()) {
    const QJsonObject latest = m_orders.last().toObject();
    m_lastOrderId = latest.value(QStringLiteral("id")).toString();
    m_lastOrderDate = latest.value(QStringLiteral("date")).toString();
    m_lastOrderAmount = latest.value(QStringLiteral("amount")).toDouble();
    m_orderState = latest.value(QStringLiteral("state")).toString();
  }
  emit changed();
}

void Buyer::updateOrder(const QString &id, const QString &state, const QJsonObject &fills) {
  bool modified = false;
  for (qsizetype i = 0; i < m_orders.size(); ++i) {
    QJsonObject order = m_orders.at(i).toObject();
    if (order.value(QStringLiteral("id")).toString() != id) continue;
    if (order.value(QStringLiteral("state")).toString() != state) {
      order.insert(QStringLiteral("state"), state);
      modified = true;
    }
    for (auto it = fills.begin(); it != fills.end(); ++it) {
      if (order.value(it.key()) != it.value()) {
        order.insert(it.key(), it.value());
        modified = true;
      }
    }
    if (modified) m_orders.replace(i, order);
  }
  if (id == m_lastOrderId) m_orderState = state;
  if ((modified || m_journalUnsaved) && !saveOrders())
    setStatus(QStringLiteral("Could not save local order history. Check CoinSpot for the authoritative status."));
  emit changed();
}

bool Buyer::saveOrders() {
  m_journalUnsaved = m_orderPath.isEmpty() || !OrderStore::save(m_orderPath, m_orders);
  return !m_journalUnsaved;
}

void Buyer::retrySaveJournal() {
  if (!m_journalUnsaved) return;
  if (saveOrders()) setStatus(QStringLiteral("Local order history saved. Buying is available again."));
  else setStatus(QStringLiteral("Could not save local order history. Buying remains blocked; check that %1 is writable.").arg(m_orderPath));
}

void Buyer::removeAttempt(const QString &attemptId) {
  for (qsizetype i = 0; i < m_orders.size(); ++i) {
    if (m_orders.at(i).toObject().value(QStringLiteral("attemptId")).toString() != attemptId) continue;
    m_orders.removeAt(i);
    if (!m_orderPath.isEmpty() && !OrderStore::save(m_orderPath, m_orders))
      setStatus(QStringLiteral("Could not update local order history. Check CoinSpot before another buy."));
    emit changed();
    return;
  }
}

void Buyer::setCredentials(const QString &key, const QString &secret) {
  if (m_busy || m_previewLoading) return;
  m_orderPoll.stop();
  m_lastOrderId.clear(); m_lastOrderDate.clear(); m_orderState.clear();
  if (!m_key.isEmpty()) sodium_memzero(m_key.data(), static_cast<size_t>(m_key.size()));
  if (!m_secret.isEmpty()) sodium_memzero(m_secret.data(), static_cast<size_t>(m_secret.size()));
  m_key = key.trimmed().toUtf8();
  m_secret = secret.trimmed().toUtf8();
  loadOrders();
  setStatus(connected() ? QStringLiteral("Checking Full Access API key…")
                        : QStringLiteral("Enter both API key and secret"));
  validateFullKey();
}

void Buyer::setReadOnlyCredentials(const QString &key, const QString &secret) {
  if (m_busy || m_previewLoading) return;
  m_orderPoll.stop();
  m_openOrders = {};
  m_ordersLoaded = false;
  m_uncertainRefreshed = false; m_reviewRequested = false;
  m_availableAudValue = -1; m_balanceAt = {};
  if (!m_readKey.isEmpty()) sodium_memzero(m_readKey.data(), static_cast<size_t>(m_readKey.size()));
  if (!m_readSecret.isEmpty()) sodium_memzero(m_readSecret.data(), static_cast<size_t>(m_readSecret.size()));
  m_readKey = key.trimmed().toUtf8();
  m_readSecret = secret.trimmed().toUtf8();
  setStatus(readOnlyReady() ? QStringLiteral("Checking Read Only API key…")
                            : QStringLiteral("Enter both read-only API key and secret"));
  validateReadKey();
}

void Buyer::clearCredentials() {
  if (m_busy || m_previewLoading) return;
  if (!m_key.isEmpty()) sodium_memzero(m_key.data(), static_cast<size_t>(m_key.size()));
  if (!m_secret.isEmpty()) sodium_memzero(m_secret.data(), static_cast<size_t>(m_secret.size()));
  if (!m_readKey.isEmpty()) sodium_memzero(m_readKey.data(), static_cast<size_t>(m_readKey.size()));
  if (!m_readSecret.isEmpty()) sodium_memzero(m_readSecret.data(), static_cast<size_t>(m_readSecret.size()));
  m_key.clear(); m_secret.clear();
  m_readKey.clear(); m_readSecret.clear();
  ++m_fullValidationSerial; ++m_readValidationSerial;
  m_fullKeyVerified = false; m_readKeyVerified = false;
  m_fullKeyStatus = QStringLiteral("Not entered");
  m_readKeyStatus = QStringLiteral("Not entered");
  m_availableAudValue = -1; m_balanceAt = {};
  m_orderPoll.stop();
  m_lastOrderId.clear(); m_lastOrderDate.clear(); m_orderState.clear();
  m_orders = {}; m_openOrders = {}; m_orderPath.clear();
  m_ordersLoaded = false;
  m_journalProblem = false; m_journalUnsaved = false;
  m_uncertainBuy = false; m_uncertainRefreshed = false; m_reviewRequested = false;
  setStatus(QStringLiteral("API key cleared"));
}

QString Buyer::saveCredentials(const QString &passphrase, const QString &confirmation) {
  if (m_busy || !connected() || !readOnlyReady())
    return QStringLiteral("Enter both Full Access and Read Only API keys before saving.");
  // Saving replaces any existing file, so only keys CoinSpot accepted may do that.
  if (!keysVerified())
    return QStringLiteral("CoinSpot has not verified both API keys yet. The saved file was not changed.");
  if (passphrase.size() < 12) return QStringLiteral("Use a passphrase of at least 12 characters.");
  if (passphrase != confirmation) return QStringLiteral("The passphrases do not match.");
  QString error;
  if (!CredentialStore::save(CredentialStore::defaultPath(), passphrase,
                             {m_key, m_secret, m_readKey, m_readSecret}, &error))
    return error;
  m_hasSavedCredentials = true;
  setStatus(QStringLiteral("Credentials saved in the encrypted file. Use the passphrase to unlock next time."));
  return {};
}

QString Buyer::unlockCredentials(const QString &passphrase) {
  if (m_busy || m_previewLoading) return QStringLiteral("PlainBuy is busy. Try again in a moment.");
  Credentials credentials;
  QString error;
  if (!CredentialStore::load(CredentialStore::defaultPath(), passphrase, &credentials, &error))
    return error;
  if (!m_key.isEmpty()) sodium_memzero(m_key.data(), static_cast<size_t>(m_key.size()));
  if (!m_secret.isEmpty()) sodium_memzero(m_secret.data(), static_cast<size_t>(m_secret.size()));
  if (!m_readKey.isEmpty()) sodium_memzero(m_readKey.data(), static_cast<size_t>(m_readKey.size()));
  if (!m_readSecret.isEmpty()) sodium_memzero(m_readSecret.data(), static_cast<size_t>(m_readSecret.size()));
  m_key = credentials.key;
  m_secret = credentials.secret;
  m_readKey = credentials.readKey;
  m_readSecret = credentials.readSecret;
  m_hasSavedCredentials = true;
  m_orderPoll.stop();
  loadOrders();
  // The key check progress and any problem show under Buy, so no status is needed.
  setStatus({});
  validateKeys();
  return {};
}

void Buyer::forgetSavedCredentials() {
  if (m_busy) return;
  QString error;
  if (!CredentialStore::remove(CredentialStore::defaultPath(), &error)) {
    setStatus(error);
    return;
  }
  m_hasSavedCredentials = false;
  setStatus(QStringLiteral("Encrypted credential file removed; current session key remains active"));
}

void Buyer::refreshBestAsk() { fetchBook(); }

bool Buyer::bookFresh() const {
  return m_bookAt.isValid() && m_bookAt.secsTo(QDateTime::currentDateTimeUtc()) <= 60;
}

QString Buyer::spendableAud() const {
  return m_availableAudValue >= 0 ? QString::number(wholeCents(m_availableAudValue), 'f', 2) : QString();
}

QString Buyer::marketPriceFor(const QString &aud) const {
  double budget = 0;
  if (!bookFresh() || !parseMoney(aud, 2, &budget)) return {};
  const double price = currentPrice(m_askLevels, budget);
  return price > 0 ? QString::number(price, 'f', 2) : QString();
}

QVariantMap Buyer::quote(const QString &aud, const QString &price) const {
  return quoteFor(aud, price, m_availableAudValue, bookFresh() ? m_bestAskValue : 0);
}

QString Buyer::buyBlockedReason() const {
  if (!connected() || !readOnlyReady())
    return m_hasSavedCredentials && !connected() ? QStringLiteral("Unlock your saved API keys to buy.")
                                                 : QStringLiteral("Add your CoinSpot API keys to buy.");
  if (m_fullKeyStatus == QLatin1String("Rejected by CoinSpot") || m_readKeyStatus == QLatin1String("Rejected by CoinSpot"))
    return QStringLiteral("CoinSpot rejected an API key. Use Account → Enter API keys to replace them.");
  if (m_fullKeyStatus == QLatin1String("Could not verify; retry") || m_readKeyStatus == QLatin1String("Could not verify; retry"))
    return QStringLiteral("Could not reach CoinSpot to verify the API keys. Use Account → Retry API checks.");
  if (!keysVerified()) return QStringLiteral("Waiting for CoinSpot to verify both API keys.");
  if (m_journalProblem) return QStringLiteral("Buying is blocked: the local order journal is unreadable.");
  if (m_journalUnsaved) return QStringLiteral("Buying is blocked until the latest order update is saved.");
  if (m_uncertainBuy) return QStringLiteral("Buying is blocked until you review the earlier buy below.");
  return {};
}

void Buyer::fetchBook(bool forPreview) {
  if (m_previewLoading && !forPreview) return;
  const quint64 serial = ++m_bookRequestSerial;
  QNetworkRequest request(QUrl(QStringLiteral("https://www.coinspot.com.au/pubapi/v2/orders/open/BTC")));
  request.setTransferTimeout(10000);
  auto *reply = m_network.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, serial, forPreview] {
    if (serial != m_bookRequestSerial) {
      if (forPreview) { m_previewLoading = false; emit changed(); }
      reply->deleteLater();
      return;
    }
    const auto data = reply->readAll();
    const auto doc = QJsonDocument::fromJson(data);
    const auto asks = doc.object().value(QStringLiteral("sellorders")).toArray();
    const auto bids = doc.object().value(QStringLiteral("buyorders")).toArray();
    if (reply->error() == QNetworkReply::NoError &&
        doc.object().value(QStringLiteral("status")).toString() == QLatin1String("ok") &&
        !asks.isEmpty()) {
      double lowest = INFINITY;
      double highestBid = 0;
      QVector<AskLevel> levels;
      for (const auto &entry : asks) {
        const auto object = entry.toObject();
        const double rate = object.value(QStringLiteral("rate")).toDouble();
        const double amount = object.value(QStringLiteral("amount")).toDouble();
        if (std::isfinite(rate) && std::isfinite(amount) && rate > 0 && amount > 0) {
          lowest = std::min(lowest, rate);
          levels.push_back({rate, amount});
        }
      }
      for (const auto &entry : bids) {
        const double rate = entry.toObject().value(QStringLiteral("rate")).toDouble();
        if (std::isfinite(rate) && rate > highestBid) highestBid = rate;
      }
      m_askLevels = levels;
      m_bestAsk = std::isfinite(lowest) ? QString::number(lowest, 'f', 2) : QString();
      m_bestAskValue = std::isfinite(lowest) ? lowest : 0;
      m_bookAt = QDateTime::currentDateTimeUtc();
      m_marketSpread = highestBid > 0 && std::isfinite(lowest)
                           ? QString::number((lowest / highestBid - 1.0) * 100.0, 'f', 2) + QLatin1Char('%')
                           : QString();
    } else {
      // A stale book must not drive an automatic price.
      m_askLevels.clear();
      m_bestAsk.clear();
      m_bestAskValue = 0;
      m_bookAt = {};
      m_marketSpread.clear();
    }
    emit changed();
    emit marketUpdated();
    if (forPreview) finishPreview();
    reply->deleteLater();
  });
}

void Buyer::finishPreview() {
  m_previewLoading = false;
  if (m_followMarket) {
    m_maxPrice = currentPrice(m_askLevels, m_budget);
    m_amount = m_maxPrice > 0 ? coinAmount(m_budget, m_maxPrice, kMarketsFeeRate) : 0;
    if (m_maxPrice <= 0) {
      setStatus(m_askLevels.isEmpty()
                    ? QStringLiteral("Could not load the CoinSpot order book. No order was prepared.")
                    : QStringLiteral("CoinSpot's visible sell orders do not cover this amount. No order was prepared."));
      return;
    }
    if (m_amount < 0.00000001) {
      setStatus(QStringLiteral("The amount is too small for the current price."));
      return;
    }
  }
  setStatus(bookFresh() ? QStringLiteral("Current order book loaded; review the confirmation.")
                        : QStringLiteral("Could not refresh the order book; the confirmation will flag the missing quote."));
  emit previewReady();
}

bool Buyer::prepare(const QString &aud, const QString &maxPrice, bool followMarket) {
  if (m_busy || m_previewLoading) { setStatus(QStringLiteral("An order or price check is already in progress")); return false; }
  if (m_journalProblem) { setStatus(QStringLiteral("The local order journal is unreadable. Buying is blocked; check the journal warning below.")); return false; }
  if (m_journalUnsaved) { setStatus(QStringLiteral("The latest order update is not saved in local history yet. Save it before placing another order.")); return false; }
  if (m_uncertainBuy) { setStatus(QStringLiteral("Review and acknowledge the uncertain buy request before placing another order.")); return false; }
  if (!connected()) { setStatus(QStringLiteral("Add your CoinSpot API key first")); return false; }
  if (!readOnlyReady()) { setStatus(QStringLiteral("Add a read-only API key for the balance check first")); return false; }
  if (!keysVerified()) { setStatus(QStringLiteral("Verify both CoinSpot API keys before buying. Use Account → Retry API checks if needed.")); return false; }
  if (!parseMoney(aud, 2, &m_budget)) { setStatus(QStringLiteral("Enter an AUD amount with up to two decimals")); return false; }
  m_followMarket = followMarket;
  m_amount = 0;
  if (!followMarket) {
    if (!parseMoney(maxPrice, 8, &m_maxPrice)) { setStatus(QStringLiteral("Enter a positive maximum BTC price in AUD")); return false; }
    m_amount = coinAmount(m_budget, m_maxPrice, kMarketsFeeRate);
    if (m_amount < 0.00000001) { setStatus(QStringLiteral("The amount is too small for this maximum price")); return false; }
  }
  m_previewLoading = true;
  setStatus(QStringLiteral("Getting the current BTC/AUD price for confirmation…"));
  fetchBook(true);
  return true;
}

QVariantMap Buyer::preview() const {
  const double tradeValue = m_amount * m_maxPrice;
  const bool askKnown = m_bestAskValue > 0 && m_bookAt.isValid() &&
                        m_bookAt.secsTo(QDateTime::currentDateTimeUtc()) <= 30;
  const QString marketContext = askKnown
      ? (m_maxPrice >= m_bestAskValue
          ? QStringLiteral("The ask at %1 was A$%2. Your price reaches it, so this should fill straight away.")
          : QStringLiteral("The ask at %1 was A$%2. Your price is below it, so the order may stay open."))
            .arg(m_bookAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")), QString::number(m_bestAskValue, 'f', 2))
      : QStringLiteral("The order book request failed or its quote is stale; immediate fill is unknown.");
  return {{QStringLiteral("btc"), m_amount},
          {QStringLiteral("maxPrice"), m_maxPrice},
          {QStringLiteral("tradeValue"), tradeValue},
          {QStringLiteral("fee"), tradeValue * kMarketsFeeRate},
          {QStringLiteral("total"), tradeValue * (1.0 + kMarketsFeeRate)},
          {QStringLiteral("limit"), m_budget},
          {QStringLiteral("marketContext"), marketContext}};
}

QNetworkReply *Buyer::postPrivate(const QString &endpoint, const QJsonObject &fields, bool readOnly) {
  qint64 &nonce = readOnly ? m_readNonce : m_nonce;
  nonce = std::max(nonce + 1, QDateTime::currentMSecsSinceEpoch());
  QJsonObject payload = fields;
  payload.insert(QStringLiteral("nonce"), static_cast<double>(nonce));
  const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  const QByteArray &key = readOnly ? m_readKey : m_key;
  const QByteArray &secret = readOnly ? m_readSecret : m_secret;
  const QByteArray signature = QMessageAuthenticationCode::hash(body, secret, QCryptographicHash::Sha512).toHex();
  QNetworkRequest request(QUrl(QStringLiteral("https://www.coinspot.com.au/api/v2/") + endpoint));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader("key", key);
  request.setRawHeader("sign", signature);
  request.setTransferTimeout(15000);
  return m_network.post(request, body);
}

void Buyer::refreshBalance(bool force) {
  if (!m_readKeyVerified || m_busy || (m_balanceLoading && !force)) return;
  m_balanceLoading = true;
  // Only the newest balance request may update the value, so a reply sent
  // before a purchase cannot overwrite the balance after it.
  const quint64 serial = ++m_balanceSerial;
  const QByteArray readKey = m_readKey;
  auto *reply = postPrivate(QStringLiteral("ro/my/balance/AUD?available=yes"), {}, true);
  connect(reply, &QNetworkReply::finished, this, [this, reply, readKey, serial] {
    if (serial != m_balanceSerial) { reply->deleteLater(); return; }
    m_balanceLoading = false;
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    if (readKey != m_readKey || !m_readKeyVerified || m_busy) { reply->deleteLater(); return; }
    const QJsonValue available = response.value(QStringLiteral("balance"))
        .toObject().value(QStringLiteral("AUD")).toObject().value(QStringLiteral("available"));
    const bool okay = reply->error() == QNetworkReply::NoError &&
                      response.value(QStringLiteral("status")).toString() == QLatin1String("ok") &&
                      available.isDouble() && std::isfinite(available.toDouble()) && available.toDouble() >= 0;
    m_availableAudValue = okay ? available.toDouble() : -1;
    m_balanceAt = okay ? QDateTime::currentDateTimeUtc() : QDateTime();
    emit changed();
    emit marketUpdated();
    reply->deleteLater();
  });
}

void Buyer::submit() {
  if (m_busy || m_previewLoading || m_journalProblem || m_journalUnsaved || m_uncertainBuy || !keysVerified() || m_amount <= 0) return;
  m_busy = true;
  ++m_balanceSerial;  // The pre-submit check supersedes any balance request in flight.
  m_balanceLoading = false;
  setStatus(QStringLiteral("Checking available AUD…"));
  auto *reply = postPrivate(QStringLiteral("ro/my/balance/AUD?available=yes"), {}, true);
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonValue available = response.value(QStringLiteral("balance"))
        .toObject().value(QStringLiteral("AUD")).toObject().value(QStringLiteral("available"));
    if (reply->error() != QNetworkReply::NoError ||
        response.value(QStringLiteral("status")).toString() != QLatin1String("ok") ||
        !available.isDouble() || !std::isfinite(available.toDouble()) || available.toDouble() < 0) {
      m_busy = false;
      setStatus(QStringLiteral("Could not verify available AUD. No order was submitted."));
    } else {
      const double funds = available.toDouble();
      m_availableAudValue = funds;
      m_balanceAt = QDateTime::currentDateTimeUtc();
      const double required = m_amount * m_maxPrice * (1.0 + kMarketsFeeRate);
      if (!std::isfinite(funds) || funds + 0.000001 < required) {
        m_busy = false;
        setStatus(QStringLiteral("Insufficient available AUD: A$%1 needed, A$%2 available. No order was submitted.")
                      .arg(QString::number(required, 'f', 2), availableAud()));
      } else {
        placePreparedOrder();
      }
    }
    reply->deleteLater();
  });
}

void Buyer::placePreparedOrder() {
  const QString attemptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString attemptDate = QDateTime::currentDateTimeUtc().date().toString(Qt::ISODate);
  m_orders.append(QJsonObject{{QStringLiteral("attemptId"), attemptId},
                              {QStringLiteral("date"), attemptDate},
                              {QStringLiteral("amount"), m_amount},
                              {QStringLiteral("rate"), m_maxPrice},
                              {QStringLiteral("state"), QStringLiteral("Buy request in progress; outcome unknown until confirmed.")},
                              {QStringLiteral("pending"), true}});
  if (m_orderPath.isEmpty() || !OrderStore::save(m_orderPath, m_orders)) {
    m_orders.removeLast();
    m_busy = false;
    m_amount = 0;
    setStatus(QStringLiteral("Could not record the buy attempt locally. No order was submitted."));
    return;
  }
  m_uncertainBuy = true;
  m_uncertainRefreshed = false;
  setStatus(QStringLiteral("Submitting order…"));
  const QJsonObject payload{{QStringLiteral("cointype"), QStringLiteral("BTC")},
                            {QStringLiteral("markettype"), QStringLiteral("AUD")},
                            {QStringLiteral("amount"), m_amount},
                            {QStringLiteral("rate"), m_maxPrice}};
  auto *reply = postPrivate(QStringLiteral("my/buy"), payload, false);
  connect(reply, &QNetworkReply::finished, this, [this, reply, attemptId, attemptDate] {
    const QByteArray data = reply->readAll();
    const QJsonObject response = QJsonDocument::fromJson(data).object();
    m_busy = false;
    if (reply->error() == QNetworkReply::NoError &&
        response.value(QStringLiteral("status")).toString() == QLatin1String("ok") &&
        !response.value(QStringLiteral("id")).toVariant().toString().isEmpty()) {
      m_lastOrderId = response.value(QStringLiteral("id")).toVariant().toString();
      m_lastOrderDate = attemptDate;
      m_lastOrderAmount = m_amount;
      m_amount = 0;
      m_orderState = QStringLiteral("Submitted; checking fills…");
      for (qsizetype i = 0; i < m_orders.size(); ++i) {
        QJsonObject order = m_orders.at(i).toObject();
        if (order.value(QStringLiteral("attemptId")).toString() != attemptId) continue;
        order.insert(QStringLiteral("id"), m_lastOrderId);
        order.insert(QStringLiteral("state"), m_orderState);
        order.remove(QStringLiteral("pending"));
        m_orders.replace(i, order);
        break;
      }
      m_uncertainBuy = false;
      if (!saveOrders())
        setStatus(QStringLiteral("Order accepted by CoinSpot: %1. Could not save it to local history, so buying stays blocked until it is saved.").arg(m_lastOrderId));
      else
        setStatus(QStringLiteral("Order accepted by CoinSpot: %1").arg(m_lastOrderId));
      m_orderPoll.start();
      refreshOrderStatus();
      refreshBalance(true);
    } else if (reply->error() == QNetworkReply::NoError &&
               response.value(QStringLiteral("status")).toString() == QLatin1String("error")) {
      m_amount = 0;
      removeAttempt(attemptId);
      m_uncertainBuy = false;
      setStatus(QStringLiteral("CoinSpot rejected the order: %1").arg(response.value(QStringLiteral("message")).toString(QStringLiteral("Unknown reason"))));
    } else {
      m_amount = 0;
      for (qsizetype i = 0; i < m_orders.size(); ++i) {
        QJsonObject order = m_orders.at(i).toObject();
        if (order.value(QStringLiteral("attemptId")).toString() != attemptId) continue;
        order.insert(QStringLiteral("state"), QStringLiteral("Outcome unknown; may have placed an order."));
        m_orders.replace(i, order);
        break;
      }
      saveOrders();
      setStatus(QStringLiteral("Buy request outcome unknown. Use Refresh to review below and check CoinSpot before another buy."));
    }
    reply->deleteLater();
  });
}

QString Buyer::describeOrder(const QJsonArray &openOrders, const QJsonArray &completedOrders,
                             const QString &orderId, double requestedAmount) {
  bool open = false;
  double filled = 0;
  for (const auto &entry : openOrders)
    if (entry.toObject().value(QStringLiteral("id")).toString() == orderId) open = true;
  for (const auto &entry : completedOrders) {
    const QJsonObject object = entry.toObject();
    if (object.value(QStringLiteral("id")).toString() == orderId)
      filled += object.value(QStringLiteral("amount")).toDouble();
  }
  if (filled > 0 && filled + 0.000000005 >= requestedAmount)
    return QStringLiteral("Filled: %1 BTC recorded in trade history.").arg(QString::number(filled, 'f', 8));
  if (open && filled > 0)
    return QStringLiteral("Partly filled: %1 BTC recorded; remainder open.").arg(QString::number(filled, 'f', 8));
  if (open) return QStringLiteral("Open: waiting for a seller at your price or lower.");
  if (filled > 0)
    return QStringLiteral("Partly filled: %1 BTC recorded; remainder no longer open.").arg(QString::number(filled, 'f', 8));
  return QStringLiteral("No open order or matching fill found. Check CoinSpot for cancellation or delayed history.");
}

bool Buyer::keepsRecordedFill(const QJsonObject &order, const QJsonObject &fills) {
  // Completed-order history is capped at 500 records, so an old fill can drop
  // out of the response. Never let a smaller result replace a recorded fill.
  const double recorded = order.value(QStringLiteral("filledBtc")).toDouble();
  return recorded > 0 && fills.value(QStringLiteral("filledBtc")).toDouble() + 0.000000005 < recorded;
}

QJsonObject Buyer::fillSummary(const QJsonArray &completedOrders, const QString &orderId) {
  double btc = 0, weightedRate = 0, totalAud = 0, feeAud = 0;
  bool hasTotal = true, hasFee = true;
  for (const auto &entry : completedOrders) {
    const QJsonObject fill = entry.toObject();
    if (fill.value(QStringLiteral("id")).toString() != orderId) continue;
    const double amount = fill.value(QStringLiteral("amount")).toDouble();
    const double rate = fill.value(QStringLiteral("rate")).toDouble();
    if (!std::isfinite(amount) || !std::isfinite(rate) || amount <= 0 || rate <= 0) continue;
    btc += amount;
    weightedRate += amount * rate;
    const QJsonValue total = fill.value(QStringLiteral("total"));
    if (total.isDouble() && std::isfinite(total.toDouble()) && total.toDouble() >= 0)
      totalAud += total.toDouble();
    else hasTotal = false;
    const QJsonValue fee = fill.value(QStringLiteral("audfeeExGst"));
    const QJsonValue gst = fill.value(QStringLiteral("audGst"));
    if (fee.isDouble() && gst.isDouble() && std::isfinite(fee.toDouble()) &&
        std::isfinite(gst.toDouble()) && fee.toDouble() >= 0 && gst.toDouble() >= 0)
      feeAud += fee.toDouble() + gst.toDouble();
    else hasFee = false;
  }
  if (btc <= 0) return {};
  QJsonObject result{{QStringLiteral("filledBtc"), btc},
                     {QStringLiteral("averageFillPrice"), weightedRate / btc}};
  if (hasTotal) result.insert(QStringLiteral("reportedTradeTotalAud"), totalAud);
  if (hasFee) result.insert(QStringLiteral("reportedFeeAud"), feeAud);
  return result;
}

void Buyer::reviewUncertainBuy() {
  if (!m_uncertainBuy || !m_readKeyVerified || m_busy) return;
  if (m_statusBusy) {
    setStatus(QStringLiteral("An order refresh is already running. Try again when it finishes."));
    return;
  }
  m_uncertainRefreshed = false;
  m_reviewRequested = true;
  setStatus(QStringLiteral("Refreshing CoinSpot orders for review…"));
  refreshOrderStatus();
}

void Buyer::acknowledgeUncertainBuy() {
  if (!m_uncertainBuy || !m_uncertainRefreshed || m_busy || m_orderPath.isEmpty()) return;
  QJsonArray reviewed = m_orders;
  for (qsizetype i = 0; i < reviewed.size(); ++i) {
    QJsonObject order = reviewed.at(i).toObject();
    if (!order.value(QStringLiteral("pending")).toBool()) continue;
    order.remove(QStringLiteral("pending"));
    order.insert(QStringLiteral("state"), QStringLiteral("Reviewed; outcome remains unconfirmed."));
    reviewed.replace(i, order);
  }
  if (!OrderStore::save(m_orderPath, reviewed)) {
    setStatus(QStringLiteral("Could not save the acknowledgement. Another buy remains blocked."));
    return;
  }
  m_orders = reviewed;
  m_journalUnsaved = false;
  m_uncertainBuy = false;
  m_uncertainRefreshed = false;
  m_reviewRequested = false;
  setStatus(QStringLiteral("Unknown buy outcome acknowledged. Check CoinSpot again before submitting a similar order."));
}

void Buyer::refreshOrderStatus() {
  if (!m_readKeyVerified || m_statusBusy) return;
  m_statusBusy = true;
  const QJsonObject fields{{QStringLiteral("cointype"), QStringLiteral("BTC")},
                           {QStringLiteral("markettype"), QStringLiteral("AUD")}};
  const QByteArray readKey = m_readKey;
  auto *openReply = postPrivate(QStringLiteral("ro/my/orders/market/open"), fields, true);
  connect(openReply, &QNetworkReply::finished, this, [this, openReply, fields, readKey] {
    const QJsonObject openResponse = QJsonDocument::fromJson(openReply->readAll()).object();
    const bool openOkay = openReply->error() == QNetworkReply::NoError &&
                           openResponse.value(QStringLiteral("status")).toString() == QLatin1String("ok") &&
                           openResponse.value(QStringLiteral("buyorders")).isArray();
    openReply->deleteLater();
    if (readKey != m_readKey || !m_readKeyVerified) { m_statusBusy = false; return; }
    if (!openOkay) {
      m_statusBusy = false;
      m_reviewRequested = false;
      m_uncertainRefreshed = false;
      m_openOrders = {};
      m_ordersLoaded = false;
      m_orderState = QStringLiteral("Could not refresh order status. Check CoinSpot.");
      setStatus(QStringLiteral("Could not refresh open orders. Check CoinSpot before placing another buy."));
      return;
    }
    m_openOrders = openResponse.value(QStringLiteral("buyorders")).toArray();
    m_ordersLoaded = true;
    emit changed();
    if (m_orders.isEmpty()) { m_statusBusy = false; m_reviewRequested = false; return; }
    QJsonObject historyFields = fields;
    QString earliestDate;
    for (const auto &entry : m_orders) {
      const QJsonObject order = entry.toObject();
      // Fully filled orders cannot change, so they need not widen the history window.
      if (order.value(QStringLiteral("filledBtc")).toDouble() + 0.000000005 >=
          order.value(QStringLiteral("amount")).toDouble() &&
          order.value(QStringLiteral("filledBtc")).toDouble() > 0) continue;
      const QString date = order.value(QStringLiteral("date")).toString();
      if (!date.isEmpty() && (earliestDate.isEmpty() || date < earliestDate)) earliestDate = date;
    }
    if (!earliestDate.isEmpty()) historyFields.insert(QStringLiteral("startdate"), earliestDate);
    historyFields.insert(QStringLiteral("limit"), 500);
    auto *historyReply = postPrivate(QStringLiteral("ro/my/orders/market/completed"), historyFields, true);
    connect(historyReply, &QNetworkReply::finished, this, [this, historyReply, readKey] {
      const QJsonObject history = QJsonDocument::fromJson(historyReply->readAll()).object();
      const bool historyOkay = historyReply->error() == QNetworkReply::NoError &&
                                history.value(QStringLiteral("status")).toString() == QLatin1String("ok") &&
                                history.value(QStringLiteral("buyorders")).isArray();
      m_statusBusy = false;
      if (readKey != m_readKey || !m_readKeyVerified) { historyReply->deleteLater(); return; }
      if (historyOkay) {
        const QJsonArray completed = history.value(QStringLiteral("buyorders")).toArray();
        const QJsonArray trackedOrders = m_orders;
        for (const auto &entry : trackedOrders) {
          const QJsonObject order = entry.toObject();
          const QString id = order.value(QStringLiteral("id")).toString();
          if (id.isEmpty()) continue;
          const QJsonObject fills = fillSummary(completed, id);
          if (keepsRecordedFill(order, fills)) continue;
          updateOrder(id, describeOrder(m_openOrders, completed, id,
                                        order.value(QStringLiteral("amount")).toDouble()),
                      fills);
        }
        if (m_reviewRequested && m_uncertainBuy) {
          m_uncertainRefreshed = true;
          setStatus(QStringLiteral("Order lists refreshed. Compare them with CoinSpot before acknowledging the unknown buy outcome."));
        }
      } else {
        m_uncertainRefreshed = false;
        m_orderState = QStringLiteral("Open-order check worked, but fill history is unavailable. Check CoinSpot.");
        if (m_reviewRequested)
          setStatus(QStringLiteral("Could not refresh completed orders. The unknown buy remains blocked."));
      }
      m_reviewRequested = false;
      emit changed();
      historyReply->deleteLater();
    });
  });
}

void Buyer::cancelOrder(const QString &id) {
  if (!keysVerified() || m_busy || id.isEmpty()) return;
  bool found = false;
  for (const auto &entry : m_openOrders)
    if (entry.toObject().value(QStringLiteral("id")).toString() == id) found = true;
  if (!found) { setStatus(QStringLiteral("Refresh open orders before cancelling; this order is not currently listed.")); return; }
  m_busy = true;
  setStatus(QStringLiteral("Requesting cancellation of order %1…").arg(id));
  auto *reply = postPrivate(QStringLiteral("my/buy/cancel"),
                            QJsonObject{{QStringLiteral("id"), id}}, false);
  connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    m_busy = false;
    if (reply->error() == QNetworkReply::NoError &&
        response.value(QStringLiteral("status")).toString() == QLatin1String("ok")) {
      updateOrder(id, QStringLiteral("Cancellation requested; awaiting confirmation."));
      setStatus(QStringLiteral("Cancellation requested for %1. CoinSpot may still fill it; verify its status.").arg(id));
    } else if (reply->error() != QNetworkReply::NoError) {
      setStatus(QStringLiteral("Cancellation outcome unknown for %1. Check CoinSpot.").arg(id));
    } else {
      setStatus(QStringLiteral("CoinSpot rejected cancellation: %1").arg(response.value(QStringLiteral("message")).toString()));
    }
    refreshOrderStatus();
    reply->deleteLater();
  });
}
} // namespace pb
