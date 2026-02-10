# LED GPIO48 测试

## 修改内容
已将 LED GPIO 从 GPIO2 改为 GPIO48（新版 ESP32-S3-DevKitC-1 的 RGB LED）

## 烧录步骤

```bash
idf.py build
idf.py -p COM9 flash
idf.py monitor
```

## 预期结果
- 启动时 LED 应该快速闪烁 5 次（测试）
- 然后进入正常状态指示模式

## 如果 GPIO48 无效
请改回 GPIO2，或尝试外接 LED：

```c
// main/main.h 第 27 行
#define STATUS_LED_GPIO         GPIO_NUM_2
```
