#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace pb {
struct AskLevel { double rate; double amount; };
class Buyer : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(bool readOnlyReady READ readOnlyReady NOTIFY changed)
  Q_PROPERTY(bool keysVerified READ keysVerified NOTIFY changed)
  Q_PROPERTY(bool fullKeyVerified READ fullKeyVerified NOTIFY changed)
  Q_PROPERTY(bool readKeyVerified READ readKeyVerified NOTIFY changed)
  Q_PROPERTY(QString fullKeyStatus READ fullKeyStatus NOTIFY changed)
  Q_PROPERTY(QString readKeyStatus READ readKeyStatus NOTIFY changed)
  Q_PROPERTY(bool hasSavedCredentials READ hasSavedCredentials NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString bestAsk READ bestAsk NOTIFY changed)
  Q_PROPERTY(QString recommendedPrice READ recommendedPrice NOTIFY recommendedPriceChanged)
  Q_PROPERTY(QString marketSpread READ marketSpread NOTIFY changed)
  Q_PROPERTY(QString lastOrderId READ lastOrderId NOTIFY changed)
  Q_PROPERTY(QString availableAud READ availableAud NOTIFY changed)
  Q_PROPERTY(QString orderState READ orderState NOTIFY changed)
  Q_PROPERTY(QVariantList openOrders READ openOrders NOTIFY changed)
  Q_PROPERTY(QVariantList orderHistory READ orderHistory NOTIFY changed)
  Q_PROPERTY(bool ordersLoaded READ ordersLoaded NOTIFY changed)
  Q_PROPERTY(bool uncertainBuy READ uncertainBuy NOTIFY changed)
  Q_PROPERTY(bool uncertainRefreshed READ uncertainRefreshed NOTIFY changed)
  Q_PROPERTY(bool journalProblem READ journalProblem NOTIFY changed)
  Q_PROPERTY(QString journalPath READ journalPath NOTIFY changed)
  Q_PROPERTY(QString version READ version CONSTANT)
public:
  explicit Buyer(QObject *parent = nullptr);
  ~Buyer() override;
  bool connected() const { return !m_key.isEmpty() && !m_secret.isEmpty(); }
  bool readOnlyReady() const { return !m_readKey.isEmpty() && !m_readSecret.isEmpty(); }
  bool keysVerified() const { return m_fullKeyVerified && m_readKeyVerified; }
  bool fullKeyVerified() const { return m_fullKeyVerified; }
  bool readKeyVerified() const { return m_readKeyVerified; }
  QString fullKeyStatus() const { return m_fullKeyStatus; }
  QString readKeyStatus() const { return m_readKeyStatus; }
  bool hasSavedCredentials() const { return m_hasSavedCredentials; }
  bool busy() const { return m_busy; }
  bool previewLoading() const { return m_previewLoading; }
  QString status() const { return m_status; }
  QString bestAsk() const { return m_bestAsk; }
  QString recommendedPrice() const { return m_recommendedPrice; }
  QString marketSpread() const { return m_marketSpread; }
  QString lastOrderId() const { return m_lastOrderId; }
  QString availableAud() const { return m_availableAud; }
  QString orderState() const { return m_orderState; }
  QVariantList openOrders() const { return m_openOrders.toVariantList(); }
  QVariantList orderHistory() const { return m_orders.toVariantList(); }
  bool ordersLoaded() const { return m_ordersLoaded; }
  bool uncertainBuy() const { return m_uncertainBuy; }
  bool uncertainRefreshed() const { return m_uncertainRefreshed; }
  bool journalProblem() const { return m_journalProblem; }
  QString journalPath() const { return m_orderPath; }
  QString version() const;
  Q_INVOKABLE void setCredentials(const QString &key, const QString &secret);
  Q_INVOKABLE void setReadOnlyCredentials(const QString &key, const QString &secret);
  Q_INVOKABLE void clearCredentials();
  Q_INVOKABLE void validateKeys();
  Q_INVOKABLE void saveCredentials(const QString &passphrase, const QString &confirmation);
  Q_INVOKABLE void unlockCredentials(const QString &passphrase);
  Q_INVOKABLE void forgetSavedCredentials();
  Q_INVOKABLE void refreshBestAsk();
  Q_INVOKABLE void recommend(const QString &aud);
  Q_INVOKABLE void refreshBalance();
  Q_INVOKABLE void suggestAvailableSpend();
  Q_INVOKABLE void refreshOrderStatus();
  Q_INVOKABLE void reviewUncertainBuy();
  Q_INVOKABLE void acknowledgeUncertainBuy();
  Q_INVOKABLE void cancelOrder(const QString &id);
  Q_INVOKABLE bool prepare(const QString &aud, const QString &maxPrice);
  Q_INVOKABLE QString preview() const;
  Q_INVOKABLE void submit();
  static bool parseMoney(const QString &text, int maxDecimals, double *value);
  static double coinAmount(double audBudget, double maxPrice);
  static double spendWithFeeReserve(double availableAud);
  static double recommendedCap(QVector<AskLevel> asks, double audBudget);
  static QString describeOrder(const QJsonArray &openOrders,
                               const QJsonArray &completedOrders,
                               const QString &orderId, double requestedAmount);
  static QJsonObject fillSummary(const QJsonArray &completedOrders, const QString &orderId);
  static bool apiStatusOkay(const QJsonObject &response);
signals:
  void changed();
  void recommendedPriceChanged();
  void previewReady();
  void suggestedSpendReady(const QString &amount);
private:
  void setStatus(const QString &value);
  void fetchBook(double budget, bool forPreview = false);
  QNetworkReply *postPrivate(const QString &endpoint, const QJsonObject &fields, bool readOnly);
  void placePreparedOrder();
  void loadOrders();
  void updateOrder(const QString &id, const QString &state, const QJsonObject &fills = {});
  void removeAttempt(const QString &attemptId);
  void validateFullKey();
  void validateReadKey();
  QNetworkAccessManager m_network;
  QByteArray m_key, m_secret, m_readKey, m_readSecret;
  qint64 m_nonce = 0;
  qint64 m_readNonce = 0;
  bool m_busy = false;
  bool m_previewLoading = false;
  bool m_statusBusy = false;
  bool m_balanceSuggestionLoading = false;
  bool m_hasSavedCredentials = false;
  bool m_fullKeyVerified = false;
  bool m_readKeyVerified = false;
  QString m_fullKeyStatus = QStringLiteral("Not entered");
  QString m_readKeyStatus = QStringLiteral("Not entered");
  quint64 m_fullValidationSerial = 0;
  quint64 m_readValidationSerial = 0;
  bool m_ordersLoaded = false;
  bool m_uncertainBuy = false;
  bool m_uncertainRefreshed = false;
  bool m_journalProblem = false;
  bool m_reviewRequested = false;
  double m_budget = 0;
  double m_maxPrice = 0;
  double m_amount = 0;
  double m_lastOrderAmount = 0;
  QString m_status, m_bestAsk, m_lastOrderId;
  QString m_availableAud, m_orderState;
  QString m_lastOrderDate;
  QString m_orderPath;
  QJsonArray m_orders, m_openOrders;
  QString m_recommendedPrice, m_marketSpread;
  double m_bestAskValue = 0;
  QDateTime m_bookAt;
  quint64 m_bookRequestSerial = 0;
  QTimer m_orderPoll;
};
} // namespace pb
