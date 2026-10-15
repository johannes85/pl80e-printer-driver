# Removes only the two known, obsolete packages created by this investigation.
# Never uses /force or /uninstall and never touches the manufacturer's package.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
try {
$active = Get-PrinterDriver -Name 'PL80E Clean' -ErrorAction Stop
$activeHash = (Get-FileHash -LiteralPath $active.InfPath -Algorithm SHA256).Hash
$results = @()
foreach ($published in @('oem15.inf','oem16.inf')) {
    $path = Join-Path $env:WINDIR ('INF\'+$published)
    if (-not (Test-Path -LiteralPath $path)) { continue }
    $text = [IO.File]::ReadAllText($path)
    if ($text -notmatch 'PL80E Clean Project' -or $text -notmatch '"PL80E Clean"' -or
        $text -notmatch '4D36E979-E325-11CE-BFC1-08002BE10318') { throw "Identität unklar: $published" }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $activeHash) { throw "Paket ist aktiv: $published" }
    $output = (& (Join-Path $env:WINDIR 'System32\pnputil.exe') /delete-driver $published 2>&1 | Out-String)
    $code = $LASTEXITCODE
    $results += [pscustomobject]@{Package=$published; ExitCode=$code; Output=$output}
    if ($code -ne 0) { throw "Windows hat die Entfernung von $published verweigert: $output" }
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'test-output\package-cleanup.json') -Encoding UTF8
} catch {
    [pscustomobject]@{Status='FAILED';Error=$_.Exception.Message;Details=($_ | Out-String)} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'test-output\package-cleanup.json') -Encoding UTF8
    exit 1
}
