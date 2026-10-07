#pragma once
#include <QByteArray>
#include <QString>

namespace pb {
struct Credentials { QByteArray key; QByteArray secret; QByteArray readKey; QByteArray readSecret; };
class CredentialStore {
public:
  static QString defaultPath();
  static bool exists(const QString &path);
  static bool save(const QString &path, const QString &passphrase,
                   const Credentials &credentials, QString *error);
  static bool load(const QString &path, const QString &passphrase,
                   Credentials *credentials, QString *error);
  static bool remove(const QString &path, QString *error);
};
} // namespace pb
