#include "services/buyer.h"
#include "services/credentialstore.h"
#include "services/orderstore.h"
#include <QFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QtTest>

class BuyerTest : public QObject {
  Q_OBJECT
private slots:
  void parsesAmounts() {
    double value = 0;
    QVERIFY(pb::Buyer::parseMoney("100,25", 2, &value));
    QCOMPARE(value, 100.25);
    QVERIFY(!pb::Buyer::parseMoney("100.256", 2, &value));
    QVERIFY(!pb::Buyer::parseMoney("-10", 2, &value));
    QVERIFY(!pb::Buyer::parseMoney("0", 2, &value));
  }
  void recognisesKeyStatusResponses() {
    QVERIFY(pb::Buyer::apiStatusOkay(QJsonObject{{"status", "ok"}}));
    QVERIFY(!pb::Buyer::apiStatusOkay(QJsonObject{{"status", "error"}}));
    QVERIFY(!pb::Buyer::apiStatusOkay(QJsonObject{}));
  }
  void respectsBudget() {
    const double amount = pb::Buyer::coinAmount(100.0, 100000.0);
    QCOMPARE(amount, 0.001);
    QVERIFY(amount * 100000.0 <= 100.0);
    const double fractional = pb::Buyer::coinAmount(100.01, 123456.78);
    QVERIFY(fractional * 123456.78 <= 100.01 + 0.00000001);
    QVERIFY((fractional + 0.00000001) * 123456.78 > 100.01);
  }
  void reservesAudForMarketFee() {
    const double fee = pb::Buyer::kMarketsFeeRate;
    const double amount = pb::Buyer::coinAmount(100.0, 100000.0, fee);
    QVERIFY(amount < 0.001);
    QVERIFY(amount * 100000.0 * (1.0 + fee) <= 100.0);
    QVERIFY((amount + 0.00000001) * 100000.0 * (1.0 + fee) > 100.0);
    const double fractional = pb::Buyer::coinAmount(1234.56, 98765.43, fee);
    QVERIFY(fractional * 98765.43 * (1.0 + fee) <= 1234.56 + 0.00000001);
    QVERIFY((fractional + 0.00000001) * 98765.43 * (1.0 + fee) > 1234.56);
    QCOMPARE(pb::Buyer::coinAmount(100.0, 100000.0, -0.1), 0.0);
  }
  void roundsSuggestedSpendDownToCents() {
    QCOMPARE(pb::Buyer::wholeCents(100.0), 100.0);
    QCOMPARE(pb::Buyer::wholeCents(1234.567), 1234.56);
    QCOMPARE(pb::Buyer::wholeCents(0.009), 0.0);
    QCOMPARE(pb::Buyer::wholeCents(0.0), 0.0);
  }
  void suggestsLowestSufficientAsk() {
    const QVector<pb::AskLevel> asks{{101000, 0.001}, {100000, 0.0005}};
    QCOMPARE(pb::Buyer::recommendedCap(asks, 40), 100000.0);
    QCOMPARE(pb::Buyer::recommendedCap(asks, 100), 101000.0);
    QCOMPARE(pb::Buyer::recommendedCap(asks, 500), 0.0);
    QCOMPARE(pb::Buyer::recommendedCap({{0, 99}, {-1, 1}}, 100), 0.0);
  }
  void currentPriceRoundsUpToCoveringLevel() {
    const QVector<pb::AskLevel> asks{{100000.004, 0.0005}, {101000.123, 0.01}};
    QCOMPARE(pb::Buyer::currentPrice(asks, 40), 100000.01);
    QCOMPARE(pb::Buyer::currentPrice(asks, 100), 101000.13);
    QCOMPARE(pb::Buyer::currentPrice(asks, 5000), 0.0);
    const QVector<pb::AskLevel> exact{{100000.0, 1.0}};
    QCOMPARE(pb::Buyer::currentPrice(exact, 100), 100000.0);
    // The rounded-up price still buys no more BTC than the covering level holds.
    QVERIFY(pb::Buyer::coinAmount(100, 101000.13, pb::Buyer::kMarketsFeeRate) <= 0.0105);
  }
  void quotesOrdersWithFeeAndBalance() {
    const QVariantMap okay = pb::Buyer::quoteFor("100", "100000", 250, 99000);
    QVERIFY(okay.value("valid").toBool());
    const double btc = okay.value("btc").toDouble();
    QCOMPARE(btc, pb::Buyer::coinAmount(100, 100000, pb::Buyer::kMarketsFeeRate));
    QVERIFY(okay.value("total").toDouble() <= 100.0);
    QCOMPARE(okay.value("fee").toDouble(), okay.value("tradeValue").toDouble() * 0.001);
    QVERIFY(okay.value("askKnown").toBool());
    QVERIFY(okay.value("reachesAsk").toBool());
    QVERIFY(!pb::Buyer::quoteFor("100", "98000", -1, 99000).value("reachesAsk").toBool());
    QVERIFY(!pb::Buyer::quoteFor("100", "98000", -1, 0).value("askKnown").toBool());
    const QVariantMap over = pb::Buyer::quoteFor("300", "100000", 250, 0);
    QVERIFY(!over.value("valid").toBool());
    QVERIFY(over.value("error").toString().contains("250.00"));
    QVERIFY(over.contains("btc"));
    QCOMPARE(pb::Buyer::quoteFor("", "100000", -1, 0).value("error").toString(), QString("Enter an amount in AUD."));
    QCOMPARE(pb::Buyer::quoteFor("0.00", "100000", -1, 0).value("error").toString(), QString("No AUD to spend."));
    QVERIFY(!pb::Buyer::quoteFor("1.234", "100000", -1, 0).value("valid").toBool());
    QCOMPARE(pb::Buyer::quoteFor("100", "", -1, 0).value("error").toString(), QString("No current price for this amount yet."));
    QVERIFY(!pb::Buyer::quoteFor("100", "abc", -1, 0).value("valid").toBool());
    QVERIFY(!pb::Buyer::quoteFor("0.01", "100000000", -1, 0).value("valid").toBool());
  }
  void blockedReasonExplainsMissingKeys() {
    pb::Buyer buyer;
    QVERIFY(!buyer.buyBlockedReason().isEmpty());
  }
  void reportsOrderProgressConservatively() {
    const QJsonArray open{QJsonObject{{"id", "order-1"}}};
    const QJsonArray partial{QJsonObject{{"id", "order-1"}, {"amount", 0.004}}};
    const QJsonArray complete{QJsonObject{{"id", "order-1"}, {"amount", 0.01}}};
    QCOMPARE(pb::Buyer::describeOrder(open, {}, "order-1", 0.01),
             QString("Open: waiting for a seller at your price or lower."));
    QVERIFY(pb::Buyer::describeOrder(open, partial, "order-1", 0.01).startsWith("Partly filled:"));
    QVERIFY(pb::Buyer::describeOrder({}, complete, "order-1", 0.01).startsWith("Filled:"));
    QVERIFY(pb::Buyer::describeOrder({}, {}, "order-1", 0.01).startsWith("No open order"));
    QVERIFY(pb::Buyer::describeOrder({}, {}, "order-1", 0.00000001).startsWith("No open order"));
  }
  void keepsRecordedFillWhenHistoryIsTruncated() {
    const QJsonObject filled{{"id", "order-1"}, {"amount", 0.01}, {"filledBtc", 0.01}};
    QVERIFY(pb::Buyer::keepsRecordedFill(filled, {}));
    QVERIFY(pb::Buyer::keepsRecordedFill(filled, QJsonObject{{"filledBtc", 0.004}}));
    QVERIFY(!pb::Buyer::keepsRecordedFill(filled, QJsonObject{{"filledBtc", 0.01}}));
    QVERIFY(!pb::Buyer::keepsRecordedFill(QJsonObject{{"id", "order-1"}, {"amount", 0.01}}, {}));
    QVERIFY(!pb::Buyer::keepsRecordedFill(QJsonObject{{"filledBtc", 0.004}},
                                          QJsonObject{{"filledBtc", 0.01}}));
  }
  void aggregatesReportedFills() {
    const QJsonArray fills{
        QJsonObject{{"id", "order-1"}, {"amount", 0.004}, {"rate", 100000.0},
                    {"total", 400.0}, {"audfeeExGst", 0.35}, {"audGst", 0.035}},
        QJsonObject{{"id", "order-1"}, {"amount", 0.006}, {"rate", 101000.0},
                    {"total", 606.0}, {"audfeeExGst", 0.55}, {"audGst", 0.055}},
        QJsonObject{{"id", "other"}, {"amount", 2.0}, {"rate", 1.0}}};
    const QJsonObject summary = pb::Buyer::fillSummary(fills, "order-1");
    QCOMPARE(summary.value("filledBtc").toDouble(), 0.01);
    QCOMPARE(summary.value("averageFillPrice").toDouble(), 100600.0);
    QCOMPARE(summary.value("reportedTradeTotalAud").toDouble(), 1006.0);
    QCOMPARE(summary.value("reportedFeeAud").toDouble(), 0.99);
    QVERIFY(pb::Buyer::fillSummary(fills, "missing").isEmpty());
    const QJsonObject noFee = pb::Buyer::fillSummary(
        {QJsonObject{{"id", "order-1"}, {"amount", 0.01}, {"rate", 100000.0}}}, "order-1");
    QVERIFY(!noFee.contains("reportedFeeAud"));
    QVERIFY(!noFee.contains("reportedTradeTotalAud"));
  }
  void pendingAttemptBlocksAnotherBuyAfterRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray oldDataHome = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    const QByteArray key("pending-attempt-test-key");
    const QString path = pb::OrderStore::pathForKey(key);
    QVERIFY(pb::OrderStore::save(path, {QJsonObject{{"attemptId", "attempt-1"},
                                                     {"pending", true},
                                                     {"state", "Outcome unknown"}}}));
    {
      pb::Buyer buyer;
      buyer.setCredentials(QString::fromUtf8(key), "secret");
      QVERIFY(buyer.uncertainBuy());
      QVERIFY(!buyer.uncertainRefreshed());
      buyer.acknowledgeUncertainBuy();
      QVERIFY(buyer.uncertainBuy());
      QVERIFY(!buyer.prepare("100", "100000"));
    }
    if (oldDataHome.isNull()) qunsetenv("XDG_DATA_HOME");
    else qputenv("XDG_DATA_HOME", oldDataHome);
  }
  void unverifiedKeysDoNotReplaceSavedFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray oldDataHome = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    QString error;
    const QString path = pb::CredentialStore::defaultPath();
    QVERIFY2(pb::CredentialStore::save(path, "old passphrase 123", {"old-key", "old-secret", "old-read", "old-read-secret"}, &error),
             qPrintable(error));
    QFile before(path);
    QVERIFY(before.open(QIODevice::ReadOnly));
    const QByteArray original = before.readAll();
    before.close();
    {
      pb::Buyer buyer;
      buyer.setCredentials("new-key", "mistyped-secret");
      buyer.setReadOnlyCredentials("new-read", "new-read-secret");
      QVERIFY(!buyer.keysVerified());
      QVERIFY(!buyer.saveCredentials("new passphrase 123", "new passphrase 123").isEmpty());
    }
    QFile after(path);
    QVERIFY(after.open(QIODevice::ReadOnly));
    QCOMPARE(after.readAll(), original);
    pb::Credentials unlocked;
    QVERIFY(pb::CredentialStore::load(path, "old passphrase 123", &unlocked, &error));
    QCOMPARE(unlocked.key, QByteArray("old-key"));
    if (oldDataHome.isNull()) qunsetenv("XDG_DATA_HOME");
    else qputenv("XDG_DATA_HOME", oldDataHome);
  }
  void savesOrderJournal() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/private/orders.json";
    const QJsonArray orders{QJsonObject{{"id", "order-1"}, {"amount", 0.01},
                                         {"rate", 100000.0}, {"state", "Open"}}};
    QVERIFY(pb::OrderStore::save(path, orders));
    QCOMPARE(pb::OrderStore::load(path), orders);
    QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther |
                                         QFileDevice::WriteGroup | QFileDevice::WriteOther),
             QFileDevice::Permissions{});
    QCOMPARE(QFile::permissions(dir.path() + "/private") &
                 (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
  }
  void damagedJournalBlocksBuying() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray oldDataHome = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", dir.path().toUtf8());
    const QByteArray key("damaged-journal-test-key");
    const QString path = pb::OrderStore::pathForKey(key);
    bool missingOkay = false;
    QVERIFY(pb::OrderStore::load(path, &missingOkay).isEmpty());
    QVERIFY(missingOkay);
    QVERIFY(pb::OrderStore::save(path, {}));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("{damaged", 8), 8);
    file.close();
    bool okay = true;
    QVERIFY(pb::OrderStore::load(path, &okay).isEmpty());
    QVERIFY(!okay);
    {
      pb::Buyer buyer;
      buyer.setCredentials(QString::fromUtf8(key), "secret");
      QVERIFY(buyer.journalProblem());
      QCOMPARE(buyer.journalPath(), path);
      QVERIFY(!buyer.prepare("100", "100000"));
    }
    if (oldDataHome.isNull()) qunsetenv("XDG_DATA_HOME");
    else qputenv("XDG_DATA_HOME", oldDataHome);
  }
  void encryptedCredentialsRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/private/credentials.enc";
    QString error;
    const pb::Credentials original{"sample-key", "sample-secret", "sample-read-key", "sample-read-secret"};
    QVERIFY2(pb::CredentialStore::save(path, "a long test passphrase", original, &error), qPrintable(error));
    QVERIFY(pb::CredentialStore::exists(path));
    QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther |
                                         QFileDevice::WriteGroup | QFileDevice::WriteOther),
             QFileDevice::Permissions{});
    pb::Credentials unlocked;
    QVERIFY2(pb::CredentialStore::load(path, "a long test passphrase", &unlocked, &error), qPrintable(error));
    QCOMPARE(unlocked.key, original.key);
    QCOMPARE(unlocked.secret, original.secret);
    QCOMPARE(unlocked.readKey, original.readKey);
    QCOMPARE(unlocked.readSecret, original.readSecret);
    QVERIFY(!pb::CredentialStore::load(path, "wrong passphrase", &unlocked, &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.seek(file.size() - 1));
    const char changed = static_cast<char>(file.read(1).at(0) ^ 0x01);
    QVERIFY(file.seek(file.size() - 1));
    QCOMPARE(file.write(&changed, 1), 1);
    file.close();
    QVERIFY(!pb::CredentialStore::load(path, "a long test passphrase", &unlocked, &error));
  }
};
QTEST_GUILESS_MAIN(BuyerTest)
#include "tst_buyer.moc"
