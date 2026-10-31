[CmdletBinding()]
param([string]$Version = '1.0.0')
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+([.-][A-Za-z0-9.-]+)?$') { throw 'Invalid version.' }
$root = Split-Path -Parent $PSScriptRoot
$name = "LightGif-$Version-windows-x86"
$stage = Join-Path $root "dist/$name"
$zip = Join-Path $root "dist/$name.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zip)) { throw 'Package destination exists; choose a new version or archive the previous package first.' }
if (!(Test-Path -LiteralPath "$root/build/LightGif.exe")) { throw 'Run build.ps1 and test.ps1 before packaging.' }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach ($pair in @(
    @('build/LightGif.exe', 'LightGif.exe'),
    @('LICENSE', 'LICENSE.txt'),
    @('THIRD_PARTY.md', 'THIRD_PARTY.md'),
    @('docs/USAGE.zh-CN.md', '使用说明.md'),
    @('docs/USAGE.en.md', 'README.en.md'),
    @('assets/demo.gif', 'example.gif'),
    @('examples/default.gifcfg', 'default.gifcfg')
)) {
    Copy-Item -LiteralPath (Join-Path $root $pair[0]) -Destination (Join-Path $stage $pair[1])
}
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $root 'dist/SHA256SUMS.txt'), "$hash  $name.zip`n", [Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath $zip | Select-Object Name,Length
Write-Host "SHA256 $hash"
