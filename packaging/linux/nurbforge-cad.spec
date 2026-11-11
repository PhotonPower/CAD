# RPM for AlmaLinux / RHEL 10 (docs/build/almalinux10.md). Built from the source tree:
#   rpmbuild -bb packaging/linux/nurbforge-cad.spec --define "_srcdir $PWD" \
#            --define "_topdir $PWD/build/rpmbuild" [--define "nurbforge_tag <tag>"]
%global nurbforge_tag_opt %{?nurbforge_tag:-DNFCAD_NURBFORGE_TAG=%{nurbforge_tag}}

Name:           nurbforge-cad
Version:        0.0.1
Release:        1%{?dist}
Summary:        Desktop CAD front end for the Nurbforge NURBS kernel
License:        MIT AND MPL-2.0
URL:            https://github.com/PhotonPower/CAD

BuildRequires:  gcc-c++ cmake ninja-build git
BuildRequires:  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtsvg-devel qt6-qtshadertools-devel
Requires:       qt6-qtbase-gui qt6-qtsvg
Recommends:     python3

%description
Desktop CAD with four views, object snaps, drawing and modelling commands on exact NURBS
geometry (Nurbforge kernel), STEP import and export.

%prep
cp %{_srcdir}/LICENSE %{_srcdir}/THIRD_PARTY_NOTICES.md .

%build
cmake -S %{_srcdir} -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=%{_prefix} -DNFCAD_BUILD_TESTS=OFF %{nurbforge_tag_opt}
cmake --build build

%install
DESTDIR=%{buildroot} cmake --install build --component app

%files
%license LICENSE THIRD_PARTY_NOTICES.md
%{_bindir}/NurbforgeCAD
%{_datadir}/applications/io.github.photonpower.NurbforgeCAD.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.github.photonpower.NurbforgeCAD.svg
%{_datadir}/metainfo/io.github.photonpower.NurbforgeCAD.metainfo.xml
%{_datadir}/nurbforge-cad/

%changelog
* Wed Nov 11 2026 Nurbforge CAD developers - 0.0.1-1
- First package (milestone M7)
