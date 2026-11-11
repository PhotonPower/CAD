#pragma once

#include <QString>
#include <QStringList>

namespace nfcad::support {

/// Local crash reports (M8; nothing is sent anywhere).
///
/// install() prepares a report file name in `folder` and installs handlers for fatal signals
/// (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS), unhandled Windows exceptions and
/// std::terminate. On a crash the handler writes – with async-signal-safe calls only – the
/// application version, the cause and a backtrace to <folder>/crash-<start time>.txt, then lets
/// the process end as it would have.
///
/// After the next start, newReports() lists the reports written since the last call of
/// markSeen() (the application shows them once).
class CrashHandler {
 public:
  static void install(const QString& folder, const QString& version);
  /// Default folder: <app data>/crash-reports.
  [[nodiscard]] static QString defaultFolder();
  [[nodiscard]] static QStringList newReports(const QString& folder);
  static void markSeen(const QString& folder);
};

}  // namespace nfcad::support
