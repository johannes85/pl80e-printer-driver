#Requires -RunAsAdministrator
#Requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$stateRoot=Join-Path $env:ProgramData 'PL80E Open Source'
$statePath=Join-Path $stateRoot 'installation.json'
if(-not (Test-Path -LiteralPath $statePath)) {throw 'No installation state found. This script only removes installations created by its matching Install.ps1.'}
$state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if($state.DriverName -ne 'PL80E Open Source' -or $state.Thumbprint -notmatch '^[A-Fa-f0-9]{40}$') {throw 'Invalid installation state.'}
foreach($name in $state.Queues) {
    $q=Get-Printer -Name $name -ErrorAction SilentlyContinue
    if($q -and $q.DriverName -ne $state.DriverName) {throw "Queue now uses another driver: $name"}
    if($q -and (Get-PrintJob -PrinterName $name)) {throw "Print jobs are still queued: $name"}
}
$other=Get-Printer | Where-Object {$_.DriverName -eq $state.DriverName -and $_.Name -notin $state.Queues}
if($other) {throw 'Other queues still use this driver. Remove or migrate them first.'}
foreach($name in $state.Queues) {if(Get-Printer -Name $name -ErrorAction SilentlyContinue) {Remove-Printer -Name $name}}
if(Get-PrinterDriver -Name $state.DriverName -ErrorAction SilentlyContinue) {Remove-PrinterDriver -Name $state.DriverName}
if($state.InfPath) {
    $package=Get-WindowsDriver -Online -All | Where-Object {$_.OriginalFileName -eq $state.InfPath}
    foreach($item in $package) {
        if($item.Driver -notmatch '^oem\d+\.inf$') {throw 'Unexpected Driver Store package name.'}
        & pnputil.exe /delete-driver $item.Driver
        if($LASTEXITCODE -ne 0) {throw 'Driver Store package is still in use. Certificate trust retained; close printing applications and retry.'}
    }
}
foreach($store in @('TrustedPublisher','Root','My')) {
    $path="Cert:\LocalMachine\$store\$($state.Thumbprint)"
    $cert=Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
    if($cert) {
        if($cert.Subject -ne 'CN=PL80E Open Source Local Installation') {throw 'Unexpected certificate; nothing removed.'}
        if($store -eq 'My') {Remove-Item -LiteralPath $path -DeleteKey} else {Remove-Item -LiteralPath $path}
    }
}
# Delete only known installer-owned files, never a recursive computed path.
foreach($file in @('PL80EClean.dll','PL80EClean.gpd','PL80EClean.inf','PL80EClean.cat')) {
    $path=Join-Path $stateRoot ('Driver\'+$file)
    if(Test-Path -LiteralPath $path) {Remove-Item -LiteralPath $path}
}
Remove-Item -LiteralPath $statePath
Write-Host 'Driver, managed queues and local signing certificate removed.'
