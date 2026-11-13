#include "ui/plugin_form.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
#include <limits>
#include <map>

namespace nfcad::ui {

namespace {
constexpr double kHuge = 1e12;

/// Decimal point as in the command line and the number lists, not the system locale.
template <typename Spin>
Spin* c_locale(Spin* w) {
  QLocale c = QLocale::c();
  c.setNumberOptions(QLocale::OmitGroupSeparator);
  w->setLocale(c);
  return w;
}

QFormLayout* make_form(QWidget* parent = nullptr) {
  auto* f = new QFormLayout(parent);
  f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);  // macOS default keeps size hints
  f->setRowWrapPolicy(QFormLayout::WrapLongRows);  // narrow dock: the editor goes below its label
  return f;
}

QString suffix(const QJsonObject& p) {
  const QString unit = p["unit"].toString();
  return unit.isEmpty() ? QString() : QStringLiteral(" ") + unit;
}
}  // namespace

PluginForm::PluginForm(const QJsonArray& schema, const QJsonObject& values, Callbacks callbacks,
                       QWidget* parent)
    : QWidget(parent), callbacks_(std::move(callbacks)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto* ungrouped = make_form();
  layout->addLayout(ungrouped);
  std::map<QString, QFormLayout*> groups;
  for (const QJsonValue& v : schema) {
    const QJsonObject p = v.toObject();
    const QString name = p["name"].toString();
    if (name.isEmpty()) continue;
    values_.insert(name, values.contains(name) ? values[name] : p["default"]);
    QWidget* editor = makeEditor(p);
    editor->setToolTip(p["description"].toString());
    const QString label = p["label"].toString(name);
    const QString group = p["group"].toString();
    if (group.isEmpty()) {
      ungrouped->addRow(label, editor);
      continue;
    }
    auto& form = groups[group];
    if (form == nullptr) {
      auto* box = new QGroupBox(group, this);
      form = make_form(box);
      layout->addWidget(box);
    }
    form->addRow(label, editor);
  }
  layout->addStretch(1);
}

void PluginForm::set(const QString& name, const QJsonValue& value) {
  if (values_.value(name) == value) return;
  values_.insert(name, value);
  emit valuesChanged(values_);
}

void PluginForm::setValid(const QString& name, bool valid) {
  if (valid) {
    invalid_.removeAll(name);
  } else if (!invalid_.contains(name)) {
    invalid_ << name;
  }
}

