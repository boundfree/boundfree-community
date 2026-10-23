<#
.SYNOPSIS
  Validates a Windows BoundFree EEG release after build_release.ps1.

.DESCRIPTION
  Verifies the staged release tree, frozen sidecar, core command-line smoke
  scenarios, optional Inno Setup installer, and writes a checklist for the
  remaining manual Windows 10 acceptance work. It never reads, creates, or
  copies private keys. Code signing is observed for dev/beta and required
  for rc/stable; an unsigned dev build remains valid.

.EXAMPLE
  .\scripts\verify_windows_release.ps1

.EXAMPLE
  .\scripts\verify_windows_release.ps1 -StageDir C:\Temp\BoundFreeEEG
#>

[CmdletBinding()]
param(
    [string] $RepoRoot,
    [string] $StageDir,
    [string] $InstallerPath,
    [switch] $SkipInstaller
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# $PSScriptRoot is empty inside param() default expressions under Windows
# PowerShell 5.1, so the repository root is resolved here instead.
if (-not $RepoRoot) {
    $RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][string] $Executable,
        [string[]] $Arguments = @(),
        [string] $WorkingDirectory
    )

    Write-Host "==> $Name" -ForegroundColor Cyan
    # GUI-subsystem applications do not reliably set LASTEXITCODE in pwsh.
    # Explicit process handles also ensure the verifier waits for Qt to exit.
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = (($Arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' ')
    $info.WorkingDirectory = if ($WorkingDirectory) { $WorkingDirectory } else { $RepoRoot }
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(180000)) {
        $process.Kill()
        throw "$Name timed out after 180 seconds."
    }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    if ($stdout) { Write-Host $stdout }
    if ($stderr) { Write-Host $stderr }
    if ($process.ExitCode -ne 0) {
        throw "$Name failed with exit code $($process.ExitCode)."
    }
    return ($stdout + [Environment]::NewLine + $stderr)
}

function Require-Path {
    param([Parameter(Mandatory = $true)][string] $Path, [Parameter(Mandatory = $true)][string] $Description)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description missing: $Path"
    }
}

$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$versionPath = Join-Path $RepoRoot "version.json"
Require-Path $versionPath "version.json"
$version = Get-Content -Raw -LiteralPath $versionPath | ConvertFrom-Json

if ($version.release_channel -notin @("dev", "alpha", "beta", "rc", "stable")) {
    throw "Unknown release channel: $($version.release_channel)"
}
$RequiresSignature = $version.release_channel -in @("rc", "stable")

if (-not $StageDir) {
    $StageDir = Join-Path $RepoRoot "release\stage\BoundFreeEEG"
}
if (-not $InstallerPath) {
    $InstallerPath = Join-Path $RepoRoot "dist\BoundFreeEEG_Setup_$($version.version).exe"
}

$StageDir = [System.IO.Path]::GetFullPath($StageDir)
$app = Join-Path $StageDir "bf_app.exe"
$sidecar = Join-Path $StageDir "sidecar\boundfree_sidecar.exe"
$csvFixture = Join-Path $RepoRoot "tests\fixtures\generated\simple_eeg.csv"
$edfFixture = Join-Path $RepoRoot "tests\fixtures\generated\simple_eeg.edf"
$outputRoot = Join-Path $RepoRoot "release\windows-acceptance"
$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$outputDir = Join-Path $outputRoot $runId

Write-Host "BoundFree EEG Windows acceptance" -ForegroundColor Green
Write-Host "repo=$RepoRoot"
Write-Host "version=$($version.version) channel=$($version.release_channel)"
Write-Host "stage=$StageDir"

