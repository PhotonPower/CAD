#include "plugins/signature.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <array>

extern "C" {
#include <monocypher-ed25519.h>
#include <monocypher.h>
}

namespace nfcad::plugins {

namespace {
constexpr auto kKeyPrefix = "ed25519:";
constexpr auto kSecretHeader = "nfcad-plugin-secret-key ed25519 ";
constexpr int kSeedSize = 32;
constexpr int kPublicSize = 32;
constexpr int kSignatureSize = 64;

const auto* bytes(const QByteArray& a) { return reinterpret_cast<const uint8_t*>(a.constData()); }

/// Relative path from SIGNATURE that stays inside the plugin folder.
bool safe_relative_path(const QString& p) {
  if (p.isEmpty() || p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char('\\')) ||
      p.contains(QLatin1Char(':')) || QDir::isAbsolutePath(p)) {
    return false;
  }
  const QStringList parts = p.split(QLatin1Char('/'));
  return std::none_of(parts.begin(), parts.end(), [](const QString& s) {
    return s.isEmpty() || s == QLatin1String(".") || s == QLatin1String("..");
  });
}

QByteArray sign(const QByteArray& seed, const QByteArray& message) {
  std::array<uint8_t, 32> s{};
  std::copy_n(bytes(seed), kSeedSize, s.begin());
  std::array<uint8_t, 64> secret{};
  std::array<uint8_t, 32> pub{};
  crypto_ed25519_key_pair(secret.data(), pub.data(), s.data());  // wipes s
  QByteArray signature(kSignatureSize, '\0');
  crypto_ed25519_sign(reinterpret_cast<uint8_t*>(signature.data()), secret.data(), bytes(message),
                      static_cast<size_t>(message.size()));
  crypto_wipe(secret.data(), secret.size());
  return signature;
}

Verification fail(Verification::Status status, const QString& message, Verification v = {}) {
  v.status = status;
  v.message = message;
  return v;
}
}  // namespace

QByteArray parse_public_key(const QString& line) {
  const QString token = line.trimmed().section(QRegularExpression(QStringLiteral("\\s+")), 0, 0);
  if (!token.startsWith(QLatin1String(kKeyPrefix))) return {};
  const auto decoded = QByteArray::fromBase64Encoding(token.mid(8).toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || decoded.decoded.size() != kPublicSize) return {};
  return decoded.decoded;
}

QString format_public_key(const QByteArray& key) {
  return QLatin1String(kKeyPrefix) + QString::fromLatin1(key.toBase64());
}

QByteArray signed_message(const std::map<QString, QByteArray>& sha256_by_path) {
  QByteArray m("nfcad-plugin-signature v1\n");
  for (const auto& [path, hash] : sha256_by_path) m += hash.toHex() + "  " + path.toUtf8() + '\n';
  return m;
}

QStringList plugin_files(const QString& dir) {
  QStringList out;
  const QDir root(dir);
  QDirIterator it(dir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString rel = root.relativeFilePath(it.next());
    const QStringList parts = rel.split(QLatin1Char('/'));
    const bool skip = rel == QLatin1String(kSignatureFile) || rel.endsWith(QLatin1String(".pyc")) ||
                      parts.contains(QStringLiteral("__pycache__")) ||
                      std::any_of(parts.begin(), parts.end(), [](const QString& s) { return s.startsWith(QLatin1Char('.')); });
    if (!skip) out << rel;
  }
  out.sort();
  return out;
}

KeyPair generate_key_pair() {
  KeyPair k;
  k.seed.resize(kSeedSize);
  // QRandomGenerator::system() uses the operating system's cryptographically safe source
  QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(k.seed.data()), kSeedSize / 4);
  k.public_key = public_key_from_seed(k.seed);
  return k;
}

QByteArray public_key_from_seed(const QByteArray& seed) {
  if (seed.size() != kSeedSize) return {};
  std::array<uint8_t, 32> s{};
  std::copy_n(bytes(seed), kSeedSize, s.begin());
  std::array<uint8_t, 64> secret{};
  QByteArray pub(kPublicSize, '\0');
  crypto_ed25519_key_pair(secret.data(), reinterpret_cast<uint8_t*>(pub.data()), s.data());
  crypto_wipe(secret.data(), secret.size());
  return pub;
}

QByteArray read_secret_key(const QString& path, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = QObject::tr("cannot read %1").arg(path);
    return {};
  }
  const QByteArray line = f.readAll().trimmed();
  const QByteArray header(kSecretHeader);
  const auto decoded = QByteArray::fromBase64Encoding(line.mid(header.size()), QByteArray::AbortOnBase64DecodingErrors);
  if (!line.startsWith(header) || !decoded || decoded.decoded.size() != kSeedSize) {
    if (error) *error = QObject::tr("%1 is not a plugin signing key").arg(path);
    return {};
  }
  return decoded.decoded;
}

