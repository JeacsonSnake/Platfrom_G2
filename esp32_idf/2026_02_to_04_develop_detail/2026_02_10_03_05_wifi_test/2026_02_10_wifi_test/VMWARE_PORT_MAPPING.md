# VMware NAT 端口映射配置指南

## 目标
让 ESP32 通过 Windows 主机 IP (192.168.110.31) 访问虚拟机中的 EMQX 服务

## 配置步骤

### 步骤 1：打开虚拟网络编辑器

1. VMware 菜单 → **编辑** → **虚拟网络编辑器**
2. 点击 **"更改设置"**（需要管理员权限）
3. 选择 **VMnet8 (NAT 模式)**
4. 点击 **"NAT 设置"**

### 步骤 2：添加端口转发规则

在 "NAT 设置" 对话框中，点击 **"添加"**，添加以下规则：

#### 规则 1：MQTT 端口
| 字段 | 值 |
|------|-----|
| 主机端口 | 1883 |
| 类型 | TCP |
| 虚拟机 IP 地址 | 192.168.233.100 |
| 虚拟机端口 | 1883 |
| 描述 | EMQX MQTT |

#### 规则 2：MQTT WebSocket
| 字段 | 值 |
|------|-----|
| 主机端口 | 8083 |
| 类型 | TCP |
| 虚拟机 IP 地址 | 192.168.233.100 |
| 虚拟机端口 | 8083 |
| 描述 | EMQX WebSocket |

#### 规则 3：EMQX 管理界面
| 字段 | 值 |
|------|-----|
| 主机端口 | 18083 |
| 类型 | TCP |
| 虚拟机 IP 地址 | 192.168.233.100 |
| 虚拟机端口 | 18083 |
| 描述 | EMQX Dashboard |

### 步骤 3：保存并应用

1. 点击 **"确定"** 关闭 NAT 设置
2. 点击 **"确定"** 关闭虚拟网络编辑器
3. **无需重启虚拟机**

### 步骤 4：验证配置

在 Windows 命令提示符中执行：

```cmd
# 测试 MQTT 端口
telnet 192.168.110.31 1883

# 或测试 WebSocket 端口
telnet 192.168.110.31 8083

# 或测试管理界面（浏览器访问）
http://192.168.110.31:18083/
```

如果连接成功，说明端口映射配置正确。

## 网络拓扑

```
┌─────────────────┐         ┌──────────────────┐         ┌────────────────────┐
│   ESP32-S3      │◄───────►│   Windows 主机   │◄───────►│   Ubuntu 虚拟机    │
│ 192.168.110.180 │  WiFi   │  192.168.110.31  │  NAT    │  192.168.233.100   │
└─────────────────┘         └────────┬─────────┘         │     (EMQX)         │
                                     │                   └────────────────────┘
                               端口映射: 1883→1883
                               端口映射: 8083→8083  
                               端口映射: 18083→18083
```

## 故障排查

### 问题 1：telnet 连接失败

**检查**：
1. Windows 防火墙是否阻止了端口
2. 虚拟机中的 EMQX 是否正在运行
3. 端口映射配置是否正确

**解决**：
```powershell
# 以管理员身份运行 PowerShell，添加防火墙规则
New-NetFirewallRule -DisplayName "EMQX MQTT" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow
New-NetFirewallRule -DisplayName "EMQX WebSocket" -Direction Inbound -Protocol TCP -LocalPort 8083 -Action Allow
New-NetFirewallRule -DisplayName "EMQX Dashboard" -Direction Inbound -Protocol TCP -LocalPort 18083 -Action Allow
```

### 问题 2：虚拟机 IP 变化

如果虚拟机 IP 变化（非 192.168.233.100），需要：
1. 在虚拟机中设置静态 IP
2. 或更新端口映射中的虚拟机 IP

## 下一步

配置完成后，ESP32 将连接到 `192.168.110.31:1883`，流量会被转发到虚拟机中的 EMQX。