QWidget* PluginForm::makeEditor(const QJsonObject& p) {
  const QString name = p["name"].toString();
  const QString type = p["type"].toString();
  const QJsonValue value = values_.value(name);

  if (type == QLatin1String("number")) {
    auto* w = c_locale(new QDoubleSpinBox(this));
    w->setRange(p["min"].toDouble(-kHuge), p["max"].toDouble(kHuge));
    w->setDecimals(p["decimals"].toInt(4));
    if (p.contains("step")) w->setSingleStep(p["step"].toDouble());
    w->setSuffix(suffix(p));
    w->setValue(value.toDouble());
    connect(w, &QDoubleSpinBox::valueChanged, this, [this, name](double x) { set(name, x); });
    return w;
  }
  if (type == QLatin1String("integer")) {
    auto* w = c_locale(new QSpinBox(this));
    w->setRange(p["min"].toInt(std::numeric_limits<int>::min()), p["max"].toInt(std::numeric_limits<int>::max()));
    w->setSuffix(suffix(p));
    w->setValue(value.toInt());
    connect(w, &QSpinBox::valueChanged, this, [this, name](int x) { set(name, x); });
    return w;
  }
  if (type == QLatin1String("boolean")) {
    auto* w = new QCheckBox(this);
    w->setChecked(value.toBool());
    connect(w, &QCheckBox::toggled, this, [this, name](bool on) { set(name, on); });
    return w;
  }
  if (type == QLatin1String("choice")) {
    auto* w = new QComboBox(this);
    for (const QJsonValue& o : p["options"].toArray()) w->addItem(o.toString());
    w->setCurrentText(value.toString());
    connect(w, &QComboBox::currentTextChanged, this, [this, name](const QString& t) { set(name, t); });
    return w;
  }
  if (type == QLatin1String("list")) return makeList(p);
  if (type == QLatin1String("table")) return makeTable(p);
  if (type == QLatin1String("json")) return makeJson(p);
  if (type == QLatin1String("numbers")) {
    // comma separated, decimal point (as in the command line)
    auto* w = new QLineEdit(this);
    QStringList parts;
    for (const QJsonValue& x : value.toArray()) parts << QString::number(x.toDouble(), 'g', 10);
    w->setText(parts.join(QStringLiteral(", ")));
    w->setPlaceholderText(tr("e.g. 486.1, 587.6, 656.3") + suffix(p));
    connect(w, &QLineEdit::textChanged, this, [this, name, w](const QString& t) {
      QJsonArray out;
      bool ok = true;
      for (const QString& s : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool good = false;
        const double x = QLocale::c().toDouble(s.trimmed(), &good);
        ok = ok && good && std::isfinite(x);
        out.append(x);
      }
      setValid(name, ok);
      w->setStyleSheet(ok ? QString() : QStringLiteral("border: 1px solid #e05050;"));
      if (ok) set(name, out);
    });
    return w;
  }
  if (type == QLatin1String("files")) {
    auto* box = new QWidget(this);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    auto* list = new QListWidget(box);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setMaximumHeight(90);
    list->setToolTip(p["description"].toString());
    QStringList start;
    if (value.isString()) {  // a ';'-separated text (documents from before the files type)
      start = value.toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    } else {
      for (const QJsonValue& f : value.toArray()) start << f.toString();
    }
    for (const QString& f : start) {
      auto* item = new QListWidgetItem(QFileInfo(f).fileName(), list);
      item->setData(Qt::UserRole, f.trimmed());
      item->setToolTip(f.trimmed());
    }
    column->addWidget(list);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add…"), box);
    auto* remove = new QPushButton(tr("Remove"), box);
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    column->addLayout(row);
    auto store = [this, name, list] {
      QJsonArray files;
      for (int i = 0; i < list->count(); ++i) files.append(list->item(i)->data(Qt::UserRole).toString());
      set(name, files);
    };
    const QString filter = p["filter"].toString();
    connect(add, &QPushButton::clicked, this, [this, list, filter, store] {
      const QStringList chosen = QFileDialog::getOpenFileNames(this, {}, {}, filter);
      for (const QString& f : chosen) {
        bool known = false;  // the same file twice is pointless
        for (int i = 0; i < list->count(); ++i) known = known || list->item(i)->data(Qt::UserRole).toString() == f;
        if (known) continue;
        auto* item = new QListWidgetItem(QFileInfo(f).fileName(), list);
        item->setData(Qt::UserRole, f);
        item->setToolTip(f);
      }
      store();
    });
    connect(remove, &QPushButton::clicked, this, [list, store] {
      qDeleteAll(list->selectedItems());
      store();
    });
    return box;
  }
  if (type == QLatin1String("file")) {
    auto* box = new QWidget(this);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    auto* edit = new QLineEdit(value.toString(), box);
    edit->setPlaceholderText(tr("choose a file with …"));
    edit->setMinimumWidth(80);
    auto* browse = new QPushButton(QStringLiteral("…"), box);
    browse->setFixedWidth(32);
    browse->setToolTip(tr("Choose a file"));
    row->addWidget(edit);
    row->addWidget(browse);
    connect(edit, &QLineEdit::textChanged, this, [this, name](const QString& t) { set(name, t); });
    const bool save = p["save"].toBool();
    const QString filter = p["filter"].toString();
    connect(browse, &QPushButton::clicked, this, [this, edit, save, filter] {
      const QString f = save ? QFileDialog::getSaveFileName(this, {}, edit->text(), filter)
                             : QFileDialog::getOpenFileName(this, {}, edit->text(), filter);
      if (!f.isEmpty()) edit->setText(f);
    });
    return box;
  }
  if (type == QLatin1String("point")) {
    auto* box = new QWidget(this);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    auto* row = new QHBoxLayout;
    column->addLayout(row);
    const QJsonArray xyz = value.toArray();
    std::array<QDoubleSpinBox*, 3> spins{};
    for (int i = 0; i < 3; ++i) {
      spins[i] = c_locale(new QDoubleSpinBox(box));
      spins[i]->setRange(-kHuge, kHuge);
      spins[i]->setDecimals(3);
      spins[i]->setButtonSymbols(QAbstractSpinBox::NoButtons);
      spins[i]->setMinimumWidth(40);
      spins[i]->setValue(xyz.size() == 3 ? xyz[i].toDouble() : 0.0);
      spins[i]->setPrefix(QStringLiteral("xyz").mid(i, 1) + QStringLiteral(" "));
      row->addWidget(spins[i]);
    }
    auto update = [this, name, spins] {
      set(name, QJsonArray{spins[0]->value(), spins[1]->value(), spins[2]->value()});
    };
    for (auto* s : spins) connect(s, &QDoubleSpinBox::valueChanged, this, update);
    auto* pick = new QPushButton(tr("Pick"), box);
    pick->setToolTip(tr("Click a point in a view (with object snaps)"));
    pick->setEnabled(static_cast<bool>(callbacks_.pick_point));
    column->addWidget(pick, 0, Qt::AlignLeft);
    connect(pick, &QPushButton::clicked, this, [this, spins] {
      callbacks_.pick_point([spins](double x, double y, double z) {
        spins[0]->setValue(x);
        spins[1]->setValue(y);
        spins[2]->setValue(z);
      });
    });
    return box;
  }
  if (type == QLatin1String("entity")) {
    auto* box = new QWidget(this);
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    auto* label = new QLabel(box);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* take = new QPushButton(tr("Use selection"), box);
    auto* clear = new QPushButton(tr("×"), box);
    clear->setMaximumWidth(28);
    row->addWidget(label, 1);
    row->addWidget(take);
    row->addWidget(clear);
    const bool multiple = p["multiple"].toBool();
    QStringList kinds;
    for (const QJsonValue& k : p["kinds"].toArray()) kinds << k.toString();
    auto show = [this, label](const QJsonArray& ids) {
      QStringList names;
      for (const QJsonValue& id : ids) {
        const QString n = callbacks_.entity_name ? callbacks_.entity_name(id.toInteger()) : QString();
        names << (n.isEmpty() ? tr("#%1 (missing)").arg(id.toInteger()) : n);
      }
      label->setText(names.isEmpty() ? tr("—") : names.join(QStringLiteral(", ")));
    };
    show(value.toArray());
    connect(take, &QPushButton::clicked, this, [this, name, kinds, multiple, show] {
      if (!callbacks_.selection) return;
      QJsonArray ids;
      for (const auto& [id, n] : callbacks_.selection(kinds)) {
        ids.append(id);
        if (!multiple) break;
      }
      show(ids);
      set(name, ids);
    });
    connect(clear, &QPushButton::clicked, this, [this, name, show] {
      show({});
      set(name, QJsonArray{});
    });
    return box;
  }
  // text and unknown types
  auto* w = new QLineEdit(value.toString(), this);
  connect(w, &QLineEdit::textChanged, this, [this, name](const QString& t) { set(name, t); });
  return w;
}

