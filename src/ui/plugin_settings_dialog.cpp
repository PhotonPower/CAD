#include "ui/plugin_settings_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStandardPaths>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "plugins/plugin_manager.hpp"
#include "plugins/signature.hpp"

namespace nfcad::ui {

PluginSettingsDialog::PluginSettingsDialog(plugins::PluginManager* manager, QWidget* parent)
    : QDialog(parent), manager_(manager) {
  setWindowTitle(tr("Plugin settings"));
  auto* layout = new QVBoxLayout(this);
  auto* form = new QFormLayout;
  auto* python_row = new QHBoxLayout;
  python_ = new QLineEdit(manager_->python(), this);
  auto* browse = new QPushButton(tr("…"), this);
  python_row->addWidget(python_);
  python_row->addWidget(browse);
  form->addRow(tr("Python interpreter:"), python_row);
  const QString pkg = plugins::PluginManager::nfcadPythonPath();
  form->addRow(tr("nfcad package:"), new QLabel(pkg.isEmpty() ? tr("not found") : pkg, this));
  layout->addLayout(form);

  // the Python environment (P7, decision D5): check it, or create a venv
  auto* env_row = new QHBoxLayout;
  auto* check = new QPushButton(tr("Check environment"), this);
  check->setToolTip(tr("Runs the interpreter and looks for the packages the plugins need (plugin.json \"requires\")"));
  auto* create = new QPushButton(tr("Create environment…"), this);
  create->setToolTip(tr("Creates a new virtual environment (venv) from the interpreter above and installs numpy"));
  env_row->addWidget(check);
  env_row->addWidget(create);
  env_row->addStretch();
  layout->addLayout(env_row);
  report_ = new QPlainTextEdit(this);
  report_->setReadOnly(true);
  report_->setMaximumHeight(120);
  report_->setPlaceholderText(tr("Check environment: Python version and packages of the plugins"));
  report_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  layout->addWidget(report_);
  connect(check, &QPushButton::clicked, this, &PluginSettingsDialog::checkEnvironment);
  connect(create, &QPushButton::clicked, this, &PluginSettingsDialog::createEnvironment);

  auto* installed = new QLabel(tr("Installed plugins (Plugins → Install plugin…): %1")
                                   .arg(QDir::toNativeSeparators(plugins::PluginManager::installFolder())),
                               this);
  installed->setWordWrap(true);
  installed->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(installed);
  layout->addWidget(new QLabel(tr("Further plugin folders (every sub-folder with a plugin.json):"), this));
  folders_ = new QListWidget(this);
  for (const QString& f : manager_->folders()) {
    if (f != plugins::PluginManager::installFolder()) folders_->addItem(f);
  }
  layout->addWidget(folders_);
  auto* folder_buttons = new QHBoxLayout;
  auto* add = new QPushButton(tr("Add folder…"), this);
  auto* remove = new QPushButton(tr("Remove"), this);
  folder_buttons->addWidget(add);
  folder_buttons->addWidget(remove);
  folder_buttons->addStretch();
  layout->addLayout(folder_buttons);
  // signatures (P5)
  const QStringList keys = manager_->trustedKeys();
  auto* publisher = new QLabel(
      keys.isEmpty() ? tr("Trusted publisher: none built in – signed plugins cannot be loaded (see keys/trusted-publishers.txt)")
                     : tr("Trusted publisher key: %1").arg(keys.join(QStringLiteral(", "))),
      this);
  publisher->setWordWrap(true);
  publisher->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(publisher);
  developer_ = new QCheckBox(tr("Developer mode: also load unsigned plugins"), this);
  developer_->setChecked(manager_->developerMode());
  developer_->setToolTip(tr("Unsigned plugins run Python code that nobody has vouched for. Only for plugins you are developing."));
  if (qEnvironmentVariable("NFCAD_DEVELOPER_MODE") == QLatin1String("1")) {
    developer_->setEnabled(false);
    developer_->setText(developer_->text() + tr(" (on through NFCAD_DEVELOPER_MODE)"));
  }
  layout->addWidget(developer_);
  if (!manager_->problems().isEmpty()) {
    auto* problems = new QLabel(tr("Problems:\n") + manager_->problems().join(QLatin1Char('\n')), this);
    problems->setWordWrap(true);
    problems->setStyleSheet(QStringLiteral("color: #f0a060;"));
    layout->addWidget(problems);
  }
  auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  layout->addWidget(box);

  connect(browse, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, tr("Python interpreter"), python_->text());
    if (!f.isEmpty()) python_->setText(f);
  });
  connect(add, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, tr("Plugin folder"));
    if (!d.isEmpty()) folders_->addItem(d);
  });
  connect(remove, &QPushButton::clicked, this, [this] { delete folders_->currentItem(); });
  connect(box, &QDialogButtonBox::accepted, this, [this] {
    apply();
    accept();
  });
  connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  resize(620, 560);
}

