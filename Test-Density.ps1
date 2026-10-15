[CmdletBinding()]
param([string]$PrinterName='PL80E Open Source Test (DEV)')
$ErrorActionPreference='Stop'
$queue=Get-Printer -Name $PrinterName
if($queue.DriverName -ne 'PL80E Open Source (DEV)' -or $queue.PortName -ne 'FILE:') { throw 'Nur die Clean-Dateitestwarteschlange verwenden.' }
Add-Type -Path (Join-Path $PSScriptRoot 'PrintApi.cs')
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'test-output') -Force | Out-Null
$results=@()
foreach($density in 1..15) {
    $capture=Join-Path $PSScriptRoot ('test-output\density-'+$density+'-'+[guid]::NewGuid().ToString('N')+'.prn')
    [CleanPrintApi]::RenderFixture($PrinterName,$capture,$false,1,5,$density) | Out-Null
    $ready=$false
    for($attempt=0;$attempt -lt 100;$attempt++) {
        if((Test-Path -LiteralPath $capture) -and (Get-Item -LiteralPath $capture).Length -gt 119900) { $ready=$true; break }
        Start-Sleep -Milliseconds 200
    }
    if(-not $ready) { throw 'Druckdatei wurde nicht erzeugt.' }
    $bytes=[IO.File]::ReadAllBytes($capture)
    $header=[Text.Encoding]::ASCII.GetString($bytes,0,[Math]::Min(240,$bytes.Length))
    if($header -notmatch ('(?m)^DENSITY '+$density+'\r?$')) { throw "Falscher DENSITY-Wert fuer $density." }
    if($header -notmatch '(?m)^SPEED 5\r?$') { throw 'Geschwindigkeit wurde unerwartet veraendert.' }
    $results += [pscustomobject]@{Density=$density;Status='PASS';Capture=$capture}
    Write-Output "PASS: DENSITY $density through Windows GDI/DEVMODE/FILE"
}
[pscustomobject]@{Status='PASS';Time=(Get-Date).ToString('o');Runs=$results} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'test-output\density-validation.json') -Encoding UTF8
