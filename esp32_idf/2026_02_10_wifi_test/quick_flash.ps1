# 快速烧录脚本 - 绕过 idf.py，直接使用 esptool
# 适用于 idf.py 环境损坏但已有编译好的固件的情况

param(
    [string]$Port = "COM9",
    [int]$Baud = 921600
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 快速烧录工具" -ForegroundColor Cyan
Write-Host "（绕过 idf.py，直接使用 esptool）" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查 esptool
Write-Host "检查 esptool..." -ForegroundColor Yellow
$esptoolCheck = python -c "import esptool; print('OK')" 2>&1
if ($esptoolCheck -eq "OK") {
    Write-Host "  esptool 已安装" -ForegroundColor Green
} else {
    Write-Host "  安装 esptool..." -ForegroundColor Yellow
    pip install esptool
}

# 检查固件文件
$files = @{
    "bootloader" = "build/bootloader/bootloader.bin"
    "partition_table" = "build/partition_table/partition-table.bin"
    "app" = "build/test.bin"
}

Write-Host ""
Write-Host "检查固件文件..." -ForegroundColor Yellow
$allExist = $true
foreach ($name in $files.Keys) {
    $path = $files[$name]
    if (Test-Path $path) {
        $size = (Get-Item $path).Length
        Write-Host "  [OK] $name`: $path ($size bytes)" -ForegroundColor Green
    } else {
        Write-Host "  [MISSING] $name`: $path" -ForegroundColor Red
        $allExist = $false
    }
}

if (-not $allExist) {
    Write-Host ""
    Write-Host "错误: 固件文件不存在!" -ForegroundColor Red
    Write-Host ""
    Write-Host "如果项目未编译，请尝试以下方法之一:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "方法 1: 使用 ESP-IDF 命令提示符" -ForegroundColor Cyan
    Write-Host "  1. 按 Win 键，搜索 'ESP-IDF'" -ForegroundColor Gray
    Write-Host "  2. 打开 'ESP-IDF 5.x PowerShell'" -ForegroundColor Gray
    Write-Host "  3. 执行: cd E:\Platform_G2\esp32_idf" -ForegroundColor Gray
    Write-Host "  4. 执行: idf.py build" -ForegroundColor Gray
    Write-Host ""
    Write-Host "方法 2: 修复 VSCode ESP-IDF 环境" -ForegroundColor Cyan
    Write-Host "  查看 FIX_IDF_ENV.md 获取详细步骤" -ForegroundColor Gray
    exit 1
}

# 确认烧录
Write-Host ""
Write-Host "即将烧录到 $Port，波特率 $Baud" -ForegroundColor Yellow
Write-Host "请确保 ESP32-S3 已进入下载模式:" -ForegroundColor Yellow
Write-Host "  1. 按住 BOOT 按钮" -ForegroundColor Gray
Write-Host "  2. 按一下 RESET 按钮" -ForegroundColor Gray
Write-Host "  3. 松开 BOOT 按钮" -ForegroundColor Gray
Write-Host ""

$confirm = Read-Host "确认开始烧录? (y/N)"
if ($confirm -ne 'y' -and $confirm -ne 'Y') {
    Write-Host "已取消" -ForegroundColor Gray
    exit 0
}

# 执行烧录
Write-Host ""
Write-Host "开始烧录..." -ForegroundColor Green
Write-Host ""

$cmd = "python -m esptool --port $Port --chip esp32s3 --baud $Baud write_flash " +
       "0x0 $($files['bootloader']) 0x8000 $($files['partition_table']) 0x10000 $($files['app'])"

Write-Host "执行: $cmd" -ForegroundColor DarkGray
Write-Host ""

Invoke-Expression $cmd

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "烧录成功!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "查看串口日志:" -ForegroundColor Cyan
    Write-Host "  python -m serial.tools.miniterm $Port 115200" -ForegroundColor Gray
    Write-Host ""
    Write-Host "或安装 PuTTY/SSCOM 连接 $Port 波特率 115200" -ForegroundColor Gray
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "烧录失败!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "排查建议:" -ForegroundColor Yellow
    Write-Host "1. 确认 ESP32 已进入下载模式" -ForegroundColor Gray
    Write-Host "2. 尝试更换端口: -Port COM8" -ForegroundColor Gray
    Write-Host "3. 降低波特率: -Baud 115200" -ForegroundColor Gray
}
