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
  void respectsBudget() {
    const double amount = pb::Buyer::coinAmount(100.0, 100000.0);
    QCOMPARE(amount, 0.001);
    QVERIFY(amount * 100000.0 <= 100.0);
    const double fractional = pb::Buyer::coinAmount(100.01, 123456.78);
    QVERIFY(fractional * 123456.78 <= 100.01 + 0.00000001);
    QVERIFY((fractional + 0.00000001) * 123456.78 > 100.01);
  }
  void suggestsLowestSufficientAsk() {
    const QVector<pb::AskLevel> asks{{101000, 0.001}, {100000, 0.0005}};
    QCOMPARE(pb::Buyer::recommendedCap(asks, 40), 100000.0);
    QCOMPARE(pb::Buyer::recommendedCap(asks, 100), 101000.0);
    QCOMPARE(pb::Buyer::recommendedCap(asks, 500), 0.0);
    QCOMPARE(pb::Buyer::recommendedCap({{0, 99}, {-1, 1}}, 100), 0.0);
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
