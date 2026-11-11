#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <map>

namespace nfcad::plugins {

/// Plugin signatures (docs/plugins.md "Signing", decision D3; ADR 0009).
///
/// A signed plugin folder contains a file `SIGNATURE` (JSON):
///   { "format": "nfcad-plugin-signature", "version": 1,
///     "key": "ed25519:<base64 public key>",
///     "files": { "plugin.json": "<sha256 hex>", "demo_plugin.py": "<sha256 hex>", ... },
///     "signature": "<base64 Ed25519 signature of signed_message(files)>" }
/// Only the listed files belong to the plugin: CAD copies exactly these into a private
/// directory, checks them there and runs the plugin from the copy.

inline constexpr auto kSignatureFile = "SIGNATURE";

/// Public keys built into the application (keys/trusted-publishers.txt), "ed25519:<base64>".
[[nodiscard]] QStringList built_in_publisher_keys();

/// "ed25519:<base64>" from a key line (comments after whitespace are ignored); empty if invalid.
[[nodiscard]] QByteArray parse_public_key(const QString& line);
[[nodiscard]] QString format_public_key(const QByteArray& key);

/// Text that is signed: a header line and "<sha256 hex>  <path>" per file, sorted by path.
[[nodiscard]] QByteArray signed_message(const std::map<QString, QByteArray>& sha256_by_path);

/// Files of a plugin folder to sign: all regular files except SIGNATURE, __pycache__, *.pyc
/// and hidden files; relative paths with '/'.
[[nodiscard]] QStringList plugin_files(const QString& dir);

struct KeyPair {
  QByteArray seed;        ///< 32 bytes, the secret
  QByteArray public_key;  ///< 32 bytes
};
[[nodiscard]] KeyPair generate_key_pair();
[[nodiscard]] QByteArray public_key_from_seed(const QByteArray& seed);
/// Secret key file: "nfcad-plugin-secret-key ed25519 <base64 seed>"; empty seed on errors.
[[nodiscard]] QByteArray read_secret_key(const QString& path, QString* error = nullptr);
[[nodiscard]] bool write_secret_key(const QString& path, const QByteArray& seed, QString* error = nullptr);

/// Writes <dir>/SIGNATURE over plugin_files(dir). Returns an error message or empty.
[[nodiscard]] QString sign_plugin(const QString& dir, const QByteArray& seed);

struct Verification {
  enum class Status { Valid, Unsigned, Invalid, UntrustedKey };
  Status status = Status::Unsigned;
  QString message;       ///< for the user; empty if valid
  QString key;           ///< "ed25519:…" of the signer (if a SIGNATURE was readable)
  QStringList files;     ///< signed files (relative)
  [[nodiscard]] bool ok() const { return status == Status::Valid; }
};

/// Checks the plugin in `dir` against `trusted` keys. With a non-empty `copy_to`, the signed
/// files are copied there first and the copy is what gets checked (no change between check
/// and use); files not listed in SIGNATURE are not copied.
[[nodiscard]] Verification verify_plugin(const QString& dir, const QStringList& trusted,
                                         const QString& copy_to = {});

}  // namespace nfcad::plugins
