[CmdletBinding()]
param(
    [string]$PrinterName = 'PL80E Open Source Test (DEV)',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'test-output')
)
$ErrorActionPreference = 'Stop'
$printer = Get-Printer -Name $PrinterName -ErrorAction Stop
if ($printer.DriverName -ne 'PL80E Open Source (DEV)' -or $printer.PortName -ne 'FILE:') {
    throw 'Der Dateitest braucht eine separate PL80E-Clean-Warteschlange am FILE:-Anschluss.'
}
Add-Type -Path (Join-Path $PSScriptRoot 'PrintApi.cs')
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

function Read-Capture([string]$Path,[bool]$Landscape,[int]$Pages) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $cursor = 0
    $pageResults = @()
    for ($page=0; $page -lt $Pages; $page++) {
        $text = [Text.Encoding]::ASCII.GetString($bytes,$cursor,[Math]::Min(512,$bytes.Length-$cursor))
        $match = [regex]::Match($text,'BITMAP 0,0,(\d+),(\d+),1,')
        if (-not $match.Success) { throw "BITMAP-Header fehlt in Seite $page." }
        if (-not $text.StartsWith("SIZE 100.0 mm, 150.0 mm`r`n")) { throw 'Falsche Medienabmessungen.' }
        $stride = [int]$match.Groups[1].Value
        $height = [int]$match.Groups[2].Value
        if ($stride -ne 100 -or $height -ne 1199) { throw "Unerwartetes Raster: $stride Byte x $height Zeilen." }
        $payload = $cursor + $match.Index + $match.Length
        $end = $payload + $stride*$height
        if ($end + 13 -gt $bytes.Length) { throw 'Rasterdaten sind abgeschnitten.' }
        $tail = [Text.Encoding]::ASCII.GetString($bytes,$end,13)
        if ($tail -ne "`r`nPRINT 1,1`r`n") { throw 'Rasterlänge oder PRINT-Abschluss ist falsch.' }
        $sourceHeight = if ($Landscape) { 799 } else { 1199 }
        for ($sy=0; $sy -lt $sourceHeight; $sy++) {
            for ($sx=0; $sx -lt 16; $sx++) {
                $x = if ($Landscape) { 798-$sy } else { $sx }
                $y = if ($Landscape) { $sx } else { $sy }
                $value = $bytes[$payload+$y*$stride+[int][Math]::Floor($x/8)]
                $black = ($value -band (0x80 -shr ($x%8))) -eq 0
                $expected = (($sy -shr (15-$sx)) -band 1) -ne 0
                if ($page -eq 1) { $expected = -not $expected }
                if ($black -ne $expected) { throw "Rastermarker falsch: Seite $page, Quellpixel $sx,$sy." }
            }
        }
        # Right-edge padding bit of every physical row must remain white.
        for ($y=0; $y -lt $height; $y++) {
            if (($bytes[$payload+$y*$stride+99] -band 1) -eq 0) { throw 'Padding-Bit ist schwarz.' }
        }
        if ($page -eq 0) {
            # Save a conventional 1-bpp BMP for visual inspection of the real
            # captured driver output. Raster payload is not rescaled.
            $bmpPath = [IO.Path]::ChangeExtension($Path,'.bmp')
            $stream = [IO.File]::Create($bmpPath)
            $writer = New-Object IO.BinaryWriter($stream)
            try {
                $writer.Write([uint16]0x4d42); $writer.Write([uint32](62+$stride*$height))
                $writer.Write([uint32]0); $writer.Write([uint32]62)
                $writer.Write([uint32]40); $writer.Write([int32]799); $writer.Write([int32](-$height))
                $writer.Write([uint16]1); $writer.Write([uint16]1); $writer.Write([uint32]0)
                $writer.Write([uint32]($stride*$height)); $writer.Write([int32]7992); $writer.Write([int32]7992)
                $writer.Write([uint32]2); $writer.Write([uint32]2)
                $writer.Write([uint32]0); $writer.Write([uint32]0x00ffffff)
                $writer.Write($bytes,$payload,$stride*$height)
            } finally { $writer.Dispose(); $stream.Dispose() }
        }
        $pageResults += [pscustomobject]@{Page=$page+1; RasterBytes=$stride*$height; Rows=$height; Markers='PASS'; Padding='PASS'}
        $cursor = $end + 13
    }
    if ($cursor -ne $bytes.Length) { throw "Unerwartete zusätzliche Daten: $($bytes.Length-$cursor) Byte." }
    return $pageResults
}

$runs = @()
foreach ($landscape in @($false,$true)) {
    $name = if ($landscape) { 'landscape' } else { 'portrait' }
    $capture = Join-Path $OutputDirectory ($name+'-'+[guid]::NewGuid().ToString('N')+'.prn')
    $caps = [CleanPrintApi]::RenderFixture($PrinterName,$capture,$landscape,2)
    # EndDoc can return before a spooled document has finished reaching FILE:.
    $ready = $false
    for ($attempt=0; $attempt -lt 150; $attempt++) {
        if ((Test-Path -LiteralPath $capture) -and (Get-Item -LiteralPath $capture).Length -gt 239800) {
            try { $result = @(Read-Capture $capture $landscape 2); $ready=$true; break } catch { $lastFailure=$_ }
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $ready) {
        if ($lastFailure) { throw $lastFailure }
        throw "Druckdatei wurde nicht rechtzeitig vollständig erzeugt: $capture"
    }
    $runs += [pscustomobject]@{Orientation=$name; LogicalWidth=$caps[0]; LogicalHeight=$caps[1]; Dpi=$caps[2]; Capture=$capture; Pages=$result}
}
$report = [pscustomobject]@{Status='PASS'; Timestamp=(Get-Date).ToString('o'); Printer=$PrinterName; Runs=$runs}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'validation.json') -Encoding UTF8
$report | ConvertTo-Json -Depth 6
