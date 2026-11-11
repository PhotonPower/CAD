# Changelog

All notable changes are documented here. Format: [Keep a Changelog](https://keepachangelog.com),
versioning: [SemVer](https://semver.org) (0.x: API and file format may change).

## [Unreleased]

### Added
- Project structure, documentation (plan, UI concept, object snaps, rendering, architecture,
  ADRs 0001–0005, build guides for macOS, Windows 11 and AlmaLinux / RHEL 10)
- CMake build with Qt 6 and Nurbforge (installed package, local checkout or download)
- Main window skeleton: tool column with collapsible groups, 2×2 viewports (`QRhiWidget`),
  display-mode buttons, snap bar
- CI: macOS (required), Windows 11 and AlmaLinux 10 (prepared)
