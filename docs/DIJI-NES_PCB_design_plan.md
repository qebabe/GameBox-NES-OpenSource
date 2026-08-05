# DIJI-NES ESP32-S3 PCB Design Plan

本文档用于把当前 DIJI-NES 项目从开发板/模块连线方案，整理为第一版自研 PCB 的原理图规划。目标是先做一块可验证的 ESP32-S3 掌机主板，而不是一步到位追求最终量产形态。

## 1. 设计目标

- 主控使用 ESP32-S3，优先选择 `ESP32-S3-WROOM-1-N16R8` 模组。
- 支持当前固件需要的显示、音频、SD 卡、按键输入。
- USB-C 支持供电、下载、串口日志。
- 第一版 PCB 以功能验证和调试便利为主，预留测试点和扩展接口。
- 尽量避免使用 ESP32-S3 启动绑带脚做用户按键，降低上电异常和下载失败概率。

## 2. 推荐芯片/模块选择

第一版建议使用 `ESP32-S3-WROOM-1-N16R8` 模组，而不是裸 ESP32-S3 芯片。

原因：

- Flash、PSRAM、晶振、射频匹配和天线已在模组内完成。
- PCB 布局难度显著降低。
- 更适合快速打样和功能验证。
- 当前固件配置已经按 16 MB Flash + 8 MB PSRAM 设计。

如后续要做更紧凑或更低成本版本，再评估裸芯片方案。

## 3. 原理图页面划分

建议按功能分成以下原理图页：

| 页面 | 内容 |
| --- | --- |
| `Power` | USB-C 输入、锂电池接口、充电 IC、5 V/3.3 V 电源、开关、电量检测 |
| `ESP32-S3` | ESP32-S3-WROOM-1-N16R8、EN、BOOT、USB D+/D-、UART、测试点 |
| `Display` | ST7789/ILI9341 SPI TFT、背光控制、FPC 或排针接口 |
| `Storage` | MicroSD 卡座，SPI 或 SDMMC 二选一 |
| `Audio` | 优先保留参考板 ES8311 麦克风/Codec + 功放链路；MAX98357A 作为简化备选 |
| `Input` | I2C IO 扩展芯片、A/B/SELECT/START/UP/DOWN/LEFT/RIGHT 8 个按键 |
| `Expansion` | I2C、UART、3V3、GND、备用 GPIO 测试点 |

## 4. 当前固件原始引脚映射

当前默认原始硬件配置来自 `src/board_config.h` 的非 `DIJI_BOARD_LCDWIKI_ES3C28P` 分支。

| 模块 | 信号 | 当前 GPIO |
| --- | --- | --- |
| TFT | CS | GPIO10 |
| TFT | DC | GPIO11 |
| TFT | RST | GPIO12 |
| TFT | SCLK | GPIO14 |
| TFT | MOSI | GPIO13 |
| SD SPI | CS | GPIO42 |
| SD SPI | SCLK | GPIO40 |
| SD SPI | MISO | GPIO39 |
| SD SPI | MOSI | GPIO41 |
| I2S | BCLK | GPIO5 |
| I2S | LRCLK | GPIO4 |
| I2S | DOUT | GPIO6 |
| Button | A | GPIO48 |
| Button | B | GPIO47 |
| Button | SELECT | GPIO16 |
| Button | START | GPIO15 |
| Button | UP | GPIO17 |
| Button | DOWN | GPIO3 |
| Button | LEFT | GPIO8 |
| Button | RIGHT | GPIO18 |

这套引脚可以作为第一版参考，但正式 PCB 建议调整掉 `GPIO3` 这类启动相关/特殊脚上的用户按键。

## 5. 建议的第一版 PCB 引脚规划

以下规划保留当前大部分固件映射，只调整风险较高或更适合硬件设计的部分。若基于现有 ESP32-S3 + ES8311 麦克风原理图改版，建议保留原音频 I2C，并在同一条 I2C 总线上增加按键扩展芯片。

