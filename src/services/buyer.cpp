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
  connect(&m_orderPoll, &QTimer::timeout, this, &Buyer::refreshOrderStatus);
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

double Buyer::coinAmount(double audBudget, double maxPrice) {
  if (!std::isfinite(audBudget) || !std::isfinite(maxPrice) || audBudget <= 0 || maxPrice <= 0)
    return 0;
  // Calculate satoshis with extra precision. The final check prevents a
  // floating-point round-up from crossing the user's AUD ceiling.
  const long double raw = static_cast<long double>(audBudget) /
                          static_cast<long double>(maxPrice) * 100000000.0L;
  const long double nearest = std::round(raw);
  auto satoshis = std::floor(std::abs(raw - nearest) < 0.0000001L ? nearest : raw);
  while (satoshis > 0 && satoshis * static_cast<long double>(maxPrice) / 100000000.0L >
                              static_cast<long double>(audBudget))
    --satoshis;
  return static_cast<double>(satoshis / 100000000.0L);
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
    if (cumulativeBtc >= static_cast<long double>(coinAmount(audBudget, level.rate)) &&
        coinAmount(audBudget, level.rate) > 0)
      return level.rate;
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
  if (m_busy) return;
  validateFullKey();
  validateReadKey();
}

void Buyer::loadOrders() {
  m_orderPath.clear();
  m_orders = {};
  m_openOrders = {};
  m_ordersLoaded = false;
  m_uncertainBuy = false;
  m_uncertainRefreshed = false;
  m_reviewRequested = false;
  m_lastOrderId.clear(); m_lastOrderDate.clear(); m_orderState.clear();
  m_lastOrderAmount = 0;
  if (!connected()) { emit changed(); return; }
  m_orderPath = OrderStore::pathForKey(m_key);
  m_orders = OrderStore::load(m_orderPath);
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
  if (modified && !m_orderPath.isEmpty() && !OrderStore::save(m_orderPath, m_orders))
    setStatus(QStringLiteral("Could not save local order history. Check CoinSpot for the authoritative status."));
  emit changed();
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
  if (m_busy) return;
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
  if (m_busy) return;
  m_orderPoll.stop();
  m_openOrders = {};
  m_ordersLoaded = false;
  m_uncertainRefreshed = false; m_reviewRequested = false;
  m_availableAud.clear();
  if (!m_readKey.isEmpty()) sodium_memzero(m_readKey.data(), static_cast<size_t>(m_readKey.size()));
  if (!m_readSecret.isEmpty()) sodium_memzero(m_readSecret.data(), static_cast<size_t>(m_readSecret.size()));
  m_readKey = key.trimmed().toUtf8();
  m_readSecret = secret.trimmed().toUtf8();
  setStatus(readOnlyReady() ? QStringLiteral("Checking Read Only API key…")
                            : QStringLiteral("Enter both read-only API key and secret"));
  validateReadKey();
}

void Buyer::clearCredentials() {
  if (m_busy) return;
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
  m_availableAud.clear();
  m_orderPoll.stop();
  m_lastOrderId.clear(); m_lastOrderDate.clear(); m_orderState.clear();
  m_orders = {}; m_openOrders = {}; m_orderPath.clear();
  m_ordersLoaded = false;
  m_uncertainBuy = false; m_uncertainRefreshed = false; m_reviewRequested = false;
  setStatus(QStringLiteral("API key cleared"));
}

void Buyer::saveCredentials(const QString &passphrase, const QString &confirmation) {
  if (m_busy || !connected() || !readOnlyReady()) {
    setStatus(QStringLiteral("Enter both Full Access and Read Only API keys before saving."));
    return;
  }
  if (passphrase.size() < 12 || passphrase != confirmation) {
    setStatus(QStringLiteral("Use a matching passphrase of at least 12 characters."));
    return;
  }
  QString error;
  if (!CredentialStore::save(CredentialStore::defaultPath(), passphrase,
                             {m_key, m_secret, m_readKey, m_readSecret}, &error)) {
    setStatus(error);
    return;
  }
  m_hasSavedCredentials = true;
  setStatus(QStringLiteral("Credentials saved in the encrypted file. Use the passphrase to unlock next time."));
}

