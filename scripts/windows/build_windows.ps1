<#
.SYNOPSIS
  One-shot Windows dev build entry for BoundFree EEG Native.

.DESCRIPTION
  Reads config/windows-dev.local.json for machine-local defaults, then
  creates or reuses a Python 3.12 virtual environment, installs sidecar build
  dependencies, locates a Qt6 MSVC 2022 x64 installation, invokes
  build_release.ps1, and runs automated Windows acceptance.

  Accepts only dev/alpha/beta channels; always passes -SkipSigning.
  rc/stable releases must use build_release.ps1 directly with Authenticode.

.PARAMETER QtRoot
  Qt6 MSVC 2022 x64 root. Checked in order:
    1. This parameter  2. qt_root in windows-dev.local.json
    3. $env:BF_QT_ROOT  4. $env:QT_ROOT  5. Drive-scan for Qt/msvc2022_64

.PARAMETER BootstrapPython
  Python 3.12 launcher. Default: bootstrap_python in config, else "py".

.PARAMETER VenvDir
  Virtual-environment directory. Default: venv_dir in config, else ".venv".

.PARAMETER LicensePublicKeyFile
  RSA-2048 SubjectPublicKeyInfo PEM. Default: repo license_public.pem.

.PARAMETER SkipInstaller
  Skip the Inno Setup compile step.

.PARAMETER SkipVerification
  Skip verify_windows_release.ps1 after build.

.EXAMPLE
  .\scripts\windows\build_windows.ps1

.EXAMPLE
  .\scripts\windows\build_windows.ps1 -QtRoot "E:\Qt\6.8.2\msvc2022_64"
#>

[CmdletBinding()]
param(
    [string] $QtRoot,
    [string] $BootstrapPython,
    [string] $VenvDir,
    [string] $LicensePublicKeyFile,
    [switch] $SkipInstaller,
    [switch] $SkipVerification
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($env:OS -ne "Windows_NT") {
    throw "scripts/windows/build_windows.ps1 must run in native Windows PowerShell."
}

# ---- Paths ------------------------------------------------------------------
# $PSScriptRoot = ...boundfree-eeg-native\scripts\windows
# Repo root is two levels up.
$RepoRoot    = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
$ConfigPath  = Join-Path $RepoRoot "config\windows-dev.local.json"
$ReleaseScript = Join-Path $PSScriptRoot "build_release.ps1"
$VerifyScript  = Join-Path $PSScriptRoot "verify_windows_release.ps1"

# ---- Local config -----------------------------------------------------------
$Cfg = $null
if (Test-Path -LiteralPath $ConfigPath -PathType Leaf) {
    try { $Cfg = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json }
    catch { throw "Bad windows-dev.local.json: $($_.Exception.Message)" }
    if ($Cfg.schema_version -ne 1) {
        throw "Unsupported windows-dev.local.json schema_version '$($Cfg.schema_version)'."
    }
}

function Get-CfgValue([string]$Key) {
    if ($null -eq $Cfg) { return $null }
    $p = $Cfg.PSObject.Properties[$Key]
    if ($null -eq $p) { return $null }
    return $p.Value
}

# CLI param wins; fall back to config; then hard default.
if (-not $PSBoundParameters.ContainsKey("BootstrapPython")) {
    $BootstrapPython = "py"
    $cfgPy = Get-CfgValue "bootstrap_python"
    if ($cfgPy) { $BootstrapPython = [string]$cfgPy }
}
if (-not $PSBoundParameters.ContainsKey("VenvDir")) {
    $VenvDir = ".venv"
    $cfgVenv = Get-CfgValue "venv_dir"
    if ($cfgVenv) { $VenvDir = [string]$cfgVenv }
}
if (-not $PSBoundParameters.ContainsKey("QtRoot")) {
    $cfgQt = Get-CfgValue "qt_root"
    if ($cfgQt) { $QtRoot = [string]$cfgQt }
}
if (-not $PSBoundParameters.ContainsKey("LicensePublicKeyFile")) {
    $LicensePublicKeyFile = "license_public.pem"
    $cfgKey = Get-CfgValue "license_public_key_file"
    if ($cfgKey) { $LicensePublicKeyFile = [string]$cfgKey }
}

# bootstrap_python_args: from config (e.g. ["-3.12"] for Windows Py launcher).
$BootstrapArgs = @()
$cfgBootstrapArgs = Get-CfgValue "bootstrap_python_args"
if ($cfgBootstrapArgs) {
    $BootstrapArgs = @($cfgBootstrapArgs | ForEach-Object { [string]$_ })
}

# iscc_path and configuration forwarded to build_release.ps1.
$IsccPath = ""
$cfgIscc = Get-CfgValue "iscc_path"
if ($cfgIscc) { $IsccPath = [string]$cfgIscc }

$Configuration = "Release"
$cfgConfig = Get-CfgValue "configuration"
if ($cfgConfig) { $Configuration = [string]$cfgConfig }
if ($Configuration -notin @("Release", "Debug", "RelWithDebInfo")) { $Configuration = "Release" }

# Resolve relative paths against repo root.
if (-not [System.IO.Path]::IsPathRooted($VenvDir)) {
    $VenvDir = Join-Path $RepoRoot $VenvDir
}
$VenvDir    = [System.IO.Path]::GetFullPath($VenvDir)
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"

if (-not [System.IO.Path]::IsPathRooted($LicensePublicKeyFile)) {
    $LicensePublicKeyFile = Join-Path $RepoRoot $LicensePublicKeyFile
}
$LicensePublicKeyFile = [System.IO.Path]::GetFullPath($LicensePublicKeyFile)

# ---- Helpers ----------------------------------------------------------------
function Write-Step([string]$msg) {
    Write-Host ""
    Write-Host "==> $msg" -ForegroundColor Cyan
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string] $Name,
        [Parameter(Mandatory)][string] $Executable,
        [string[]] $Arguments = @(),
        [string] $WorkingDirectory = $RepoRoot
    )
    Write-Step $Name
    Push-Location $WorkingDirectory
    try {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Name failed (exit $LASTEXITCODE)." }
    } finally {
        Pop-Location
    }
}