# Release layout contract.
@(
    @{ Path = $app; Description = "Native application" },
    @{ Path = $sidecar; Description = "Frozen sidecar" },
    @{ Path = (Join-Path $StageDir "vc_redist.x64.exe"); Description = "Microsoft Visual C++ x64 Redistributable prerequisite" },
    @{ Path = (Join-Path $StageDir "version.json"); Description = "Staged version metadata" },
    @{ Path = (Join-Path $StageDir "contracts\error_codes.json"); Description = "Staged error-code registry" },
    @{ Path = (Join-Path $StageDir "i18n\bf_app_zh_CN.qm"); Description = "Simplified Chinese translation" },
    @{ Path = (Join-Path $StageDir "i18n\bf_app_en_US.qm"); Description = "English translation" },
    @{ Path = (Join-Path $StageDir "i18n\bf_app_ja_JP.qm"); Description = "Japanese translation" },
    @{ Path = (Join-Path $StageDir "i18n\bf_app_zh_TW.qm"); Description = "Traditional Chinese translation" },
    @{ Path = (Join-Path $StageDir "legal"); Description = "Legal assets directory" },
    @{ Path = $csvFixture; Description = "CSV fixture" },
    @{ Path = $edfFixture; Description = "EDF fixture" }
) | ForEach-Object { Require-Path $_.Path $_.Description }

