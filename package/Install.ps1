#Requires -RunAsAdministrator
#Requires -Version 5.1
[CmdletBinding()]
param([string]$PortName='USB001',[string]$QueueName='PL80E Open Source')
$ErrorActionPreference='Stop'
if(-not [Environment]::Is64BitProcess) {throw 'Use 64-bit PowerShell.'}
if(-not (Get-PrinterPort -Name $PortName -ErrorAction SilentlyContinue)) {throw "Printer port not found: $PortName"}
$driverName='PL80E Open Source'
$existing=Get-Printer -Name $QueueName -ErrorAction SilentlyContinue
if($existing -and ($existing.DriverName -ne $driverName -or $existing.PortName -ne $PortName)) {throw 'The queue exists with a different driver or port.'}
$stateRoot=Join-Path $env:ProgramData 'PL80E Open Source'
New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
# Only administrators and SYSTEM may modify installation state or signed files.
& icacls.exe $stateRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
if($LASTEXITCODE -ne 0) {throw 'Cannot protect installation directory.'}
$statePath=Join-Path $stateRoot 'installation.json'
$previous=if(Test-Path -LiteralPath $statePath) {Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json}else{$null}
$cert=$null
if($previous) {$cert=Get-Item -LiteralPath ('Cert:\LocalMachine\My\'+$previous.Thumbprint) -ErrorAction SilentlyContinue}
if($previous -and -not $cert) {throw 'The recorded signing identity is missing. Uninstall this installation before creating a replacement identity.'}
if($cert -and ($cert.Subject -ne 'CN=PL80E Open Source Local Installation' -or -not $cert.HasPrivateKey)) {throw 'Unexpected signing identity.'}
if($cert -and $cert.NotAfter -lt (Get-Date).AddDays(30)) {throw 'The local signing certificate expires soon. Uninstall before creating a new installation identity.'}
if(-not $cert) {
    $cert=New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=PL80E Open Source Local Installation' `
        -CertStoreLocation 'Cert:\LocalMachine\My' -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddYears(5)
}
$queues=@($QueueName)
if($previous) {$queues=@(@($previous.Queues)+$QueueName | Select-Object -Unique)}
$previousInf=if($previous){$previous.InfPath}else{$null}
[pscustomobject]@{Thumbprint=$cert.Thumbprint;Queues=$queues;DriverName=$driverName;InfPath=$previousInf} |
    ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
$payload=Join-Path $stateRoot 'Driver'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach($file in @('PL80EClean.dll','PL80EClean.gpd','PL80EClean.inf','PL80EClean.cat')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $payload -Force
}
# The build-generated catalog already contains the hashes of the unchanged
# driver files. Sign only the catalog; modifying the DLL would invalidate it.
$catalog=Join-Path $payload 'PL80EClean.cat'
$signature=Set-AuthenticodeSignature -LiteralPath $catalog -Certificate $cert -HashAlgorithm SHA256
if($signature.Status -eq 'HashMismatch') {throw 'Catalog signing failed.'}
$public=[Security.Cryptography.X509Certificates.X509Certificate2]::new($cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
foreach($storeName in @('Root','TrustedPublisher')) {
    $store=New-Object Security.Cryptography.X509Certificates.X509Store($storeName,'LocalMachine')
    try {$store.Open('ReadWrite');$store.Add($public)} finally {$store.Close()}
}
# Save recovery information before registration, so Uninstall can clean up a
# partially completed installation as well.
[pscustomobject]@{Thumbprint=$cert.Thumbprint;Queues=$queues;DriverName=$driverName;InfPath=$previousInf} |
    ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
if((Get-AuthenticodeSignature -LiteralPath $catalog).Status -ne 'Valid') {throw 'Windows does not trust the signed catalog.'}
& pnputil.exe /add-driver (Join-Path $payload 'PL80EClean.inf')
if($LASTEXITCODE -ne 0) {throw 'Driver Store staging failed.'}
$hash=(Get-FileHash -LiteralPath (Join-Path $payload 'PL80EClean.inf')).Hash
$storeInf=Get-ChildItem -LiteralPath "$env:WINDIR\System32\DriverStore\FileRepository" -Filter 'pl80eclean.inf_amd64_*' -Directory |
    ForEach-Object {Join-Path $_.FullName 'pl80eclean.inf'} |
    Where-Object {(Get-FileHash -LiteralPath $_).Hash -eq $hash} | Select-Object -First 1
if(-not $storeInf) {throw 'Staged driver INF was not found.'}
Add-PrinterDriver -Name $driverName -InfPath $storeInf
if(-not $existing) {Add-Printer -Name $QueueName -DriverName $driverName -PortName $PortName -PrintProcessor winprint -Datatype RAW}
[pscustomobject]@{Thumbprint=$cert.Thumbprint;Queues=$queues;DriverName=$driverName;InfPath=$storeInf} |
    ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
Write-Host "Installed: $QueueName ($PortName). Local certificate expires $($cert.NotAfter.ToString('yyyy-MM-dd'))."