# Resolve a bare command name to an unambiguous single path string.
function Resolve-Executable([string]$Command) {
    if ($Command -match '[\\/]') {
        $p = if ([System.IO.Path]::IsPathRooted($Command)) {
            $Command
        } else {
            Join-Path $RepoRoot $Command
        }
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) {
            throw "Executable not found: $p"
        }
        return [string](Resolve-Path -LiteralPath $p).Path
    }
    # Get-Command can return multiple hits; take the first Application entry.
    $hit = Get-Command $Command -CommandType Application -ErrorAction SilentlyContinue |
           Select-Object -First 1
    if ($null -eq $hit) {
        throw "Executable '$Command' not found. Install it or pass the parameter explicitly."
    }
    return [string]$hit.Source
}

function Resolve-QtRoot {
    $candidates = [System.Collections.Generic.List[string]]::new()

    function Add-Candidate([string]$Root) {
        if (-not $Root) { return }
        if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return }
        $r = (Resolve-Path -LiteralPath $Root).Path
        if (-not $candidates.Contains($r)) { $candidates.Add($r) }
        Get-ChildItem -LiteralPath $r -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object {
                $sub = Join-Path $_.FullName "msvc2022_64"
                if (-not $candidates.Contains($sub)) { $candidates.Add($sub) }
            }
    }

    Add-Candidate $QtRoot
    Add-Candidate $env:BF_QT_ROOT
    Add-Candidate $env:QT_ROOT
    foreach ($drv in Get-PSDrive -PSProvider FileSystem) {
        Add-Candidate (Join-Path $drv.Root "Qt")
        Add-Candidate (Join-Path $drv.Root "QT")
    }

    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $c "bin\windeployqt.exe")) {
            return [string](Resolve-Path -LiteralPath $c).Path
        }
    }
    throw "Qt6 MSVC 2022 x64 not found. Install Qt, set BF_QT_ROOT, or pass -QtRoot."
}

