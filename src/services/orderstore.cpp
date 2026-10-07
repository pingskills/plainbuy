#include "services/orderstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace pb {
QString OrderStore::pathForKey(const QByteArray &apiKey) {
  const QByteArray fingerprint = QCryptographicHash::hash(apiKey, QCryptographicHash::Sha256).toHex();
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath(QStringLiteral("orders-%1.json").arg(QString::fromLatin1(fingerprint)));
}

QJsonArray OrderStore::load(const QString &path) {
  if (QFileInfo(path).isSymLink()) return {};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  const QByteArray data = file.read(1048577);
  if (data.size() > 1048576) return {};
  return QJsonDocument::fromJson(data).array();
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
