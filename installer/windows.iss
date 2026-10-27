; SYNC — Windows installer (Inno Setup 6)
; Installs SYNC.vst3 into the standard VST3 folder scanned by FL Studio.

[Setup]
AppName=SYNC
AppVersion=1.0.0
AppPublisher=Offkey555
DefaultDirName={autopf}\Offkey555\SYNC
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputDir=..\dist
OutputBaseFilename=SYNC-Windows-Installer
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=SYNC (Offkey555)

[Files]
Source: "..\dist\SYNC.vst3\*"; DestDir: "{commoncf64}\VST3\SYNC.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\dist\SYNC.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\SYNC (standalone)"; Filename: "{app}\SYNC.exe"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SYNC.vst3"
