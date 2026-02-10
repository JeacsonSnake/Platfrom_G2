# CMake 构建错误修复

## 错误现象
```
CMake Error in CMakeLists.txt:
  Evaluation file to be written multiple times with different content.
  This is generally caused by the content evaluating the configuration type,
  language, or location of object files:
   E:/Platform_G2/esp32_idf/build/ldgen_libraries
```

## 错误原因
- CMake 缓存损坏
- 构建目录中存在冲突的构建文件
- 多次不同配置的构建尝试

---

## 解决方案

### 步骤 1: 清理构建目录

在 VSCode ESP-IDF 终端中执行：

```bash
# 完整清理（推荐）
idf.py fullclean

# 或手动删除 build 目录
rm -rf build
```

### 步骤 2: 重新配置项目

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 检查/修改配置（可选）
idf.py menuconfig
```

### 步骤 3: 重新构建

```bash
# 构建项目
idf.py build
```

---

## 完整命令流程

```bash
# 在项目目录中执行
cd E:/Platform_G2/esp32_idf

# 1. 清理
idf.py fullclean

# 2. 设置目标
idf.py set-target esp32s3

# 3. 构建
idf.py build

# 4. 烧录（进入下载模式后）
idf.py -p COM9 flash

# 5. 监控日志
idf.py monitor
```

---

## 如果问题仍然存在

### 方法 A: 手动删除 build 目录

```powershell
# 在 PowerShell 中执行
Remove-Item -Recurse -Force E:\Platform_G2\esp32_idf\build

# 然后重新构建
idf.py set-target esp32s3
idf.py build
```

### 方法 B: 重新安装 ESP-IDF Python 环境

```powershell
# 激活 ESP-IDF Python 环境
& "C:\Users\labIn\.espressif\python_env\idf5.5_py3.11_env\Scripts\Activate.ps1"

# 重新安装依赖
cd E:\esp\v5.5.2\esp-idf
pip install -r requirements.txt --force-reinstall

# 清理并重新构建
cd E:\Platform_G2\esp32_idf
idf.py fullclean
idf.py build
```

### 方法 C: 使用 VSCode 的 "清理项目" 按钮

1. 打开 VSCode
2. 底部状态栏找到 ESP-IDF 区域
3. 点击垃圾桶图标 "ESP-IDF: Clean Project"
4. 等待清理完成
5. 点击构建按钮重新构建

---

## 预防措施

- 不要在构建过程中中断（Ctrl+C）
- 切换分支或修改 CMakeLists.txt 后建议清理重建
- 定期执行 `idf.py fullclean` 保持构建目录干净