void PluginSettingsDialog::checkEnvironment() {
  // the interpreter in the field (not yet applied)
  QStringList requirements{QStringLiteral("numpy")};
  for (const auto& h : manager_->plugins()) {
    for (const QString& r : h->manifest().requirements) {
      if (!requirements.contains(r)) requirements << r;
    }
  }
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const plugins::EnvironmentReport r = plugins::check_environment(python_->text().trimmed(), requirements);
  QApplication::restoreOverrideCursor();
  QStringList lines;
  lines << (r.python_version.isEmpty() ? r.error : tr("Python %1  %2").arg(r.python_version, r.error.isEmpty() ? tr("ok") : r.error));
  for (const plugins::PackageStatus& p : r.packages) {
    const QString want = p.requirement.op.isEmpty() ? p.requirement.name : p.requirement.name + p.requirement.op + p.requirement.version;
    lines << QStringLiteral("%1 %2  %3").arg(p.satisfied ? QStringLiteral("✓") : QStringLiteral("✗"), want,
                                              p.satisfied ? p.version : p.error);
  }
  if (!r.pipCommand().isEmpty()) lines << QString() << tr("Install the missing packages with:") << r.pipCommand();
  report_->setPlainText(lines.join(QLatin1Char('\n')));
}

void PluginSettingsDialog::createEnvironment() {
  const QString base = python_->text().trimmed();
  const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("python-env"));
  const QString target = QFileDialog::getSaveFileName(this, tr("New Python environment (folder)"), suggested);
  if (target.isEmpty()) return;
  if (QFileInfo::exists(target)) {
    report_->setPlainText(tr("%1 exists already – choose a new folder.").arg(target));
    return;
  }
#ifdef Q_OS_WIN
  const QString python = QDir(target).filePath(QStringLiteral("Scripts/python.exe"));
#else
  const QString python = QDir(target).filePath(QStringLiteral("bin/python"));
#endif
  report_->setPlainText(tr("Creating %1 from %2 …").arg(target, base));
  // two steps, asynchronous: venv, then numpy into it
  auto* venv = new QProcess(this);
  venv->setProcessChannelMode(QProcess::MergedChannels);
  connect(venv, &QProcess::readyRead, this, [this, venv] { report_->appendPlainText(QString::fromLocal8Bit(venv->readAll()).trimmed()); });
  connect(venv, &QProcess::finished, this, [this, venv, python](int code, QProcess::ExitStatus status) {
    venv->deleteLater();
    if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(python)) {
      report_->appendPlainText(tr("Creating the environment failed."));
      return;
    }
    report_->appendPlainText(tr("Installing numpy …"));
    auto* pip = new QProcess(this);
    pip->setProcessChannelMode(QProcess::MergedChannels);
    connect(pip, &QProcess::readyRead, this, [this, pip] { report_->appendPlainText(QString::fromLocal8Bit(pip->readAll()).trimmed()); });
    connect(pip, &QProcess::finished, this, [this, pip, python](int c, QProcess::ExitStatus s) {
      pip->deleteLater();
      if (s == QProcess::NormalExit && c == 0) {
        python_->setText(python);
        report_->appendPlainText(tr("Done – the new interpreter is set above (applied with OK). Install the kernel "
                                    "packages into it, then use Check environment."));
      } else {
        report_->appendPlainText(tr("Installing numpy failed."));
      }
    });
    pip->start(python, {QStringLiteral("-m"), QStringLiteral("pip"), QStringLiteral("install"), QStringLiteral("numpy")});
  });
  venv->start(base, {QStringLiteral("-m"), QStringLiteral("venv"), target});
}

void PluginSettingsDialog::apply() {
  manager_->setPython(python_->text().trimmed());
  QStringList folders;
  for (int i = 0; i < folders_->count(); ++i) folders << folders_->item(i)->text();
  if (developer_->isEnabled() && developer_->isChecked() != manager_->developerMode()) {
    QSettings().setValue(QStringLiteral("plugins/developerMode"), developer_->isChecked());
  }
  manager_->setFolders(folders);  // reloads
}

}  // namespace nfcad::ui
