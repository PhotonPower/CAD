#pragma once

#include <QSettings>
#include <QStringList>

namespace nfcad::support {

/// Version of the settings layout; raised with every migration step (M8).
inline constexpr int kSettingsVersion = 1;

/// Brings `settings` to kSettingsVersion, step by step, and stores the version under
/// "settings/version". Steps:
///   1 – the application was called "Nurbforge CAD" until 2026-11-11: take over its settings
///       (keys not set yet) from QSettings(<organisation>, "Nurbforge CAD") for each of
///       `previous_organisations` (macOS stores by domain, Windows / Linux by name).
/// Returns the version found before (0: none).
int migrate_settings(QSettings& settings, const QStringList& previous_organisations);

}  // namespace nfcad::support