// ---------------------------------------------------------------- structured parameters (W4a)

namespace {
/// The defaults of an entry's parameters.
QJsonObject entry_defaults(const QJsonArray& schema) {
  QJsonObject out;
  for (const QJsonValue& v : schema) out.insert(v.toObject()["name"].toString(), v.toObject()["default"]);
  return out;
}

/// What names an entry in the list: the field `item_label`, else the first text or choice.
QString entry_text(const QJsonObject& p, const QJsonObject& entry, int index) {
  QString key = p["item_label"].toString();
  if (key.isEmpty()) {
    for (const QJsonValue& v : p["of"].toArray()) {
      const QString t = v.toObject()["type"].toString();
      if (t == QLatin1String("text") || t == QLatin1String("choice")) {
        key = v.toObject()["name"].toString();
        break;
      }
    }
  }
  const QJsonValue named = key.isEmpty() ? QJsonValue() : entry.value(key);
  const QString text = named.isString() ? named.toString() : named.isDouble() ? QString::number(named.toDouble()) : QString();
  return QStringLiteral("%1. %2").arg(index + 1).arg(text.isEmpty() ? QObject::tr("entry") : text);
}
}  // namespace

bool PluginForm::editEntry(const QJsonArray& schema, QJsonObject& entry) {
  if (callbacks_.edit_entry) return callbacks_.edit_entry(schema, entry);
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Edit entry"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new PluginForm(schema, entry, callbacks_, &dialog);  // nested lists work the same way
  layout->addWidget(form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, form] {
    if (form->invalid().isEmpty()) dialog.accept();  // fix the red fields first
  });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return false;
  entry = form->values();
  return true;
}

