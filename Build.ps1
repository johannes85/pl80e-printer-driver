[CmdletBinding()]
param([string]$Zig = (Join-Path $PSScriptRoot 'tools\zig-windows-x86_64-0.13.0\zig.exe'))
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Zig)) { throw 'Portable Zig 0.13.0 fehlt. Siehe README.md.' }
$build = Join-Path $PSScriptRoot 'build'
$package = Join-Path $build 'release-driver'
New-Item -ItemType Directory -Path $build,$package -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $build 'zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $build 'zig-local-cache'
& $Zig cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror `
    (Join-Path $PSScriptRoot 'src\raster.c') (Join-Path $PSScriptRoot 'tests\raster_test.c') `
    -o (Join-Path $build 'raster-test.exe')
if ($LASTEXITCODE -ne 0) { throw 'Raster-Test konnte nicht kompiliert werden.' }
& (Join-Path $build 'raster-test.exe') (Join-Path $build 'row-preservation.prn')
if ($LASTEXITCODE -ne 0) { throw 'Raster-Test fehlgeschlagen.' }
$rc = Join-Path $PSScriptRoot 'tools\microsoft.windows.sdk.buildtools\bin\10.0.26100.0\x64\rc.exe'
if (-not (Test-Path -LiteralPath $rc)) { throw 'Der portable Microsoft-Ressourcencompiler fehlt.' }
& $rc /nologo /fo (Join-Path $build 'driver-version.res') (Join-Path $PSScriptRoot 'src\driver-version.rc')
if ($LASTEXITCODE -ne 0) { throw 'Versionsressource konnte nicht gebaut werden.' }
& $Zig cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror -shared `
    -I (Join-Path $PSScriptRoot 'tools\ddi-headers') `
    (Join-Path $PSScriptRoot 'src\driver.c') (Join-Path $PSScriptRoot 'src\raster.c') `
    (Join-Path $PSScriptRoot 'src\config.c') `
    (Join-Path $PSScriptRoot 'src\PL80EClean.def') `
    (Join-Path $build 'driver-version.res') `
    -lgdi32 -lwinspool -lkernel32 -luser32 -lcomctl32 -lshell32 -o (Join-Path $package 'PL80EClean.dll')
if ($LASTEXITCODE -ne 0) { throw 'Treiber-Build fehlgeschlagen.' }
& $Zig cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror `
    -I (Join-Path $PSScriptRoot 'tools\ddi-headers') `
    (Join-Path $PSScriptRoot 'tests\driver_api_test.c') -lversion -luser32 -o (Join-Path $build 'driver-api-test.exe')
