[CmdletBinding(SupportsShouldProcess)]
param([string]$Thumbprint = 'AEA4C9DC9E3FCA11042C2CCBCAFD617A8C3E8956')
$ErrorActionPreference = 'Stop'
if ($Thumbprint -notmatch '^[A-Fa-f0-9]{40}$') { throw 'Ungültiger Fingerabdruck.' }
foreach($driverName in @('PL80E Open Source (DEV)','PL80E Clean')) {
    if(Get-PrinterDriver -Name $driverName -ErrorAction SilentlyContinue) {throw 'Uninstall the development queues and driver before removing their certificate trust.'}
}
foreach ($name in @('Root','TrustedPublisher')) {
    $store = New-Object Security.Cryptography.X509Certificates.X509Store($name,'LocalMachine')
    try {
        $store.Open('ReadWrite')
        $matches = $store.Certificates.Find('FindByThumbprint',$Thumbprint,$false)
        foreach ($cert in $matches) {
            if ($cert.Subject -ne 'CN=PL80E Clean Local Development') { throw 'Unerwartetes Zertifikat. Keine Änderung durchgeführt.' }
            if ($PSCmdlet.ShouldProcess("LocalMachine\$name\$Thumbprint",'Eigenes Entwicklungssignierzertifikat entfernen')) { $store.Remove($cert) }
        }
    } finally { $store.Close() }
}
