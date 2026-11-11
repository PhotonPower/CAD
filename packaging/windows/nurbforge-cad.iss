; Inno Setup script (docs/build/windows11.md). Compile with
;   iscc /DSourceDir=<staged install dir> /DAppVersion=<x.y.z> /DOutputDir=<dir> nurbforge-cad.iss
; The staged directory is produced by `cmake --install build --component app --prefix <dir>`
; (Qt is deployed into it by windeployqt via qt_generate_deploy_app_script).

#ifndef AppVersion
  #define AppVersion "0.0.1"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6B0E9A7E-3E0B-4B83-9C1D-2D0F4C5C7A10}
AppName=Nurbforge CAD
AppVersion={#AppVersion}
AppPublisher=PhotonPower
AppPublisherURL=https://github.com/PhotonPower/CAD
DefaultDirName={autopf}\Nurbforge CAD
DefaultGroupName=Nurbforge CAD
LicenseFile={#SourceDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=NurbforgeCAD-{#AppVersion}-win64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Nurbforge CAD"; Filename: "{app}\bin\NurbforgeCAD.exe"
Name: "{autodesktop}\Nurbforge CAD"; Filename: "{app}\bin\NurbforgeCAD.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Registry]
Root: HKA; Subkey: "Software\Classes\.nfcad"; ValueType: string; ValueData: "NurbforgeCAD.Document"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\NurbforgeCAD.Document"; ValueType: string; ValueData: "Nurbforge CAD document"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\NurbforgeCAD.Document\shell\open\command"; ValueType: string; ValueData: """{app}\bin\NurbforgeCAD.exe"" ""%1"""

[Run]
Filename: "{app}\bin\NurbforgeCAD.exe"; Description: "{cm:LaunchProgram,Nurbforge CAD}"; Flags: nowait postinstall skipifsilent
