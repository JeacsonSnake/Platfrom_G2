# ESP32-S3 网络连接测试脚本
# 使用方法: .\network_test.ps1 -Esp32IP "192.168.233.xxx"

param(
    [string]$Esp32IP = "",
    [string]$EMQXIP = "192.168.233.100",
    [string]$Gateway = "192.168.233.1"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ESP32-S3 网络连接测试工具" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 测试 1: 检查本机网络配置
Write-Host "[1/5] 检查本机网络配置..." -ForegroundColor Yellow
$localIP = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like "192.168.233.*" }).IPAddress
if ($localIP) {
    Write-Host "  本机 IP: $localIP" -ForegroundColor Green
    Write-Host "  已在目标网段中" -ForegroundColor Green
} else {
    Write-Host "  警告: 本机不在 192.168.233.xxx 网段!" -ForegroundColor Red
    Write-Host "  请连接到正确的 WiFi 网络" -ForegroundColor Yellow
}
Write-Host ""

# 测试 2: Ping 网关
Write-Host "[2/5] 测试网关连通性 ($Gateway)..." -ForegroundColor Yellow
$pingGateway = Test-Connection -ComputerName $Gateway -Count 2 -Quiet
if ($pingGateway) {
    Write-Host "  网关可访问" -ForegroundColor Green
} else {
    Write-Host "  网关无法访问" -ForegroundColor Red
}
Write-Host ""

# 测试 3: Ping EMQX 服务器
Write-Host "[3/5] 测试 EMQX 服务器连通性 ($EMQXIP)..." -ForegroundColor Yellow
$pingEMQX = Test-Connection -ComputerName $EMQXIP -Count 2 -Quiet
if ($pingEMQX) {
    Write-Host "  EMQX 服务器可访问" -ForegroundColor Green
    
    # 检查 EMQX 端口
    Write-Host "  检查 MQTT 端口 1883..." -ForegroundColor Gray
    $mqttPort = Test-NetConnection -ComputerName $EMQXIP -Port 1883 -WarningAction SilentlyContinue
    if ($mqttPort.TcpTestSucceeded) {
        Write-Host "  MQTT 端口 1883 开放" -ForegroundColor Green
    } else {
        Write-Host "  MQTT 端口 1883 无法连接" -ForegroundColor Red
    }
    
    Write-Host "  检查 WebSocket 端口 8083..." -ForegroundColor Gray
    $wsPort = Test-NetConnection -ComputerName $EMQXIP -Port 8083 -WarningAction SilentlyContinue
    if ($wsPort.TcpTestSucceeded) {
        Write-Host "  WebSocket 端口 8083 开放" -ForegroundColor Green
    }
    
    Write-Host "  检查 HTTP 端口 18083..." -ForegroundColor Gray
    $httpPort = Test-NetConnection -ComputerName $EMQXIP -Port 18083 -WarningAction SilentlyContinue
    if ($httpPort.TcpTestSucceeded) {
        Write-Host "  HTTP 管理端口 18083 开放" -ForegroundColor Green
        Write-Host "  管理界面: http://$EMQXIP`:18083/" -ForegroundColor Cyan
    }
} else {
    Write-Host "  EMQX 服务器无法访问" -ForegroundColor Red
    Write-Host "  请确认服务器是否开机" -ForegroundColor Yellow
}
Write-Host ""

# 测试 4: Ping ESP32
if ($Esp32IP) {
    Write-Host "[4/5] 测试 ESP32 连通性 ($Esp32IP)..." -ForegroundColor Yellow
    $pingESP = Test-Connection -ComputerName $Esp32IP -Count 4 -Delay 1
    if ($pingESP) {
        $avgTime = ($pingESP | Measure-Object -Property ResponseTime -Average).Average
        Write-Host "  ESP32 可访问，平均延迟: $([int]$avgTime)ms" -ForegroundColor Green
    } else {
        Write-Host "  ESP32 无法访问" -ForegroundColor Red
        Write-Host "  可能原因:" -ForegroundColor Yellow
        Write-Host "  - ESP32 未正确连接到 WiFi" -ForegroundColor Gray
        Write-Host "  - ESP32 与电脑不在同一网段" -ForegroundColor Gray
        Write-Host "  - ESP32 防火墙设置" -ForegroundColor Gray
    }
} else {
    Write-Host "[4/5] 跳过 ESP32 测试 (未提供 IP)" -ForegroundColor Yellow
    Write-Host "  提示: 从串口日志获取 ESP32 IP 后，使用参数 -Esp32IP " -ForegroundColor Gray
}
Write-Host ""

# 测试 5: ARP 扫描（查找同网段设备）
Write-Host "[5/5] 扫描同网段设备..." -ForegroundColor Yellow
$subnet = "192.168.233"
Write-Host "  扫描 $subnet.1-254..." -ForegroundColor Gray

$jobs = @()
for ($i = 1; $i -le 254; $i++) {
    $ip = "$subnet.$i"
    $jobs += Start-Job -ScriptBlock {
        param($ip)
        if (Test-Connection -ComputerName $ip -Count 1 -Quiet -Delay 1) {
            $ip
        }
    } -ArgumentList $ip
}

$activeHosts = @()
$jobs | ForEach-Object {
    $result = $_ | Wait-Job -Timeout 5 | Receive-Job
    if ($result) {
        $activeHosts += $result
    }
    Remove-Job $_
}

if ($activeHosts.Count -gt 0) {
    Write-Host "  发现 $($activeHosts.Count) 个活动设备:" -ForegroundColor Green
    $activeHosts | ForEach-Object { Write-Host "    $_" -ForegroundColor Gray }
} else {
    Write-Host "  未发现活动设备（或扫描超时）" -ForegroundColor Yellow
}
Write-Host ""

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "测试完成" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
