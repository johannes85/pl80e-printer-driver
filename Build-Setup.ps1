[CmdletBinding()]
param([string]$OutputDirectory=(Join-Path $PSScriptRoot 'build\setup-publish'),[switch]$FrameworkDependent)
$ErrorActionPreference='Stop'
$dotnet=Join-Path $PSScriptRoot 'tools\dotnet\dotnet.exe'
if(-not (Test-Path -LiteralPath $dotnet)) {throw 'Run Prepare-SetupTools.ps1 first to download the portable .NET 10 SDK.'}
$env:DOTNET_CLI_HOME=Join-Path $PSScriptRoot 'build\dotnet-home'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
$env:DOTNET_NOLOGO='1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE='false'
$env:NUGET_PACKAGES=Join-Path $PSScriptRoot 'build\nuget-packages'
$versionLine=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'src\version.h') | Where-Object {$_ -match '^#define CLEAN_VERSION_TEXT '}
if($versionLine -notmatch '"([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)"') {throw 'Cannot determine setup version.'}
$version=$Matches[1]
& $dotnet publish (Join-Path $PSScriptRoot 'setup\PL80ESetup.csproj') -c Release -r win-x64 `
    --self-contained $(if($FrameworkDependent){'false'}else{'true'}) -p:Version=$version -o $OutputDirectory
if($LASTEXITCODE -ne 0) {throw 'C# setup build failed.'}
Write-Host "Setup built: $(Join-Path $OutputDirectory 'PL80ESetup.exe')"
