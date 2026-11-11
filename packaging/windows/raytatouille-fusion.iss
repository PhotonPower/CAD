; Inno Setup script (docs/build/windows11.md). Compile with
;   iscc /DSourceDir=<staged install dir> /DAppVersion=<x.y.z> /DOutputDir=<dir> raytatouille-fusion.iss
; The staged directory is produced by `cmake --install build --component app --prefix <dir>`
; (Qt is deployed into it by windeployqt via qt_generate_deploy_app_script).

#ifndef AppVersion
  #define AppVersion "0.0.1"
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
; same AppId as the former "Nurbforge CAD" installer: installing updates / replaces it
AppId={{6B0E9A7E-3E0B-4B83-9C1D-2D0F4C5C7A10}
AppName=Raytatouille Fusion
AppVersion={#AppVersion}
AppPublisher=PhotonPower
AppPublisherURL=https://github.com/PhotonPower/CAD
DefaultDirName={autopf}\Raytatouille Fusion
DefaultGroupName=Raytatouille Fusion
LicenseFile={#SourceDir}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=RaytatouilleFusion-{#AppVersion}-win64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\Raytatouille Fusion"; Filename: "{app}\bin\RaytatouilleFusion.exe"
Name: "{autodesktop}\Raytatouille Fusion"; Filename: "{app}\bin\RaytatouilleFusion.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Registry]
Root: HKA; Subkey: "Software\Classes\.nfcad"; ValueType: string; ValueData: "RaytatouilleFusion.Document"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\RaytatouilleFusion.Document"; ValueType: string; ValueData: "Raytatouille Fusion document"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\RaytatouilleFusion.Document\shell\open\command"; ValueType: string; ValueData: """{app}\bin\RaytatouilleFusion.exe"" ""%1"""

[Run]
Filename: "{app}\bin\RaytatouilleFusion.exe"; Description: "{cm:LaunchProgram,Raytatouille Fusion}"; Flags: nowait postinstall skipifsilent
