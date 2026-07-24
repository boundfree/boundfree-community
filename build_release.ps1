<#
.SYNOPSIS
  One-shot Windows release pipeline for BoundFree EEG Native.

.DESCRIPTION
  Runs, in order:
    1. CMake MSVC Release build of the native app (bf_app.exe)
    2. PyInstaller freeze of the Python sidecar (boundfree_sidecar.exe)
    3. CMake install into release\stage\BoundFreeEEG
    4. windeployqt over the staged bf_app.exe + precise Qt base translations
    5. Copy frozen sidecar + legal/version/contracts into stage; assert app .qm
    6. Code signing of staged binaries (bf_app.exe + sidecar exe) — REQUIRED
       for stable/rc, OPTIONAL for dev/alpha/beta
    7. Inno Setup compile (ISCC) producing dist\BoundFreeEEG_Setup_<ver>.exe
    8. Code signing + verify of the installer — REQUIRED for stable/rc

  Signing policy:
    - Channel stable or rc: signing (SignTool + CertificateThumbprint/Pfx) is
      REQUIRED for both staged binaries and the installer. -SkipSigning is
      REJECTED for stable/rc (it cannot bypass the gate into a "successful"
      release); it is only honoured for dev/alpha/beta local churn.
    - Channel dev/alpha/beta: signing is OPTIONAL; an unsigned build is fine
      and -SkipSigning is honoured.

  Signing order rationale: staged exes are signed and verified BEFORE the
  installer is built, so the installer embeds already-signed binaries. The
  installer itself is then signed and verified as the final step.

.PARAMETER QtRoot
  Qt6 MSVC installation root, e.g. E:\Qt\6.8.2\msvc2022_64.

.PARAMETER Python
  Python interpreter used to build and freeze the sidecar. Defaults to the
  BF_PYTHON_EXECUTABLE env var or "python".

.PARAMETER Configuration
  CMake build configuration. Defaults to "Release".

.PARAMETER SignTool
  Path to signtool.exe. Defaults to the SDK discovery heuristic.

.PARAMETER CertificateThumbprint
  SHA-1 thumbprint of the code signing certificate in the Windows cert store.

.PARAMETER CertificatePfx
  PFX file path for the code signing certificate.

.PARAMETER CertificatePassword
  Password for the PFX file.

.PARAMETER TimestampServer
  RFC 3161 timestamp server URL. Defaults to http://timestamp.digicert.com.

.PARAMETER SkipInstaller
  Skip the Inno Setup compile step (still stages + signs the tree).

.PARAMETER SkipSigning
  Force-skip signing. REJECTED for stable/rc channels; only honoured for
  dev/alpha/beta.

.PARAMETER IsccPath
  Path to ISCC.exe. Defaults to the standard Inno Setup install location.

