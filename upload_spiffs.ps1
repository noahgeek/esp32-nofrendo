param([string]$Port = "COM9", [string]$Rom = "data\rom.nes")

$ESPTOOL = "C:\Users\admin\AppData\Local\arduino15\packages\esp32\tools\esptool_py\4.5.1\esptool.exe"
$ROM_OFFSET = "0x290000"
$ROM_MAXSIZE = 0x160000

if (-not (Test-Path $Rom)) {
    Write-Host "ROM file not found: $Rom" -ForegroundColor Red
    exit 1
}

$romSize = (Get-Item $Rom).Length
if ($romSize -gt $ROM_MAXSIZE) {
    Write-Host ("ROM too large: {0} > {1}" -f $romSize, $ROM_MAXSIZE) -ForegroundColor Red
    exit 1
}

Write-Host ("=== Flashing {0} ({1:N0} bytes) to {2} ===" -f $Rom, $romSize, $ROM_OFFSET) -ForegroundColor Cyan

& $ESPTOOL --chip esp32c3 --port $Port --baud 921600 write_flash $ROM_OFFSET $Rom
if ($LASTEXITCODE -ne 0) {
    Write-Host "Upload failed! Check COM port (current: $Port)" -ForegroundColor Red
    exit 1
}
Write-Host "ROM flashed! Press RST to restart ESP32-C3." -ForegroundColor Green