$ErrorActionPreference = 'Stop'
$pfx = $null
$version = Get-Content version.json -Raw | ConvertFrom-Json
$parameters = @{
    QtRoot = $env:QT_ROOT_DIR
    Python = (Get-Command python).Source
    LicensePublicKeyFile = (Join-Path $PWD 'license_public.pem')
}
if ($version.release_channel -in @('stable', 'rc')) {
    if (-not $env:BF_CODE_SIGN_PFX_BASE64 -or -not $env:BF_CODE_SIGN_PASSWORD) {
        throw 'Stable requires BF_CODE_SIGN_PFX_BASE64 and BF_CODE_SIGN_PASSWORD repository secrets.'
    }
    $pfx = Join-Path $env:RUNNER_TEMP 'community-signing.pfx'
    [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:BF_CODE_SIGN_PFX_BASE64))
    $parameters.CertificatePfx = $pfx
    $parameters.CertificatePassword = $env:BF_CODE_SIGN_PASSWORD
    $parameters.SignTool = (Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin\*\x64\signtool.exe' | Sort-Object FullName -Descending | Select-Object -First 1).FullName
} else {
    $parameters.SkipSigning = $true
}
try {
    & ./scripts/windows/build_release.ps1 @parameters
} finally {
    if ($pfx -and (Test-Path $pfx)) { Remove-Item $pfx -Force }
}
Copy-Item build-info.json release/stage/BoundFreeEEG/
# Run existing smoke suite against the staged frozen runtime.
& ./scripts/windows/verify_windows_release.ps1
# Exercise actual Setup and uninstaller on the disposable hosted Windows runner.
$installer = (Get-ChildItem dist/BoundFreeEEG_Setup_*.exe | Select-Object -First 1).FullName
$destination = Join-Path $env:RUNNER_TEMP 'community-installed'
$install = Start-Process $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$destination`"") -Wait -PassThru
if ($install.ExitCode -notin @(0, 3010)) { throw "Setup exit code $($install.ExitCode)" }
$originalPath = $env:PATH
try {
    # Ensure a developer Qt/Python path cannot rescue the installed application.
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    Remove-Item Env:BF_PYTHON_EXECUTABLE -ErrorAction SilentlyContinue
    Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue
    $env:BF_RESOURCE_ROOT = $destination
    foreach ($argument in @('--version', '--smoke-sidecar', '--smoke-ui-workspace-navigation')) {
        $result = Start-Process (Join-Path $destination 'bf_app.exe') -ArgumentList $argument -WorkingDirectory $env:RUNNER_TEMP -Wait -PassThru
        if ($result.ExitCode -ne 0) { throw "Installed app failed $argument ($($result.ExitCode))" }
    }
} finally {
    $env:PATH = $originalPath
    Remove-Item Env:BF_RESOURCE_ROOT -ErrorAction SilentlyContinue
}
$uninstall = Start-Process (Join-Path $destination 'unins000.exe') -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
if ($uninstall.ExitCode -ne 0) { throw "Uninstall exit code $($uninstall.ExitCode)" }
if (Test-Path (Join-Path $destination 'bf_app.exe')) { throw 'Uninstaller left bf_app.exe behind' }
