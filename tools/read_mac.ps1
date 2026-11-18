# PowerShell script to quickly read ESP32-C3 MAC Address without flashing
param (
    [string]$Port = "COM10"
)

Write-Host "Reading MAC Address from ESP32-C3 on $Port..." -ForegroundColor Cyan
python -m esptool --chip esp32c3 --port $Port read-mac

