#include "plugins/distribution.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

namespace nfcad::plugins {

namespace {
constexpr qint64 kMaxFileSize = 64LL * 1024 * 1024;    // one file in a package
constexpr qint64 kMaxTotalSize = 256LL * 1024 * 1024;  // a whole package, unpacked
constexpr int kMaxFiles = 5000;

bool safe_relative(const QString& p) {
  if (p.isEmpty() || p.startsWith(QLatin1Char('/')) || p.contains(QLatin1Char('\\')) || p.contains(QLatin1Char(':'))) {
    return false;
  }
  const QStringList parts = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  return !parts.isEmpty() && !parts.contains(QStringLiteral("..")) && !parts.contains(QStringLiteral("."));
}

std::pair<QList<int>, QString> numeric(const QString& v) {
  static const QRegularExpression re(QStringLiteral("^v?([0-9]+(?:\\.[0-9]+)*)(.*)$"));
  const auto m = re.match(v.trimmed());
  QList<int> parts;
  if (!m.hasMatch()) return {parts, v};
  for (const QString& p : m.captured(1).split(QLatin1Char('.'))) parts << p.toInt();
  return {parts, m.captured(2)};
}

/// Copies a directory tree (rename across file systems is not possible).
bool copy_tree(const QString& from, const QString& to) {
  if (!QDir().mkpath(to)) return false;
  QDirIterator it(from, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString src = it.next();
    const QString dst = QDir(to).filePath(QDir(from).relativeFilePath(src));
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (!QFile::copy(src, dst)) return false;
  }
  return true;
}

constexpr auto kCheckScript = R"PY(
import sys, json, importlib, importlib.metadata as md
out = {"python": sys.version.split()[0], "packages": {}}
for name in json.loads(sys.argv[1]):
    try:
        m = importlib.import_module(name)
        v = getattr(m, "__version__", None)
        if not isinstance(v, str):
            try:
                v = md.version(name)
            except Exception:
                v = ""
        out["packages"][name] = {"found": True, "version": v}
    except Exception as e:
        out["packages"][name] = {"found": False, "error": f"{type(e).__name__}: {e}"}
print(json.dumps(out))
)PY";
}  // namespace

int compare_versions(const QString& a, const QString& b) {
  const auto [pa, sa] = numeric(a);
  const auto [pb, sb] = numeric(b);
  for (qsizetype i = 0; i < std::max(pa.size(), pb.size()); ++i) {
    const int x = pa.value(i, 0);
    const int y = pb.value(i, 0);
    if (x != y) return x < y ? -1 : 1;
  }
  if (sa == sb) return 0;
  if (sa.isEmpty()) return 1;  // 1.0 > 1.0-rc1
  if (sb.isEmpty()) return -1;
  return sa < sb ? -1 : 1;
}

std::optional<Requirement> parse_requirement(const QString& text) {
  static const QRegularExpression re(QStringLiteral("^\\s*([A-Za-z_][A-Za-z0-9_.]*)\\s*(?:(>=|==)\\s*([0-9][0-9A-Za-z.+-]*))?\\s*$"));
  const auto m = re.match(text);
  if (!m.hasMatch()) return std::nullopt;
  return Requirement{m.captured(1), m.captured(2), m.captured(3)};
}

QString pack_plugin(const QString& dir, const QString& package) {
  QString error;
  const auto manifest = read_manifest(dir, &error);
  if (!manifest) return error;
  QStringList files = plugin_files(dir);
  if (QFileInfo::exists(QDir(dir).filePath(QLatin1String(kSignatureFile)))) files << QLatin1String(kSignatureFile);
  QFile out(package);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QObject::tr("cannot write %1").arg(package);
  QZipWriter zip(&out);
  zip.setCompressionPolicy(QZipWriter::AutoCompress);
  for (const QString& rel : files) {
    QFile f(QDir(dir).filePath(rel));
    if (!f.open(QIODevice::ReadOnly)) return QObject::tr("cannot read %1").arg(rel);
    zip.addFile(manifest->id + QLatin1Char('/') + rel, f.readAll());  // one top-level folder = the id
  }
  zip.close();
  return zip.status() == QZipWriter::NoError ? QString() : QObject::tr("cannot write %1").arg(package);
}