.EXAMPLE
  .\build_release.ps1 -QtRoot E:\Qt\6.8.2\msvc2022_64 -Python python.exe `
    -SignTool "C:\Path\signtool.exe" -CertificateThumbprint "ABCDEF..."
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $QtRoot,

    [string] $Python = $env:BF_PYTHON_EXECUTABLE,

    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string] $Configuration = "Release",

    [string] $SignTool,

    [string] $CertificateThumbprint,

    [string] $CertificatePfx,

    [string] $CertificatePassword,

    [string] $TimestampServer = "http://timestamp.digicert.com",

    [string] $IsccPath = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",

    [Parameter(Mandatory = $true)]
    [string] $LicensePublicKeyFile,

    [switch] $SkipInstaller,

    [switch] $SkipSigning
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
$NativeDir = Join-Path $RepoRoot "native"
$SidecarDir = Join-Path $RepoRoot "sidecar"
$BuildDir = Join-Path $NativeDir "build-windows"
$StageDir = Join-Path $RepoRoot (Join-Path "release" "stage" | Join-Path -ChildPath "BoundFreeEEG")
$DistDir = Join-Path $RepoRoot "dist"

function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "==> $msg" -ForegroundColor Cyan
}

function Invoke-Native([string]$exe, [string[]]$args, [string]$workDir) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.WorkingDirectory = if ($workDir) { $workDir } else { (Get-Location).Path }
    foreach ($a in $args) { $psi.ArgumentList.Add($a) }
    $p = [System.Diagnostics.Process]::Start($psi)
    $stdoutTask = $p.StandardOutput.ReadToEndAsync()
    $stderrTask = $p.StandardError.ReadToEndAsync()
    $p.WaitForExit()
    $out = $stdoutTask.Result
    $err = $stderrTask.Result
    if ($out) { Write-Host $out }
    if ($err) { Write-Host $err -ForegroundColor Yellow }
    if ($p.ExitCode -ne 0) {
        throw "Command failed (exit $($p.ExitCode)): $exe $($args -join ' ')"
    }
}

# Read product version + channel to drive signing policy and ISCC version.
if (-not $Python) { $Python = "python" }
$VersionJson = Get-Content -Raw (Join-Path $RepoRoot "version.json") | ConvertFrom-Json
$ProductVersion = $VersionJson.version
$ReleaseChannel = $VersionJson.release_channel
# Derive a pure 4-part dotted-decimal FileVersion for Win32 VS_VERSIONINFO.
# SemVer prerelease/build suffixes (e.g. "-dev") are illegal there.
$SemVerBase = ($ProductVersion -split '[-+]')[0]
$Parts = $SemVerBase -split '\.'
while ($Parts.Count -lt 4) { $Parts += '0' }
$FileVersion = ($Parts[0..3] -join '.')

# Signing policy: stable/rc REQUIRE signing; SkipSigning is rejected there.
$ChannelRequiresSigning = $ReleaseChannel -in @("stable", "rc")
if ($SkipSigning -and $ChannelRequiresSigning) {
    throw "Channel '$ReleaseChannel' requires code signing; -SkipSigning is only permitted for dev/alpha/beta builds."
}

Write-Step "BoundFree EEG release build"
Write-Host "version=$ProductVersion channel=$ReleaseChannel"
Write-Host "QtRoot=$QtRoot Python=$Python Configuration=$Configuration"
Write-Host "StageDir=$StageDir signing_required=$ChannelRequiresSigning"

if (-not (Test-Path $QtRoot)) {
    throw "QtRoot not found: $QtRoot"
}

# License public key is mandatory: release builds must embed a release public
# key so offline license verification is enforced. Private keys are never used.
if (-not (Test-Path $LicensePublicKeyFile)) {
    throw "LicensePublicKeyFile not found: $LicensePublicKeyFile"
}
Write-Host "LicensePublicKeyFile=$LicensePublicKeyFile"

# 1. CMake configure + build (native)
Write-Step "1/8 CMake configure + build (MSVC $Configuration)"
Invoke-Native "cmake.exe" @("-S", $NativeDir, "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64", "-DCMAKE_PREFIX_PATH=$QtRoot", "-DBF_REQUIRE_LICENSE=ON", "-DBF_LICENSE_PUBLIC_KEY_FILE=$LicensePublicKeyFile")
Invoke-Native "cmake.exe" @("--build", $BuildDir, "--config", $Configuration, "--target", "bf_app")

# 2. PyInstaller freeze (sidecar)
Write-Step "2/8 PyInstaller freeze (sidecar)"
Invoke-Native $Python @("-m", "PyInstaller", "--noconfirm", "--clean", "--distpath", (Join-Path $RepoRoot "release\sidecar-dist"), "--workpath", (Join-Path $RepoRoot "release\sidecar-build"), "boundfree_sidecar.spec") -workDir $SidecarDir

# 3. CMake install into stage
Write-Step "3/8 CMake install -> $StageDir"
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
Invoke-Native "cmake.exe" @("--install", $BuildDir, "--config", $Configuration, "--prefix", $StageDir)

# 4. windeployqt + Qt base translations
Write-Step "4/8 windeployqt + Qt base translations"
$WinDeployQt = Join-Path $QtRoot "bin\windeployqt.exe"
if (-not (Test-Path $WinDeployQt)) {
    throw "windeployqt not found at: $WinDeployQt"
}
Invoke-Native $WinDeployQt @("--release", "--no-translations", "--no-system-d3d-compiler", "--no-opengl-sw", (Join-Path $StageDir "bf_app.exe"))

# Qt 基础翻译（qtbase 等）：windeployqt --no-translations 避免复制全部 Qt locale，
# 这里精确复制 stable 语言所需的基础翻译。Qt6 约定 zh_CN 使用 qtbase_zh_CN.qm，
# en_US 运行通常无需 translator，qtbase_en.qm 按存在即复制（不强求）。
# zh_CN 缺失即失败：Qt 标准控件本地化是发布必备资产。
$QtTranslationsSrc = Join-Path $QtRoot "translations"
$QtTranslationsDst = Join-Path $StageDir "translations"
if (-not (Test-Path $QtTranslationsSrc)) {
    throw "Qt translations dir not found: $QtTranslationsSrc (expected under QtRoot)"
}
New-Item -ItemType Directory -Force -Path $QtTranslationsDst | Out-Null

# qtbase_zh_CN.qm 为发布强制资产（zh_CN 标准控件本地化）。
$QtBaseZhCn = Join-Path $QtTranslationsSrc "qtbase_zh_CN.qm"
if (-not (Test-Path $QtBaseZhCn)) {
    throw "Qt base translation missing: $QtBaseZhCn (required for zh_CN)"
}
Copy-Item $QtBaseZhCn (Join-Path $QtTranslationsDst "qtbase_zh_CN.qm") -Force

# qtbase_en.qm 为可选：存在即复制（供 en_US 控件本地化），缺失不阻断发布。
$QtBaseEn = Join-Path $QtTranslationsSrc "qtbase_en.qm"
if (Test-Path $QtBaseEn) {
    Copy-Item $QtBaseEn (Join-Path $QtTranslationsDst "qtbase_en_US.qm") -Force
} else {
    Write-Host "qtbase_en.qm not present; en_US 运行不依赖 Qt base translator" -ForegroundColor Yellow
}

# 5. Stage sidecar + version/contracts/legal; assert app .qm
Write-Step "5/8 Stage sidecar + version/contracts/legal; assert app .qm"
$SidecarSrc = Join-Path $RepoRoot (Join-Path "release" "sidecar-dist" | Join-Path -ChildPath "boundfree_sidecar")
$SidecarDst = Join-Path $StageDir "sidecar"
if (-not (Test-Path $SidecarSrc)) {
    throw "Frozen sidecar not found at: $SidecarSrc"
}
if (Test-Path $SidecarDst) { Remove-Item -Recurse -Force $SidecarDst }
Copy-Item -Recurse $SidecarSrc $SidecarDst

# version.json + contracts already installed by CMake for bf_app; ensure present.
Copy-Item (Join-Path $RepoRoot "version.json") $StageDir -Force
New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "contracts") | Out-Null
Copy-Item (Join-Path $RepoRoot "contracts\error_codes.json") (Join-Path $StageDir "contracts") -Force

# i18n 应用 .qm：由 CMake install 产出（native/i18n/*.ts 经 lrelease 编译为
# bf_app_<locale>.qm，install 到 stage/i18n/）。stage 不再复制 .ts；.qm 缺失即失败。
$I18nDst = Join-Path $StageDir "i18n"
foreach ($locale in @("zh_CN", "en_US")) {
    $appQm = Join-Path $I18nDst "bf_app_$locale.qm"
    if (-not (Test-Path $appQm)) {
        throw "App translation missing: $appQm (CMake lrelease/install should produce bf_app_$locale.qm)"
    }
}

# legal assets
$LegalDst = Join-Path $StageDir "legal"
New-Item -ItemType Directory -Force -Path $LegalDst | Out-Null
Copy-Item (Join-Path $RepoRoot "legal\*") $LegalDst -Recurse -Force

# Resolve signing capability once, shared by steps 6 and 8.
$CanSign = $false
$SignArgs = @()
if ($CertificateThumbprint) {
    $CanSign = $true
    $SignArgs = @("sign", "/sha1", $CertificateThumbprint, "/tr", $TimestampServer, "/td", "sha256", "/fd", "sha256")
} elseif ($CertificatePfx) {
    $CanSign = $true
    $SignArgs = @("sign", "/f", $CertificatePfx, "/tr", $TimestampServer, "/td", "sha256", "/fd", "sha256")
    if ($CertificatePassword) { $SignArgs += @("/p", $CertificatePassword) }
}

function Invoke-Sign([string]$target) {
    if (-not (Test-Path $target)) { throw "Sign target missing: $target" }
    Invoke-Native $SignTool ($SignArgs + @($target))
    Invoke-Native $SignTool @("verify", "/pa", $target)
}

# 6. Sign staged binaries BEFORE the installer is built, so the installer
#    embeds already-signed exes. Required for stable/rc; optional otherwise.
if ($ChannelRequiresSigning) {
    Write-Step "6/8 Sign staged binaries (required for channel=$ReleaseChannel)"
    if (-not $CanSign) {
        throw "Channel '$ReleaseChannel' requires code signing but no CertificateThumbprint or CertificatePfx was provided."
    }
    if (-not $SignTool) { throw "SignTool path required for signing; pass -SignTool." }
    if (-not (Test-Path $SignTool)) { throw "signtool not found at: $SignTool" }
    Invoke-Sign (Join-Path $StageDir "bf_app.exe")
    Invoke-Sign (Join-Path $SidecarDst "boundfree_sidecar.exe")
} elseif ($CanSign -and -not $SkipSigning) {
    Write-Step "6/8 Sign staged binaries (optional for channel=$ReleaseChannel)"
    if (-not $SignTool) { throw "SignTool path required for signing; pass -SignTool." }
    Invoke-Sign (Join-Path $StageDir "bf_app.exe")
    Invoke-Sign (Join-Path $SidecarDst "boundfree_sidecar.exe")
} else {
    Write-Step "6/8 Sign staged binaries (skipped: channel=$ReleaseChannel, SkipSigning=$SkipSigning)"
}

# 7. Inno Setup compile (embeds the signed staged tree)
if (-not $SkipInstaller) {
    Write-Step "7/8 Inno Setup compile"
    if (-not (Test-Path $IsccPath)) {
        throw "ISCC not found at: $IsccPath (install Inno Setup 6 or pass -IsccPath)"
    }
    if (-not (Test-Path $DistDir)) { New-Item -ItemType Directory -Path $DistDir | Out-Null }
    Invoke-Native $IsccPath @("/DBoundFreeVersion=$ProductVersion", "/DBoundFreeFileVersion=$FileVersion", "/Qp", (Join-Path $RepoRoot "installer\BoundFreeEEG.iss")) -workDir $RepoRoot
} else {
    Write-Step "7/8 Inno Setup compile (SKIPPED)"
}

# 8. Sign the installer itself and verify. Required for stable/rc.
$InstallerPath = Join-Path $DistDir "BoundFreeEEG_Setup_$ProductVersion.exe"
if ($ChannelRequiresSigning) {
    Write-Step "8/8 Sign installer (required for channel=$ReleaseChannel)"
    if ($SkipInstaller) {
        throw "Channel '$ReleaseChannel' requires a signed installer; -SkipInstaller cannot be combined with a signing-required channel."
    }
    if (-not $CanSign) {
        throw "Channel '$ReleaseChannel' requires installer signing but no certificate was provided."
    }
    Invoke-Sign $InstallerPath
} elseif ($CanSign -and -not $SkipSigning -and -not $SkipInstaller) {
    Write-Step "8/8 Sign installer (optional for channel=$ReleaseChannel)"
    Invoke-Sign $InstallerPath
} else {
    Write-Step "8/8 Sign installer (skipped: channel=$ReleaseChannel, SkipSigning=$SkipSigning)"
}

Write-Step "Done"
Write-Host "Stage: $StageDir"
if (-not $SkipInstaller) {
    Write-Host "Installer: $InstallerPath"
}