$stagedVersion = Get-Content -Raw -LiteralPath (Join-Path $StageDir "version.json") | ConvertFrom-Json
if ($stagedVersion.version -ne $version.version -or $stagedVersion.ipc_protocol_version -ne $version.ipc_protocol_version) {
    throw "Stage version.json differs from repository version.json."
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

# Automated release-smoke coverage. bf_app resolves the frozen sidecar from its application directory.
Invoke-Checked -Name "Application version" -Executable $app -Arguments @("--version") | Out-File -Encoding utf8 (Join-Path $outputDir "01-version.txt")
Invoke-Checked -Name "System UI smoke" -Executable $app -Arguments @("--smoke-m8-system-ui") | Out-File -Encoding utf8 (Join-Path $outputDir "02-system-ui.txt")
Invoke-Checked -Name "Sidecar handshake" -Executable $app -Arguments @("--smoke-sidecar") | Out-File -Encoding utf8 (Join-Path $outputDir "03-sidecar.txt")
Invoke-Checked -Name "License gate smoke" -Executable $app -Arguments @("--smoke-license-gate") | Out-File -Encoding utf8 (Join-Path $outputDir "03a-license-gate.txt")
Invoke-Checked -Name "CSV open and slice" -Executable $app -Arguments @("--smoke-open-slice", $csvFixture, "0", "0.05") | Out-File -Encoding utf8 (Join-Path $outputDir "04-csv-slice.txt")
Invoke-Checked -Name "EDF open and slice" -Executable $app -Arguments @("--smoke-open-slice", $edfFixture, "0", "0.05") | Out-File -Encoding utf8 (Join-Path $outputDir "05-edf-slice.txt")
Invoke-Checked -Name "Offline DSP smoke" -Executable $app -Arguments @("--smoke-processing-offline") | Out-File -Encoding utf8 (Join-Path $outputDir "06-dsp.txt")
Invoke-Checked -Name "Playback smoke" -Executable $app -Arguments @("--smoke-m5-playback") | Out-File -Encoding utf8 (Join-Path $outputDir "07-playback.txt")
Invoke-Checked -Name "Spectral-analysis smoke" -Executable $app -Arguments @("--smoke-m4-analysis") | Out-File -Encoding utf8 (Join-Path $outputDir "08-spectral.txt")

$recordDir = Join-Path $outputDir "record"
$exportDir = Join-Path $outputDir "export"
Invoke-Checked -Name "Recording smoke" -Executable $app -Arguments @("--smoke-m7-record", $csvFixture, $recordDir) | Out-File -Encoding utf8 (Join-Path $outputDir "09-record.txt")
Invoke-Checked -Name "Export smoke" -Executable $app -Arguments @("--smoke-m7-export", $csvFixture, $exportDir) | Out-File -Encoding utf8 (Join-Path $outputDir "10-export.txt")

$signatureRows = @()
foreach ($path in @($app, $sidecar)) {
    $signature = Get-AuthenticodeSignature -LiteralPath $path
    $signatureRows += [PSCustomObject]@{ Path = $path; Status = $signature.Status; StatusMessage = $signature.StatusMessage }
}

if (-not $SkipInstaller) {
    Require-Path $InstallerPath "Inno Setup installer"
    $installerHash = Get-FileHash -Algorithm SHA256 -LiteralPath $InstallerPath
    $installerSignature = Get-AuthenticodeSignature -LiteralPath $InstallerPath
    $signatureRows += [PSCustomObject]@{ Path = $InstallerPath; Status = $installerSignature.Status; StatusMessage = $installerSignature.StatusMessage }
    "SHA256  $($installerHash.Hash)  $InstallerPath" | Out-File -Encoding ascii (Join-Path $outputDir "installer-sha256.txt")
}
if ($RequiresSignature) {
    foreach ($row in $signatureRows) {
        if ($row.Status -ne 'Valid') { throw "Required Authenticode verification failed: $($row.Path) ($($row.Status))" }
    }
    if ($SkipInstaller) { throw 'Stable/rc acceptance requires the signed installer.' }
}
$signatureRows | ConvertTo-Json | Out-File -Encoding utf8 (Join-Path $outputDir "signature-status.json")

$installerSection = if ($SkipInstaller) {
@"
## 安装包

- [ ] 本次跳过安装包验证：使用 `-SkipInstaller`。
"@
} else {
@"
## 安装包与系统集成

- [ ] 从 `$(Split-Path -Leaf $InstallerPath)` 安装，确认可自选安装目录。
- [ ] 在默认目录和自选目录各安装一次；从开始菜单、桌面快捷方式、安装目录分别启动。
- [ ] 对同版本执行覆盖安装；确认仍可启动、语言资源和边车均可加载。
- [ ] 从 Windows 设置/控制面板卸载；确认快捷方式和安装目录被移除。
- [ ] 记录安装包 SHA-256：见 `installer-sha256.txt`。
"@
}

$checklist = @"
# BoundFree EEG Windows 10 验收记录

- 日期：$(Get-Date -Format "yyyy-MM-dd HH:mm:ss K")
- 版本：$($version.version)
- 通道：$($version.release_channel)
- 仓库：$RepoRoot
- Stage：$StageDir
- 自动化输出：$outputDir

## 已自动验证

- [x] Stage 包含 Native、冻结边车、版本、错误码、稳定语言 `.qm` 与法务目录。
- [x] `bf_app --version`。
- [x] 系统 UI、边车握手、CSV/EDF 打开切片。
- [x] 离线 DSP、回放、频谱分析、文件回放录制与片段导出。

## 需在真实桌面人工验证

- [ ] 打开 CSV、EDF、HDF5、BrainVision、EEGLAB、FIF 样本；检查元数据、通道数、波形和单位。
- [ ] 检查通道显隐、滚动、1--120 秒时间窗、幅值缩放和亮/暗主题。
- [ ] 检查 Raw/CAR/Bipolar、陷波、带通及每通道覆盖；检查 PSD、FFT、STFT、趋势和相干性。
- [ ] 检查播放/暂停/seek/0.25--4 倍速、标注新增/编辑/删除、CSV 标注导入导出。
- [ ] 检查视频偏移、EEG 时钟从属、空白段黑屏和倍速跟随。
- [ ] 检查录制状态、磁盘告警、EDF/HDF5 输出和 EDF/HDF5/CSV 片段导出回读。
- [ ] 使用正式公钥和旧版真实授权 key 验证激活兼容性；若无材料，记录为 pending。
- [ ] 真实厂商样本 Intan、BBK/DAT、Open Ephys、Plexon、Blackrock 的验收不在本次基础夹具范围内。
$installerSection
## 签名

- 当前脚本只记录 Authenticode 状态：见 `signature-status.json`。
- 未购买代码签名证书时，`dev` 包可为 unsigned；记录为 `skipped`，不得标记为 `rc` 或 `stable`。
"@

$checklistPath = Join-Path $outputDir "windows-acceptance-checklist.md"
$checklist | Out-File -Encoding utf8 $checklistPath

Write-Host "Windows automated acceptance passed." -ForegroundColor Green
Write-Host "Manual checklist: $checklistPath"
Write-Host "Signature status: $(Join-Path $outputDir 'signature-status.json')"
