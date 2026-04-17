# MAX31850 温度传感器接线验证指南 v3

**日期**: 2026-03-31  
**版本**: 3.0 (Bit-Bang GPIO Implementation)  
**硬件**: ESP32-S3-DevKitC-1 + 4×MAX31850KATB+  
**1-Wire总线**: GPIO14 (IO14)  
**驱动类型**: GPIO Bit-Bang with Critical Section

---

## 1. 硬件连接图

### 1.1 总体连接示意图

```
                    ESP32-S3 (3.3V)
                         │
        ┌────────────────┼────────────────┐
        │                │                │
       [4.7KΩ]        GPIO14            GND
        │                │                │
        └────────┬───────┘                │
                 │                        │
    ╔════════════╪════════════════════════╪═══════ 1-Wire BUS
    ║            │                        │
    ║   ┌────────┴────────┐              GND
    ║   │                 │
    ║  DQ                 VDD
    ║   │                 │
    ║ ┌─┴─┐             ┌─┴─┐
    ║ │U1 │───AD0(GND)  │   │
    ║ │   │───AD1(GND)  │   │
    ║ └───┘             └───┘
    ║ MAX31850-0 (地址00)  P1
    ║
    ║   ┌────────┬────────┐
    ║   │        │        │
    ║  DQ       VDD      [4.7KΩ]──┐
    ║   │        │                 │
    ║ ┌─┴─┐    ┌─┴─┐               │
    ║ │U2 │───AD0(3.3V)            │
    ║ │   │───AD1(GND)             │
    ║ └───┘                        │
    ║ MAX31850-1 (地址01)  P2      │
    ║                              │
    ║   ┌────────┬────────┐       │
    ║   │        │        │       │
    ║  DQ       VDD      [4.7KΩ]──┤
    ║   │        │                │
    ║ ┌─┴─┐    ┌─┴─┐              │
    ║ │U3 │───AD0(GND)            │
    ║ │   │───AD1(3.3V)           │
    ║ └───┘                       │
    ║ MAX31850-2 (地址10)  P3     │
    ║                             │
    ║   ┌────────┬────────┐      │
    ║   │        │        │      │
    ║  DQ       VDD      [4.7KΩ]─┘
    ║   │        │
    ║ ┌─┴─┐    ┌─┴─┐
    ║ │U4 │───AD0(3.3V)
    ║ │   │───AD1(3.3V)
    ║ └───┘
    ║ MAX31850-3 (地址11)  P4
    ║
    ╚═══════════════════════════════════════
```

### 1.2 引脚对应表

| 传感器 | PCB位号 | 逻辑地址 | AD0接法 | AD1接法 | 上拉电阻 | 对应 ID |
|--------|---------|----------|---------|---------|----------|---------|
| P1 | U1 | 00 (0x00) | GND (0V) | GND (0V) | 4.7KΩ → 3.3V | sensors[0] |
| P2 | U2 | 01 (0x01) | 3.3V | GND (0V) | 4.7KΩ → 3.3V | sensors[1] |
| P3 | U3 | 10 (0x02) | GND (0V) | 3.3V | 4.7KΩ → 3.3V | sensors[2] |
| P4 | U4 | 11 (0x03) | 3.3V | 3.3V | 4.7KΩ → 3.3V | sensors[3] |

### 1.3 ESP32-S3 GPIO14 连接确认

```
ESP32-S3 GPIO14 (IO14)
    │
    ├──→ 连接到 MAX31850 的 DQ 引脚（所有4个传感器并联）
    │
    └──→ 每个传感器的 DQ 引脚通过 4.7KΩ 电阻上拉到 3.3V
```

**重要**: GPIO14 必须配置为 **开漏输出 (Open-Drain)** 模式，配合外部上拉电阻工作。

---

## 2. 接线验证步骤

### 2.1 准备工作

**工具清单**:
- 数字万用表（电压档 + 通断档）
- 4.7KΩ电阻 × 4（每个传感器一个）
- 杜邦线若干

**供电检查**:
```bash
# 测量ESP32-S3板载3.3V输出
- 红表笔 → 3.3V引脚
- 黑表笔 → GND
- 预期读数: 3.25V ~ 3.35V
```

### 2.2 逐步验证

#### 步骤1: 连接公共GND
```
验证点: 所有MAX31850的GND引脚 → ESP32的GND

万用表测试（通断档）:
- 从ESP32的GND测试到每个传感器的GND焊盘
- 应听到蜂鸣声（电阻<1Ω）
```

