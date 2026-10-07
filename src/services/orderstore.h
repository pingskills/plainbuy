#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QString>

namespace pb {
class OrderStore {
public:
  static QString pathForKey(const QByteArray &apiKey);
  static QJsonArray load(const QString &path);
  static bool save(const QString &path, const QJsonArray &orders);
};
} // namespace pb
