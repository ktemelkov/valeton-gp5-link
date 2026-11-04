$ErrorActionPreference = 'Stop'

$header = Get-Content (Join-Path $PSScriptRoot '..\src\gp5.h') -Raw
if ($header -notmatch 'Presets\[\]\[24\]')
{
    throw 'Preset storage must hold exactly 24 bytes per packet.'
}

$rows = [regex]::Matches($header, '\{(0x[0-9a-fA-F]+(?:,\s*0x[0-9a-fA-F]+)*)\}')
if ($rows.Count -ne 100)
{
    throw "Expected 100 presets, found $($rows.Count)."
}

for ($preset = 0; $preset -lt $rows.Count; $preset++)
{
    # Build the reference's raw vendor, length, type and preset payload bytes.
    $raw = @(0x01, 0x00, 0x06, 0x11, 0x43, $preset, 0x00, 0x00, 0x00)
    $crc = 0
    foreach ($value in $raw)
    {
        $crc = $crc -bxor $value
        for ($bit = 0; $bit -lt 8; $bit++)
        {
            if ($crc -band 0x80)
            {
                $crc = (($crc -shl 1) -band 0xFF) -bxor 0x07
            }
            else
            {
                $crc = ($crc -shl 1) -band 0xFF
            }
        }
    }

    $expected = @(0x80, 0x80, 0xF0, ($crc -shr 4), ($crc -band 0x0F))
    foreach ($value in $raw)
    {
        $expected += @(($value -shr 4), ($value -band 0x0F))
    }
    $expected += 0xF7

    $actual = @([regex]::Matches($rows[$preset].Value, '0x[0-9a-fA-F]+') |
        ForEach-Object { [Convert]::ToInt32($_.Value.Substring(2), 16) })
    if (($actual -join ',') -ne ($expected -join ','))
    {
        throw "Preset $preset does not match the reference's 24-byte SysEx packet."
    }
}

Write-Output 'PASS: All 100 preset packets match the reference format and CRC.'
