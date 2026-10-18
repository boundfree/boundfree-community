; BoundFree EEG Windows installer (Inno Setup 6, Unicode).
;
; Version is injected at compile time via ISCC /D bound by build_release.ps1
; (see -DBoundFreeVersion). When run standalone it falls back to the literal
; placeholder so the file stays parseable for asset checks.
;
; Target: Windows 10 x64. DisableDirPage=no (user may choose install folder).
; The installer stages the full release directory (bf_app + Qt runtime,
; frozen sidecar, version/contracts/i18n/legal), creates Start Menu and
; desktop shortcuts, supports Inno Setup's standard update flow, and registers
; an uninstaller. Install/update/uninstall behavior remains a manual gate.

#ifndef BoundFreeVersion
  #define BoundFreeVersion "0.1.84-dev"
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

; build_release.ps1 reads this from the official Visual C++ Redistributable
; staged by windeployqt and injects its four-part ProductVersion.
#ifndef BoundFreeVCRedistVersion
  #define BoundFreeVCRedistVersion "14.0.0.0"
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
; Keep the VC++ prerequisite inside Setup and extract it to {tmp} during
; PrepareToInstall, so a failed runtime install stops the application setup.
Source: "release\stage\BoundFreeEEG\vc_redist.x64.exe"; Flags: dontcopy noencryption

; The entire staged release tree lives under release\stage\BoundFreeEEG.
; Flags: recursesubdirs copies the directory structure; noprivileges is NOT
; set because we install under {autopf} (Program Files) which needs elevation.
Source: "release\stage\BoundFreeEEG\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe"; Flags: recursesubdirs createallsubdirs ignoreversion

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
function NextVersionPart(var VersionText: String): Integer;
var
  DotPosition: Integer;
  Part: String;
begin
  while Length(VersionText) > 0 do
  begin
    if (VersionText[1] >= '0') and (VersionText[1] <= '9') then
      Break;
    Delete(VersionText, 1, 1);
  end;

  DotPosition := Pos('.', VersionText);
  if DotPosition = 0 then
  begin
    Part := VersionText;
    VersionText := '';
  end
  else
  begin
    Part := Copy(VersionText, 1, DotPosition - 1);
    Delete(VersionText, 1, DotPosition);
  end;
  Result := StrToIntDef(Part, 0);
end;

function CompareVersionStrings(LeftText, RightText: String): Integer;
var
  Index: Integer;
  LeftPart: Integer;
  RightPart: Integer;
begin
  Result := 0;
  for Index := 1 to 4 do
  begin
    LeftPart := NextVersionPart(LeftText);
    RightPart := NextVersionPart(RightText);
    if LeftPart < RightPart then
    begin
      Result := -1;
      Exit;
    end;
    if LeftPart > RightPart then
    begin
      Result := 1;
      Exit;
    end;
  end;
end;

function IsVCRedistInstalled: Boolean;
var
  Installed: Cardinal;
  InstalledVersion: String;
begin
  Result := False;
  if not RegQueryDWordValue(HKLM64, 'Software\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) then
    Exit;
  if Installed <> 1 then
    Exit;
  if not RegQueryStringValue(HKLM64, 'Software\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', InstalledVersion) then
    Exit;
  Result := CompareVersionStrings(InstalledVersion, '{#BoundFreeVCRedistVersion}') >= 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  RedistPath: String;
begin
  Result := '';
  if IsVCRedistInstalled then
    Exit;

  ExtractTemporaryFile('vc_redist.x64.exe');
  RedistPath := ExpandConstant('{tmp}\vc_redist.x64.exe');
  if not Exec(RedistPath, '/install /quiet /norestart', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := 'Could not start the Microsoft Visual C++ Redistributable installer.';
    Exit;
  end;

  if ResultCode = 3010 then
  begin
    NeedsRestart := True;
    Result := 'The Microsoft Visual C++ Redistributable requires a restart. Restart Windows, then run this installer again.';
    Exit;
  end;

  if IsVCRedistInstalled then
    Exit;

  if ResultCode <> 0 then
    Result := 'The Microsoft Visual C++ Redistributable installer failed (exit code ' + IntToStr(ResultCode) + ').'
  else
    Result := 'The Microsoft Visual C++ Redistributable completed without installing the required runtime.';
end;

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