void Buyer::unlockCredentials(const QString &passphrase) {
  if (m_busy) return;
  Credentials credentials;
  QString error;
  if (!CredentialStore::load(CredentialStore::defaultPath(), passphrase,
                             &credentials, &error)) {
    setStatus(error);
    return;
  }
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
  setStatus(readOnlyReady() ? QStringLiteral("Credentials unlocked; checking both API keys…")
                            : QStringLiteral("Trading key unlocked. Add a separate read-only key for balance and status checks."));
  validateKeys();
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

void Buyer::refreshBestAsk() { fetchBook(0); }

void Buyer::recommend(const QString &aud) {
  double budget = 0;
  if (!parseMoney(aud, 2, &budget)) {
    setStatus(QStringLiteral("Enter an AUD amount before requesting a price"));
    return;
  }
  fetchBook(budget);
}

void Buyer::fetchBook(double budget) {
  const quint64 serial = ++m_bookRequestSerial;
  QNetworkRequest request(QUrl(QStringLiteral("https://www.coinspot.com.au/pubapi/v2/orders/open/BTC")));
  request.setTransferTimeout(10000);
  auto *reply = m_network.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, budget, serial] {
    if (serial != m_bookRequestSerial) { reply->deleteLater(); return; }
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
      m_bestAsk = std::isfinite(lowest) ? QString::number(lowest, 'f', 2) : QString();
      m_bestAskValue = std::isfinite(lowest) ? lowest : 0;
      m_bookAt = QDateTime::currentDateTimeUtc();
      m_marketSpread = highestBid > 0 && std::isfinite(lowest)
                           ? QString::number((lowest / highestBid - 1.0) * 100.0, 'f', 2) + QLatin1Char('%')
                           : QString();
      if (budget > 0) {
        const double cap = recommendedCap(levels, budget);
        m_recommendedPrice = cap > 0 ? QString::number(cap, 'f', 8) : QString();
        emit recommendedPriceChanged();
        setStatus(cap > 0
                      ? QStringLiteral("Suggested cap from the current CoinSpot order book. Quotes can change before an order fills.")
                      : QStringLiteral("The visible CoinSpot sell orders do not cover this amount; no price suggested."));
      }
    } else {
      m_bestAsk.clear();
      m_bestAskValue = 0;
      m_bookAt = {};
      m_marketSpread.clear();
      if (budget > 0) {
        m_recommendedPrice.clear();
        emit recommendedPriceChanged();
        setStatus(QStringLiteral("Could not load the CoinSpot order book. Try again later."));
      }
    }
    emit changed();
    reply->deleteLater();
  });
}

bool Buyer::prepare(const QString &aud, const QString &maxPrice) {
  if (m_busy) { setStatus(QStringLiteral("An order request is already in progress")); return false; }
  if (m_uncertainBuy) { setStatus(QStringLiteral("Review and acknowledge the uncertain buy request before placing another order.")); return false; }
  if (!connected()) { setStatus(QStringLiteral("Add your CoinSpot API key first")); return false; }
  if (!readOnlyReady()) { setStatus(QStringLiteral("Add a read-only API key for the balance check first")); return false; }
  if (!keysVerified()) { setStatus(QStringLiteral("Verify both CoinSpot API keys before buying. Use Account → Retry API checks if needed.")); return false; }
  if (!parseMoney(aud, 2, &m_budget)) { setStatus(QStringLiteral("Enter an AUD amount with up to two decimals")); return false; }
  if (!parseMoney(maxPrice, 8, &m_maxPrice)) { setStatus(QStringLiteral("Enter a positive maximum BTC price in AUD")); return false; }
  m_amount = coinAmount(m_budget, m_maxPrice);
  if (m_amount < 0.00000001) { setStatus(QStringLiteral("The amount is too small for this maximum price")); return false; }
  return true;
}

