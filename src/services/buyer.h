#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVector>
#include <functional>

namespace pb {
struct AskLevel { double rate; double amount; };
class Buyer : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(bool readOnlyReady READ readOnlyReady NOTIFY changed)
  Q_PROPERTY(bool keysVerified READ keysVerified NOTIFY changed)
  Q_PROPERTY(bool keysChecking READ keysChecking NOTIFY changed)
  Q_PROPERTY(bool fullKeyVerified READ fullKeyVerified NOTIFY changed)
  Q_PROPERTY(bool readKeyVerified READ readKeyVerified NOTIFY changed)
  Q_PROPERTY(QString fullKeyStatus READ fullKeyStatus NOTIFY changed)
  Q_PROPERTY(QString readKeyStatus READ readKeyStatus NOTIFY changed)
  Q_PROPERTY(bool hasSavedCredentials READ hasSavedCredentials NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString bestAsk READ bestAsk NOTIFY changed)
  Q_PROPERTY(QString marketSpread READ marketSpread NOTIFY changed)
  Q_PROPERTY(QString lastOrderId READ lastOrderId NOTIFY changed)
  Q_PROPERTY(QString availableAud READ availableAud NOTIFY changed)
  Q_PROPERTY(QString spendableAud READ spendableAud NOTIFY changed)
  Q_PROPERTY(QString balanceUpdated READ balanceUpdated NOTIFY changed)
  Q_PROPERTY(QString bookUpdated READ bookUpdated NOTIFY changed)
  Q_PROPERTY(QString buyBlockedReason READ buyBlockedReason NOTIFY changed)
  Q_PROPERTY(QString orderState READ orderState NOTIFY changed)
  Q_PROPERTY(QVariantList openOrders READ openOrders NOTIFY changed)
  Q_PROPERTY(QVariantList orderHistory READ orderHistory NOTIFY changed)
  Q_PROPERTY(bool ordersLoaded READ ordersLoaded NOTIFY changed)
  Q_PROPERTY(bool ordersFailed READ ordersFailed NOTIFY changed)
  Q_PROPERTY(bool uncertainBuy READ uncertainBuy NOTIFY changed)
  Q_PROPERTY(bool uncertainRefreshed READ uncertainRefreshed NOTIFY changed)
  Q_PROPERTY(bool journalProblem READ journalProblem NOTIFY changed)
  Q_PROPERTY(bool journalUnsaved READ journalUnsaved NOTIFY changed)
  Q_PROPERTY(QString journalPath READ journalPath NOTIFY changed)
  Q_PROPERTY(QString version READ version CONSTANT)
public:
  explicit Buyer(QObject *parent = nullptr);
  ~Buyer() override;
  bool connected() const { return !m_key.isEmpty() && !m_secret.isEmpty(); }
  bool readOnlyReady() const { return !m_readKey.isEmpty() && !m_readSecret.isEmpty(); }
  bool keysVerified() const { return m_fullKeyVerified && m_readKeyVerified; }
  bool keysChecking() const {
    return m_fullKeyStatus == QLatin1String("Checking…") || m_readKeyStatus == QLatin1String("Checking…");
  }
  bool fullKeyVerified() const { return m_fullKeyVerified; }
  bool readKeyVerified() const { return m_readKeyVerified; }
  QString fullKeyStatus() const { return m_fullKeyStatus; }
  QString readKeyStatus() const { return m_readKeyStatus; }
  bool hasSavedCredentials() const { return m_hasSavedCredentials; }
  bool busy() const { return m_busy; }
  bool previewLoading() const { return m_previewLoading; }
  QString status() const { return m_status; }
  QString bestAsk() const { return m_bestAsk; }
  QString marketSpread() const { return m_marketSpread; }
  QString lastOrderId() const { return m_lastOrderId; }
  QString availableAud() const {
    return m_availableAudValue >= 0 ? QString::number(wholeCents(m_availableAudValue), 'f', 2) : QString();
  }
  QString spendableAud() const;
  QString balanceUpdated() const { return m_balanceAt.isValid() ? m_balanceAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")) : QString(); }
  QString bookUpdated() const { return m_bookAt.isValid() ? m_bookAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")) : QString(); }
  QString buyBlockedReason() const;
  QString orderState() const { return m_orderState; }
  QVariantList openOrders() const { return m_openOrders.toVariantList(); }
  QVariantList orderHistory() const { return m_orders.toVariantList(); }
  bool ordersLoaded() const { return m_ordersLoaded; }
  // The last open-order refresh failed, so no list is shown.
  bool ordersFailed() const { return m_ordersFailed; }
  bool uncertainBuy() const { return m_uncertainBuy; }
  bool uncertainRefreshed() const { return m_uncertainRefreshed; }
  bool journalProblem() const { return m_journalProblem; }
  bool journalUnsaved() const { return m_journalUnsaved; }
  QString journalPath() const { return m_orderPath; }
  QString version() const;
  Q_INVOKABLE void setCredentials(const QString &key, const QString &secret);
  Q_INVOKABLE void setReadOnlyCredentials(const QString &key, const QString &secret);
  Q_INVOKABLE void clearCredentials();
  Q_INVOKABLE void validateKeys();
  // Both return an error message, or an empty string on success.
  Q_INVOKABLE QString saveCredentials(const QString &passphrase, const QString &confirmation);
  Q_INVOKABLE QString unlockCredentials(const QString &passphrase);
  Q_INVOKABLE void forgetSavedCredentials();
  Q_INVOKABLE void refreshBestAsk();
  Q_INVOKABLE void refreshBalance(bool force = false);
  Q_INVOKABLE void refreshOrderStatus();
  Q_INVOKABLE void reviewUncertainBuy();
  Q_INVOKABLE void acknowledgeUncertainBuy();
  Q_INVOKABLE void retrySaveJournal();
  Q_INVOKABLE void cancelOrder(const QString &id);
  Q_INVOKABLE bool prepare(const QString &aud, const QString &maxPrice, bool followMarket = false);
  Q_INVOKABLE QVariantMap preview() const;
  Q_INVOKABLE QString marketPriceFor(const QString &aud) const;
  Q_INVOKABLE QVariantMap quote(const QString &aud, const QString &price) const;
  Q_INVOKABLE void submit();
  static bool parseMoney(const QString &text, int maxDecimals, double *value);
  // CoinSpot's listed Markets fee, reserved in case it is charged in AUD on top of the trade.
  static constexpr double kMarketsFeeRate = 0.001;
  static double coinAmount(double audBudget, double maxPrice, double feeRate = 0);
  static double wholeCents(double aud);
  static double recommendedCap(QVector<AskLevel> asks, double audBudget);
  static double currentPrice(const QVector<AskLevel> &asks, double audBudget);
  static QVariantMap quoteFor(const QString &aud, const QString &price, double availableAud, double bestAsk);
  static QString describeOrder(const QJsonArray &openOrders,
                               const QJsonArray &completedOrders,
                               const QString &orderId, double requestedAmount);
  static QJsonObject fillSummary(const QJsonArray &completedOrders, const QString &orderId);
  static bool keepsRecordedFill(const QJsonObject &order, const QJsonObject &fills);
  static bool apiStatusOkay(const QJsonObject &response);
  // CoinSpot's error message, else the network or HTTP error, for status lines.
  static QString failureReason(QNetworkReply::NetworkError error, const QString &errorString,
                               int httpStatus, const QJsonObject &response);
signals:
  void changed();
  void marketUpdated();
  void previewReady();
private:
  void setStatus(const QString &value);
  void fetchBook(bool forPreview = false);
  void finishPreview();
  bool bookFresh() const;
  using PrivateHandler = std::function<void(QNetworkReply *, const QJsonObject &)>;
  // Queues a signed request; each key has at most one in flight, so nonces reach CoinSpot in order.
  void postPrivate(const QString &endpoint, const QJsonObject &fields, bool readOnly, PrivateHandler done);
  void sendNextPrivate(bool readOnly);
  static QString failureReason(QNetworkReply *reply, const QJsonObject &response);
  void placePreparedOrder();
  void loadOrders();
  void updateOrder(const QString &id, const QString &state, const QJsonObject &fills = {});
  void removeAttempt(const QString &attemptId);
  bool saveOrders();
  void validateFullKey();
  void validateReadKey();
  QNetworkAccessManager m_network;
  QByteArray m_key, m_secret, m_readKey, m_readSecret;
  qint64 m_nonce = 0;
  qint64 m_readNonce = 0;
  struct PrivateRequest {
    QString endpoint;
    QJsonObject fields;
    QByteArray key, secret;
    PrivateHandler done;
  };
  QList<PrivateRequest> m_fullQueue, m_readQueue;
  bool m_fullInFlight = false;
  bool m_readInFlight = false;
  bool m_busy = false;
  bool m_previewLoading = false;
  bool m_statusBusy = false;
  bool m_statusIsOrderFailure = false;
  bool m_balanceLoading = false;
  bool m_followMarket = false;
  bool m_hasSavedCredentials = false;
  bool m_fullKeyVerified = false;
  bool m_readKeyVerified = false;
  QString m_fullKeyStatus = QStringLiteral("Not entered");
  QString m_readKeyStatus = QStringLiteral("Not entered");
  quint64 m_fullValidationSerial = 0;
  quint64 m_readValidationSerial = 0;
  bool m_ordersLoaded = false;
  bool m_ordersFailed = false;
  bool m_uncertainBuy = false;
  bool m_uncertainRefreshed = false;
  bool m_journalProblem = false;
  bool m_journalUnsaved = false;
  bool m_reviewRequested = false;
  double m_budget = 0;
  double m_maxPrice = 0;
  double m_amount = 0;
  double m_lastOrderAmount = 0;
  QString m_status, m_bestAsk, m_lastOrderId;
  QString m_orderState;
  double m_availableAudValue = -1;
  QDateTime m_balanceAt;
  QString m_lastOrderDate;
  QString m_orderPath;
  QJsonArray m_orders, m_openOrders;
  QString m_marketSpread;
  QVector<AskLevel> m_askLevels;
  double m_bestAskValue = 0;
  QDateTime m_bookAt;
  quint64 m_bookRequestSerial = 0;
  quint64 m_balanceSerial = 0;
  QTimer m_orderPoll;
  QTimer m_bookPoll;
};
} // namespace pb
