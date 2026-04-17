# 紧急修复：LED_TASK 栈溢出

## 问题现象
```
***ERROR*** A stack overflow in task LED_TASK has been detected.
```

## 原因
LED 任务栈大小不足（2048 字节 → 需要 4096 字节）

## 修复
已修改 `main/main.c`：
```c
// 修改前
xTaskCreate(status_led_task, "LED_TASK", 2048, NULL, 5, NULL);

// 修改后
xTaskCreate(status_led_task, "LED_TASK", 4096, NULL, 5, NULL);
```

## 重新烧录步骤

```bash
# 1. 重新编译（增量编译，很快）
idf.py build

# 2. 进入下载模式（BOOT+RESET）

# 3. 烧录
idf.py -p COM9 flash

# 4. 监控
idf.py monitor
```

或使用 VSCode：点击 "Build" → "Flash" → "Monitor"