| 模块 | 信号 | 建议 GPIO | 说明 |
| --- | --- | --- | --- |
| USB | D- | GPIO19 | ESP32-S3 原生 USB |
| USB | D+ | GPIO20 | ESP32-S3 原生 USB |
| Boot | BOOT | GPIO0 | 10 k 上拉，按键到 GND |
| Reset | EN/CHIP_PU | EN | 10 k 上拉，1 uF 到 GND，按键到 GND |
| TFT | CS | GPIO10 | 沿用当前固件 |
| TFT | DC | GPIO11 | 沿用当前固件 |
| TFT | RST | GPIO12 | 沿用当前固件 |
| TFT | SCLK | GPIO14 | 沿用当前固件 |
| TFT | MOSI | GPIO13 | 沿用当前固件 |
| TFT | BL | GPIO45 或备用 GPIO | 若使用 GPIO45，需确认上电状态不会影响启动；也可换普通 GPIO |
| SD SPI | CS | GPIO42 | 沿用当前固件 |
| SD SPI | SCLK | GPIO40 | 沿用当前固件 |
| SD SPI | MISO | GPIO39 | 沿用当前固件 |
| SD SPI | MOSI | GPIO41 | 沿用当前固件 |
| I2S | BCLK | GPIO5 | 沿用当前固件 |
| I2S | LRCLK | GPIO4 | 沿用当前固件 |
| I2S | DOUT | GPIO6 | 沿用当前固件 |
| Buttons | A/B/SELECT/START/UP/DOWN/LEFT/RIGHT | I2C 扩展芯片 | 推荐方案，避免继续消耗 ESP32-S3 GPIO |

若不使用 I2C 扩展芯片，才需要重新挑选 8 个空闲 GPIO。直接 GPIO 方案需要避开 `GPIO0`、`GPIO19`、`GPIO20`、音频 I2S/I2C、TFT SPI、SD 卡和启动绑带脚。

## 6. 电源设计建议

第一版建议做两种供电入口：

- USB-C 5 V 输入。
- 单节锂电池输入，带充电管理。

典型电源链路：

```text
USB-C 5V -> 充电/电源路径管理 -> VBAT/VSYS -> 3.3V Buck/LDO -> ESP32-S3 + TFT + SD + ES8311/功放
```

注意事项：

- ESP32-S3 3.3 V 电源建议至少按 500 mA 以上能力设计。
- 如果屏幕背光、音频功放和 Wi-Fi 同时工作，3.3 V 电源余量要更大。
- USB 输入处放 ESD 保护和至少 10 uF 电容。
- ESP32-S3 模组 3.3 V 附近放 10 uF + 0.1 uF 去耦。
- 音频 Codec、功放和屏幕背光的电源走线要稍粗，避免音频爆音或屏幕闪烁。

## 7. USB-C 与下载

ESP32-S3 可以直接使用原生 USB 下载和打印日志：

| USB-C | ESP32-S3 |
| --- | --- |
| D- | GPIO19 |
| D+ | GPIO20 |
| VBUS | 5 V 输入 |
| CC1 | 5.1 k 到 GND |
| CC2 | 5.1 k 到 GND |
| GND | GND |

建议：

- D+/D- 串联 22 ohm 或 33 ohm 电阻，靠近 ESP32-S3 放置。
- USB 入口加 ESD 保护。
- D+/D- 差分线尽量等长、短、少过孔。
- 第一版可不放 CH340/CH343，除非希望保留传统 UART 下载链路。

## 8. BOOT/RESET 电路

必须保留：

- `EN/CHIP_PU`：10 k 上拉到 3.3 V，1 uF 到 GND，RESET 按键拉到 GND。
- `GPIO0`：10 k 上拉到 3.3 V，BOOT 按键拉到 GND。

可选：

- 若后续加 USB-UART 芯片，可增加 DTR/RTS 自动下载电路。
- 若只用 ESP32-S3 原生 USB，第一版可以手动 BOOT + RESET。

## 9. 显示屏设计

当前固件支持 ST7789 和 ILI9341，第一版 PCB 要先定具体屏幕模组。

推荐先用已经验证过的 2.8 寸 SPI TFT：

- 分辨率：320 x 240。
- 接口：SPI。
- 供电：确认是 3.3 V 逻辑。
- 背光：建议由 MOSFET 或三极管控制，不要直接大电流从 GPIO 拉背光。

如果使用 FPC 屏幕，需要确认：

- FPC 间距和针脚顺序。
- 背光 LED 正负极和限流方式。
- 触摸芯片是否存在，以及是否需要 I2C。

## 10. SD 卡设计

第一版可以继续使用 SPI MicroSD，优点是和当前原始固件一致，改动少。