bool write_secret_key(const QString& path, const QByteArray& seed, QString* error) {
  if (QFileInfo::exists(path)) {
    if (error) *error = QObject::tr("%1 exists – not overwritten").arg(path);
    return false;
  }
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) {
    if (error) *error = QObject::tr("cannot write %1").arg(path);
    return false;
  }
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);  // 0600
  f.write(QByteArray(kSecretHeader) + seed.toBase64() + '\n');
  if (!f.commit()) {
    if (error) *error = QObject::tr("cannot write %1").arg(path);
    return false;
  }
  return true;
}

QString sign_plugin(const QString& dir, const QByteArray& seed) {
  if (seed.size() != kSeedSize) return QObject::tr("invalid secret key");
  if (!QFileInfo::exists(QDir(dir).filePath(QStringLiteral("plugin.json")))) {
    return QObject::tr("no plugin.json in %1").arg(dir);
  }
  std::map<QString, QByteArray> hashes;
  QJsonObject files;
  for (const QString& rel : plugin_files(dir)) {
    QFile f(QDir(dir).filePath(rel));
    if (!f.open(QIODevice::ReadOnly)) return QObject::tr("cannot read %1").arg(rel);
    const QByteArray h = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
    hashes[rel] = h;
    files.insert(rel, QString::fromLatin1(h.toHex()));
  }
  const QJsonObject sig{{"format", "nfcad-plugin-signature"},
                        {"version", 1},
                        {"key", format_public_key(public_key_from_seed(seed))},
                        {"files", files},
                        {"signature", QString::fromLatin1(sign(seed, signed_message(hashes)).toBase64())}};
  QSaveFile out(QDir(dir).filePath(QLatin1String(kSignatureFile)));
  if (!out.open(QIODevice::WriteOnly)) return QObject::tr("cannot write SIGNATURE");
  out.write(QJsonDocument(sig).toJson(QJsonDocument::Indented));
  return out.commit() ? QString() : QObject::tr("cannot write SIGNATURE");
}

Verification verify_plugin(const QString& dir, const QStringList& trusted, const QString& copy_to) {
  using S = Verification::Status;
  QFile f(QDir(dir).filePath(QLatin1String(kSignatureFile)));
  if (!f.exists()) return fail(S::Unsigned, QObject::tr("not signed"));
  if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) return fail(S::Invalid, QObject::tr("cannot read SIGNATURE"));
  const QJsonObject sig = QJsonDocument::fromJson(f.readAll()).object();
  Verification v;
  v.key = sig["key"].toString();
  const QByteArray key = parse_public_key(v.key);
  const auto signature = QByteArray::fromBase64Encoding(sig["signature"].toString().toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
  if (sig["format"].toString() != QLatin1String("nfcad-plugin-signature") || sig["version"].toInt() != 1 ||
      key.isEmpty() || !signature || signature.decoded.size() != kSignatureSize) {
    return fail(S::Invalid, QObject::tr("SIGNATURE is malformed"), v);
  }
  const bool known = std::any_of(trusted.begin(), trusted.end(),
                                 [&](const QString& t) { return parse_public_key(t) == key; });
  if (!known) return fail(S::UntrustedKey, QObject::tr("signed with an unknown key (%1)").arg(v.key), v);

  std::map<QString, QByteArray> hashes;
  const QJsonObject files = sig["files"].toObject();
  if (!files.contains(QStringLiteral("plugin.json"))) return fail(S::Invalid, QObject::tr("plugin.json is not signed"), v);
  for (auto it = files.begin(); it != files.end(); ++it) {
    const QString rel = it.key();
    if (!safe_relative_path(rel)) return fail(S::Invalid, QObject::tr("invalid path in SIGNATURE: %1").arg(rel), v);
    QFile in(QDir(dir).filePath(rel));
    if (!in.open(QIODevice::ReadOnly)) return fail(S::Invalid, QObject::tr("signed file missing: %1").arg(rel), v);
    const QByteArray content = in.readAll();
    const QByteArray h = QCryptographicHash::hash(content, QCryptographicHash::Sha256);
    if (QString::fromLatin1(h.toHex()) != it.value().toString()) {
      return fail(S::Invalid, QObject::tr("modified after signing: %1").arg(rel), v);
    }
    hashes[rel] = h;
    if (!copy_to.isEmpty()) {
      // the copy holds exactly the bytes that were hashed
      const QString target = QDir(copy_to).filePath(rel);
      QDir().mkpath(QFileInfo(target).absolutePath());
      QFile out(target);
      if (!out.open(QIODevice::WriteOnly) || out.write(content) != content.size()) {
        return fail(S::Invalid, QObject::tr("cannot copy %1").arg(rel), v);
      }
    }
    v.files << rel;
  }
  const QByteArray message = signed_message(hashes);
  if (crypto_ed25519_check(bytes(signature.decoded), bytes(key), bytes(message), static_cast<size_t>(message.size())) != 0) {
    return fail(S::Invalid, QObject::tr("signature does not match"), v);
  }
  v.status = S::Valid;
  return v;
}

}  // namespace nfcad::plugins
