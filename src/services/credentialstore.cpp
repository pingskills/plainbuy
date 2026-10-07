#include "services/credentialstore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <array>
#include <sodium.h>

namespace pb {
namespace {
constexpr char magic[] = "PBENC001";
constexpr qsizetype headerSize = 8 + crypto_pwhash_SALTBYTES +
                                  crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr auto ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

bool deriveKey(const QString &passphrase, const unsigned char *salt,
               std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> *key) {
  QByteArray password = passphrase.toUtf8();
  const bool okay = !password.isEmpty() &&
      crypto_pwhash(key->data(), key->size(), password.constData(),
                    static_cast<unsigned long long>(password.size()), salt,
                    crypto_pwhash_OPSLIMIT_INTERACTIVE,
                    crypto_pwhash_MEMLIMIT_INTERACTIVE,
                    crypto_pwhash_ALG_ARGON2ID13) == 0;
  sodium_memzero(password.data(), static_cast<size_t>(password.size()));
  return okay;
}

void setError(QString *error, const QString &message) {
  if (error) *error = message;
}
} // namespace

QString CredentialStore::defaultPath() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath(QStringLiteral("credentials.enc"));
}

bool CredentialStore::exists(const QString &path) {
  return QFileInfo(path).isFile();
}

bool CredentialStore::save(const QString &path, const QString &passphrase,
                           const Credentials &credentials, QString *error) {
  if (sodium_init() < 0 || passphrase.isEmpty() || credentials.key.isEmpty() ||
      credentials.secret.isEmpty()) {
    setError(error, QStringLiteral("A passphrase and both API credentials are required."));
    return false;
  }
  if (QFileInfo(path).isSymLink()) {
    setError(error, QStringLiteral("The credential path must not be a symbolic link."));
    return false;
  }
  const QString parent = QFileInfo(path).absolutePath();
  const bool newDirectory = !QFileInfo::exists(parent);
  if (!QDir().mkpath(parent) ||
      (newDirectory && !QFile::setPermissions(parent, ownerOnly | QFileDevice::ExeOwner))) {
    setError(error, QStringLiteral("Could not create a private credential directory."));
    return false;
  }
  QByteArray header(magic, 8);
  header.resize(headerSize);
  auto *salt = reinterpret_cast<unsigned char *>(header.data() + 8);
  auto *nonce = salt + crypto_pwhash_SALTBYTES;
  randombytes_buf(salt, crypto_pwhash_SALTBYTES);
  randombytes_buf(nonce, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key{};
  if (!deriveKey(passphrase, salt, &key)) {
    setError(error, QStringLiteral("Could not derive the encryption key."));
    return false;
  }
  QByteArray plaintext = QJsonDocument(QJsonObject{
      {QStringLiteral("key"), QString::fromUtf8(credentials.key)},
      {QStringLiteral("secret"), QString::fromUtf8(credentials.secret)},
      {QStringLiteral("readKey"), QString::fromUtf8(credentials.readKey)},
      {QStringLiteral("readSecret"), QString::fromUtf8(credentials.readSecret)}}).toJson(QJsonDocument::Compact);
  QByteArray ciphertext(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES, 0);
  unsigned long long encryptedSize = 0;
  const int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
      reinterpret_cast<unsigned char *>(ciphertext.data()), &encryptedSize,
      reinterpret_cast<const unsigned char *>(plaintext.constData()), plaintext.size(),
      reinterpret_cast<const unsigned char *>(header.constData()), header.size(),
      nullptr, nonce, key.data());
  sodium_memzero(key.data(), key.size());
  sodium_memzero(plaintext.data(), static_cast<size_t>(plaintext.size()));
  if (result != 0) {
    setError(error, QStringLiteral("Could not encrypt credentials."));
    return false;
  }
  ciphertext.resize(static_cast<qsizetype>(encryptedSize));
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(ownerOnly) ||
      file.write(header) != header.size() || file.write(ciphertext) != ciphertext.size() ||
      !file.commit()) {
    setError(error, QStringLiteral("Could not save the encrypted credential file."));
    return false;
  }
  return true;
}

bool CredentialStore::load(const QString &path, const QString &passphrase,
                           Credentials *credentials, QString *error) {
  if (sodium_init() < 0 || passphrase.isEmpty() || !credentials) {
    setError(error, QStringLiteral("Enter the file passphrase."));
    return false;
  }
  if (QFileInfo(path).isSymLink()) {
    setError(error, QStringLiteral("The credential path must not be a symbolic link."));
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    setError(error, QStringLiteral("Could not open the encrypted credential file."));
    return false;
  }
  const QByteArray data = file.read(65537);
  if (data.size() > 65536 || data.size() < headerSize + crypto_aead_xchacha20poly1305_ietf_ABYTES ||
      data.left(8) != QByteArray(magic, 8)) {
    setError(error, QStringLiteral("Unrecognised or damaged credential file."));
    return false;
  }
  const QByteArray header = data.left(headerSize);
  const auto *salt = reinterpret_cast<const unsigned char *>(header.constData() + 8);
  const auto *nonce = salt + crypto_pwhash_SALTBYTES;
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key{};
  if (!deriveKey(passphrase, salt, &key)) {
    setError(error, QStringLiteral("Could not derive the decryption key."));
    return false;
  }
  const QByteArray ciphertext = data.mid(headerSize);
  QByteArray plaintext(ciphertext.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES, 0);
  unsigned long long decryptedSize = 0;
  const int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
      reinterpret_cast<unsigned char *>(plaintext.data()), &decryptedSize, nullptr,
      reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size(),
      reinterpret_cast<const unsigned char *>(header.constData()), header.size(), nonce, key.data());
  sodium_memzero(key.data(), key.size());
  if (result != 0) {
    sodium_memzero(plaintext.data(), static_cast<size_t>(plaintext.size()));
    setError(error, QStringLiteral("Wrong passphrase or damaged credential file."));
    return false;
  }
  plaintext.resize(static_cast<qsizetype>(decryptedSize));
  const QJsonObject object = QJsonDocument::fromJson(plaintext).object();
  sodium_memzero(plaintext.data(), static_cast<size_t>(plaintext.size()));
  const QByteArray apiKey = object.value(QStringLiteral("key")).toString().toUtf8();
  const QByteArray secret = object.value(QStringLiteral("secret")).toString().toUtf8();
  const QByteArray readKey = object.value(QStringLiteral("readKey")).toString().toUtf8();
  const QByteArray readSecret = object.value(QStringLiteral("readSecret")).toString().toUtf8();
  if (apiKey.isEmpty() || secret.isEmpty()) {
    setError(error, QStringLiteral("Unrecognised or damaged credential file."));
    return false;
  }
  credentials->key = apiKey;
  credentials->secret = secret;
  credentials->readKey = readKey;
  credentials->readSecret = readSecret;
  return true;
}

bool CredentialStore::remove(const QString &path, QString *error) {
  if (!QFileInfo::exists(path)) return true;
  if (QFileInfo(path).isSymLink() || !QFile::remove(path)) {
    setError(error, QStringLiteral("Could not remove the encrypted credential file."));
    return false;
  }
  return true;
}
} // namespace pb
