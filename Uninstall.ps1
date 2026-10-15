[CmdletBinding(SupportsShouldProcess)]
param([string]$QueueName = 'PL80E Open Source (DEV)', [switch]$RemoveDriver)
$ErrorActionPreference = 'Stop'
$printer = Get-Printer -Name $QueueName -ErrorAction SilentlyContinue
if ($printer) {
    if ($printer.DriverName -ne 'PL80E Open Source (DEV)') { throw 'Es wird nur eine PL80E-Clean-Warteschlange entfernt.' }
    if (Get-PrintJob -PrinterName $QueueName -ErrorAction Stop) { throw 'Es sind noch Druckaufträge vorhanden.' }
    if ($PSCmdlet.ShouldProcess($QueueName,'Warteschlange entfernen')) { Remove-Printer -Name $QueueName }
}
if ($RemoveDriver) {
    if (Get-Printer | Where-Object DriverName -eq 'PL80E Open Source (DEV)') { throw 'Andere Clean-Warteschlangen verwenden den Treiber noch.' }
    if ($PSCmdlet.ShouldProcess('PL80E Open Source (DEV)','Treiber entfernen')) { Remove-PrinterDriver -Name 'PL80E Open Source (DEV)' }
}
