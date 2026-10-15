[CmdletBinding()]
param(
    [string]$QueueName = 'PL80E Open Source (DEV)',
    [ValidateRange(0,10)][decimal]$GapMm = 3,
    [ValidateRange(1,15)][int]$Density = 10,
    [ValidateRange(2,6)][int]$Speed = 5
)
$ErrorActionPreference = 'Stop'
$printer = Get-Printer -Name $QueueName -ErrorAction Stop
if ($printer.DriverName -ne 'PL80E Open Source (DEV)') { throw 'Nur PL80E-Clean-Warteschlangen werden konfiguriert.' }
Add-Type -Path (Join-Path $PSScriptRoot 'PrintApi.cs')
[CleanPrintApi]::Configure($QueueName,[uint32][Math]::Round($GapMm*10),$Density,$Speed)
Write-Host "Einstellungen für neue Druckaufträge: Lücke $GapMm mm, Dichte $Density, Geschwindigkeit $Speed."
