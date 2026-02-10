# ESP32-S3 快速烧录脚本
# 使用方法:
# 1. 确保 ESP32-S3 进入下载模式 (BOOT+RESET)
# 2. 运行: .\flash_esp32.ps1 -Port COM9

param(
    [string]$Port = "COM9",
    [int]$Baud = 921600
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 烧录脚本" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查端口
Write-Host "检查端口 $Port..." -ForegroundColor Yellow
$ports = [System.IO.Ports.SerialPort]::GetPortNames()
if ($Port -notin $ports) {
    Write-Host "错误: 端口 $Port 不可用!" -ForegroundColor Red
    Write-Host "可用端口: $($ports -join ', ')" -ForegroundColor Gray
    exit 1
}
Write-Host "端口 $Port 可用" -ForegroundColor Green

# 检查固件文件
$bootloader = "build/bootloader/bootloader.bin"
$partition_table = "build/partition_table/partition-table.bin"
$app = "build/test.bin"

Write-Host ""
Write-Host "检查固件文件..." -ForegroundColor Yellow

$files = @($bootloader, $partition_table, $app)
$allExist = $true
foreach ($file in $files) {
    if (Test-Path $file) {
        $size = (Get-Item $file).Length
        Write-Host "  [OK] $file ($size bytes)" -ForegroundColor Green
    } else {
        Write-Host "  [MISSING] $file" -ForegroundColor Red
        $allExist = $false
    }
}

if (-not $allExist) {
    Write-Host ""
    Write-Host "错误: 部分固件文件不存在!" -ForegroundColor Red
    Write-Host "请先编译项目:" -ForegroundColor Yellow
    Write-Host "  idf.py build" -ForegroundColor Gray
    exit 1
}

# 烧录
Write-Host ""
Write-Host "开始烧录..." -ForegroundColor Yellow
Write-Host "端口: $Port" -ForegroundColor Gray
Write-Host "波特率: $Baud" -ForegroundColor Gray
Write-Host ""

$cmd = "python -m esptool --port $Port --chip esp32s3 --baud $Baud write_flash " +
       "0x0 $bootloader 0x8000 $partition_table 0x10000 $app"

Write-Host "执行命令:" -ForegroundColor Gray
Write-Host $cmd -ForegroundColor DarkGray
Write-Host ""

Invoke-Expression $cmd

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "烧录成功!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "查看日志命令:" -ForegroundColor Yellow
    Write-Host "  python -m serial.tools.miniterm $Port 115200" -ForegroundColor Gray
    Write-Host "  或: idf.py -p $Port monitor" -ForegroundColor Gray
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "烧录失败!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "排查建议:" -ForegroundColor Yellow
    Write-Host "1. 确认 ESP32 已进入下载模式 (BOOT+RESET)" -ForegroundColor Gray
    Write-Host "2. 检查端口是否正确" -ForegroundColor Gray
    Write-Host "3. 尝试降低波特率: .\flash_esp32.ps1 -Port $Port -Baud 115200" -ForegroundColor Gray
}
