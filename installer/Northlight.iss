; Inno Setup 6 script for Northlight's installer. scripts/package.ps1 compiles it
; (docs/building.md, "Packaging"); it is not meant to be compiled by hand.
;
; Defines the packaging script passes with /D:
;   AppVersion     Version shown to people, e.g. 0.1.0, 0.1.0-beta.1, 0.1.0-dev.1cb6f30
;   NumericVersion The X.Y.Z part only, for the setup file's version resource
;   StageDir       The staged app folder (the release build's output, signed)
;   OutputDir      Where the installer is written
;   OutputName     The installer's file name, without .exe
;   IconFile       The app's .ico, made by the JUCE build
;   Sign           Defined when signing; ISCC then also gets /Snorthlight=<command>

#ifndef AppVersion
  #error Compile this script with scripts/package.ps1
#endif

#define AppName "Northlight"
#define AppExe "Northlight.exe"
#define ProgId "Northlight.Project"

[Setup]
; The AppId identifies the installation for upgrades and uninstall. Never change it.
AppId={{D2595F08-10DB-4709-AC4B-028A5A30326B}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Leafshade
AppPublisherURL=https://github.com/northlightaudio
AppCopyright=Copyright (C) Leafshade. Licensed under the GNU AGPLv3.
VersionInfoVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoDescription={#AppName} Setup

; Per user by default, so installing needs no administrator; the dialog offers
; an install for everyone (Program Files) instead.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UsePreviousAppDir=yes

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; The app is open while upgrading: ask to close it rather than reboot.
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

WizardStyle=modern
SetupIconFile={#IconFile}
WizardSmallImageFile=..\assets\brand\northlight-icon-48.png,..\assets\brand\northlight-icon-256.png
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}

OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2/ultra64
SolidCompression=yes

#ifdef Sign
SignTool=northlight
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "association"; Description: "Open .northlight projects with {#AppName}"; GroupDescription: "File types:"

[Files]
; The staged folder is exactly the portable zip's contents.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; An upgrade replaces the bundled assets wholesale, so files a newer version
; dropped do not linger.
Type: filesandordirs; Name: "{app}\assets"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; HKA is the current user's classes for a per-user install, the machine's for an
; install for everyone. The app forwards a project opened from Explorer to the
; window already running (src/app/Main.cpp).
Root: HKA; Subkey: "Software\Classes\.northlight"; ValueType: string; ValueName: ""; ValueData: "{#ProgId}"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: association
Root: HKA; Subkey: "Software\Classes\.northlight\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: association
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: ""; ValueData: "{#AppName} Project"; Flags: uninsdeletekey; Tasks: association
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: association
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: association
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".northlight"; ValueData: ""; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

; Settings, plugin lists and recordings (%APPDATA%\Northlight, Documents\Northlight)
; belong to the person, not the install, so uninstalling leaves them.