Installation install_plugin(const QString& package, const QString& plugins_root, const QStringList& trusted,
                            bool allow_unsigned) {
  Installation r;
  auto fail = [&](const QString& m) {
    r.error = m;
    return r;
  };
  QZipReader zip(package);
  if (!zip.isReadable() || zip.status() != QZipReader::NoError) return fail(QObject::tr("%1 is not a plugin package").arg(package));
  const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
  if (entries.size() > kMaxFiles) return fail(QObject::tr("too many files in the package"));
  QTemporaryDir staging;
  if (!staging.isValid()) return fail(QObject::tr("no temporary folder"));
  qint64 total = 0;
  for (const QZipReader::FileInfo& e : entries) {
    if (e.isDir) continue;
    if (e.isSymLink || !safe_relative(e.filePath)) return fail(QObject::tr("invalid path in the package: %1").arg(e.filePath));
    if (e.size > kMaxFileSize || (total += e.size) > kMaxTotalSize) return fail(QObject::tr("package too large"));
    const QByteArray data = zip.fileData(e.filePath);
    if (data.size() != e.size) return fail(QObject::tr("damaged package: %1").arg(e.filePath));
    const QString target = staging.filePath(e.filePath);
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) return fail(QObject::tr("cannot unpack %1").arg(e.filePath));
  }
  // plugin.json at the top, or in the only top-level folder
  QString root = staging.path();
  if (!QFileInfo::exists(QDir(root).filePath(QStringLiteral("plugin.json")))) {
    const QStringList dirs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QStringList files = QDir(root).entryList(QDir::Files);
    if (dirs.size() != 1 || !files.isEmpty()) return fail(QObject::tr("the package has no plugin.json"));
    root = QDir(root).filePath(dirs.front());
  }
  QString error;
  const auto manifest = read_manifest(root, &error);
  if (!manifest) return fail(error);
  r.signature = verify_plugin(root, trusted);
  if (!r.signature.ok() && !allow_unsigned) {
    return fail(QObject::tr("%1: %2 – only signed plugins can be installed (unsigned ones in developer mode)")
                    .arg(manifest->name, r.signature.message));
  }
  // replace an installed version
  const QString target = QDir(plugins_root).filePath(manifest->id);
  if (QFileInfo::exists(target)) {
    if (const auto old = read_manifest(target)) r.replaced = old->version.isEmpty() ? QStringLiteral("?") : old->version;
    if (!QDir(target).removeRecursively()) return fail(QObject::tr("cannot remove the installed version in %1").arg(target));
  }
  if (!QDir().mkpath(plugins_root) || !copy_tree(root, target)) return fail(QObject::tr("cannot install into %1").arg(target));
  r.directory = target;
  r.manifest = *read_manifest(target);
  return r;
}

QString uninstall_plugin(const QString& plugins_root, const QString& id) {
  static const QRegularExpression valid_id(QStringLiteral("^[a-z0-9][a-z0-9_.-]{0,63}$"));
  if (!valid_id.match(id).hasMatch()) return QObject::tr("invalid plugin id");
  const QString target = QDir(plugins_root).filePath(id);
  if (!QFileInfo::exists(QDir(target).filePath(QStringLiteral("plugin.json")))) {
    return QObject::tr("%1 is not an installed plugin").arg(id);
  }
  return QDir(target).removeRecursively() ? QString() : QObject::tr("cannot remove %1").arg(target);
}

bool EnvironmentReport::ok() const {
  return error.isEmpty() && std::all_of(packages.begin(), packages.end(), [](const PackageStatus& p) { return p.satisfied; });
}

QString EnvironmentReport::pipCommand() const {
  QStringList missing;
  for (const PackageStatus& p : packages) {
    if (p.satisfied) continue;
    missing << (p.requirement.op.isEmpty() ? p.requirement.name
                                           : QStringLiteral("\"%1%2%3\"").arg(p.requirement.name, p.requirement.op,
                                                                              p.requirement.version));
  }
  if (missing.isEmpty() || python.isEmpty()) return {};
  return QStringLiteral("\"%1\" -m pip install %2").arg(QDir::toNativeSeparators(python), missing.join(QLatin1Char(' ')));
}

EnvironmentReport check_environment(const QString& python, const QStringList& requirements, int timeout_ms) {
  EnvironmentReport r;
  r.python = python;
  QStringList names;
  for (const QString& text : requirements) {
    PackageStatus s;
    if (const auto req = parse_requirement(text)) {
      s.requirement = *req;
      names << req->name;
    } else {
      s.requirement.name = text;
      s.error = QObject::tr("invalid requirement");
    }
    r.packages.push_back(s);
  }
  QProcess p;
  p.start(python, {QStringLiteral("-I"), QStringLiteral("-c"), QString::fromLatin1(kCheckScript),
                   QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(names)).toJson(QJsonDocument::Compact))});
  if (!p.waitForFinished(timeout_ms) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
    p.kill();
    r.error = p.error() == QProcess::FailedToStart ? QObject::tr("%1 cannot be started").arg(python)
                                                   : QObject::tr("%1 does not run: %2").arg(python, QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
    return r;
  }
  const QJsonObject out = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
  r.python_version = out["python"].toString();
  if (compare_versions(r.python_version, QStringLiteral("3.10")) < 0) {
    r.error = QObject::tr("Python %1 is too old (3.10 or newer needed)").arg(r.python_version);
  }
  const QJsonObject packages = out["packages"].toObject();
  for (PackageStatus& s : r.packages) {
    if (!s.error.isEmpty()) continue;
    const QJsonObject o = packages[s.requirement.name].toObject();
    s.found = o["found"].toBool();
    s.version = o["version"].toString();
    s.error = o["error"].toString();
    s.satisfied = s.found;
    if (s.found && !s.requirement.op.isEmpty()) {
      const int c = compare_versions(s.version, s.requirement.version);
      s.satisfied = s.requirement.op == QLatin1String(">=") ? c >= 0 : c == 0;
      if (!s.satisfied) s.error = QObject::tr("version %1 installed, %2%3 needed").arg(s.version, s.requirement.op, s.requirement.version);
    }
  }
  return r;
}

}  // namespace nfcad::plugins