当前立创 EDA 原理图已补充 `HYCW19-TF09-205B` MicroSD 卡座，按 SPI 模式连接：

| MicroSD 信号 | 原理图网络 | ESP32-S3 GPIO | 说明 |
| --- | --- | --- | --- |
| `CD/DAT3` | `IO39` | GPIO39 | SPI CS，已加 10 k 上拉到 3.3 V |
| `CMD` | `SPI_MOSI` | GPIO48 | SPI MOSI，与 TFT 共用 |
| `CLK` | `SPI_CLK` | GPIO47 | SPI SCLK，与 TFT 共用 |
| `DAT0` | `IO38` | GPIO38 | SPI MISO |
| `VDD` | `3.3V` | - | 已加 0.1 uF 去耦 |
| `VSS` | `GND` | - | 电源地 |
| `EH` | `GND` | - | 卡座外壳/屏蔽地 |
| `DAT1/DAT2/CD` | 未接 | - | SPI 模式第一版暂不使用 |

建议：

- SD 卡座靠近 ESP32-S3。
- CLK 线尽量短。
- CMD/MOSI、MISO、CS 可预留 22 ohm 串联电阻位置。
- SD 卡电源放 10 uF + 0.1 uF。
- 如果后续追求更高读取速度，再切换到 SDMMC。

## 11. 音频设计

若基于现有 ESP32-S3 + ES8311 参考原理图改版，建议第一版尽量保留 ES8311、麦克风和后级功放链路。这样 PCB 改动少，也给后续语音输入、录音或 AI 交互留下空间。

参考板音频链路：

```text
麦克风 -> ES8311 -> 后级功放 -> 喇叭
ESP32-S3 -- I2C --> ES8311 控制
ESP32-S3 -- I2S --> ES8311 音频数据
```

注意事项：

- ES8311 的 I2C 地址通常为 `0x18`，I2C 按键扩展芯片需要避开这个地址。
- ES8311、功放、麦克风模拟地/音频地要按参考板方式处理，尽量不要随意改地线和滤波电容。
- I2S 的 MCLK/BCLK/LRCLK/DOUT/DIN 尽量沿用参考板网络名，固件后续再适配。
- 如果当前 DIJI-NES 固件仍使用 MAX98357A 输出，后续需要增加 ES8311 初始化和输出配置。

当前 DIJI-NES 原始固件的简化音频方案是 MAX98357A：

| MAX98357A | ESP32-S3 |
| --- | --- |
| BCLK | GPIO5 |
| LRC/LRCLK | GPIO4 |
| DIN | GPIO6 |
| VIN | 3.3 V 或 5 V，按器件/模块规格确认 |
| GAIN/SD_MODE | 按目标音量和静音方式配置 |

如果不用参考板 ES8311，而改回 MAX98357A，建议：

- 喇叭接口旁边标注阻抗和功率，例如 `4 ohm 3 W` 或 `8 ohm 1 W`。
- 音频功放电源要有足够电流余量。
- I2S 线远离天线区域和 USB 差分线。
- 预留功放关断控制脚更好，方便以后做省电和消除底噪。

## 12. 按键设计

推荐使用 I2C IO 扩展芯片接 8 个 NES 按键，而不是继续占用 8 个 ESP32-S3 GPIO。

推荐芯片：

| 芯片 | IO 数量 | 说明 |
| --- | --- | --- |
| `PCF8574` | 8 | 简单、便宜、够接 8 个按键 |
| `TCA9554` | 8 | 方向寄存器更清晰，供货也常见 |
| `MCP23008` | 8 | 功能更完整，软件稍复杂 |
| `MCP23017` | 16 | 可扩展更多按键、摇杆或功能键 |

第一版建议优先选 `PCF8574` 或 `TCA9554`。如果只需要 8 个 NES 按键，`PCF8574` 已经足够。

建议连接：

| IO 扩展芯片 | 连接 |
| --- | --- |
| VCC | 3.3 V |
| GND | GND |
| SDA | 系统 I2C SDA，与 ES8311 共用 |
| SCL | 系统 I2C SCL，与 ES8311 共用 |
| A0/A1/A2 | 接 GND 或按地址规划配置 |
| INT | 可选，接 ESP32-S3 空闲 GPIO；第一版也可以不接，固件轮询 |
| P0-P7 | A/B/SELECT/START/UP/DOWN/LEFT/RIGHT |

