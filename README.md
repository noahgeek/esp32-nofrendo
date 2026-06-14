# ESP32-C3 NES 模拟器 (Nofrendo)

基于 [arduino-nofrendo](https://github.com/moononournation/arduino-nofrendo) 改造，适配 **ESP32-C3 + ST7789 240x240** 屏幕，支持 **PG-9193 蓝牙手柄** 和 **串口控制**。

## 硬件接线

### TFT 屏幕 (ST7789 240x240)

| TFT 引脚 | ESP32-C3 GPIO |
|----------|---------------|
| VCC      | 3V3           |
| GND      | GND           |
| SDA      | GPIO 6 (MOSI) |
| SCK      | GPIO 4 (SCLK) |
| DC       | GPIO 1        |
| RES      | GPIO 7        |
| BLK      | GPIO 5        |

### 功能状态

| 功能     | 状态     |
|----------|----------|
| 显示     | ST7789 240x240，NES 256x240 裁剪为 240x240 全屏 |
| 音频     | 已禁用（无硬件） |
| 存储     | ROM 直接烧录到 SPIFFS 分区，运行时通过 `esp_partition_mmap` 映射到内存（零 RAM 占用，支持任意大小 ≤1.4MB ROM） |
| 存档     | 存档/读档使用 X/Y 键，存放在 coredump 分区，2 个槽位 |
| WiFi     | 已禁用 |
| 控制     | PG-9193 蓝牙手柄 + 串口键盘 |

## 依赖库

通过 Arduino IDE 库管理器安装：

- **Arduino_GFX** — TFT 屏幕驱动
- **NimBLE-Arduino** (作者: h2zero) — 蓝牙低功耗协议栈，比默认 Bluedroid 节省约 100KB RAM
- 特别注意!!!ESP32-C3连接TFT一定要用ESP32开发板库2.0.9版本才能点亮屏幕

> ⚠️ **重要**：本项目自带完整 nofrendo 模拟器源码于 `src/` 目录。
> **请勿** 通过 Arduino IDE 库管理器安装 `nofrendo` 库，否则会与项目内 `src/` 重复定义导致编译失败。
> 如已安装，请删除 `Documents\Arduino\libraries\nofrendo` 目录。

## 按键映射

### PG-9193 蓝牙手柄

| 手柄按键 | NES 功能 |
|----------|----------|
| 方向键   | 方向键   |
| SELECT   | SELECT   |
| START    | START    |
| A        | A        |
| B        | B        |
| X        | 存档（保存当前游戏状态） |
| Y        | 读档（恢复存档） |
| L        | -        |
| R        | -        |

### 串口键盘（调试/备用）

| 按键 | 功能     |
|------|----------|
| W    | UP       |
| S    | DOWN     |
| A    | LEFT     |
| D    | RIGHT    |
| V    | SELECT   |
| B    | START    |
| K    | A        |
| J    | B        |
| I    | X (存档)  |
| U    | Y (读档)  |
| Z    | 开启 BLE 扫描 |

## 使用方法

### 1. 上传游戏 ROM

将 `.nes` 文件放入 `data/` 目录（或任意路径），运行上传脚本：

```powershell
.\upload_spiffs.ps1 -Rom .\data\Chase.nes
```

ROM 直接烧录到 Flash 的 SPIFFS 分区偏移 (0x290000)，固件通过 `esp_partition_mmap` 在运行时把它映射到内存，**完全不占用 DRAM**。
切换游戏时重新运行该脚本烧录即可。

### 2. 编译烧录

Arduino IDE 设置：
- 开发板: **ESP32C3 Dev Module**
- 分区方案: **Default 4MB with SPIFFS**

> 项目源码自包含于 `src/` 目录，不需要额外安装 nofrendo 库。

### 3. 运行

1. 上电后自动启动游戏
2. 蓝牙手柄会自动扫描连接（约 5 秒）
3. 如果手柄未自动连接，串口发送 `Z` 手动触发扫描
4. 手柄断开后按 `Z` 重新连接

## 项目文件说明

| 文件 | 说明 |
|------|------|
| `esp32-nofrendo.ino` | 主程序入口，初始化显示，启动模拟器（ROM 通过 mmap 加载） |
| `hw_config.h` | 硬件配置（引脚、控制器类型） |
| `display.cpp` | ST7789 240x240 显示驱动，NES 画面裁剪适配 |
| `controller.cpp` | 双控制器：串口 + NimBLE 蓝牙手柄 PG-9193 |
| `osd.c` | OSD 层：内存分配、视频驱动、输入、定时器 |
| `sound.c` | 音频驱动（已禁用） |
| `src/` | Nofrendo 模拟器核心源码（自包含，已针对 ESP32-C3 适配 mmap + 存档分区） |
| `upload_spiffs.ps1` | ROM 烧录脚本（直接 esptool 写入 SPIFFS 分区） |

## 技术要点

- **ROM mmap 零拷贝**：NES ROM 直接烧录到 SPIFFS 分区，运行时 `esp_partition_mmap` 把 Flash 区域映射到 CPU 地址空间，CPU/PPU 像访问 RAM 一样访问 ROM，**DRAM 占用为 0**，可运行 256KB+ 大 ROM（如 Contra、恶魔城）
- **存档持久化**：存档使用 `coredump` 分区（64KB），通过 `esp_partition_read/write` 直接读写，2 个槽位
- **NimBLE 替代 Bluedroid**：ESP32-C3 只有 400KB SRAM，NimBLE 协议栈占用约 30KB RAM（Bluedroid 约 110KB），使 BLE 和游戏能共存
- **后台 BLE 连接**：BLE 在独立 FreeRTOS 任务中异步连接，不影响游戏帧率
- **暂停游戏定时器**：BLE 初始化期间暂停 NES 定时器，避免 CPU 竞争

## 常见问题

**Q: 蓝牙手柄连不上？**
A: 确保手柄处于配对模式（通常长按 Home 键）。串口发送 `Z` 手动触发扫描。首次连接需要配对，成功后会自动重连。

**Q: 游戏运行卡顿？**
A: ESP32-C3 单核运行 NES 模拟器性能有限，复杂场景可能掉帧。关闭 BLE 可略微提升性能。

**Q: 上传 ROM 失败？**
A: 检查 `upload_spiffs.ps1` 中的 COM 口号是否正确，确保开发板已连接。

## 参考

- [arduino-nofrendo](https://github.com/moononournation/arduino-nofrendo) — 原始 NES 模拟器 Arduino 库
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — 轻量级 BLE 协议栈
- [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) — TFT 屏幕驱动库
