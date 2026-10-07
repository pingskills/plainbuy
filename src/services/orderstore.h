#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QString>

namespace pb {
class OrderStore {
public:
  static QString pathForKey(const QByteArray &apiKey);
  // A missing journal is valid and loads as an empty array. A damaged or
  // unreadable existing journal sets okay to false.
  static QJsonArray load(const QString &path, bool *okay = nullptr);
  static bool save(const QString &path, const QJsonArray &orders);
};
} // namespace pb