每个按键建议这样接：

```text
3.3V -- 10k -- Pn
Pn ---- 按键 ---- GND
```

如果使用的 IO 扩展芯片支持内部弱上拉，也可以省掉外部上拉；但第一版 PCB 建议预留 10 k 上拉电阻位置，方便调试。

建议网络名：

| 按键 | 扩展 IO | 网络名 |
| --- | --- | --- |
| A | P0 | `BTN_A` |
| B | P1 | `BTN_B` |
| SELECT | P2 | `BTN_SELECT` |
| START | P3 | `BTN_START` |
| UP | P4 | `BTN_UP` |
| DOWN | P5 | `BTN_DOWN` |
| LEFT | P6 | `BTN_LEFT` |
| RIGHT | P7 | `BTN_RIGHT` |

固件侧需要新增一个 I2C 按键读取层，把扩展芯片读到的 8 bit 映射到现有 `DIJI_BTN_A/B/SELECT/START/UP/DOWN/LEFT/RIGHT` 状态。

当前固件按键读取适合这种接法：

```text
GPIO ---- 按键 ---- GND
```

软件侧启用内部上拉，按下为低电平。这个直接 GPIO 方案保留为备选；若使用 I2C 扩展芯片，则以扩展芯片读值为准。

建议：

- 8 个按键都加测试点或至少保证可测。
- 按键走线避免经过天线禁布区。
- 不建议把普通用户按键接在启动绑带脚上。
- 如果后续想做手柄手感，可以把方向键和 AB 键按实际外壳位置先画机械草图。

## 13. PCB 布局优先级

布局建议顺序：

1. 固定外形、屏幕、按键、USB-C、SD 卡、喇叭接口位置。
2. 放 ESP32-S3 模组，并保证天线区域禁布。
3. 放电源和充电电路。
4. 放屏幕 FPC/排针和 SD 卡座。
5. 放 ES8311、麦克风、后级功放和喇叭接口；若选择简化方案则放 MAX98357A。
6. 走 USB D+/D-。
7. 走 SPI/I2S/按键线。
8. 铺地和检查回流路径。

优先保证：

- ESP32-S3 天线区域不铺铜、不走线、不放器件。
- USB 差分线短且成对。
- 3.3 V 电源粗一些，去耦电容靠近负载。
- SD CLK、TFT SCLK 不要绕远路。

## 14. 第一版打样检查清单

原理图检查：

- [ ] ESP32-S3 模组型号为 N16R8。
- [ ] EN 有上拉和 RC 延时。
- [ ] GPIO0 有上拉和 BOOT 按键。
- [ ] USB-C CC1/CC2 有 5.1 k 下拉。
- [ ] USB D+/D- 接 GPIO20/GPIO19，方向没有接反。
- [ ] 3.3 V 电源能力足够。
- [ ] TFT、SD、I2S、I2C 按键扩展芯片与固件配置一致。
- [ ] 没有把用户按键强拉启动绑带脚。
- [ ] I2C 总线上 ES8311 与按键扩展芯片地址不冲突。
- [ ] SD 卡、屏幕、音频接口方向和引脚顺序确认过。
- [ ] 关键电源和信号有测试点。

PCB 检查：

- [ ] ESP32-S3 天线禁布区满足模组要求。
- [ ] USB D+/D- 走线短、成对、少过孔。
- [ ] 电源入口、3.3 V、功放、背光走线宽度足够。
- [ ] 去耦电容靠近对应芯片电源脚。
- [ ] SD CLK 和 TFT SCLK 线短且不贴近音频输出。
- [ ] BOOT、RESET、USB、SD 卡位置方便调试。
- [ ] 丝印标清接口方向、按键名称和电源极性。

## 15. 后续要确认的问题

在正式画 PCB 前，需要再确认这些选择：

1. 屏幕最终使用 ST7789 还是 ILI9341。
2. 屏幕是排针模块、FPC 裸屏，还是带触摸的一体屏。
3. 是否需要锂电池充电和电量检测。
4. 是否需要实体音量键、耳机口、震动马达。
5. SD 卡继续 SPI，还是改为 SDMMC。
6. I2C 按键扩展芯片使用 `PCF8574` 还是 `TCA9554`。
7. 外壳尺寸和按键布局是否已经确定。

这些确认后，再开始建 KiCad 或立创 EDA 工程会更稳。
