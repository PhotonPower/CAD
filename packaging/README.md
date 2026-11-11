# Packaging (M7)

| Platform | Files | Built by |
|---|---|---|
| Windows 11 | `windows/nurbforge-cad.iss` (Inno Setup) | `.github/workflows/package.yml`, job `windows` |
| AlmaLinux / RHEL 10 | `linux/nurbforge-cad.spec` (RPM) | job `almalinux10` |
| macOS | `macos/make_dmg.sh` (unsigned DMG; signing in M8) | job `macos` |

All start from `cmake --install <build> --component app`, which installs only the application
(not the Nurbforge kernel's development files) and, on Windows and macOS, deploys Qt into the
staging directory. See the build guides in `docs/build/`.
