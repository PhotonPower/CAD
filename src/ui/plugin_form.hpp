#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <vector>

namespace nfcad::ui {

/// Form generated from a plugin's parameter schema (nfcad.Param, docs/plugin-api.md):
/// number, integer, boolean, choice, text, file, numbers (list), point (pick with snaps) and
/// entity (from the selection). Parameters with a "group" are shown in a box per group.
class PluginForm : public QWidget {
  Q_OBJECT
 public:
  struct Callbacks {
    /// Selected objects matching the kinds ("curve", "shell", …; empty: any): id and name.
    std::function<std::vector<std::pair<qint64, QString>>(const QStringList& kinds)> selection;
    /// Display name of an object (empty if it no longer exists).
    std::function<QString(qint64)> entity_name;
    /// Starts point picking in the views; the callback receives the point.
    std::function<void(std::function<void(double, double, double)>)> pick_point;
  };

  PluginForm(const QJsonArray& schema, const QJsonObject& values, Callbacks callbacks,
             QWidget* parent = nullptr);

  /// All values (defaults for parameters the user has not changed).
  [[nodiscard]] QJsonObject values() const { return values_; }
  /// Parameters whose input is invalid (e.g. a malformed number list).
  [[nodiscard]] QStringList invalid() const { return invalid_; }

 signals:
  void valuesChanged(const QJsonObject& values);

 private:
  QWidget* makeEditor(const QJsonObject& p);
  void set(const QString& name, const QJsonValue& value);
  void setValid(const QString& name, bool valid);

  Callbacks callbacks_;
  QJsonObject values_;
  QStringList invalid_;
};

}  // namespace nfcad::ui
