# 更新日志

本文件记录项目的所有重要变更。

格式基于 [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)，并遵循 [Semantic Versioning](https://semver.org/spec/v2.0.0.html)。

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### 新增

- 系统设置新增横屏 180° 旋转并同步触摸坐标；原版 ESP32-S3 将游戏显示明确为 `320×240` 全屏与 `256×240` 原生两档，并为 ST7789 面板增加可持久化的颜色反转开关，设备端和设置网页均可即时调整。
- 新增服务端远程配置中心：后台可按设备 ID、硬件通道、固件版本范围和优先级控制游戏商店、云存档及匿名统计开关；设备异步拉取并缓存到 NVS，服务不可用时保留最近有效配置，缓存失效后恢复功能均开启的安全默认值。
- 新增设备注册与 OTA 闭环：主机首次联网生成并在 NVS 持久化 256 位随机令牌，服务端仅保存令牌 SHA-256；设备后台展示认证覆盖率、固件版本分布及 OTA 检查、下载、安装、健康启动和回滚事件，并提供旧固件兼容开关及管理员认证重置。
- 新增匿名设备使用统计：掌机联网、游戏启动/下载，以及配套服务记录商店、OTA、云存档行为；统计数据不包含 SSID、客户端 IP、单独的 MAC 字段或 ROM 文件名。服务端实现和管理入口不属于公开仓库。
- 进入系统设置时自动启动统一设置网页，退出系统设置时关闭；网页首页支持调整音量、显示模式、开机主页、倒带时长和自动关机，并集中提供壁纸及 ROM 管理，避免多个 HTTP 服务重复占用端口和内存。
- 统一网页的 ROM 管理支持通过局域网或设备热点拖拽、多选上传 `.nes` 到 SD 卡 `/rom`，并浏览、删除卡中已有 ROM；上传采用流式写入，限制单文件 8 MB，校验 iNES 头与声明长度，同名文件拒绝覆盖，退出设置后按需刷新游戏列表。
- 设置新增“壁纸”页面，以每屏 `3×2` 缩略图宫格选择内置壁纸或 SD 卡 `/wallpapers` 中的 JPEG；统一网页同样使用自适应缩略图宫格，并支持浏览器拖动裁切、缩放及处理为 `320×240` 后上传。
- 设置新增“SD 卡状态”页面，可查看挂载状态、卡类型、文件系统、容量、已用和剩余空间；支持触屏/手柄重新检测，并通过二次确认执行格式化，完成后自动创建 `rom`、`saves`、`wallpapers` 目录并刷新游戏列表。
- 游戏商店后端新增阿里云 OSS 私有桶存储抽象、ROM/封面清单、幂等迁移与完整性核验工具，并支持 `local`、`dual`、`object` 三阶段无删除切换。
- 新增受后台 Basic Auth 保护的 OSS 在线诊断接口，可在实际服务进程中验证配置以及上传、HEAD、下载、列举、删除权限，响应会脱敏且自动清理临时对象。
- 系统界面新增统一手柄焦点导航：P1/P2 均可操作菜单，SELECT 在内容区与底部操作栏之间切换，主页、游戏列表、商店、设置和操作弹窗均支持无触屏完整操作；危险操作默认聚焦取消项。
- 修复 LCDWIKI ES3C28P 软件关机后的触摸唤醒：关机前确认 FT6336 中断模式并保持触摸复位脚为高电平，唤醒后不再复位触摸芯片，避免长按过程被打断。
- 将原始 ESP32-S3 + ST7789 硬件提升为显式板型：`esp32s3-n16r8` 环境固定选择原版显示、SPI SD、MAX98357A 和 8 键 GPIO 配置，缺失或重复选择板型时在编译期报错，避免后续新增硬件环境意外覆盖原版引脚。
- 修正 C3 手柄旧摇杆接线文档和 ESP-NOW v3 遗留测试，移除 S3 环境重复的 RTTI 编译参数；新增原版 S3、LCDWIKI S3、C3 手柄三板 merged/OTA 固件包及 SHA-256 校验文件。
- 原版 `esp32s3-n16r8` 与 LCDWIKI 主机统一使用 `0.6.1-dev` 固件版本，两个 S3 OTA 通道保持独立但版本号同步，便于统一发布和管理。
- ESP32-C3 手柄固件升级到 0.1.2：活动时保持 60Hz，连接空闲降为 10Hz，离线改为间歇扫频，5 分钟无按键进入按键唤醒的 Deep-sleep；电池 ADC 改为每 10 秒 8 次平均采样和分段电量曲线，并支持通过短跳转直接从 OSS 下载经 SHA-256 校验的 OTA 固件。
- OTA 固件后台为已发布通道新增启用/停用、强制更新、删除和原子替换 BIN 操作，并校验上传镜像的目标芯片，阻止 ESP32-C3 手柄固件误发布到 ESP32-S3 主机通道。
- OTA 管理页上传的 BIN 改存私有 OSS；新主机和手柄通过短期签名 URL 直接从 OSS 下载，服务器仅返回短跳转，旧固件保留兼容代理；发布索引增加跨进程锁、OSS 事务备份与自动恢复，删除/替换/发布失败时回滚对象，避免孤儿文件和并发覆盖。
- 服务启动时输出虚拟环境、Python 解释器和存储配置摘要，并在后台自动执行 OSS 幂等上传、完整性核验、清单发布与热重载；健康接口提供进度/结果，亦可通过受保护 API 手动触发，无需 SSH 执行迁移。
- 新增云端存档：设备 ID 自动建立独立命名空间，3 个手动槽/自动槽/电池 SRAM 上传与恢复、缩略图、冲突检测及每槽历史版本；二进制存入 OSS，版本元数据存入 SQLite。
- 本地存档写入后自动加入持久化待同步队列，开机联网或进入商店、OTA、云存档时自动上传；设置页保留立即同步和经过 SHA-256 校验的原子恢复入口。
- 云存档待同步队列细化到具体槽位，避免单个手动槽、自动槽或 SRAM 变化时重复上传该游戏的全部存档；旧版按 ROM 记录的队列继续兼容。
- 云端存档设置页新增“立即同步”“从云端恢复”和“返回”操作焦点，支持方向键、A/START 和 B 完整操作。
- 新增三个手动存档槽和一个自动续玩槽，均保存 160×120 RGB565 缩略图与时间戳；主页新增“继续”入口。
- 游戏列表封面优先显示自动槽的最近游戏画面；没有自动截图时使用时间戳最新的手动槽截图，再回退到商店封面或占位图。横向截图按 4:3 等比居中，不做拉伸。
- 新增可配置 5/10/20 秒的 Rewind，使用 PSRAM 周期完整前态和中间 XOR 差分环形缓冲，按 `SELECT+LEFT` 倒带。
- 新增 Mapper 7 AxROM 与 Mapper 66 GxROM，包括 bank、AxROM 单屏镜像及存档/Rewind 状态。
- ESP-NOW 手柄协议升级到 v4，连接后通过四时间戳定期同步主机与手柄运行时间，状态包携带实体按键采样时刻，游戏列表的 P1/P2 在线角标实时显示平滑后的单向通信延迟。
- ESP-NOW 手柄协议升级到 v3，在电量、序号去重和非阻塞震动基础上增加动态信道探测/确认、信道持久化及配对后单播；WiFi 与手柄可在路由器当前信道同时工作。
- 无线生命周期改为开机连接已保存 WiFi 并常驻；商店、云存档和 OTA 复用现有连接，掉线自动重连后重新绑定 ESP-NOW 信道。
- 设置页 WiFi 项改为实时显示“已连接/已断开/未配置”，系统更新页同步显示当前 SSID 和 RSSI。
- 商店列表缓存更新增加 LittleFS 空间估算、低空间时的旧索引回收、备份恢复，以及页码、条目、写入位置、剩余空间和堆内存串口诊断。
- 修复商店列表以 100 条分页时 HTTPS/JSON 内存峰值过高、并跨 TLS 请求长期持有 LittleFS 文件句柄，导致第二页写入中途失效的问题；现改为 25 条分页且每页独立追加。
- LittleFS 缓存未命中改用静默 `stat` 检查，不再为每个未下载 ROM、封面或未创建云存档队列输出 VFS `open()` 错误。
- WiFi 常驻后，手动槽、自动槽和 Battery SRAM 落盘后会在约 2 秒后合并启动后台云同步；HTTPS 失败保留持久化队列并延迟重试，同步期间的新存档不会被旧上传误清除。
- 云存档 HTTPS 上传转为独立低优先级任务，设置页实时显示“同步中/待同步/已同步”，不再要求用户进入云存档页手动触发。
- 修复中文 ROM 名称在追加 `.slotN.sav.tmp` 或 `.auto.thumb.tmp` 后超过 LittleFS/VFS 32 字节事务文件名上限，导致部分手动槽和缩略图无法保存的问题；越界派生路径改用来源标识加 ROM 哈希的短文件名，已能成功使用的旧路径保持不变。
- 新增触摸控制、触摸校准、ESP-NOW 双手柄、内置 ROM 存储、联网游戏商店和设置界面。
- 配网热点名称改为 `GameBox-` 加设备 MAC 后 6 位，并使用免密码连接。
- 新增 GameBox OTA：使用 eFuse STA MAC 生成固定设备 ID，支持 HTTPS 版本检查、升级确认、下载进度、SHA-256 与固件镜像校验、双 OTA 分区切换和首启确认。
- 商店后端新增 OTA manifest、版本检查、应用固件下载接口，以及受密码保护的固件上传管理页面。
- 新增软件电源管理：可立即进入深度睡眠，也可设置无操作 5/10/20 分钟自动关机；关闭屏幕、功放、音频和无线后，通过长按触摸屏约 1.5 秒重新启动。
- 调整设置首页入口：隐藏独立音量入口，将关机放到首页，并将设备信息与 OTA 检查统一为独立的“系统更新”入口。
- 将开机主页改为 DeskBox 风格的壁纸时钟，显示本地时间、秒、日期和星期；新增“游戏”入口进入原 ROM 列表，并支持从列表返回时钟主页。
- 壁纸随应用固件嵌入；已有 WiFi 配置时，启动阶段完成联网校时并保持 WiFi，ESP-NOW 手柄同时恢复到路由器当前信道。
- 壁纸时钟右下角改为紧凑的“游戏”和“设置”双入口。
- 系统设置由六宫格改为支持触摸上下滑动的单列列表，重新加入音量设置，并新增可持久化的开机主页选项，可选择壁纸时钟或游戏列表。
- 设置列表升级为像素级跟手滚动，加入滑动惯性、边界阻尼和回弹收束，不再按整行跳动。
- 设置列表改用 PSRAM 离屏画布合成后整块推送，并将滚动刷新限制在约 30 FPS，避免直接清屏重画造成闪烁。
- 本地游戏列表改为连续惯性滚动，移除上一页/下一页，保留封面联动、点击启动和静止长按删除，并使用独立 PSRAM 画布避免闪烁。
- 游戏商店改为连续滚动缓存列表，服务端分页与后台刷新对用户透明；底栏改为更新、下载、返回、顶部和更多。
- 修复从游戏列表进入商店时停留在原页面的问题：现在会立即显示加载状态，优先展示本地缓存，并明确提示后台更新成功或失败。
- 游戏商店改为异步预取分页：进入商店后台加载前 3 页共 75 条，浏览到第 2 页时提前请求第 4 页，之后始终预留约一页滑动余量；HTTPS 请求和缓存合并不再阻塞界面，预取期间发起的下载会取消后续预取并自动排队执行，本地索引继续增量更新并作为断网兜底。

### 安全与稳定性

- 修复先设置 `TZ=CST-8` 后又调用 `configTime(0,0,...)`，被 Arduino-ESP32 内部重新覆盖为 UTC 的问题；桌面与 HTTPS 校时统一改用 `configTzTime("CST-8", ...)`，串口同时输出 UTC、东八区时间和当前时区。
- 修复开机绘制壁纸后同步等待 WiFi（最长 20 秒）和 NTP（最长 10 秒）期间主循环尚未启动、所有按键和触摸无响应的问题；开机联网和校时现改为后台进行，连接成功后自动恢复 ESP-NOW 信道和云存档同步。
- 扩充中文联网诊断串口日志：记录开机/前台 WiFi 阶段与耗时、状态码、SSID、信道、RSSI、IP/网关/DNS、SNTP 等待进度与系统时间，以及商店后台任务的核心、堆内存和明确失败阶段；日志不输出 WiFi 密码。
- 开机联网的兜底阶段使用非阻塞异步扫描当前环境，将可见 AP 与最多 8 条已保存凭据匹配并选择 RSSI 最强项；扫描、候选和最终选择均输出中文日志，未匹配时才回退到第一条保存记录。
- 修复开机异步扫描进入配网页面时仅删除结果却未停止底层扫描，导致网页把 `WIFI_SCAN_RUNNING(-1)` 错当作“没有扫描到 WiFi”的问题；配网页面现在复用进行中的扫描并区分扫描中、扫描失败和零结果。
- 修复 120ms 每信道配置同时把 Arduino 整次扫描超时压缩到 2400ms、与现场约 2300～2500ms 的正常扫描耗时发生竞态的问题；配网页面扫描使用 200ms/信道，开机兜底扫描使用 500ms/信道，首次 `-2` 会等待底层完成事件并以 600ms/信道自动重试，不再立即误连第一条保存记录。
- OSS 默认使用私有桶和服务端生成对象键；资源交付默认由主站同域流式代理，避免现有固件因 OSS 证书链不同而下载失败，确认自定义域名证书后可切换短时签名直链。
- 云存档服务端在提交前校验 v2 ROM 身份、Mapper、PRG/CHR 大小、载荷长度、CRC32、缩略图尺寸和 SRAM 大小；过期 base revision 返回冲突且不替换当前版本。
- 云存档历史裁剪使用 SQLite 持久化对象回收队列；旧 OSS 对象删除失败不再让已提交的新存档误报失败，后台维护会自动重试清理。
- OSS 自动维护周期新增云存档 SQLite 在线一致性备份，避免服务器本地元数据丢失后无法从 OSS 二进制对象还原当前槽位关系。
- 云存档历史裁剪改用 SQLite 持久化对象回收队列；旧 OSS 对象删除失败不再让已提交的新存档误报失败，后台维护会自动重试清理。
- 带电池标志的游戏新增独立 `.srm` 自动存档：启动恢复、30 秒脏写、暂停/退出/关机/OTA 前原子保存。
- 快速存档升级为 v2，在恢复任何模拟器状态前校验 ROM CRC32、Mapper、PRG/CHR 大小、载荷长度和文件 CRC；旧 v1 存档保持只读兼容。
- OTA 新固件改为完成关键初始化并稳定运行 10 秒后才取消回滚，关键任务或资源初始化失败时保留回滚能力。
- 修正 iNES 1.0 元数据和 dirty header 判断，明确识别并拒绝尚未支持的 NES 2.0 与 four-screen ROM。
- 存档新增 CRC32 校验，截断、大小异常或内容损坏的存档会在恢复状态前被拒绝。
- 存档改为临时文件加备份替换，写入失败时保留上一份有效存档。
- 不同目录下的同名 ROM 使用不同存档路径，避免互相覆盖。
- 商店 HTTPS 启用证书校验；ROM 下载校验文件大小、SHA-256 和 iNES 文件头。
- 商店列表和下载文件使用临时文件替换，降低断电或网络中断造成缓存损坏的风险。
- WiFi 凭据仅在实际连接成功后保存，配网状态接口使用正确的 JSON 转义。
- 商店后台任务使用原子状态同步，并修复菜单退出与无线连接恢复之间的竞态窗口。

### 架构

- 重排开机启动流水线：屏幕、帧缓冲和按键完成后立即播放动画；Core 0 后台依次挂载 SD/LittleFS、校验续玩、扫描 ROM、初始化音频与触摸，Core 1 同时播放动画并推进 WiFi 快速连接或兜底扫描，动画结束后通过明确完成门再进入主页和 OTA 健康观察。
- 优化开机联网顺序：优先用上次实际连接成功的 SSID 快速连接 2.5 秒，成功后跳过环境扫描；仅在明确失败或超时时才扫描并选择现场信号最强的已保存网络。首次升级尚无成功记录时兼容使用第一条保存凭据，并在连接成功后记录选择结果。
- 将帧缓冲、DMA 行缓冲、显示队列、Core 0 显示任务及暂停/恢复逻辑迁入独立 `DisplayPipeline`，降低主程序全局状态耦合。
- 将 ROM 装载、当前存档读写、运行/暂停状态、启动超时、电池存档节流和自适应帧调度迁入独立 `GameSession`。
- 新增 `WirelessManager` 统一协调 WiFi、游戏商店连接、ESP-NOW 手柄恢复和关机无线清理，状态使用原子变量跨后台任务同步。
- 新增 `ScreenController` 顶层页面状态控制；壁纸时钟画布与渲染迁入独立 `HomeScreen`，Library/Settings/Store 的画布、选择、触摸滚动和分页状态收敛到各自 Screen，并将页面实现从主入口移出。`main.cpp` 现为约 550 行，只保留装配、声明、`setup/loop` 和顶层状态机。

### 验证状态

- OSS 资源与云存档后端自动测试覆盖幂等迁移、双读回退、同域流式传输、设备命名空间、存档破坏、冲突和历史裁剪；掌机端新增云同步源码待用户执行 PlatformIO 编译与真机联网验证。
- 宿主机 native 单元测试共 47 项通过（含 Mapper 7/66、ESP-NOW v3 信道计划、存档路径和触摸输入），OTA 后端接口测试共 5 项通过；10 个可再分发测试 ROM 已建立尺寸、Mapper、SHA-256 与兼容性记录。
- `lcdwiki-es3c28p` 与 `esp32s3-n16r8` ESP32 固件编译通过；新增存档和 OTA 健康确认仍待真机回归。

---

## [v0.3.0] - 2026-04-26

### 修复

- 修复《超级马里奥兄弟》大马里奥碰到敌人后角色消失的问题。
- 修复多精灵场景下部分角色/敌人被错误丢弃的问题。
- 改善横向卷轴游戏左右边缘花屏/接缝问题。
- 修复 CNROM 小容量 CHR ROM bank 镜像问题，改善《影之传说》菜单花屏现象。
- 修复稳定 60 FPS 场景下 Display 任务可能占满 CPU0 并触发 task watchdog 重启的问题。
- 不支持的 Mapper、损坏/不完整 ROM 现在会显示错误提示并返回主菜单。

### Fixed

- Fixed Big Mario disappearing after touching enemies in Super Mario Bros.
- Fixed incorrect sprite dropping in object-heavy scenes.
- Improved visible edge artifacts/seams in horizontal scrolling games.
- Fixed CNROM CHR bank mirroring for smaller CHR ROMs, improving The Legend of Kage menu graphics.
- Fixed possible task watchdog resets when the Display task saturated CPU0 during stable 60 FPS scenes.
- Unsupported mappers and invalid/incomplete ROMs now show an error message and return to the main menu.

### 变更

- 将固定隔帧跳帧改为奇数周期自适应跳帧，避免与游戏内受伤/无敌闪烁锁相。
- 增加左右 4px overscan 裁边，实际显示区域为 248x240。
- 按 NES OAM 正序选择每条扫描线前 8 个精灵，并反向绘制以保持低索引精灵优先级。
- 增加游戏启动保护：启动后数秒内没有成功渲染帧时提示失败并返回主菜单。

### Changed

- Replaced fixed frame skipping with odd-cycle adaptive frame skipping to avoid locking onto in-game blinking effects.
- Added 4px horizontal overscan crop on each side; visible area is 248x240.
- Selects the first 8 sprites per scanline in NES OAM order, then renders in reverse to preserve low-index priority.
- Added startup guard: if no frame is rendered after a few seconds, show a failure message and return to menu.

### 性能

- 新增背景 tile 行 2bpp 解码查表，减少部分 PPU 背景渲染开销。
- 当前大部分游戏约 57-61 FPS；重精灵场景约 55-58 FPS。
- 相比最激进的固定隔帧跳帧方案，部分场景可能低约 1 FPS，但精灵显示兼容性更稳定。
- Display 任务在每帧 DMA 后主动让出时间片，降低看门狗风险；部分场景 DMA 统计值可能略高。

### Performance

- Added background tile row 2bpp decode lookup table to reduce part of PPU background rendering cost.
- Most games now run around 57-61 FPS; object-heavy scenes are around 55-58 FPS.
- Some scenes may be about 1 FPS slower than the most aggressive fixed frame-skip mode, but sprite compatibility is more stable.
- Display task now yields after each frame DMA to reduce watchdog risk; DMA timing may be slightly higher in some scenes.

---

## [v0.2.1] - 2026-03-28

### 打包与文档

- 新增仓库内预编译合并固件目录 `firmware/`，提供一键烧录文件 `DIJI-NES_v0.2.1.bin`。
- README 新增“方式二：乐鑫 Flash Download Tool”烧录说明（ESP32S3 + 地址 `0x0`）。
- README 补充项目实物图、电路图与烧录示意图，降低首次上手门槛。

### Packaging & Docs

- Added in-repo prebuilt merged firmware folder `firmware/` with one-click flash image `DIJI-NES_v0.2.1.bin`.
- Added README instructions for “Option 2: Espressif Flash Download Tool” (ESP32S3 + address `0x0`).
- Added project photo, circuit image, and flash-tool screenshot in README for easier onboarding.

### 说明

- 本版本以发布流程和使用体验优化为主，不涉及核心模拟器功能逻辑变更。

### Notes

- This version focuses on release workflow and usability improvements, with no core emulator logic changes.

---

## [v0.2.0] - 2026-03-24

### 性能优化 (48 FPS -> 60 FPS)

- **MMC3 4-bank PRG 缓存**：预计算 `prgBank0~3Offset`，消除 `cpuReadMapper4` 中的运行时分支和乘法。
- **CHR bank 指针缓存**：`chrBankPtrs[8]` 直接指向 8 个 1KB CHR 页，PPU 访问零开销。
- **Nametable 指针缓存**：`ntPtrs[4]` 直接指向 4 个 nametable，消除镜像计算。
- **CPU 时钟重构**：赤字追踪模式 (`cycles -= target; while(cycles<0) cycles += step()`)，每次 `clock(113)` 消除约 75 次空转循环。
- **OAM 预评估**：帧开始时构建 `spriteIndicesPerLine[240][8]`，每条扫描线不再遍历 64 个精灵。
- **无分支背景像素写入**：全透明 tile 快速路径 + 32 位写入。
- **指针内联**：`renderBackgroundLine`/`renderSpriteLine`/`checkSprite0HitFast` 将 `ntPtrs`/`chrBankPtrs` 缓存为局部变量，每条扫描线消除约 132 次函数调用。
- **IRAM_ATTR**：关键热路径函数放入 IRAM 加速执行。

### Performance Optimizations (48 FPS -> 60 FPS)

- **MMC3 4-bank PRG cache**: Pre-computed `prgBank0~3Offset`, eliminated runtime branching in `cpuReadMapper4`.
- **CHR bank pointer cache**: `chrBankPtrs[8]` direct pointers to 8 x 1KB CHR pages, zero-overhead PPU access.
- **Nametable pointer cache**: `ntPtrs[4]` direct pointers to 4 nametables, eliminated mirror calculation.
- **CPU clock refactor**: Deficit-tracking mode, eliminated about 75 idle loops per `clock(113)` call.
- **OAM pre-evaluation**: Built `spriteIndicesPerLine[240][8]` at frame start, no more scanning 64 sprites per scanline.
- **Branchless background pixel writes**: Transparent tile fast path + 32-bit writes.
- **Pointer inlining**: Cached `ntPtrs`/`chrBankPtrs` as local variables in `renderBackgroundLine`/`renderSpriteLine`/`checkSprite0HitFast`, eliminating about 132 function calls per scanline.
- **IRAM_ATTR**: Critical hot-path functions placed in IRAM for faster execution.

### 缺陷修复

- **[严重] PRG RAM ($6000-$7FFF) 总线路由缺失**：CPU 读写只转发 `addr >= 0x8000` 给卡带，$6000-$7FFF (SRAM) 读返回 0、写被丢弃。这是超级玛丽 3 黑屏的根本原因。
- **[严重] MMC3 IRQ 电平触发修复**：`acknowledgeIrq()` 在 `cpu.irq()` 检查 I-flag 之前清除 pending，当 CPU I-flag 置位时 IRQ 永久丢失。现在 pending 只由游戏写 $E000 清除。
- **[严重] IRQ 跟随实际渲染状态**：`ppu.renderEnabled` 硬编码为 `true`，即使游戏关闭渲染 ($2001) MMC3 IRQ 计数器仍在递减。导致 KOF97 菜单切换时 VRAM 更新被破坏。修复为检查实际 `ppuMask & 0x18`。
- **CPU 中断周期计数**：`irq()` 和 `nmi()` 使用 `cycles = 7` 绝对赋值，丢弃上一条指令剩余周期，改为 `cycles += 7`。
- **VBlank 周期数修正**：从 2501 修正为 2274（20 条扫描线 x 约 113.67 周期）。
- **脏 iNES 头检测**：盗版 ROM 的 header bytes 8-15 常有垃圾数据，导致 mapper 高位错误。现已自动检测并仅使用 `flags6` 低半字节。
- **ntPtrs 空指针崩溃**：`updateNtPtrs()` 在 `load()` 中 `setVramPointer()` 之前调用导致空指针。已加保护并在 `setVramPointer()` 中自动调用。

### Bug Fixes

- **[Critical] Missing PRG RAM ($6000-$7FFF) bus routing**: CPU read/write was only forwarded to cartridge for `addr >= 0x8000`. $6000-$7FFF returned 0 on read and writes were dropped. This was the root cause of the SMB3 black screen.
- **[Critical] MMC3 IRQ level-trigger fix**: `acknowledgeIrq()` cleared pending before `cpu.irq()` checked the I-flag, causing IRQs to be permanently lost when CPU I-flag was set. Pending is now only cleared by the game writing $E000.
- **[Critical] IRQ follows actual rendering state**: `ppu.renderEnabled` was hardcoded to `true`, causing MMC3 IRQ counting even when rendering was disabled via $2001. This corrupted VRAM updates in the KOF97 menu. Fixed by checking actual `ppuMask & 0x18`.
- **CPU interrupt cycle accounting**: `irq()`/`nmi()` used absolute `cycles = 7`, changed to additive `cycles += 7`.
- **VBlank cycle correction**: Corrected from 2501 to 2274 (20 scanlines x about 113.67 cycles).
- **Dirty iNES header detection**: Bootleg ROMs with garbage in header bytes 8-15 now auto-detected, using only the low mapper nibble from `flags6`.
- **ntPtrs null-pointer crash**: `updateNtPtrs()` could be called before `setVramPointer()` during `load()`. Fixed with null protection and automatic update in `setVramPointer()`.

### 新功能

- **无 SD 卡启动界面**：不插 SD 卡不再黑屏，显示菜单提示 "No SD card detected" 和 "Press A to retry"。

### New Features

- **No-SD-card startup screen**: Without an SD card inserted, the screen no longer goes black. A menu appears showing "No SD card detected" and "Press A to retry".

### 兼容性

- **超级玛丽 3 (Super Mario Bros. 3)**：现已完全可玩（之前黑屏）。
- **MMC3 游戏**：分屏滚动和扫描线 IRQ 时序正常工作。
- **脏头 ROM**：自动检测并正确处理。

### Compatibility

- **Super Mario Bros. 3**: Now fully playable (was black screen).
- **MMC3 games**: Split-screen scrolling and scanline IRQ timing working correctly.
- **Dirty-header ROMs**: Auto-detected and handled gracefully.

---

## [v0.1.0] - 2026-02-23

### 首次发布

- 6502 CPU 全指令集模拟（约 150 个操作码）。
- PPU：背景渲染、滚动、64 个精灵（8x8 和 8x16 模式）。
- APU：方波、三角波、噪声、DMC 通道，通过 I2S DAC 输出。
- 双核架构：Core 0（音频 + 显示），Core 1（模拟）。
- Mapper 支持：NROM (0), MMC1 (1), UxROM (2), CNROM (3), MMC3 (4，部分)。
- SD 卡 ROM 浏览菜单。
- 暂停菜单：存档/读档。
- 大部分游戏约 50 FPS。
- SPI ST7789 320x240 显示 (LovyanGFX)。

### Initial Release

- 6502 CPU full instruction set emulation (~150 opcodes).
- PPU: background rendering, scrolling, 64 sprites (8x8 and 8x16 modes).
- APU: square, triangle, noise, and DMC channels via I2S DAC.
- Dual-core architecture: Core 0 (audio + display), Core 1 (emulation).
- Mapper support: NROM (0), MMC1 (1), UxROM (2), CNROM (3), MMC3 (4, partial).
- SD card ROM browser with menu system.
- Pause menu with save/load state.
- About 50 FPS for most games.
- SPI ST7789 320x240 display via LovyanGFX.
