[CmdletBinding()]
param(
    [string]$SourcePrinter = 'PL80E',
    [string]$QueueName = 'PL80E Open Source (DEV)',
    [string]$PortName,
    [ValidateRange(0,10)][decimal]$GapMm = 3,
    [ValidateRange(1,15)][int]$Density = 10,
    [ValidateRange(2,6)][int]$Speed = 5,
    [switch]$TestOnly
)
$ErrorActionPreference = 'Stop'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Die Installation braucht eine als Administrator gestartete 64-Bit-PowerShell. Siehe README.md.'
}
if (-not [Environment]::Is64BitProcess) { throw 'Bitte die 64-Bit-PowerShell verwenden.' }
if ($QueueName -eq $SourcePrinter) { throw 'Eine neue Warteschlange verwenden; der bestehende PL80E bleibt erhalten.' }
if ($TestOnly) {
    if ($PSBoundParameters.ContainsKey('QueueName') -eq $false) { $QueueName = 'PL80E Open Source Test (DEV)' }
    $PortName = 'FILE:'
} elseif (-not $PortName) {
    $PortName = (Get-Printer -Name $SourcePrinter -ErrorAction Stop).PortName
}
if (-not (Get-PrinterPort -Name $PortName -ErrorAction SilentlyContinue)) { throw "Anschluss fehlt: $PortName" }
$package = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $package 'PL80ECleanDev.dll'))) {
    $package = Join-Path $PSScriptRoot 'dist\PL80E-Open-Source-DEV'
}
foreach ($file in @('PL80ECleanDev.dll','PL80ECleanDev.gpd')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $file))) { throw "Build fehlt: $file" }
}
$existing = Get-Printer -Name $QueueName -ErrorAction SilentlyContinue
if ($existing -and ($existing.DriverName -ne 'PL80E Open Source (DEV)' -or $existing.PortName -ne $PortName)) {
    throw "Die Warteschlange '$QueueName' existiert mit einem anderen Treiber oder Anschluss."
}
foreach ($file in @('PL80ECleanDev.dll','PL80ECleanDev.cat')) {
    $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $package $file)
    if ($signature.Status -ne 'Valid') {
        throw "Die Signatur von $file wird von Windows nicht akzeptiert ($($signature.Status)). Die separate Zertifikatsfreigabe ist erforderlich; siehe README.md."
    }
}
# Stage a signed package through the regular Driver Store path. No signing
# policy is relaxed; unsigned/untrusted packages are rejected before staging.
$inf = Join-Path $package 'PL80ECleanDev.inf'
& (Join-Path $env:WINDIR 'System32\pnputil.exe') /add-driver $inf
if ($LASTEXITCODE -ne 0) { throw "Windows hat das signierte Paket abgelehnt (pnputil $LASTEXITCODE)." }
$infHash = (Get-FileHash -LiteralPath $inf -Algorithm SHA256).Hash
$storeRoot = Join-Path $env:WINDIR 'System32\DriverStore\FileRepository'
$storeInf = Get-ChildItem -LiteralPath $storeRoot -Filter 'pl80ecleandev.inf_amd64_*' -Directory |
    ForEach-Object { Join-Path $_.FullName 'pl80ecleandev.inf' } |
    Where-Object { (Test-Path -LiteralPath $_) -and (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash -eq $infHash } |
    Select-Object -First 1
if (-not $storeInf) { throw 'Das identische INF wurde nach dem Staging nicht im Driver Store gefunden.' }
Add-PrinterDriver -Name 'PL80E Open Source (DEV)' -InfPath $storeInf
Add-Type -Path (Join-Path $PSScriptRoot 'PrintApi.cs')
if (-not $existing) {
    Add-Printer -Name $QueueName -DriverName 'PL80E Open Source (DEV)' -PortName $PortName -PrintProcessor 'winprint' -Datatype 'RAW'
}
[CleanPrintApi]::Configure($QueueName,[uint32][Math]::Round($GapMm*10),$Density,$Speed)
# Installing creates no print job and does not change the default printer.
Get-Printer -Name $QueueName | Select-Object Name,DriverName,PortName
Write-Host 'Installiert. Jetzt Test-Driver.ps1 an der FILE:-Testwarteschlange ausführen.'
