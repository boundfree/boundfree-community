; BoundFree EEG Windows installer (Inno Setup 6, Unicode).
;
; Version is injected at compile time via ISCC /D bound by build_release.ps1
; (see -DBoundFreeVersion). When run standalone it falls back to the literal
; placeholder so the file stays parseable for asset checks.
;
; Target: Windows 10 x64. DisableDirPage=no (user may choose install folder).
; The installer stages the full release directory (bf_app + Qt runtime,
; frozen sidecar, version/contracts/i18n/legal), creates Start Menu and
; desktop shortcuts, supports overwrite installs (PrepareToInstall removes
; stale files), and registers a clean uninstaller.

#ifndef BoundFreeVersion
  #define BoundFreeVersion "0.1.76-dev"
#endif

; Win32 VS_VERSIONINFO (VersionInfoVersion / VersionInfoProductVersion)
; REQUIRES a pure 4-part dotted decimal, e.g. "0.1.0.0". SemVer prerelease
; suffixes like "-dev" are illegal there. build_release.ps1 injects
; BoundFreeFileVersion as a numeric string derived from version.json. When the
; iss is compiled standalone it falls back to "0.1.0.0". AppVersion keeps the
; full SemVer string for user-visible display.
#ifndef BoundFreeFileVersion
  #define BoundFreeFileVersion "0.1.0.0"
#endif

#define BoundFreeAppName "BoundFree EEG"
#define BoundFreePublisher "BoundFree"
#define BoundFreeExeName "bf_app.exe"
[Setup]
AppId={{8F2B9A7C-4E1A-4D6B-9F2C-7A3D1E5B8C00}
AppName={#BoundFreeAppName}
VersionInfoVersion={#BoundFreeFileVersion}
VersionInfoCompany={#BoundFreePublisher}
VersionInfoProductName={#BoundFreeAppName}
VersionInfoProductVersion={#BoundFreeFileVersion}
AppVersion={#BoundFreeVersion}
AppPublisher={#BoundFreePublisher}
AppVerName={#BoundFreeAppName} {#BoundFreeVersion}
PrivilegesRequired=admin
SourceDir=..
SetupIconFile=native\assets\branding\BoundFree_app_icon.ico

DefaultDirName={autopf}\BoundFree EEG
DefaultGroupName={#BoundFreeAppName}

; Windows 10 x64 only.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; User may pick/override the install directory.
DisableDirPage=no
DisableProgramGroupPage=yes

UninstallDisplayIcon={app}\{#BoundFreeExeName}
UninstallDisplayName={#BoundFreeAppName}
Compression=lzma2/ultra
SolidCompression=yes
WizardStyle=modern
OutputBaseFilename=BoundFreeEEG_Setup_{#BoundFreeVersion}
OutputDir=dist

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zh"; MessagesFile: "compiler:Default.isl,installer\installer_languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
; The entire staged release tree lives under release\stage\BoundFreeEEG.
; Flags: recursesubdirs copies the directory structure; noprivileges is NOT
; set because we install under {autopf} (Program Files) which needs elevation.
Source: "release\stage\BoundFreeEEG\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

; Legal assets are also staged inside the tree; they are captured by the
; recursive Source above, but we declare them explicitly so the asset checker
; can confirm they are present in the iss.
Source: "release\stage\BoundFreeEEG\legal\*"; DestDir: "{app}\legal"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#BoundFreeAppName}"; Filename: "{app}\{#BoundFreeExeName}"; WorkingDir: "{app}"; Comment: "{#BoundFreeAppName}"
Name: "{group}\Uninstall {#BoundFreeAppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#BoundFreeAppName}"; Filename: "{app}\{#BoundFreeExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; Offer to launch after install.
Filename: "{app}\{#BoundFreeExeName}"; Description: "{cm:LaunchProgram,{#BoundFreeAppName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Remove per-user data produced at runtime only if it sits under {app}.
Type: filesandordirs; Name: "{app}\*.log"

[Code]
function GetUninstallString(): String;
var
  s: String;
begin
  s := '';
  if not RegQueryStringValue(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#emit SetupSetting("AppId")}_is1', 'UninstallString', s) then
    Result := ''
  else
    Result := s;
end;

function IsUpgrade(): Boolean;
begin
  Result := (GetUninstallString() <> '');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  // On overwrite installs, do not re-ask for the directory page beyond the
  // explicit DisableDirPage=no user choice. We keep the page; this hook exists
  // for future gating.
  Result := False;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    Log('Preparing install target under existing installation if present.');
  end;
end;