QString Buyer::preview() const {
  const QString marketContext = m_bestAskValue > 0 && m_bookAt.isValid() &&
      m_bookAt.secsTo(QDateTime::currentDateTimeUtc()) <= 30
      ? (m_maxPrice >= m_bestAskValue
          ? QStringLiteral("Your price reaches the last observed ask of A$%1, so this may fill immediately.")
                .arg(QString::number(m_bestAskValue, 'f', 2))
          : QStringLiteral("Your price is below the last observed ask of A$%1, so it may stay open.")
                .arg(QString::number(m_bestAskValue, 'f', 2)))
      : QStringLiteral("The order book quote is unavailable or over 30 seconds old; immediate fill is unknown.");
  return QStringLiteral("Buy %1 BTC at no more than A$%2 per BTC?\n\n"
                        "Maximum AUD spend: A$%3.\n"
                        "Estimated BTC after the 0.1% fee: about %4 BTC.\n\n"
                        "%5\n\nAvailable AUD will be checked before submission. The order may fill partly.")
      .arg(QString::number(m_amount, 'f', 8), QString::number(m_maxPrice, 'f', 8),
           QString::number(m_amount * m_maxPrice, 'f', 2),
           QString::number(m_amount * 0.999, 'f', 8), marketContext);
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

void Buyer::refreshBalance() {
  if (!m_readKeyVerified || m_busy) return;
  const QByteArray readKey = m_readKey;
  auto *reply = postPrivate(QStringLiteral("ro/my/balance/AUD?available=yes"), {}, true);
  connect(reply, &QNetworkReply::finished, this, [this, reply, readKey] {
    const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
    if (readKey != m_readKey || !m_readKeyVerified) { reply->deleteLater(); return; }
    const QJsonValue available = response.value(QStringLiteral("balance"))
        .toObject().value(QStringLiteral("AUD")).toObject().value(QStringLiteral("available"));
    m_availableAud = reply->error() == QNetworkReply::NoError &&
                             response.value(QStringLiteral("status")).toString() == QLatin1String("ok") &&
                             available.isDouble() && std::isfinite(available.toDouble()) && available.toDouble() >= 0
                         ? QString::number(available.toDouble(), 'f', 2) : QString();
    emit changed();
    reply->deleteLater();
  });
}

void Buyer::submit() {
  if (m_busy || m_uncertainBuy || !keysVerified() || m_amount <= 0) return;
  m_busy = true;
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
      m_availableAud = QString::number(funds, 'f', 2);
      const double required = m_amount * m_maxPrice;
      if (!std::isfinite(funds) || funds + 0.000001 < required) {
        m_busy = false;
        setStatus(QStringLiteral("Insufficient available AUD: A$%1 needed, A$%2 available. No order was submitted.")
                      .arg(QString::number(required, 'f', 2), m_availableAud));
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
      if (m_orderPath.isEmpty() || !OrderStore::save(m_orderPath, m_orders))
        setStatus(QStringLiteral("Order accepted by CoinSpot: %1. Could not update local history; check CoinSpot after restarting.").arg(m_lastOrderId));
      else
        setStatus(QStringLiteral("Order accepted by CoinSpot: %1").arg(m_lastOrderId));
      m_orderPoll.start();
      refreshOrderStatus();
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
      if (!m_orderPath.isEmpty()) OrderStore::save(m_orderPath, m_orders);
      setStatus(QStringLiteral("Buy request outcome unknown. Refresh orders and review CoinSpot before another buy."));
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
      const QString date = entry.toObject().value(QStringLiteral("date")).toString();
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
          if (!id.isEmpty()) updateOrder(id, describeOrder(m_openOrders, completed, id,
                                                           order.value(QStringLiteral("amount")).toDouble()),
                                         fillSummary(completed, id));
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
