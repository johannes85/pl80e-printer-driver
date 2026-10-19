[CmdletBinding()]
param(
    [string]$OutputDirectory=(Join-Path $PSScriptRoot 'build\setup-publish'),
    [string]$Zig=(Join-Path $PSScriptRoot 'tools\zig-windows-x86_64-0.13.0\zig.exe'),
    [switch]$IntegrationTest
)
$ErrorActionPreference='Stop'
if(-not (Test-Path -LiteralPath $Zig)) {throw 'Run Prepare-BuildTools.ps1 first.'}
$rc=Join-Path $PSScriptRoot 'tools\microsoft.windows.sdk.buildtools\bin\10.0.26100.0\x64\rc.exe'
if(-not (Test-Path -LiteralPath $rc)) {throw 'The Windows resource compiler is missing. Run Prepare-BuildTools.ps1.'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $PSScriptRoot 'build\zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $PSScriptRoot 'build\zig-local-cache'
$resource=Join-Path $OutputDirectory 'setup.res'
& $rc /nologo /fo $resource (Join-Path $PSScriptRoot 'setup\setup.rc')
if($LASTEXITCODE -ne 0) {throw 'Setup resources could not be built.'}
$defines=@()
if($IntegrationTest) {$defines+= '-DSETUP_INTEGRATION_TEST'}
& $Zig cc -target x86_64-windows-gnu -std=c11 -Os -s -Wall -Wextra -Werror -municode '-Wl,--subsystem,windows' @defines `
    (Join-Path $PSScriptRoot 'setup\setup.c') $resource `
    -lwinspool -lsetupapi -lcrypt32 -lncrypt -lwintrust -ladvapi32 -luser32 -lgdi32 -lcomctl32 -lshell32 -lole32 `
    -o (Join-Path $OutputDirectory 'PL80ESetup.exe')
if($LASTEXITCODE -ne 0) {throw 'Native C setup build failed.'}
Write-Host "Setup built: $(Join-Path $OutputDirectory 'PL80ESetup.exe')"