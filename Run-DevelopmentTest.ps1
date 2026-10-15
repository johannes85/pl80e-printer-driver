[CmdletBinding()]
param([switch]$TrustDevelopmentCertificate)
$ErrorActionPreference = 'Stop'
$logRoot = Join-Path $PSScriptRoot 'test-output'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$statusPath = Join-Path $logRoot 'installation-status.json'
Start-Transcript -LiteralPath (Join-Path $logRoot 'installation.log') -Force | Out-Null
try {
    if ($TrustDevelopmentCertificate) { & (Join-Path $PSScriptRoot 'Trust-DevelopmentCertificate.ps1') }
    & (Join-Path $PSScriptRoot 'Install.ps1') -TestOnly
    & (Join-Path $PSScriptRoot 'Test-Driver.ps1') -OutputDirectory $logRoot
    # Expose the USB queue only after the real Windows file tests pass.
    & (Join-Path $PSScriptRoot 'Install.ps1')
    [pscustomobject]@{Status='PASS'; Time=(Get-Date).ToString('o'); Queue='PL80E Open Source (DEV)'; FileTest='PASS'; PaperTest='pending'} |
        ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
} catch {
    $exception = $_.Exception
    $chain = @()
    while ($exception) {
        $chain += $exception.ToString()
        $exception = $exception.InnerException
    }
    [pscustomobject]@{Status='FAILED'; Time=(Get-Date).ToString('o'); Error=$_.Exception.Message; ExceptionChain=$chain; Details=($_ | Out-String)} |
        ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
    Write-Error $_ -ErrorAction Continue
    exit 1
} finally { Stop-Transcript | Out-Null }