#### 步骤2: 连接公共VDD（3.3V）
```
验证点: 所有MAX31850的VDD引脚 → ESP32的3.3V

万用表测试（电压档）:
- 不连接传感器，先测量ESP32的3.3V输出是否正常
- 逐个连接传感器的VDD，测量电压不应明显下降
- 预期: 3.20V ~ 3.35V
```

#### 步骤3: 连接GPIO14到1-Wire总线
```
接线: ESP32的GPIO14 → 所有MAX31850的DQ引脚

重要: 每个传感器的DQ引脚都需要独立的上拉电阻到3.3V！

正确接法（星型拓扑）:
         3.3V
          │
    ┌─────┼─────┬─────┐
   4.7K  4.7K  4.7K  4.7K
    │     │     │     │
   DQ0   DQ1   DQ2   DQ3
    │     │     │     │
    └─────┴─────┴─────┘
              │
           GPIO14
              │
            (ESP32)

错误接法（多个传感器共用单个上拉）:
         3.3V
          │
        4.7K  ← 只有一个上拉，可能导致信号衰减
          │
    ┌─────┴─────┬─────┐
   DQ0   DQ1   DQ2   DQ3
```

#### 步骤4: 配置地址引脚（AD0, AD1）

使用万用表验证每个传感器的地址引脚电平:

| 传感器 | AD0目标 | AD1目标 | 万用表测量点 |
|--------|---------|---------|-------------|
| P1(U1) | GND(0V) | GND(0V) | AD0-GND≈0V, AD1-GND≈0V |
| P2(U2) | 3.3V | GND(0V) | AD0-GND≈3.3V, AD1-GND≈0V |
| P3(U3) | GND(0V) | 3.3V | AD0-GND≈0V, AD1-GND≈3.3V |
| P4(U4) | 3.3V | 3.3V | AD0-GND≈3.3V, AD1-GND≈3.3V |

---

## 3. 软件验证

### 3.1 编译和烧录

```powershell
# 1. 设置ESP-IDF环境
. $env:IDF_PATH/export.ps1

# 2. 清理并重新构建
idf.py fullclean
idf.py build

# 3. 进入下载模式（按住BOOT，按RESET，松开BOOT）
# 4. 烧录
idf.py -p COM9 flash

# 5. 打开串口监视器
idf.py -p COM9 monitor
```

### 3.2 预期输出

#### 初始化阶段

```
I (1234) MAX31850: MAX31850 Init: GPIO14 configured as open-drain
I (1245) MAX31850: Found device 1: ROM ID 3B1234567890ABCD
I (1267) MAX31850: Found device 2: ROM ID 3B1234567890ABCE
I (1289) MAX31850: Found device 3: ROM ID 3B1234567890ABCF
I (1300) MAX31850: Found device 4: ROM ID 3B1234567890ABD0
I (1311) MAX31850: MAX31850 Init: Found 4 sensor(s) on GPIO14
I (1322) MAX31850: Sensor [0]: ROM=0x3B1234567890ABCD, HW_ADDR=00
I (1333) MAX31850: Sensor [1]: ROM=0x3B1234567890ABCE, HW_ADDR=01
I (1344) MAX31850: Sensor [2]: ROM=0x3B1234567890ABCF, HW_ADDR=02
I (1355) MAX31850: Sensor [3]: ROM=0x3B1234567890ABD0, HW_ADDR=03
I (1366) MAX31850: Sensor [0] initial read: Temp=25.50°C
I (1377) MAX31850: Sensor [1] initial read: Temp=26.25°C
I (1388) MAX31850: Sensor [2] initial read: Temp=24.75°C
I (1399) MAX31850: Sensor [3] initial read: Temp=25.00°C
I (1410) MAX31850: MAX31850 initialized successfully
```

**注意**: ROM ID 的顺序由 Search ROM 算法决定，取决于 64-bit ID 的位冲突解决顺序，可能与硬件地址顺序不同。

#### 正常运行阶段

轮询任务每 250ms 读取一个传感器，每 1s 完成一轮 4 个传感器的读取。

