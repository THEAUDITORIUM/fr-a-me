; ==============================================================================
;  fr(a)me — installeur Windows (Inno Setup 6)
;
;  Compilé par la CI :  iscc /DAppVersion=1.0.0 /DArtefacts=<dossier Release> frame.iss
;  Installe :
;    • VST3       → C:\Program Files\Common Files\VST3\frame.vst3
;    • Standalone → C:\Program Files\fr(a)me\frame.exe (+ raccourci menu Démarrer)
; ==============================================================================

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\Frame_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6F0B8C1E-3A47-4F7B-9E52-6A1D2C9B7E10}
AppName=fr(a)me
AppVersion={#AppVersion}
AppVerName=fr(a)me {#AppVersion}
AppPublisher=fr(a)me audio
DefaultDirName={autopf}\fr(a)me
DefaultGroupName=fr(a)me
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=frame-{#AppVersion}-windows-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
UninstallDisplayName=fr(a)me {#AppVersion}
SetupLogging=yes

[Languages]
Name: "french";  MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Installation complète"
Name: "custom"; Description: "Installation personnalisée"; Flags: iscustom

[Components]
Name: "vst3";       Description: "Plugin VST3";            Types: full custom
Name: "standalone"; Description: "Application standalone"; Types: full custom

[Files]
Source: "{#Artefacts}\VST3\frame.vst3\*"; DestDir: "{commoncf64}\VST3\frame.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\frame.exe"; DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\fr(a)me"; Filename: "{app}\frame.exe"; Components: standalone
Name: "{group}\Désinstaller fr(a)me"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\frame.vst3"
