#include "kernel/step_import.hpp"

#include <QFileInfo>
#include <exception>
#include <nurbforge/io/step.hpp>

namespace nfcad::kernel {

std::size_t StepImportResult::faceCount() const {
  std::size_t n = 0;
  for (const ImportedShell& s : shells) n += s.shell.faces().size();
  return n;
}

StepImportResult import_step(const QString& path) {
  StepImportResult result;
  std::vector<std::string> warnings;
  nf::StepReadOptions options;
  options.unit = nf::StepLengthUnit::Millimetre;
  options.skip_unsupported = true;
  options.warnings = &warnings;
  try {
    auto named = nf::read_step_named(path.toStdString(), options);
    const QString base = QFileInfo(path).completeBaseName();
    int index = 1;
    for (nf::NamedShell& n : named) {
      QString name = QString::fromStdString(n.name).trimmed();
      if (name.isEmpty()) name = QStringLiteral("%1 %2").arg(base).arg(index);
      result.shells.push_back({name, std::move(n.shell)});
      ++index;
    }
  } catch (const std::exception& e) {
    result.error = QString::fromUtf8(e.what());
  }
  for (const std::string& w : warnings) result.warnings << QString::fromStdString(w);
  return result;
}

}  // namespace nfcad::kernel
