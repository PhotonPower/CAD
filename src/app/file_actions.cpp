// File handling of the main window: new, open, import STEP, save (docs/plan.md, M2).
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <cstdio>

#include "app/main_window.hpp"
#include "document/document.hpp"
#include "document/nfcad_io.hpp"
#include "kernel/geometry_ops.hpp"
#include "kernel/step_import.hpp"
#include "document/selection.hpp"
#include "tessellation/stl_export.hpp"
#include "render/scene.hpp"
#include "tessellation/tessellation_service.hpp"
#include "viewport/viewport.hpp"
#include "viewport/viewport_grid.hpp"

namespace nfcad::app {

namespace {
const QString kNfcadFilter = QObject::tr("Raytatouille Fusion documents (*.nfcad)");
const QString kStepFilter = QObject::tr("STEP files (*.step *.stp *.STEP *.STP)");

bool is_step(const QString& path) {
  const QString ext = QFileInfo(path).suffix().toLower();
  return ext == QLatin1String("step") || ext == QLatin1String("stp");
}
}  // namespace

void MainWindow::updateTitle() {
  const QString path = doc_->filePath();
  const QString name = path.isEmpty() ? tr("Untitled") : QFileInfo(path).fileName();
  setWindowTitle(name + QStringLiteral("[*] – ") + tr("Raytatouille Fusion"));
  setWindowFilePath(path);
  setWindowModified(doc_->isModified());
}

bool MainWindow::maybeSave() {
  if (!doc_->isModified() || benchmark_frames_ > 0) return true;
  const auto answer = QMessageBox::warning(
      this, tr("Raytatouille Fusion"), tr("The document has been modified. Save the changes?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if (answer == QMessageBox::Save) return save();
  return answer == QMessageBox::Discard;
}

void MainWindow::newDocument() {
  if (!maybeSave()) return;
  doc_->clear();
  updateTitle();
  grid_->zoomExtentsAll();
}

void MainWindow::openDialog() {
  if (!maybeSave()) return;
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open"), QFileInfo(doc_->filePath()).absolutePath(),
      tr("All supported (*.nfcad *.step *.stp *.STEP *.STP)") + QStringLiteral(";;") +
          kNfcadFilter + QStringLiteral(";;") + kStepFilter);
  if (!path.isEmpty()) openFile(path);
}

void MainWindow::importStepDialog() {
  const QString path = QFileDialog::getOpenFileName(this, tr("Import STEP"), {}, kStepFilter);
  if (!path.isEmpty()) importStep(path, /*replace=*/false, /*quiet=*/false);
}

bool MainWindow::openFile(const QString& path, bool quiet) {
  load_timer_.start();
  if (is_step(path)) {
    importStep(path, /*replace=*/true, quiet);
    return true;  // asynchronous; errors are reported when reading finished
  }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const QString error = document::load_nfcad(*doc_, path);
  QApplication::restoreOverrideCursor();
  if (!error.isEmpty()) {
    if (!quiet) {
      QMessageBox::critical(this, tr("Open"), tr("Cannot open %1:\n%2").arg(path, error));
    }
    std::fprintf(stderr, "cannot open %s: %s\n", qPrintable(path), qPrintable(error));
    return false;
  }
  updateTitle();
  zoomExtentsWhenTessellated();
  return true;
}

void MainWindow::importStep(const QString& path, bool replace, bool quiet) {
  if (!load_timer_.isValid() || !replace) load_timer_.start();
  statusBar()->showMessage(tr("Reading %1 …").arg(QFileInfo(path).fileName()));
  QApplication::setOverrideCursor(Qt::BusyCursor);
  auto* watcher = new QFutureWatcher<kernel::StepImportResult>(this);
  connect(watcher, &QFutureWatcher<kernel::StepImportResult>::finished, this,
          [this, watcher, path, replace, quiet] {
            QApplication::restoreOverrideCursor();
            kernel::StepImportResult result = watcher->result();
            watcher->deleteLater();
            if (!result.error.isEmpty()) {
              statusBar()->clearMessage();
              std::fprintf(stderr, "cannot read %s: %s\n", qPrintable(path),
                           qPrintable(result.error));
              if (!quiet) {
                QMessageBox::critical(this, tr("STEP"),
                                      tr("Cannot read %1:\n%2").arg(path, result.error));
              }
              return;
            }
            const std::size_t face_count = result.faceCount();
            std::vector<document::Entity> entities;
            for (kernel::ImportedShell& s : result.shells) {
              entities.push_back(document::make_entity(std::move(s.shell), s.name));
            }
            std::fprintf(stdout, "read %s: %zu shells, %zu faces in %.2f s\n",
                         qPrintable(QFileInfo(path).fileName()), entities.size(),
                         face_count, load_timer_.elapsed() / 1000.0);
            // the shells of a file form one group in the model tree
            const QString group_name = QFileInfo(path).completeBaseName();
            if (replace) {
              for (document::Entity& e : entities) e.group = 1;
              doc_->reset({}, std::move(entities), {{1, group_name, 0, true}});
            } else {
              doc_->undoStack()->beginMacro(tr("Import STEP"));
              const auto ids = doc_->addEntities(std::move(entities), tr("Import STEP"));
              doc_->groupEntities(ids, group_name);
              doc_->undoStack()->endMacro();
            }
            updateTitle();
            if (!result.warnings.isEmpty() && !quiet) {
              QMessageBox::information(
                  this, tr("STEP"),
                  tr("%n warning(s) while reading:\n", nullptr,
                     static_cast<int>(result.warnings.size())) +
                      result.warnings.mid(0, 20).join(QLatin1Char('\n')));
            }
            zoomExtentsWhenTessellated();
          });
  watcher->setFuture(QtConcurrent::run([path] { return kernel::import_step(path); }));
}

void MainWindow::zoomExtentsWhenTessellated() {
  if (tessellator_->pending() == 0) {
    grid_->zoomExtentsAll();
    if (benchmark_frames_ > 0) QTimer::singleShot(500, this, &MainWindow::startBenchmark);
    return;
  }
  zoom_after_tessellation_ = true;
  statusBar()->showMessage(tr("Tessellating …"));
}

bool MainWindow::save() {
  if (doc_->filePath().isEmpty()) return saveAs();
  return saveTo(doc_->filePath());
}

bool MainWindow::saveAs() {
  QString path = QFileDialog::getSaveFileName(this, tr("Save As"), doc_->filePath(), kNfcadFilter);
  if (path.isEmpty()) return false;
  if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".nfcad");
  return saveTo(path);
}

bool MainWindow::saveTo(const QString& path) {
  const QString error = document::save_nfcad(*doc_, path);
  if (!error.isEmpty()) {
    QMessageBox::critical(this, tr("Save"), tr("Cannot save %1:\n%2").arg(path, error));
    return false;
  }
  doc_->setFilePath(path);
  doc_->undoStack()->setClean();
  updateTitle();
  statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 4000);
  return true;
}