可以通过以下方式查看数据：
```c
// 在主程序中添加温度打印任务
void heating_print_task(void *pvParameters) {
    float temp;
    max31850_err_t err;
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI("HEATING", "Temperature print task started");
    
    while (1) {
        ESP_LOGI("HEATING", "========== Temperature Report ==========");
        for (uint8_t i = 0; i < 4; i++) {
            err = max31850_get_temperature(i, &temp);
            if (err == MAX31850_OK) {
                ESP_LOGI("HEATING", "Sensor %d (P%d): %.2f °C  [OK]", i, i+1, temp);
            } else {
                ESP_LOGW("HEATING", "Sensor %d (P%d): %s", 
                         i, i+1, max31850_err_to_string(err));
            }
        }
        ESP_LOGI("HEATING", "=======================================");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

### 3.3 调试接口

```c
// 获取指定传感器温度
float temp;
max31850_err_t err = max31850_get_temperature(0, &temp);

// 获取详细数据
max31850_sensor_t data;
max31850_err_t err = max31850_get_data(0, &data);

// 强制立即更新（阻塞式）
max31850_err_t err = max31850_force_update(0, &temp, pdMS_TO_TICKS(500));

// 检查在线状态
bool online = max31850_is_online(0);

// 打印原始数据（调试）
max31850_dump_scratchpad(0);
```

---

## 4. 故障诊断

### 4.1 "BUS FAULT: Line shorted to GND"

**可能原因**:
- GPIO14 短路到 GND
- 某个传感器的 DQ 引脚损坏短路
- 上拉电阻接错（接到了 GND 而非 3.3V）

**排查步骤**:
```
1. 断电，测量 GPIO14 对 GND 电阻
   - 应约为 4.7KΩ（上拉电阻值）
   - 如果接近 0Ω: 存在短路

2. 逐个断开传感器，找出短路的设备

3. 检查上拉电阻连接方向
```

### 4.2 "No device present during search"

**可能原因**:
- GPIO14 未连接到传感器 DQ
- 上拉电阻缺失或阻值错误
- 传感器电源未连接
- 时序问题（较少见）

**排查步骤**:
```
1. 测量 GPIO14 对地电压（空闲时应为 3.3V）
   - 如果为 0V: 检查短路
   - 如果为 3.3V: 检查 Reset 时是否能看到下降沿

2. 测量 DQ 引脚对地电阻（断电时）
   - 应为 4.7KΩ 左右
   - 如果为无穷大: 检查上拉电阻是否连接

3. 测量 VDD 引脚电压（上电时）
   - 应为 3.3V
```

### 4.3 "CRC Error"

**可能原因**:
- 信号完整性问题
- 上拉电阻阻值过大（上升时间慢）
- 布线过长或干扰

**解决方案**:
```
1. 缩短 1-Wire 总线长度（建议 <10米）

2. 尝试减小上拉电阻值:
   - 4.7KΩ → 2.2KΩ 或 1KΩ
   
3. 使用示波器检查信号质量:
   - 上升时间应 <1μs
   - 高电平应 >2.7V
   - 低电平应 <0.3V
```

### 4.4 "Thermocouple Open Circuit"

**可能原因**:
- 热电偶未连接
- 热电偶接线松动
- MAX31850 的 T+/T- 引脚焊接问题

**排查步骤**:
```
1. 检查热电偶连接:
   - 确认热电偶插头插入到位
   - 测量热电偶电阻（应 <100Ω）

2. 使用 max31850_dump_scratchpad() 查看详细数据:
   I (1234) MAX31850: Sensor 0 Scratchpad Dump:
   I (1235) MAX31850:   [0] Temp LSB:    0xFF
   I (1236) MAX31850:   [1] Temp MSB:    0xFF
   I (1237) MAX31850:   [4] Fault Reg:   0x01  ← 0x01 表示 OC 故障

3. 检查 T+/T- 引脚是否有虚焊
```

### 4.5 只找到部分传感器

**可能原因**:
- 某个传感器接线错误
- 地址引脚配置错误（两个传感器地址冲突）
- 传感器损坏

**排查步骤**:
```
1. 单独测试每个传感器:
   - 只连接一个传感器，确认能被识别
   - 逐个增加传感器，找出问题设备

2. 检查地址引脚连接:
   - 用万用表测量 AD0 和 AD1 的电平
   - 确认与目标地址配置一致
