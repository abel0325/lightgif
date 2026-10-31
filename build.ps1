[CmdletBinding()]
param([switch]$Tests)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/scripts/toolchain.ps1"
$src = Join-Path $PSScriptRoot 'src'
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location -LiteralPath $out
try {
    & rc.exe /nologo /I $src /fo app.res "$src/app.rc"
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    & cl.exe /nologo /std:c++17 /utf-8 /O1 /MT /EHsc /GL /W3 /DUNICODE /D_UNICODE /DNOMINMAX "$src/app.cpp" "$src/renderer.cpp" "$src/gif_encoder.cpp" app.res /Fe:LightGif.exe /link /SUBSYSTEM:WINDOWS,6.01 /MACHINE:X86 /LTCG /OPT:REF /OPT:ICF user32.lib gdi32.lib gdiplus.lib comctl32.lib comdlg32.lib shell32.lib uxtheme.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
    if ($Tests) {
        foreach ($test in @('encoder_test','renderer_test','integration_benchmark')) {
            $sources = @("$PSScriptRoot/tests/$test.cpp", "$src/gif_encoder.cpp")
            if ($test -ne 'encoder_test') { $sources += "$src/renderer.cpp" }
            & cl.exe /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W3 @sources "/Fe:$test.exe" /link gdiplus.lib user32.lib gdi32.lib
            if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $test" }
        }
    }
    Get-Item -LiteralPath "$out/LightGif.exe" | Select-Object Name,Length
} finally { Pop-Location }