QWidget* PluginForm::makeList(const QJsonObject& p) {
  const QString name = p["name"].toString();
  const QJsonArray schema = p["of"].toArray();
  auto* box = new QWidget(this);
  box->setObjectName(QStringLiteral("list:") + name);
  auto* column = new QVBoxLayout(box);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(2);
  auto* list = new QListWidget(box);
  list->setMaximumHeight(130);
  column->addWidget(list);
  auto* row = new QHBoxLayout;
  auto button = [box, row](const QString& text, const QString& id) {
    auto* b = new QPushButton(text, box);
    b->setObjectName(id);
    row->addWidget(b);
    return b;
  };
  QPushButton* add = button(tr("Add"), QStringLiteral("add"));
  QPushButton* edit = button(tr("Edit…"), QStringLiteral("edit"));
  QPushButton* remove = button(tr("Remove"), QStringLiteral("remove"));
  QPushButton* up = button(QStringLiteral("↑"), QStringLiteral("up"));
  QPushButton* down = button(QStringLiteral("↓"), QStringLiteral("down"));
  row->addStretch(1);
  column->addLayout(row);
  auto show = [this, name, list, p](int current) {
    list->clear();
    const QJsonArray entries = values_.value(name).toArray();
    for (int k = 0; k < entries.size(); ++k) list->addItem(entry_text(p, entries[k].toObject(), k));
    if (current >= 0 && current < list->count()) list->setCurrentRow(current);
  };
  show(-1);
  auto change = [this, name, show](const std::function<void(QJsonArray&)>& f, int current) {
    QJsonArray entries = values_.value(name).toArray();
    f(entries);
    set(name, entries);
    show(current);
  };
  connect(add, &QPushButton::clicked, this, [this, schema, list, change] {
    QJsonObject entry = entry_defaults(schema);
    if (!editEntry(schema, entry)) return;
    const int at = list->count();
    change([&entry](QJsonArray& e) { e.append(entry); }, at);
  });
  auto edit_current = [this, name, schema, list, change] {
    const int k = list->currentRow();
    const QJsonArray entries = values_.value(name).toArray();
    if (k < 0 || k >= entries.size()) return;
    QJsonObject entry = entries[k].toObject();
    if (!editEntry(schema, entry)) return;
    change([k, &entry](QJsonArray& e) { e[k] = entry; }, k);
  };
  connect(edit, &QPushButton::clicked, this, edit_current);
  connect(list, &QListWidget::itemDoubleClicked, this, edit_current);
  connect(remove, &QPushButton::clicked, this, [list, change] {
    const int k = list->currentRow();
    if (k < 0) return;
    change([k](QJsonArray& e) { e.removeAt(k); }, k - 1);
  });
  connect(up, &QPushButton::clicked, this, [list, change] {
    const int k = list->currentRow();
    if (k <= 0) return;
    change([k](QJsonArray& e) {
      const QJsonValue a = e[k];
      e[k] = e[k - 1];
      e[k - 1] = a;
    }, k - 1);
  });
  connect(down, &QPushButton::clicked, this, [list, change] {
    const int k = list->currentRow();
    if (k < 0 || k + 1 >= list->count()) return;
    change([k](QJsonArray& e) {
      const QJsonValue a = e[k];
      e[k] = e[k + 1];
      e[k + 1] = a;
    }, k + 1);
  });
  return box;
}

