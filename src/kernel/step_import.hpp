#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include <nurbforge/topology/shell.hpp>

namespace nfcad::kernel {

struct ImportedShell {
  QString name;
  nf::Shell shell;
};

struct StepImportResult {
  std::vector<ImportedShell> shells;
  QStringList warnings;
  QString error;  ///< non-empty if reading failed
  [[nodiscard]] std::size_t faceCount() const;
};

/// Reads a STEP file (AP203/AP214 B-spline and analytic faces) into Nurbforge shells, in
/// millimetres. Unsupported entities are skipped with a warning. Thread-safe.
[[nodiscard]] StepImportResult import_step(const QString& path);

}  // namespace nfcad::kernel
