; ============================================================================
; RS Roar Ripper - Installer
; ============================================================================

#define MyAppName      "RS Roar Ripper"
#define MyAppVersion   "1.5.0"
#define MyAppPublisher "Roar Sound"
#define MyAppURL       "https://github.com/chillermanu-sudo/RS-RoarRipper"
#define MyAppExeName   "RoarRipper.exe"

; Pfad zu den gebauten Binaries (Release x64)
#define BuildRoot "C:\PlugIns\RoarSound\RoarRipper\Builds\VisualStudio2026\x64\Release"
; Name des VST3-Bundles (Ordner) - anpassen, falls es anders heisst
#define VST3Name "RoarRipper.vst3"

[Setup]
AppId={{3E9B7C4A-52D1-4A6E-9F0C-RSRIPP15001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\RoarSound\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=InstallerOutput
OutputBaseFilename=RSRoarRipper-{#MyAppVersion}-Setup
LicenseFile=LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Components]
Name: "vst3";      Description: "VST3-Plugin (fuer deine DAW)";  Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone-Anwendung";          Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Desktop-Verknuepfung erstellen"; GroupDescription: "Zusaetzliche Symbole:"; Components: standalone; Flags: unchecked

[Files]
; VST3-Plugin: gesamtes Bundle in den Standard-VST3-Ordner
Source: "{#BuildRoot}\VST3\{#VST3Name}\*"; DestDir: "{commoncf64}\VST3\{#VST3Name}"; Components: vst3; Flags: recursesubdirs ignoreversion
; Standalone-EXE
Source: "{#BuildRoot}\Standalone Plugin\{#MyAppExeName}"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
; Optional: README mit ausliefern (falls gewuenscht, Pfad anpassen)
; Source: "C:\PlugIns\RoarSound\RoarRipper\README.md"; DestDir: "{app}"; Flags: ignoreversion
; Lizenz- und Quellcode-Hinweise immer mit installieren
Source: "LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "SOURCE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

; Lizenz auch direkt ins VST3-Bundle legen
Source: "LICENSE"; DestDir: "{commoncf64}\VST3\{#VST3Name}\Contents\Resources"; DestName: "LICENSE.txt"; Components: vst3; Flags: ignoreversion
Source: "THIRD_PARTY_NOTICES.md"; DestDir: "{commoncf64}\VST3\{#VST3Name}\Contents\Resources"; Components: vst3; Flags: ignoreversion
Source: "SOURCE.txt"; DestDir: "{commoncf64}\VST3\{#VST3Name}\Contents\Resources"; Components: vst3; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Components: standalone
Name: "{group}\Deinstallieren"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; Components: standalone

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{#MyAppName} jetzt starten"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone