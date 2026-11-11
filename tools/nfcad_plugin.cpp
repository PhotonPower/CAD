// nfcad-plugin – keys and signatures for Raytatouille Fusion plugins (docs/plugin-api.md).
//
//   nfcad-plugin keygen <secret-key-file>       new key pair; prints the public key line
//   nfcad-plugin pubkey <secret-key-file>       prints the public key line
//   nfcad-plugin sign   <plugin-dir> <secret-key-file>
//   nfcad-plugin verify <plugin-dir> [ed25519:<key> …]   default: the built-in publisher keys
//   nfcad-plugin pack   <plugin-dir> [<file>.nfplugin]   package for Plugins → Install plugin…
#include <QCoreApplication>
#include <QTextStream>

#include "plugins/distribution.hpp"
#include "plugins/signature.hpp"

using namespace nfcad::plugins;

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QTextStream out(stdout);
  QTextStream err(stderr);
  const QStringList a = app.arguments().mid(1);
  const QString cmd = a.value(0);
  QString error;
  if (cmd == QLatin1String("keygen") && a.size() == 2) {
    const KeyPair k = generate_key_pair();
    if (!write_secret_key(a[1], k.seed, &error)) {
      err << error << '\n';
      return 1;
    }
    out << format_public_key(k.public_key) << "  publisher\n";
    err << "Secret key written to " << a[1] << " (keep it private, back it up).\n"
        << "Put the line above into keys/trusted-publishers.txt.\n";
    return 0;
  }
  if (cmd == QLatin1String("pubkey") && a.size() == 2) {
    const QByteArray seed = read_secret_key(a[1], &error);
    if (seed.isEmpty()) {
      err << error << '\n';
      return 1;
    }
    out << format_public_key(public_key_from_seed(seed)) << '\n';
    return 0;
  }
  if (cmd == QLatin1String("sign") && a.size() == 3) {
    const QByteArray seed = read_secret_key(a[2], &error);
    if (seed.isEmpty()) {
      err << error << '\n';
      return 1;
    }
    error = sign_plugin(a[1], seed);
    if (!error.isEmpty()) {
      err << error << '\n';
      return 1;
    }
    out << "signed " << plugin_files(a[1]).size() << " files of " << a[1] << '\n';
    return 0;
  }
  if (cmd == QLatin1String("verify") && a.size() >= 2) {
    const QStringList keys = a.size() > 2 ? a.mid(2) : built_in_publisher_keys();
    const Verification v = verify_plugin(a[1], keys);
    if (!v.ok()) {
      err << a[1] << ": " << v.message << '\n';
      return 1;
    }
    out << a[1] << ": valid (" << v.files.size() << " files, " << v.key << ")\n";
    return 0;
  }
  if (cmd == QLatin1String("pack") && (a.size() == 2 || a.size() == 3)) {
    const auto m = read_manifest(a[1], &error);
    if (!m) {
      err << error << '\n';
      return 1;
    }
    const QString file = a.size() == 3 ? a[2] : QStringLiteral("%1-%2%3").arg(m->id, m->version, QLatin1String(kPackageSuffix));
    const Verification v = verify_plugin(a[1], built_in_publisher_keys());
    if (!v.ok()) err << "note: " << v.message << " – installs only in developer mode\n";
    error = pack_plugin(a[1], file);
    if (!error.isEmpty()) {
      err << error << '\n';
      return 1;
    }
    out << "wrote " << file << '\n';
    return 0;
  }
  err << "usage: nfcad-plugin keygen <key-file> | pubkey <key-file> | sign <plugin-dir> <key-file> | "
         "verify <plugin-dir> [ed25519:<key> …] | pack <plugin-dir> [<file>.nfplugin]\n";
  return 2;
}
