# MQTT连接影响问题修复记录

**日期**: 2026-04-10  
**问题**: 修改影响MQTT连接  
**原因**: 轮询任务占用过多CPU，临界区时间过长阻塞其他任务

---

## 1. 问题分析

### 原有问题
1. **轮询任务频繁搜索**: 当传感器未找到时，每个循环都执行ROM搜索（带3次重试）
2. **临界区时间过长**: 
   - `one_wire_reset()`: 550μs 临界区
   - `one_wire_write_bit()`: 60-70μs 临界区  
   - `one_wire_read_bit()`: 20μs 临界区
3. **ROM搜索阻塞**: 搜索64位ROM需要 ~128次操作，总临界区时间 > 10ms

### 影响
- MQTT任务被饿死，无法及时处理网络数据
- WiFi连接可能断开
- 系统 watchdog 可能复位

---

## 2. 修复措施

### 修复1: 优化轮询任务调度
```c
// 新增: 喂狗操作
vTaskDelay(pdMS_TO_TICKS(1));

// 新增: 搜索失败计数器和退避策略
uint32_t search_fail_counter = 0;
...
if (found > 0) {
    search_fail_counter = 0;
} else {
    search_fail_counter++;
    // 失败后增加延时: 500ms, 1s, 1.5s, 2s, 2.5s (max)
    vTaskDelay(pdMS_TO_TICKS(500 * (search_fail_counter > 5 ? 5 : search_fail_counter)));
}
```

### 修复2: 缩短临界区时间
```c
// one_wire_reset(): 550μs -> 480μs 临界区
ONE_WIRE_CRITICAL_ENTER();
gpio_set_level_fast(g_driver.gpio_num, 0);
esp_rom_delay_us(ONE_WIRE_RESET_TIME);
gpio_set_level_fast(g_driver.gpio_num, 1);
ONE_WIRE_CRITICAL_EXIT();
// 等待和采样移到临界区外

// one_wire_write_bit(): 60-70μs -> 6-60μs 临界区
ONE_WIRE_CRITICAL_ENTER();
// 拉低和释放操作
ONE_WIRE_CRITICAL_EXIT();
// 恢复时间移到临界区外

// one_wire_read_bit(): 20μs -> 5μs 临界区
ONE_WIRE_CRITICAL_ENTER();
// 拉低和释放操作
ONE_WIRE_CRITICAL_EXIT();
// 延时和采样移到临界区外
```

---

## 3. 关键改动

### 文件: `main/heating_detect.c`

#### 改动1: 轮询任务 (max31850_polling_task)
- 添加 `vTaskDelay(1)` 喂狗
- 限制搜索重试次数（失败后减少重试）
- 添加退避延时策略
- 减少互斥锁超时时间 (1000ms -> 100ms)

#### 改动2: one_wire_reset
- 临界区只保护复位脉冲 (480μs)
- 等待和采样移到临界区外

#### 改动3: one_wire_write_bit  
- 临界区只保护拉低/释放操作 (6-60μs)
- 恢复时间移到临界区外

#### 改动4: one_wire_read_bit
- 临界区只保护拉低/释放操作 (5μs)
- 延时和采样移到临界区外

---

## 4. 预期效果

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| 单次复位临界区 | 550μs | 480μs |
| 单次写bit临界区 | 60-70μs | 6-60μs |
| 单次读bit临界区 | 20μs | 5μs |
| ROM搜索总临界区 | ~10ms | ~3ms |
| 搜索失败退避 | 无 | 500ms-2.5s |
| MQTT任务饥饿 | 严重 | 解决 |

---

## 5. 验证步骤

1. **编译测试**: `idf.py build`
2. **烧录测试**: `idf.py -p COM9 flash monitor`
3. **观察MQTT连接**: 确认WiFi和MQTT保持连接
4. **观察MAX31850**: 确认传感器搜索正常进行

---

**修复人**: Kimi Code CLI  
**日期**: 2026-04-10
