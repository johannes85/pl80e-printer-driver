$ErrorActionPreference='Stop'
$logRoot=Join-Path $PSScriptRoot 'test-output'
Start-Transcript -LiteralPath (Join-Path $logRoot 'rename-installation.log') -Force | Out-Null
try {
    Add-Type -Path (Join-Path $PSScriptRoot 'tests\RenameSettings.cs')
    $snapshots=@()
    foreach($old in @('PL80E Clean Test','PL80E Clean')) {
        $q=Get-Printer -Name $old
        if($q.DriverName -ne 'PL80E Clean') {throw 'Unexpected driver.'}
        if(Get-PrintJob -PrinterName $old) {throw 'Print jobs are still queued.'}
        $snapshots+= [pscustomobject]@{Old=$old;New=$old.Replace('PL80E Clean','PL80E Open Source');Global=[RenameSettings]::Read($old,8);User=[RenameSettings]::Read($old,9)}
    }
    $snapshots | Export-Clixml -LiteralPath (Join-Path $logRoot 'rename-settings-backup.xml')
    # Validate the new registered driver before migrating either existing queue.
    & (Join-Path $PSScriptRoot 'Install.ps1') -TestOnly -QueueName 'PL80E Open Source Validation'
    & (Join-Path $PSScriptRoot 'Test-Driver.ps1') -PrinterName 'PL80E Open Source Validation'
    foreach($item in $snapshots) {
        Rename-Printer -Name $item.Old -NewName $item.New
        Set-Printer -Name $item.New -DriverName 'PL80E Open Source'
        [RenameSettings]::Write($item.New,8,$item.Global)
        [RenameSettings]::Write($item.New,9,$item.User)
    }
    & (Join-Path $PSScriptRoot 'Test-Driver.ps1') -PrinterName 'PL80E Open Source Test'
    Remove-Printer -Name 'PL80E Open Source Validation'
    if(-not (Get-Printer | Where-Object DriverName -eq 'PL80E Clean')) {Remove-PrinterDriver -Name 'PL80E Clean'}
    [pscustomobject]@{Status='PASS';Time=(Get-Date).ToString('o')} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $logRoot 'rename-status.json')
} catch {
    [pscustomobject]@{Status='FAILED';Error=($_ | Out-String)} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $logRoot 'rename-status.json')
    throw
} finally {Stop-Transcript | Out-Null}
