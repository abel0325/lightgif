[CmdletBinding()]
param([string]$Python = 'python', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
if (!$SkipBuild) { & "$PSScriptRoot/build.ps1" -Tests }
$Python = (Get-Command $Python -ErrorAction Stop).Source
& $Python -c "import PIL, numpy; print('Decoder:', PIL.__version__, 'NumPy:', numpy.__version__)"
if ($LASTEXITCODE -ne 0) { throw 'Install tests/requirements.txt into your test Python environment.' }
$out = Join-Path $PSScriptRoot 'build/test-results'
New-Item -ItemType Directory -Path $out -Force | Out-Null
function Run-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
Push-Location -LiteralPath $out
try {
    Run-Checked "$PSScriptRoot/build/encoder_test.exe" @()
    Run-Checked $Python @("$PSScriptRoot/tests/validate.py")
    Run-Checked "$PSScriptRoot/build/renderer_test.exe" @()
    Run-Checked "$PSScriptRoot/build/integration_benchmark.exe" @()
    Run-Checked $Python @("$PSScriptRoot/tests/validate_integration.py")
    # A GUI-subsystem executable must be explicitly awaited in PowerShell.
    $p = Start-Process -FilePath "$PSScriptRoot/build/LightGif.exe" -ArgumentList @('--self-test', "`"$out/app`"") -WindowStyle Hidden -PassThru -Wait
    if ($p.ExitCode -ne 0) { throw "Application self-test failed: $($p.ExitCode)" }
    Get-Content -LiteralPath "$out/app/selftest.txt"
    Run-Checked $Python @("$PSScriptRoot/tests/validate_app.py", "$out/app")
    $p = Start-Process -FilePath "$PSScriptRoot/build/LightGif.exe" -ArgumentList @('--ui-snapshot', "`"$out/interface.png`"") -WindowStyle Hidden -PassThru -Wait
    if ($p.ExitCode -ne 0 -or !(Test-Path -LiteralPath "$out/interface.png")) { throw 'Hidden widget integration test failed.' }
    Write-Host 'PASS: encoder, renderer, app exports, presets, and hidden widget integration.'
} finally { Pop-Location }
