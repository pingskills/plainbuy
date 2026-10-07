#include "services/orderstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <cerrno>
#include <sys/stat.h>

namespace pb {
QString OrderStore::pathForKey(const QByteArray &apiKey) {
  const QByteArray fingerprint = QCryptographicHash::hash(apiKey, QCryptographicHash::Sha256).toHex();
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath(QStringLiteral("orders-%1.json").arg(QString::fromLatin1(fingerprint)));
}

QJsonArray OrderStore::load(const QString &path, bool *okay) {
  if (okay) *okay = false;
  struct stat metadata {};
  if (::lstat(QFile::encodeName(path).constData(), &metadata) != 0) {
    if (errno == ENOENT && okay) *okay = true;
    return {};
  }
  if (!S_ISREG(metadata.st_mode)) return {};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  const QByteArray data = file.read(1048577);
  if (data.size() > 1048576 || file.error() != QFileDevice::NoError) return {};
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) return {};
  const QJsonArray orders = document.array();
  for (const auto &entry : orders) {
    if (!entry.isObject()) return {};
    const QJsonObject order = entry.toObject();
    const bool hasId = !order.value(QStringLiteral("id")).toString().isEmpty();
    const bool hasAttempt = !order.value(QStringLiteral("attemptId")).toString().isEmpty();
    if (!hasId && !hasAttempt) return {};
    if (order.contains(QStringLiteral("pending")) &&
        !order.value(QStringLiteral("pending")).isBool()) return {};
    if (order.value(QStringLiteral("pending")).toBool() && !hasAttempt) return {};
  }
  if (okay) *okay = true;
  return orders;
}

bool OrderStore::save(const QString &path, const QJsonArray &orders) {
  if (QFileInfo(path).isSymLink()) return false;
  const QString parent = QFileInfo(path).absolutePath();
  const bool newDirectory = !QFileInfo::exists(parent);
  if (!QDir().mkpath(parent) ||
      (newDirectory && !QFile::setPermissions(parent, QFileDevice::ReadOwner |
                                         QFileDevice::WriteOwner | QFileDevice::ExeOwner))) return false;
  QSaveFile file(path);
  const QByteArray data = QJsonDocument(orders).toJson(QJsonDocument::Compact);
  return file.open(QIODevice::WriteOnly) &&
         file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
         file.write(data) == data.size() && file.commit();
}
} // namespace pb
