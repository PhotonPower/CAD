#include "support/settings_migration.hpp"

namespace nfcad::support {

int migrate_settings(QSettings& settings, const QStringList& previous_organisations) {
  int version = settings.value(QStringLiteral("settings/version"), 0).toInt();
  // the rename migration of 0.0.x wrote only this marker
  if (version == 0 && settings.value(QStringLiteral("migrated/from-nurbforge-cad")).toBool()) version = 1;
  const int found = version;
  if (version < 1) {
    for (const QString& organisation : previous_organisations) {
      const QSettings previous(organisation, QStringLiteral("Nurbforge CAD"));
      for (const QString& key : previous.allKeys()) {
        if (!settings.contains(key)) settings.setValue(key, previous.value(key));
      }
    }
    version = 1;
  }
  // future steps: if (version < 2) { … version = 2; }
  settings.remove(QStringLiteral("migrated"));
  settings.setValue(QStringLiteral("settings/version"), kSettingsVersion);
  return found;
}

}  // namespace nfcad::support