```

---

## 5. 技术要点

### 5.1 Bit-Bang vs RMT

本驱动采用 **GPIO Bit-Bang + 临界区保护** 实现 1-Wire 协议：

| 特性 | GPIO Bit-Bang | RMT |
|------|--------------|-----|
| 时序控制 | 软件精确控制（μs 级） | 硬件控制 |
| 中断影响 | 临界区保护，防止抢占 | 硬件自动处理 |
| CPU 占用 | 通信期间占用 CPU | 不占用 CPU |
| 实现复杂度 | 中等 | 较高 |
| 可靠性 | 高（精确时序） | 高 |

**选择原因**: GPIO Bit-Bang 提供完全的时序控制，配合临界区保护可以确保 1-Wire 协议的严格时序要求。

### 5.2 开漏输出模式

GPIO14 必须配置为 **GPIO_MODE_INPUT_OUTPUT_OD**（开漏输出）:

```c
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << gpio_num),
    .mode = GPIO_MODE_INPUT_OUTPUT_OD,  // 开漏模式
    .pull_up_en = GPIO_PULLUP_ENABLE,   // 内部上拉辅助
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
```

**原因**: 
- 1-Wire 总线需要双向通信
- 开漏模式允许多设备共享总线（线与逻辑）
- 外部上拉电阻确保高电平

### 5.3 1-Wire 时序参数

| 参数 | 标准值 | 实现值 | 说明 |
|------|--------|--------|------|
| Reset 低电平 | 480μs | 480μs | 复位脉冲 |
| Presence 等待 | 70μs | 70μs | 等待设备响应 |
| 写 1 低电平 | ≤15μs | 5μs | 保守值 |
| 写 0 低电平 | ≥60μs | 70μs | 保守值 |
| 读采样点 | 15μs | 13μs | 释放后 10μs |
| 位间隔 | ≥1μs | 2μs | 额外裕量 |

### 5.4 MAX31850 特性

**与 DS18B20 的关键区别**:

| 特性 | MAX31850 | DS18B20 |
|------|----------|---------|
| 转换命令 | 不支持（自动转换） | 需要 Convert T (0x44) |
| 写暂存器 | 不支持 | 支持 |
| 复制暂存器 | 不支持 | 支持 |
| 温度分辨率 | 0.25°C（热电偶） | 0.0625°C |
| 冷端补偿 | 内置 | 无 |
| 故障检测 | 开路/短路检测 | 无 |

**数据帧格式（9 Bytes）**:
```
Byte 0: 热电偶温度 LSB
Byte 1: 热电偶温度 MSB（14-bit 有符号，右移 2 位）
Byte 2: 冷端温度 LSB
Byte 3: 冷端温度 MSB（12-bit 有符号，右移 4 位）
Byte 4: 故障寄存器（Bit0=开路, Bit1=短GND, Bit2=短VCC）
Byte 5-7: 保留
Byte 8: CRC8（覆盖 Byte 0-7）
```

---

## 6. 常见问题 FAQ

### Q1: 为什么要用4个独立的上拉电阻？

**A**: 虽然 1-Wire 总线可以共用单个上拉电阻，但在多设备并联时：
1. 独立上拉提供更好的信号完整性
2. 如果某个传感器损坏导致总线故障，不影响其他传感器
3. 方便单独调试和更换传感器

### Q2: 可以用 GPIO 引脚内部上拉代替外部 4.7KΩ 电阻吗？

**A**: **不推荐**。ESP32 内部上拉电阻通常为 30-50KΩ，对于 1-Wire 协议来说太弱，无法提供足够的电流驱动总线，会导致上升时间过长。

### Q3: 为什么 Search ROM 返回的设备顺序与硬件地址顺序不同？

**A**: Search ROM 使用二进制搜索树算法，设备发现顺序由 64-bit ROM ID 的位冲突解决顺序决定，与硬件地址（AD0/AD1）无关。驱动内部会自动建立 ROM ID 与传感器索引的映射。

### Q4: MAX31850 可以工作在 5V 吗？

**A**: 不可以。MAX31850KATB+ 的工作电压范围是 3.0V-3.6V，必须使用 3.3V 供电。

### Q5: 如何提高温度读取精度？

**A**: 
1. 确保 MAX31850 的电源稳定，增加 100nF 去耦电容
2. 避免热电偶导线与其他电源线平行布置
3. 使用屏蔽线减少 EMI 干扰
4. MAX31850 内置冷端补偿，无需额外校准

### Q6: 驱动中的临界区保护有什么作用？

**A**: 1-Wire 协议需要微秒级精确时序。临界区（portENTER_CRITICAL）会暂时禁用中断，防止 FreeRTOS 任务切换或中断处理破坏时序。

---

## 7. 修改历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-03-27 | 初始版本，基于软件 Bit-Bang |
| v2.0 | 2026-03-31 | RMT 驱动版本（未使用） |
| v3.0 | 2026-03-31 | 重新实现 Bit-Bang，精确时序，临界区保护 |

---

**文档版本**: 3.0  
**最后更新**: 2026-03-31