# ---- Channel gate -----------------------------------------------------------
$version = Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "version.json") | ConvertFrom-Json
if ($version.release_channel -notin @("dev", "alpha", "beta")) {
    throw "Channel '$($version.release_channel)' is not supported by this unsigned dev entry. Use build_release.ps1 with Authenticode signing for rc/stable."
}

# ---- License key ------------------------------------------------------------
if (-not (Test-Path -LiteralPath $LicensePublicKeyFile -PathType Leaf)) {
    throw "License public key not found: $LicensePublicKeyFile. Never pass a private key."
}

# ---- Qt + bootstrap Python --------------------------------------------------
$ResolvedQtRoot = Resolve-QtRoot
$Bootstrap      = [string](Resolve-Executable $BootstrapPython)

# Verify Python version is 3.12 (using BootstrapArgs, e.g. "-3.12" for py launcher).
$pyVer = & $Bootstrap @BootstrapArgs -c "import sys; print('.'.join(map(str, sys.version_info[:2])))" 2>&1 |
         Select-Object -First 1
if ($pyVer.Trim() -ne "3.12") {
    throw "Python 3.12 required. '$Bootstrap $BootstrapArgs' reports '$pyVer'. Set bootstrap_python_args to -3.12 in windows-dev.local.json."
}

# ---- venv -------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $VenvPython -PathType Leaf)) {
    Write-Step "Create Python 3.12 virtual environment"
    & $Bootstrap @BootstrapArgs -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) { throw "venv creation failed." }
}

Invoke-Checked "Upgrade pip" $VenvPython @("-m", "pip", "install", "--upgrade", "pip")
Invoke-Checked "Install sidecar build dependencies" $VenvPython @(
    "-m", "pip", "install", "-e", ("{0}[formats]" -f (Join-Path $RepoRoot "sidecar")), "pyinstaller"
)
Invoke-Checked "Verify sidecar dependencies" $VenvPython @(
    "-c", "import PyInstaller, h5py, mne, neo, pyedflib; print('sidecar-build-dependencies-ok')"
)

# ---- Build ------------------------------------------------------------------
Write-Host ""
Write-Host "BoundFree EEG Windows dev build" -ForegroundColor Green
Write-Host "  version   = $($version.version)  channel = $($version.release_channel)"
Write-Host "  RepoRoot  = $RepoRoot"
Write-Host "  QtRoot    = $ResolvedQtRoot"
Write-Host "  VenvDir   = $VenvDir"
Write-Host "  LicKey    = $LicensePublicKeyFile"

Write-Step "Build release package"
Push-Location $RepoRoot
try {
    if ($IsccPath) {
        & $ReleaseScript -QtRoot $ResolvedQtRoot -Python $VenvPython -Configuration $Configuration -LicensePublicKeyFile $LicensePublicKeyFile -IsccPath $IsccPath -SkipSigning -SkipInstaller:$SkipInstaller
    } else {
        & $ReleaseScript -QtRoot $ResolvedQtRoot -Python $VenvPython -Configuration $Configuration -LicensePublicKeyFile $LicensePublicKeyFile -SkipSigning -SkipInstaller:$SkipInstaller
    }
    if ($LASTEXITCODE -ne 0) { throw "Build release package failed (exit $LASTEXITCODE)." }
} finally {
    Pop-Location
}

# ---- Verify -----------------------------------------------------------------
if ($SkipVerification) {
    Write-Warning "Verification skipped (-SkipVerification)."
    return
}

$verifyArgs = [string[]]@()
if ($SkipInstaller) { $verifyArgs += "-SkipInstaller" }
Invoke-Checked "Windows automated acceptance" $VerifyScript $verifyArgs

Write-Host ""
Write-Host "Windows dev build and acceptance complete." -ForegroundColor Green
