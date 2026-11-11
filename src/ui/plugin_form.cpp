#include "ui/plugin_form.hpp"

#include <QCheckBox>
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

}  // namespace nfcad::ui
