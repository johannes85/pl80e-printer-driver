# Run with PowerShell 7. Creates an offline signing identity and signs files.
# Does not install certificates, driver packages or printer queues.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$package = Join-Path $PSScriptRoot 'dist\PL80E-Open-Source-DEV'
$signing = Join-Path $PSScriptRoot 'build\signing'
$signTool = Join-Path $PSScriptRoot 'tools\microsoft.windows.sdk.buildtools\bin\10.0.26100.0\x64\signtool.exe'
$inf2cat = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'tools\microsoft.windows.wdk.x64') -Filter 'Inf2Cat.exe' -Recurse |
    Where-Object FullName -Match '\\x86\\' | Select-Object -First 1 -ExpandProperty FullName
if (-not $inf2cat) { $inf2cat = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'tools\microsoft.windows.wdk.x64') -Filter 'Inf2Cat.exe' -Recurse | Select-Object -First 1 -ExpandProperty FullName }
if (-not $inf2cat -or -not (Test-Path -LiteralPath $signTool)) { throw 'Die portablen Microsoft-Signierwerkzeuge fehlen.' }
$newSigningDirectory = -not (Test-Path -LiteralPath $signing)
New-Item -ItemType Directory -Path $signing -Force | Out-Null
$sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
$acl = Get-Acl -LiteralPath $signing
if ($newSigningDirectory) {
    $acl.SetAccessRuleProtection($true,$false)
    $rule = [Security.AccessControl.FileSystemAccessRule]::new($sid,'FullControl','ContainerInherit, ObjectInherit','None','Allow')
    $acl.AddAccessRule($rule)
    Set-Acl -LiteralPath $signing -AclObject $acl
} elseif (-not $acl.AreAccessRulesProtected) {
    throw 'Der vorhandene private Signierordner hat keine geschützte ACL.'
}
$pfx = Join-Path $signing 'PL80EClean.pfx'
$passwordFile = Join-Path $signing 'password.dpapi'
$entropy = [Text.Encoding]::UTF8.GetBytes('PL80E Clean signing key v0.1')
if (-not (Test-Path -LiteralPath $pfx)) {
    $rsa = [Security.Cryptography.RSA]::Create(3072)
    $request = [Security.Cryptography.X509Certificates.CertificateRequest]::new(
        'CN=PL80E Clean Local Development', $rsa, [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,$true))
    $eku = [Security.Cryptography.OidCollection]::new()
    $eku.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3')) | Out-Null
    $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($eku,$true))
    $cert = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5),[DateTimeOffset]::UtcNow.AddDays(30))
    $secret = [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
    [IO.File]::WriteAllBytes($pfx,$cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx,$secret))
    $protected = [Security.Cryptography.ProtectedData]::Protect([Text.Encoding]::UTF8.GetBytes($secret),$entropy,[Security.Cryptography.DataProtectionScope]::CurrentUser)
    [IO.File]::WriteAllBytes($passwordFile,$protected)
    [IO.File]::WriteAllBytes((Join-Path $package 'PL80EClean-Development.cer'),$cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    $cert.Dispose(); $rsa.Dispose()
} else {
    $secret = [Text.Encoding]::UTF8.GetString([Security.Cryptography.ProtectedData]::Unprotect([IO.File]::ReadAllBytes($passwordFile),$entropy,[Security.Cryptography.DataProtectionScope]::CurrentUser))
}
& $signTool sign /fd SHA256 /f $pfx /p $secret (Join-Path $package 'PL80ECleanDev.dll')
if ($LASTEXITCODE -ne 0) { throw 'DLL-Signierung fehlgeschlagen.' }
& $inf2cat ("/driver:"+$package) /os:10_X64 /verbose
if ($LASTEXITCODE -ne 0) { throw 'INF/Katalogprüfung fehlgeschlagen.' }
& $signTool sign /fd SHA256 /f $pfx /p $secret (Join-Path $package 'PL80ECleanDev.cat')
if ($LASTEXITCODE -ne 0) { throw 'Katalog-Signierung fehlgeschlagen.' }
if(-not (Test-Path -LiteralPath (Join-Path $package 'PL80EClean-Development.cer'))) {
    $identity=[Security.Cryptography.X509Certificates.X509Certificate2]::new($pfx,$secret,[Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
    try {[IO.File]::WriteAllBytes((Join-Path $package 'PL80EClean-Development.cer'),$identity.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))}
    finally {$identity.Dispose()}
}
$secret = $null
$publicCert = [Security.Cryptography.X509Certificates.X509Certificate2]::new((Join-Path $package 'PL80EClean-Development.cer'))
[pscustomobject]@{
    Subject=$publicCert.Subject; Thumbprint=$publicCert.Thumbprint;
    NotBefore=$publicCert.NotBefore.ToString('o'); NotAfter=$publicCert.NotAfter.ToString('o');
    EnhancedKeyUsage='Code Signing only'; TrustInstalledByThisScript=$false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'signing-identity.json') -Encoding UTF8
$publicCert.Dispose()
Get-ChildItem -LiteralPath $package -File | Where-Object Name -ne 'SHA256.json' | Get-FileHash -Algorithm SHA256 |
    Select-Object @{n='File';e={Split-Path $_.Path -Leaf}},Hash |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'SHA256.json') -Encoding UTF8
Compress-Archive -Path (Join-Path $package '*') -DestinationPath (Join-Path $PSScriptRoot 'dist\PL80E-Open-Source-DEV-0.1.0.14.zip') -Force
Get-Content -LiteralPath (Join-Path $package 'signing-identity.json')
