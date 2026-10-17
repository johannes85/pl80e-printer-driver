[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$toolRoot=Join-Path $PSScriptRoot 'tools'
$destination=Join-Path $toolRoot 'dotnet'
if(Test-Path -LiteralPath (Join-Path $destination 'dotnet.exe')) {
    $version=& (Join-Path $destination 'dotnet.exe') --version
    if($LASTEXITCODE -eq 0 -and $version -match '^10\.') {Write-Host $version;return}
}
New-Item -ItemType Directory -Path $toolRoot -Force | Out-Null
$archive=Join-Path $toolRoot 'dotnet-sdk.zip'
$url='https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.402/dotnet-sdk-10.0.402-win-x64.zip'
$hash='AE4FD528CB7E9F6AF6AAF7DE36715A69F761F183744F1BB3025D19F5A92447090EC561410E46266C1D10A63781ED6F3DE771760BC76C876D87DAABC367B898CD'
Invoke-WebRequest -Uri $url -OutFile $archive
if((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $hash) {throw '.NET SDK SHA512 mismatch.'}
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
Write-Host 'Portable .NET 10 SDK prepared. No global .NET installation was changed.'
