; Vesper — Inno Setup installer script (Windows)
; Build:  iscc Vesper.iss  (after building the plugin in Release)

#define AppName "Vesper"
#define AppVersion "1.0.0"
#define Company "Sable Audio"
#define BuildDir "..\..\build\Vesper_artefacts\Release"

[Setup]
AppId={{7E2A9C41-52B3-4B7E-9F1D-8A2C41E5B790}}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#Company}
DefaultDirName={commonpf64}\{#Company}\{#AppName}
DisableProgramGroupPage=yes
OutputBaseFilename=Vesper-{#AppVersion}-Windows
Compression=lzma2/max
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
LicenseFile=..\..\LICENSE.md

[Components]
Name: "vst3";       Description: "VST3 plugin";           Types: full compact; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#BuildDir}\VST3\Vesper.vst3\*"; DestDir: "{commoncf64}\VST3\Vesper.vst3"; \
    Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#BuildDir}\Standalone\Vesper.exe"; DestDir: "{app}"; \
    Components: standalone; Flags: ignoreversion
; Factory presets are embedded in the binary and self-extract on first run,
; so no preset payload is required here.

[Icons]
Name: "{autoprograms}\Vesper"; Filename: "{app}\Vesper.exe"; Components: standalone

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Vesper.vst3"