QWidget* PluginForm::makeTable(const QJsonObject& p) {
  const QString name = p["name"].toString();
  const QJsonArray columns = p["columns"].toArray();
  auto* box = new QWidget(this);
  box->setObjectName(QStringLiteral("table:") + name);
  auto* column = new QVBoxLayout(box);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(2);
  auto* table = new QTableWidget(0, static_cast<int>(columns.size()), box);
  QStringList heads;
  for (const QJsonValue& c : columns) heads << c.toObject()["label"].toString(c.toObject()["name"].toString()) + suffix(c.toObject());
  table->setHorizontalHeaderLabels(heads);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table->verticalHeader()->setDefaultSectionSize(22);
  table->setMaximumHeight(160);
  column->addWidget(table);
  auto* row = new QHBoxLayout;
  auto* add = new QPushButton(tr("Add row"), box);
  add->setObjectName(QStringLiteral("add"));
  auto* remove = new QPushButton(tr("Remove row"), box);
  remove->setObjectName(QStringLiteral("remove"));
  row->addWidget(add);
  row->addWidget(remove);
  row->addStretch(1);
  column->addLayout(row);
  auto cell_text = [](const QJsonValue& v) {
    return v.isDouble() ? QString::number(v.toDouble(), 'g', 12) : v.toString();
  };
  auto fill = [table, columns, cell_text](const QJsonArray& rows) {
    const QSignalBlocker block(table);
    table->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < rows.size(); ++r) {
      for (int c = 0; c < columns.size(); ++c) {
        const QString key = columns[c].toObject()["name"].toString();
        table->setItem(r, c, new QTableWidgetItem(cell_text(rows[r].toObject().value(key))));
      }
    }
  };
  fill(values_.value(name).toArray());
  // the rows from the cells; a cell that is no number in a number column marks the table invalid
  auto store = [this, name, table, columns] {
    QJsonArray rows;
    bool ok = true;
    for (int r = 0; r < table->rowCount(); ++r) {
      QJsonObject out;
      for (int c = 0; c < columns.size(); ++c) {
        const QJsonObject col = columns[c].toObject();
        const QString text = table->item(r, c) != nullptr ? table->item(r, c)->text().trimmed() : QString();
        const QString t = col["type"].toString();
        if (t == QLatin1String("number") || t == QLatin1String("integer")) {
          bool good = false;
          const double x = QLocale::c().toDouble(text, &good);
          good = good && std::isfinite(x) && (t == QLatin1String("number") || std::floor(x) == x);
          if (table->item(r, c) != nullptr) table->item(r, c)->setBackground(good ? QBrush() : QBrush(QColor(224, 80, 80, 90)));
          ok = ok && good;
          out.insert(col["name"].toString(), t == QLatin1String("integer") ? QJsonValue(static_cast<qint64>(x)) : QJsonValue(x));
        } else if (t == QLatin1String("choice")) {
          bool known = false;
          for (const QJsonValue& o : col["options"].toArray()) known = known || o.toString() == text;
          if (table->item(r, c) != nullptr) table->item(r, c)->setBackground(known ? QBrush() : QBrush(QColor(224, 80, 80, 90)));
          ok = ok && known;
          out.insert(col["name"].toString(), text);
        } else {
          out.insert(col["name"].toString(), text);
        }
      }
      rows.append(out);
    }
    setValid(name, ok);
    if (ok) set(name, rows);
  };
  connect(table, &QTableWidget::cellChanged, this, [store](int, int) { store(); });
  connect(add, &QPushButton::clicked, this, [this, name, columns, fill] {
    QJsonArray rows = values_.value(name).toArray();
    rows.append(entry_defaults(columns));
    set(name, rows);
    fill(rows);
  });
  connect(remove, &QPushButton::clicked, this, [this, name, table, fill] {
    const int r = table->currentRow();
    QJsonArray rows = values_.value(name).toArray();
    if (r < 0 || r >= rows.size()) return;
    rows.removeAt(r);
    set(name, rows);
    fill(rows);
  });
  return box;
}

QWidget* PluginForm::makeJson(const QJsonObject& p) {
  const QString name = p["name"].toString();
  auto* w = new QPlainTextEdit(this);
  w->setObjectName(QStringLiteral("json:") + name);
  // QJsonDocument holds an object or an array: a scalar travels inside a one-element array
  const QByteArray text = QJsonDocument(QJsonArray{values_.value(name)}).toJson(QJsonDocument::Indented);
  QString inner = QString::fromUtf8(text).trimmed();
  inner = inner.mid(1, inner.size() - 2).trimmed();  // without the wrapping brackets
  w->setPlainText(inner);
  w->setMinimumHeight(90);
  connect(w, &QPlainTextEdit::textChanged, this, [this, name, w] {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson("[" + w->toPlainText().toUtf8() + "]", &error);
    const bool ok = error.error == QJsonParseError::NoError && doc.isArray() && doc.array().size() == 1;
    setValid(name, ok);
    w->setStyleSheet(ok ? QString() : QStringLiteral("border: 1px solid #e05050;"));
    if (ok) set(name, doc.array().at(0));
  });
  return w;
}

}  // namespace nfcad::ui
