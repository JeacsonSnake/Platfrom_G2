# 清理并重新构建 ESP32 项目脚本
# 使用方法: .\clean_and_build.ps1

param(
    [string]$Port = "COM9",
    [switch]$Flash = $false,
    [switch]$Monitor = $false
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 清理重建工具" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查是否在 ESP-IDF 环境中
$inIdfEnv = $env:IDF_PATH -and (Test-Path $env:IDF_PATH)
if (-not $inIdfEnv) {
    Write-Host "警告: 未检测到 ESP-IDF 环境" -ForegroundColor Yellow
    Write-Host "尝试激活 ESP-IDF 环境..." -ForegroundColor Yellow
    
    # 尝试激活 ESP-IDF
    $exportPs1 = "C:\Users\labIn\.vscode\extensions\espressif.esp-idf-extension-2.0.2\export.ps1"
    if (Test-Path $exportPs1) {
        & $exportPs1
        Write-Host "ESP-IDF 环境已激活" -ForegroundColor Green
    } else {
        Write-Host "错误: 无法找到 ESP-IDF export.ps1" -ForegroundColor Red
        Write-Host "请通过 VSCode 的 ESP-IDF 终端运行此脚本" -ForegroundColor Yellow
        exit 1
    }
}

# 步骤 1: 清理构建目录
Write-Host "[1/4] 清理构建目录..." -ForegroundColor Yellow
if (Test-Path "build") {
    Remove-Item -Recurse -Force "build"
    Write-Host "  build 目录已删除" -ForegroundColor Green
} else {
    Write-Host "  build 目录不存在，跳过" -ForegroundColor Gray
}

# 步骤 2: 设置目标芯片
Write-Host ""
Write-Host "[2/4] 设置目标芯片 (esp32s3)..." -ForegroundColor Yellow
try {
    idf.py set-target esp32s3 2>&1 | ForEach-Object {
        if ($_ -match "error|Error|ERROR") {
            Write-Host "  $_" -ForegroundColor Red
        } else {
            Write-Host "  $_" -ForegroundColor Gray
        }
    }
    Write-Host "  目标芯片设置完成" -ForegroundColor Green
} catch {
    Write-Host "  错误: $_" -ForegroundColor Red
    exit 1
}

# 步骤 3: 构建项目
Write-Host ""
Write-Host "[3/4] 构建项目..." -ForegroundColor Yellow
Write-Host "  这可能需要几分钟时间..." -ForegroundColor Gray
$buildSuccess = $false
try {
    idf.py build 2>&1 | ForEach-Object {
        $line = $_
        if ($line -match "error|Error|ERROR|failed|FAILED") {
            Write-Host "  $line" -ForegroundColor Red
        } elseif ($line -match "warning|Warning|WARNING") {
            Write-Host "  $line" -ForegroundColor Yellow
        } elseif ($line -match "Building|Generating|Linking|Completed") {
            Write-Host "  $line" -ForegroundColor Cyan
        } else {
            Write-Host "  $line" -ForegroundColor Gray
        }
        
        if ($line -match "Project build complete") {
            $buildSuccess = $true
        }
    }
} catch {
    Write-Host "  构建错误: $_" -ForegroundColor Red
    exit 1
}

if (-not $buildSuccess) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "构建失败!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "构建成功!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

# 步骤 4: 烧录（可选）
if ($Flash) {
    Write-Host ""
    Write-Host "[4/4] 准备烧录..." -ForegroundColor Yellow
    Write-Host "  请确保 ESP32-S3 已进入下载模式:" -ForegroundColor Cyan
    Write-Host "    1. 按住 BOOT 按钮" -ForegroundColor Gray
    Write-Host "    2. 按一下 RESET 按钮" -ForegroundColor Gray
    Write-Host "    3. 松开 BOOT 按钮" -ForegroundColor Gray
    Write-Host ""
    
    $confirm = Read-Host "确认开始烧录到 $Port? (y/N)"
    if ($confirm -eq 'y' -or $confirm -eq 'Y') {
        Write-Host "  开始烧录..." -ForegroundColor Yellow
        idf.py -p $Port flash
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  烧录成功!" -ForegroundColor Green
        } else {
            Write-Host "  烧录失败!" -ForegroundColor Red
        }
    } else {
        Write-Host "  跳过烧录" -ForegroundColor Gray
    }
}

# 监控日志（可选）
if ($Monitor) {
    Write-Host ""
    Write-Host "启动串口监控 ($Port)..." -ForegroundColor Yellow
    Write-Host "按 Ctrl+] 退出" -ForegroundColor Gray
    idf.py -p $Port monitor
}

Write-Host ""
Write-Host "完成!" -ForegroundColor Green
