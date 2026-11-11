#pragma once

#include <QObject>
#include <functional>
#include <memory>

#include "commands/command.hpp"

class QLineEdit;
class QStatusBar;

namespace nfcad::document {
class Document;
class Selection;
}  // namespace nfcad::document
namespace nfcad::viewport {
class Viewport;
class ViewportGrid;
}  // namespace nfcad::viewport

namespace nfcad::app {

/// Runs interactive commands (docs/commands.md): starts them from tools or typed names, feeds
/// clicked (snapped) and typed points / numbers, applies ortho, shows prompts and the
/// rubber band, adds the results to the document (one undo step per result).
class CommandController : public QObject {
  Q_OBJECT
 public:
  CommandController(document::Document* doc, document::Selection* selection,
                    viewport::ViewportGrid* grid, QLineEdit* line, QStatusBar* status,
                    QObject* parent = nullptr);
  ~CommandController() override;

  /// Starts a command by id or alias; false if unknown.
  bool start(const QString& name);
  /// One point with snaps for someone else (e.g. a "point" parameter of a plugin, P4); the
  /// callback receives it. Esc cancels without calling it.
  void pickPoint(const QString& prompt, std::function<void(const commands::Point3&)> done);
  [[nodiscard]] bool active() const { return command_ != nullptr; }
  [[nodiscard]] QString activeId() const;
  void cancel();
  /// Enter / right-click: finishes the active command, or repeats the last one.
  void finishOrRepeat();
  /// Text from the command line (Return).
  void submit(const QString& text);

  void setOrtho(bool on);
  [[nodiscard]] bool ortho() const { return ortho_; }

 signals:
  void activeChanged(bool active);
  void orthoChanged(bool on);

 private:
  void onPointer(viewport::Viewport* view, const commands::Point3& p, bool valid, bool snapped);
  void onPicked(viewport::Viewport* view, const commands::Point3& p, bool snapped);
  void handle(commands::Command::Result r);
  void flush();
  void end(const QString& message);
  void refresh();  // prompt + preview
  [[nodiscard]] commands::Point3 constrained(const commands::Point3& p, bool snapped) const;
  [[nodiscard]] commands::PlaneFrame frameOf(const viewport::Viewport* view) const;

  document::Document* doc_;
  document::Selection* selection_;
  viewport::ViewportGrid* grid_;
  QLineEdit* line_;
  QStatusBar* status_;
  std::unique_ptr<commands::Command> command_;
  QString last_name_;
  commands::PlaneFrame plane_;
  commands::Point3 cursor_;
  bool cursor_valid_ = false;
  bool ortho_ = false;
};

}  // namespace nfcad::app