if ($LASTEXITCODE -ne 0) { throw 'DLL-ABI-Test konnte nicht gebaut werden.' }
& (Join-Path $build 'driver-api-test.exe') (Join-Path $package 'PL80EClean.dll')
if ($LASTEXITCODE -ne 0) { throw 'DLL-ABI-Test fehlgeschlagen.' }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'driver\PL80EClean.gpd'),`
    (Join-Path $PSScriptRoot 'driver\PL80EClean.inf') -Destination $package -Force
foreach ($file in @('Install.ps1','Uninstall.ps1','Test-Driver.ps1','Test-Density.ps1','Configure.ps1','PrintApi.cs','README.md','Trust-DevelopmentCertificate.ps1','Remove-DevelopmentTrust.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $package -Force
}
Get-ChildItem -LiteralPath $package -File | Where-Object Name -ne 'SHA256.json' | Get-FileHash -Algorithm SHA256 |
    Select-Object @{n='File';e={Split-Path $_.Path -Leaf}},Hash |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'SHA256.json') -Encoding UTF8
# Ship an explicit allowlist. The development directory above is intentionally
# separate and may contain debug files and results from local signing tests.
$release=Join-Path $PSScriptRoot 'dist\PL80E-Open-Source'
New-Item -ItemType Directory -Path $release -Force | Out-Null
$allowed=@('PL80EClean.dll','PL80EClean.gpd','PL80EClean.inf','PL80EClean.cat','PL80ESetup.exe','Install.ps1','Uninstall.ps1','README.md','LICENSE','DOTNET_LICENSE.txt','DOTNET_NOTICES.txt')
if(Get-ChildItem -LiteralPath $release -Force | Where-Object {$_.PSIsContainer -or $_.Name -notin $allowed}) {
    throw 'Unexpected files in release directory; use a clean release directory.'
}
foreach($name in @('PL80EClean.dll','PL80EClean.gpd','PL80EClean.inf')) {
    Copy-Item -LiteralPath (Join-Path $package $name) -Destination $release -Force
}
foreach($name in @('Install.ps1','Uninstall.ps1')) {
    $oldScript=Join-Path $release $name
    if(Test-Path -LiteralPath $oldScript) {Remove-Item -LiteralPath $oldScript}
}
foreach($name in @('README.md')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('package\'+$name)) -Destination $release -Force
}
& (Join-Path $PSScriptRoot 'Build-Setup.ps1')
Copy-Item -LiteralPath (Join-Path $build 'setup-publish\PL80ESetup.exe') -Destination $release -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tools\dotnet\LICENSE.txt') -Destination (Join-Path $release 'DOTNET_LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'tools\dotnet\ThirdPartyNotices.txt') -Destination (Join-Path $release 'DOTNET_NOTICES.txt') -Force
foreach($name in @('LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $release -Force
}
$catalog=Join-Path $release 'PL80EClean.cat'
if(Test-Path -LiteralPath $catalog) {Remove-Item -LiteralPath $catalog}
$inf2cat=Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'tools\microsoft.windows.wdk.x64') -Filter Inf2Cat.exe -Recurse |
    Select-Object -First 1 -ExpandProperty FullName
if(-not $inf2cat) {throw 'Inf2Cat build tool is missing.'}
& $inf2cat ("/driver:"+$release) /os:10_X64
if($LASTEXITCODE -ne 0) {throw 'Release catalog generation failed.'}
$versionLine=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'src\version.h') | Where-Object {$_ -match '^#define CLEAN_VERSION_TEXT '}
if($versionLine -notmatch '"([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)"') {throw 'Cannot determine package version.'}
$version=$Matches[1]
Compress-Archive -Path (Join-Path $release '*') -DestinationPath (Join-Path $PSScriptRoot ("dist\PL80E-Open-Source-$version.zip")) -Force
Write-Host "Release package: $release"

# Compact installer uses the same driver payload with a separately installed runtime.
$compact=Join-Path $PSScriptRoot 'dist\PL80E-Open-Source-Compact'
New-Item -ItemType Directory -Path $compact -Force | Out-Null
$compactFiles=@('PL80EClean.dll','PL80EClean.gpd','PL80EClean.inf','PL80EClean.cat','PL80ESetup.exe','README.md','LICENSE','DOTNET_LICENSE.txt','DOTNET_NOTICES.txt')
if(Get-ChildItem -LiteralPath $compact -Force | Where-Object {$_.PSIsContainer -or $_.Name -notin $compactFiles}) {
    throw 'Unexpected files in compact directory; use a clean compact directory.'
}
$compactSetup=Join-Path $build 'setup-compact'
& (Join-Path $PSScriptRoot 'Build-Setup.ps1') -FrameworkDependent -OutputDirectory $compactSetup
foreach($name in $compactFiles | Where-Object {$_ -notin @('PL80ESetup.exe','README.md')}) {
    Copy-Item -LiteralPath (Join-Path $release $name) -Destination $compact -Force
}
Copy-Item -LiteralPath (Join-Path $compactSetup 'PL80ESetup.exe') -Destination $compact -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package\README_COMPACT.md') -Destination (Join-Path $compact 'README.md') -Force
Compress-Archive -Path (Join-Path $compact '*') -DestinationPath (Join-Path $PSScriptRoot ("dist\PL80E-Open-Source-$version-compact.zip")) -Force
Write-Host "Compact package: $compact"

# Independent DEV identity, including spool-directory filenames and hardware ID.
$dev=Join-Path $PSScriptRoot 'dist\PL80E-Open-Source-DEV'
New-Item -ItemType Directory -Path $dev -Force | Out-Null
& $rc /nologo /d CLEAN_DEV /fo (Join-Path $build 'driver-version-dev.res') (Join-Path $PSScriptRoot 'src\driver-version.rc')
if($LASTEXITCODE -ne 0) {throw 'DEV resource build failed.'}
$devDef=Join-Path $build 'PL80ECleanDev.def'
[IO.File]::WriteAllText($devDef,[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'src\PL80EClean.def')).Replace('LIBRARY PL80EClean','LIBRARY PL80ECleanDev'))
& $Zig cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror -shared -DCLEAN_DEV `
    -I (Join-Path $PSScriptRoot 'tools\ddi-headers') `
    (Join-Path $PSScriptRoot 'src\driver.c') (Join-Path $PSScriptRoot 'src\raster.c') `
    (Join-Path $PSScriptRoot 'src\config.c') $devDef (Join-Path $build 'driver-version-dev.res') `
    -lgdi32 -lwinspool -lkernel32 -luser32 -lcomctl32 -lshell32 -o (Join-Path $dev 'PL80ECleanDev.dll')
if($LASTEXITCODE -ne 0) {throw 'DEV driver build failed.'}
& (Join-Path $build 'driver-api-test.exe') (Join-Path $dev 'PL80ECleanDev.dll')
if($LASTEXITCODE -ne 0) {throw 'DEV driver tests failed.'}
foreach($extension in @('inf','gpd')) {
    $text=[IO.File]::ReadAllText((Join-Path $PSScriptRoot ('driver\PL80EClean.'+$extension)))
    $text=$text.Replace('PL80E Open Source','PL80E Open Source (DEV)').Replace('PL80EClean','PL80ECleanDev')
    [IO.File]::WriteAllText((Join-Path $dev ('PL80ECleanDev.'+$extension)),$text)
}
foreach($name in @('Install.ps1','Uninstall.ps1','Configure.ps1','Test-Driver.ps1','Test-Density.ps1','PrintApi.cs','Trust-DevelopmentCertificate.ps1','Remove-DevelopmentTrust.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $dev -Force
}
Write-Host "Development package: $dev (run Prepare-Signing.ps1 before installing)"
