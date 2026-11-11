#include "support/crash_handler.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>
#include <QCoreApplication>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nfcad::support {

namespace {
// Everything the handler needs is prepared before a crash (no allocation in the handler).
std::array<char, 4096> g_path{};
std::array<char, 1024> g_header{};
volatile std::sig_atomic_t g_in_handler = 0;

int open_report() {
#ifdef Q_OS_WIN
  return _open(g_path.data(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, 0600);
#else
  return ::open(g_path.data(), O_WRONLY | O_CREAT | O_APPEND, 0600);
#endif
}

void put(int fd, const char* text) {
  if (fd < 0 || text == nullptr) return;
#ifdef Q_OS_WIN
  _write(fd, text, static_cast<unsigned>(std::strlen(text)));
#else
  [[maybe_unused]] const auto n = ::write(fd, text, std::strlen(text));
#endif
}

void close_report(int fd) {
  if (fd < 0) return;
#ifdef Q_OS_WIN
  _close(fd);
#else
  ::close(fd);
#endif
}

void write_report(const char* cause) {
  if (g_in_handler != 0) return;  // a crash inside the handler
  g_in_handler = 1;
  const int fd = open_report();
  put(fd, g_header.data());
  put(fd, "Cause: ");
  put(fd, cause);
  put(fd, "\nBacktrace:\n");
#ifdef Q_OS_WIN
  std::array<void*, 64> frames{};
  const USHORT n = CaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
  for (USHORT i = 0; i < n; ++i) {
    std::array<char, 32> line{};
    std::snprintf(line.data(), line.size(), "  %p\n", frames[i]);  // module offsets: see the .pdb
    put(fd, line.data());
  }
#else
  std::array<void*, 64> frames{};
  const int n = backtrace(frames.data(), static_cast<int>(frames.size()));
  if (fd >= 0) backtrace_symbols_fd(frames.data(), n, fd);
#endif
  put(fd, "\n");
  close_report(fd);
}

const char* signal_name(int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGFPE: return "SIGFPE (arithmetic error)";
    case SIGILL: return "SIGILL (illegal instruction)";
#ifdef SIGBUS
    case SIGBUS: return "SIGBUS (bus error)";
#endif
    default: return "signal";
  }
}

void on_signal(int sig) {
  write_report(signal_name(sig));
  std::signal(sig, SIG_DFL);  // end as the process would have
  std::raise(sig);
}

void on_terminate() {
  write_report("std::terminate (uncaught exception)");
  std::signal(SIGABRT, SIG_DFL);
  std::abort();
}

#ifdef Q_OS_WIN
LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
  std::array<char, 96> cause{};
  const unsigned long code = info != nullptr ? info->ExceptionRecord->ExceptionCode : 0UL;
  // 0xE06D7363 ("msc"): how MSVC throws C++ exceptions – an uncaught one arrives here before
  // std::terminate would be called
  std::snprintf(cause.data(), cause.size(), code == 0xE06D7363UL ? "uncaught C++ exception (Windows exception 0x%08lx)"
                                                                 : "Windows exception 0x%08lx",
                code);
  write_report(cause.data());
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void copy(std::array<char, 4096>& to, const QByteArray& from) {
  std::memset(to.data(), 0, to.size());
  std::memcpy(to.data(), from.constData(), std::min<std::size_t>(from.size(), to.size() - 1));
}
}  // namespace

QString CrashHandler::defaultFolder() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("crash-reports"));
}

void CrashHandler::install(const QString& folder, const QString& version) {
  QDir().mkpath(folder);
  const QString started = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
  const QString path = QDir(folder).filePath(QStringLiteral("crash-%1-%2.txt").arg(started).arg(QCoreApplication::applicationPid()));
  copy(g_path, QFile::encodeName(QDir::toNativeSeparators(path)));
  const QByteArray header = QStringLiteral("Raytatouille Fusion %1 crash report (local only, not sent)\nStarted: %2 UTC\nSystem: %3 (%4)\n")
                                .arg(version, started, QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture())
                                .toUtf8();
  std::memset(g_header.data(), 0, g_header.size());
  std::memcpy(g_header.data(), header.constData(), std::min<std::size_t>(header.size(), g_header.size() - 1));
#ifndef Q_OS_WIN
  {  // load the unwinder now: backtrace() may allocate on its first call
    std::array<void*, 2> warm{};
    backtrace(warm.data(), 2);
  }
#endif
  for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL}) std::signal(sig, on_signal);
#ifdef SIGBUS
  std::signal(SIGBUS, on_signal);
#endif
  std::set_terminate(on_terminate);
#ifdef Q_OS_WIN
  SetUnhandledExceptionFilter(on_exception);
#endif
}

QStringList CrashHandler::newReports(const QString& folder) {
  const QDir dir(folder);
  QDateTime seen;
  if (QFile f(dir.filePath(QStringLiteral(".seen"))); f.open(QIODevice::ReadOnly)) {
    seen = QDateTime::fromString(QString::fromLatin1(f.readAll()).trimmed(), Qt::ISODateWithMs);
  }
  QStringList out;
  for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time)) {
    if (!seen.isValid() || fi.lastModified() > seen) out << fi.absoluteFilePath();
  }
  return out;
}

void CrashHandler::markSeen(const QString& folder) {
  QDir().mkpath(folder);
  QFile f(QDir(folder).filePath(QStringLiteral(".seen")));
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toLatin1());
  }
}

}  // namespace nfcad::support