// ------------------------------------------------------------------------------- export

std::vector<const document::Entity*> MainWindow::exportEntities() const {
  std::vector<const document::Entity*> out;
  for (const document::SelectionItem& i : selection_->ordered()) {
    if (i.sub.kind != document::SubElement::Kind::None) continue;
    if (const document::Entity* e = doc_->entity(i.entity)) out.push_back(e);
  }
  if (out.empty()) {
    for (const auto& [id, e] : doc_->entities()) {
      if (doc_->isDisplayed(e)) out.push_back(&e);
    }
  }
  return out;
}

void MainWindow::exportDialog(bool stl) {
  const auto entities = exportEntities();
  const QString filter = stl ? tr("STL files (*.stl)") : kStepFilter;
  QString path = QFileDialog::getSaveFileName(this, stl ? tr("Export STL") : tr("Export STEP"),
                                              {}, filter);
  if (path.isEmpty()) return;
  if (QFileInfo(path).suffix().isEmpty()) path += stl ? QStringLiteral(".stl") : QStringLiteral(".step");
  QString error;
  std::size_t written = 0;
  if (stl) {
    std::vector<const tessellation::DisplayMesh*> meshes;
    for (const document::Entity* e : entities) {
      const auto it = scene_->items().find(e->id);
      if (it != scene_->items().end() && it->second.mesh && !it->second.mesh->indices.empty()) {
        meshes.push_back(it->second.mesh.get());
      }
    }
    written = meshes.size();
    error = tessellation::write_stl(path, meshes);
  } else {
    try {
      written = kernel::export_step(path, entities);
    } catch (const std::exception& e) {
      error = QString::fromUtf8(e.what());
    }
  }
  if (!error.isEmpty()) {
    QMessageBox::critical(this, tr("Export"), tr("Cannot export %1:\n%2").arg(path, error));
  } else if (written == 0) {
    statusBar()->showMessage(tr("Nothing to export (STEP: solids and surfaces, STL: objects with faces)"), 6000);
  } else {
    statusBar()->showMessage(tr("Exported %1 object(s) to %2").arg(written).arg(QFileInfo(path).fileName()), 6000);
  }
}

// ------------------------------------------------------------------------------- benchmark

void MainWindow::runBenchmarkWhenReady(int frames) { benchmark_frames_ = frames; }

void MainWindow::startBenchmark() {
  viewport::Viewport* view = grid_->views()[3];  // perspective
  std::fprintf(stdout, "loaded: %zu entities, %zu triangles in %.2f s (read + tessellate)\n",
               doc_->entities().size(), scene_->triangleCount(), load_timer_.elapsed() / 1000.0);
  std::fflush(stdout);
  auto* counter = new int(0);
  auto* timer = new QElapsedTimer;
  auto conn = std::make_shared<QMetaObject::Connection>();
  *conn = connect(view, &viewport::Viewport::frameRendered, this, [=, this] {
    if (*counter == 0) timer->start();
    if (++*counter <= benchmark_frames_) {
      view->orbitBy(QPointF(3.0, 0.0));
      return;
    }
    const double s = timer->elapsed() / 1000.0;
    std::fprintf(stdout, "orbit: %d frames in %.2f s = %.1f fps (view %dx%d)\n", benchmark_frames_,
                 s, benchmark_frames_ / s, view->width(), view->height());
    std::fflush(stdout);
    disconnect(*conn);
    delete counter;
    delete timer;
    QApplication::quit();
  });
  view->orbitBy(QPointF(1.0, 0.0));
}

}  // namespace nfcad::app
