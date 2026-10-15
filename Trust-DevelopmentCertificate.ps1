# Explicitly opt-in administrator action. This is separate from installing
# a driver: it makes exactly one local code-signing identity trusted.
[CmdletBinding()]
param([string]$ExpectedThumbprint = 'AEA4C9DC9E3FCA11042C2CCBCAFD617A8C3E8956')
$ErrorActionPreference = 'Stop'
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Administratorrechte erforderlich.' }
$certPath = Join-Path $PSScriptRoot 'PL80EClean-Development.cer'
if (-not (Test-Path -LiteralPath $certPath)) { $certPath = Join-Path $PSScriptRoot 'dist\PL80E-Open-Source-DEV\PL80EClean-Development.cer' }
$cert = New-Object Security.Cryptography.X509Certificates.X509Certificate2($certPath)
try {
    if ($ExpectedThumbprint -notmatch '^[A-Fa-f0-9]{40}$' -or $cert.Thumbprint -ne $ExpectedThumbprint) { throw 'Zertifikat-Fingerabdruck stimmt nicht mit der Freigabe überein.' }
    if ($cert.Subject -ne 'CN=PL80E Clean Local Development' -or $cert.HasPrivateKey) { throw 'Unerwartete Zertifikatsidentität.' }
    if ($cert.NotBefore -gt (Get-Date) -or $cert.NotAfter -le (Get-Date)) { throw 'Zertifikat ist noch nicht oder nicht mehr gültig.' }
    $eku = $cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' }
    $constraints = $cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.19' }
    if (-not $eku -or $eku.EnhancedKeyUsages.Count -ne 1 -or $eku.EnhancedKeyUsages[0].Value -ne '1.3.6.1.5.5.7.3.3' -or
        -not $constraints -or $constraints.CertificateAuthority) { throw 'Nur das nicht als CA ausgestellte Code-Signing-Zertifikat darf verwendet werden.' }
    foreach ($name in @('Root','TrustedPublisher')) {
        $store = New-Object Security.Cryptography.X509Certificates.X509Store($name,'LocalMachine')
        try { $store.Open('ReadWrite'); $store.Add($cert) } finally { $store.Close() }
    }
    Write-Host "Lokales Code-Signing-Zertifikat freigegeben: $($cert.Thumbprint), gültig bis $($cert.NotAfter)."
} finally { $cert.Dispose() }
