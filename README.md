# ESP32-S3-N16R8 2.4 寸 TFT 触摸小游戏

这是一个基于 PlatformIO + Arduino 框架的 ESP32-S3 项目，使用 2.4 寸 240x320 SPI TFT 触摸屏，实现一个简单横版跳跃小游戏。

## 硬件

- 开发板：ESP32-S3-N16R8
- 屏幕：2.4 寸 SPI TFT，分辨率 240x320
- 屏幕驱动：ILI9341
- 触摸芯片：XPT2046
- 通讯方式：屏幕和触摸共用 SPI，总线上的 `SCK`、`MOSI`、`MISO` 共用，`CS` 分开连接

## 引脚连接

### TFT 模块完整引脚对应关系

| TFT 模块引脚 | ESP32-S3-N16R8 引脚 | 是否使用 | 说明 |
| --- | --- | --- | --- |
| `VCC` | `3V3` | 是 | 屏幕电源正极，使用 3.3V 供电 |
| `GND` | `GND` | 是 | 电源地 |
| `CS` | `GPIO10` | 是 | TFT 屏幕片选 |
| `RESET` / `RST` | `GPIO14` | 是 | TFT 屏幕复位 |
| `DC` / `RS` | `GPIO9` | 是 | TFT 数据/命令选择 |
| `SDI` / `MOSI` | `GPIO11` | 是 | TFT SPI MOSI，同时与触摸 `T_DIN` 共用 |
| `SCK` / `CLK` | `GPIO12` | 是 | TFT SPI 时钟，同时与触摸 `T_CLK` 共用 |
| `LED` / `BL` | `GPIO21` | 是 | TFT 背光控制；如果屏幕背光直接接 `3V3`，代码中可改为 `-1` |
| `SDO` / `MISO` | `GPIO13` | 是 | TFT SPI MISO，同时与触摸 `T_DO` 共用 |
| `T_CLK` | `GPIO12` | 是 | 触摸 SPI 时钟，与 TFT `SCK` 共用 |
| `T_CS` | `GPIO8` | 是 | 触摸片选，必须和 TFT `CS` 分开 |
| `T_DIN` | `GPIO11` | 是 | 触摸 SPI MOSI，与 TFT `SDI/MOSI` 共用 |
| `T_DO` | `GPIO13` | 是 | 触摸 SPI MISO，与 TFT `SDO/MISO` 共用 |
| `T_IRQ` | `GPIO7` | 是 | 触摸中断 |
| `SD_CS` | 未连接 | 否 | TF/SD 卡片选，本项目未使用 SD 卡 |
| `SD_MOSI` / `SD_DI` | 未连接 | 否 | TF/SD 卡 MOSI，本项目未使用 SD 卡 |
| `SD_MISO` / `SD_DO` | 未连接 | 否 | TF/SD 卡 MISO，本项目未使用 SD 卡 |
| `SD_SCK` / `SD_CLK` | 未连接 | 否 | TF/SD 卡时钟，本项目未使用 SD 卡 |

说明：不同 2.4 寸 TFT 模块丝印可能略有差异，`SDI` 等同 `MOSI`，`SDO` 等同 `MISO`，`RESET` 等同 `RST`，`LED` 等同 `BL`。

### 代码中的引脚定义

```cpp
constexpr int TFT_SCK = 12;
constexpr int TFT_MOSI = 11;
constexpr int TFT_MISO = 13;
constexpr int TFT_CS = 10;
constexpr int TFT_DC = 9;
constexpr int TFT_RST = 14;
constexpr int TFT_BL = 21;
constexpr int TOUCH_CS = 8;
constexpr int TOUCH_IRQ = 7;
```

## 游戏操作

屏幕底部有三个触摸按钮：

- `BACK`：向后移动
- `FWD`：向前移动
- `JUMP`：跳跃

组合操作：

- 先按 `FWD` 再按 `JUMP`，或快速同时按 `FWD + JUMP`：向前跳跃
- 先按 `BACK` 再按 `JUMP`，或快速同时按 `BACK + JUMP`：向后跳跃

说明：XPT2046 是电阻触摸方案，通常不是真正多点触控。项目中通过“方向保持 + 两指平均点容错”的方式模拟组合按键。

## 编译

在项目目录执行：

```bash
pio run
```

## 烧录

连接 ESP32-S3 后执行：

```bash
pio run --target upload
```

如有多个串口设备，可指定上传端口：

```bash
pio run --target upload --upload-port /dev/cu.usbmodemXXXX
```

## 串口监视

```bash
pio device monitor --baud 115200
```

## 常见问题

### 屏幕花屏

当前代码使用 `40MHz` TFT SPI 频率，比较稳定。如果自行提高到 `80MHz` 后出现花屏，通常是屏幕模块或杜邦线不稳定，建议恢复到 `40MHz`。

### 触摸位置偏移

可在 `src/main.cpp` 中调整以下参数：

```cpp
constexpr int TOUCH_MIN_X = 250;
constexpr int TOUCH_MAX_X = 3900;
constexpr int TOUCH_MIN_Y = 250;
constexpr int TOUCH_MAX_Y = 3900;
constexpr int TOUCH_X_OFFSET = 0;
constexpr int TOUCH_Y_OFFSET = 0;
```

如果按钮位置整体偏左/偏右，优先调整 `TOUCH_X_OFFSET`；如果整体偏上/偏下，优先调整 `TOUCH_Y_OFFSET`。

### 背光不亮

确认 `LED` 是否接到了 `GPIO21`。如果你的屏幕背光直接接 `3V3`，可把代码中的背光引脚改为：

```cpp
constexpr int TFT_BL = -1;
```
