# ESP-IDF 环境问题修复指南

## 问题现象
```
No module named 'esp_idf_monitor'
This usually means that "idf.py" was not spawned within an ESP-IDF shell environment...
```

---

## 解决方案 1：手动激活 Python 虚拟环境（推荐）

### 步骤 1：找到 ESP-IDF 安装路径

通常在以下位置之一：
- `C:\Users\labIn\esp\esp-idf`
- `C:\esp\esp-idf`
- `E:\esp\esp-idf`

### 步骤 2：激活虚拟环境并运行

在 PowerShell 中执行：

```powershell
# 1. 切换到项目目录
cd E:\Platform_G2\esp32_idf

# 2. 激活 ESP-IDF Python 虚拟环境
# 方案 A：使用 VSCode 安装的 ESP-IDF
& "C:\Users\labIn\.espressif\python_env\idf5.5_py3.11_env\Scripts\Activate.ps1"

# 方案 B：如果上面路径不存在，尝试其他版本
& "C:\Users\labIn\.espressif\python_env\idf5.4_py3.11_env\Scripts\Activate.ps1"

# 3. 设置 IDF_PATH
$env:IDF_PATH = "C:\Users\labIn\esp\esp-idf"

# 4. 运行 export.ps1
& "C:\Users\labIn\.vscode\extensions\espressif.esp-idf-extension-2.0.2\export.ps1"

# 5. 验证
idf.py --version

# 6. 编译
idf.py build
```

---

## 解决方案 2：使用 ESP-IDF 命令提示符

### 方法 A：通过开始菜单

1. 按 `Win` 键
2. 搜索 "ESP-IDF"
3. 选择 "ESP-IDF 5.x PowerShell" 或 "ESP-IDF 5.x CMD"
4. 在打开的终端中：

```powershell
cd E:\Platform_G2\esp32_idf
idf.py build
```

### 方法 B：通过 ESP-IDF 安装目录

```powershell
# 运行 ESP-IDF 自带的启动脚本
& "C:\Users\labIn\esp\esp-idf\export.ps1"

# 然后编译
cd E:\Platform_G2\esp32_idf
idf.py build
```

---

## 解决方案 3：使用 esptool 直接烧录（绕过 idf.py）

如果编译已经完成（使用之前的 build 目录），可以直接用 esptool 烧录：

```powershell
# 1. 确保 ESP32 进入下载模式（BOOT+RESET）

# 2. 直接使用 esptool 烧录（无需 idf.py）
python -m esptool --port COM9 --chip esp32s3 --baud 921600 write_flash `
    0x0 build/bootloader/bootloader.bin `
    0x8000 build/partition_table/partition-table.bin `
    0x10000 build/test.bin

# 3. 查看串口日志
python -m serial.tools.miniterm COM9 115200
```

---

## 解决方案 4：修复 ESP-IDF 安装

如果以上方法都不行，可能是 ESP-IDF 安装损坏：

### 步骤 1：重新安装 Python 依赖

```powershell
# 1. 激活虚拟环境
& "C:\Users\labIn\.espressif\python_env\idf5.5_py3.11_env\Scripts\Activate.ps1"

# 2. 升级 pip
python -m pip install --upgrade pip

# 3. 重新安装依赖
cd C:\Users\labIn\esp\esp-idf
pip install -r requirements.txt
pip install esp_idf_monitor

# 4. 重新安装工具
python tools\idf_tools.py install
python tools\idf_tools.py install-python-env
```

### 步骤 2：通过 VSCode 重新配置

1. 按 `Ctrl+Shift+P`
2. 输入 `ESP-IDF: Configure ESP-IDF Extension`
3. 选择 `Advanced` 模式
4. 检查以下路径：
   - ESP-IDF Path: `C:\Users\labIn\esp\esp-idf`
   - Python Path: `C:\Users\labIn\.espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe`
   - Tools Path: `C:\Users\labIn\.espressif\tools`
5. 点击 `Install` 重新配置

---

## 解决方案 5：使用 Docker（终极方案）

如果本地环境始终有问题，可以使用 Docker：

```powershell
# 1. 安装 Docker Desktop

# 2. 运行 ESP-IDF Docker 容器
docker run -it --rm -v E:\Platform_G2\esp32_idf:/project -w /project espressif/idf:latest

# 3. 在容器内编译
idf.py build

# 4. 烧录（需要 privileged 模式）
# 先退出容器，然后：
docker run -it --rm --privileged -v E:\Platform_G2\esp32_idf:/project -w /project espressif/idf:latest idf.py flash
```

---

## 快速诊断命令

运行以下命令帮助定位问题：

```powershell
# 检查 Python 路径
Get-Command python

# 检查 pip 安装的包
pip list | findstr esp

# 检查环境变量
$env:IDF_PATH
$env:PATH

# 检查虚拟环境
$env:VIRTUAL_ENV

# 检查 esp_idf_monitor 是否存在
python -c "import esp_idf_monitor; print(esp_idf_monitor.__file__)"
```

---

## 推荐的快速解决流程

### 如果只需要烧录（已有编译好的固件）

```powershell
# 使用 Python + esptool（无需 idf.py）
python -m esptool --port COM9 --chip esp32s3 write_flash 0x10000 build/test.bin
```

### 如果需要重新编译

```powershell
# 1. 打开 ESP-IDF 专用 PowerShell（开始菜单搜索）
# 2. 在项目目录执行
cd E:\Platform_G2\esp32_idf
idf.py fullclean
idf.py set-target esp32s3
idf.py build
```

---

## 验证修复成功

```powershell
# 应该显示版本号
idf.py --version

# 预期输出
# ESP-IDF v5.5.2
```
