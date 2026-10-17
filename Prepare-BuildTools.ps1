[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$toolRoot = Join-Path $PSScriptRoot 'tools'
New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
$index = Invoke-RestMethod -Uri 'https://ziglang.org/download/index.json'
$release = $index.'0.13.0'.'x86_64-windows'
if (-not $release.tarball -or -not $release.shasum) { throw 'Versionsdaten für Zig 0.13.0 fehlen.' }
$zip = Join-Path $toolRoot 'zig.zip'
Invoke-WebRequest -Uri $release.tarball -OutFile $zip
if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $release.shasum) { throw 'Zig-Prüfsumme stimmt nicht.' }
$release | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $toolRoot 'zig-release.json') -Encoding UTF8
Expand-Archive -LiteralPath $zip -DestinationPath $toolRoot -Force
$include = Join-Path $toolRoot 'ddi-headers'
New-Item -ItemType Directory -Path $include -Force | Out-Null
foreach ($name in @('ddrawint.h','d3dnthal.h','dvp.h')) {
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/mingw-w64/mingw-w64/v12.0.0/mingw-w64-headers/ddk/include/$name" -OutFile (Join-Path $include $name)
}
Get-ChildItem -LiteralPath $include -File | Get-FileHash | Select-Object Path,Hash |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $toolRoot 'ddi-header-hashes.json') -Encoding UTF8
$packages = @{
    'microsoft.windows.sdk.buildtools' = '0FE1F6A5E83781E72A6506C94AED73F17897E0AC4421AEDD13560BADDD228586'
    'microsoft.windows.wdk.x64' = 'C393D03DFB640B5C92F546B32F6770EF68CD3AAF691956E7D66D8E2C28A1B55E'
}
foreach ($id in $packages.Keys) {
    $archive = Join-Path $toolRoot "$id.zip"
    $version = '10.0.26100.6584'
    Invoke-WebRequest -Uri "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg" -OutFile $archive
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $packages[$id]) { throw "Prüfsumme stimmt nicht: $id" }
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $toolRoot $id) -Force
}
& (Join-Path $PSScriptRoot 'Prepare-SetupTools.ps1')
Write-Host 'Portable build tools prepared. Run Build.ps1 next.'
