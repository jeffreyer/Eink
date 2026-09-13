# 项目交接文档 - MiniEink墨水屏设备

## 项目概述

这是一个基于ESP32-C3的墨水屏设备项目，包含设备端固件和微信小程序前端。设备通过BLE与小程序通信，支持模块化配置和相册管理功能。

**设备型号**: MiniEink (Adafruit QT Py ESP32-C3)  
**显示屏**: 三个版本（`include/common.h` 中三选一）
- 4色屏（都不定义宏）：200x200 2bpp（黑、白、黄、红）
- 6色屏（`INK6` 宏）：1.54寸 240x240 JD7601 4bpp（黑、白、黄、红、蓝、绿）
- 黑白屏（`INK_BW` 宏）：1.54寸 200x200 SSD1681 1bpp（黑、白）
  - **当前为 6 色屏模式（`#define INK6` 已开启）**；换黑白屏注释 `INK6`、打开 `INK_BW`
  - 三个驱动各自整体由宏保护（文件顶部先 `#include "common.h"` 才能看到宏），
    同一时刻只有一个驱动参与编译，避免重复定义 `BlackImage`/`gui_drawtext`
**刷新时间**: 黑白约2秒；4色约12秒；6色约30-40秒（驱动内 BUSY 超时上限 40 秒）。
三者都走异步刷屏：发出刷新命令后 MCU 立刻深度休眠，刷新结束由定时唤醒补断电  
**通信方式**: BLE (NimBLE)  
**开发环境**: PlatformIO + Arduino Framework  
**前端**: 微信小程序

---

## 6色屏适配（当前主线）

新版硬件：6色 1.54寸 240x240 墨水屏（JD7601 驱动，4bpp 打包，每字节 2 像素），
并新增 KEY_UP / KEY_DOWN 两个实体按键。固件通过 `include/common.h` 中的 `#define INK6`
区分 6 色 / 4 色编译模式。

### 引脚（6色屏）

| 信号 | GPIO |
|------|------|
| KEY_UP | 1 |
| KEY_DOWN | 2 |
| BLE_LIGHT | 20 |
| EPD BUSY | 10 |
| EPD RST | 3 |
| EPD DC | 4 |
| EPD CS | 5 |
| EPD SCK | 6 |
| EPD MOSI | 7 |

### 颜色编码（设备端，与 JD7601 面板码一致）

| 颜色 | 半字节(nibble) | 字节(2像素) |
|------|------|------|
| 黑 | 0x0 | 0x00 |
| 白 | 0x1 | 0x11 |
| 黄 | 0x2 | 0x22 |
| 红 | 0x3 | 0x33 |
| 蓝 | 0x5 | 0x55 |
| 绿 | 0x6 | 0x66 |

**前端色板校准**（`image-editor.js getPalette()` / `imageProcessor.js rgbTo6ColorCode()`，
用于预览显示与误差扩散的目标色，非设备端帧码）：

| 颜色 | 名义值 | 校准值（实拍6色条估计） |
|------|------|------|
| 白 | 255,255,255 | 240,238,228（暖白） |
| 黑 | 0,0,0 | 25,25,30 |
| 红 | 255,0,0 | 210,65,48（偏暗砖红） |
| 黄 | 255,255,0 | 235,230,55 |
| 蓝 | 0,0,255 | 23,100,182（偏暗藏蓝） |
| 绿 | 0,128,0 | 50,118,88（偏橄榄） |

### 绘制与刷新链路

- 画布：`BlackImage[28800]`（240x240，4bpp），由 `eink6.cpp` 定义
- 绘制：复用 `GUI_Paint` 的 `Paint_SetScale(7)`（4bpp，2像素/字节，高位在前），
  与 JD7601 帧格式逐字节一致，`lua_hardware_api.cpp` 在 `INK6` 下自动切换
- 刷新：`display.show()` → `epdDisplayImage()`（写帧 0x10 → 刷新 0x12 后立即返回，
  断电与面板休眠由 `epd_async` 调度）
- 4色屏代码在 `INK6` 下通过 `#ifndef INK6` 整体跳过（`eink.cpp`）

### Lua 颜色值

`0=黑, 1=白, 2=黄, 3=红, 4=蓝, 5=绿`（4色屏仅支持 0-3）。
`WIDTH` / `HEIGHT` 全局量由固件注册，`INK6` 下自动为 240。

### 按键功能（`main.cpp check_btn`）

| 按键 | 短按 | 长按(3秒) |
|------|------|------|
| KEY_UP | 模块"上一项"钩子（`subpage_prev`，相册=上一张、名言=上一条）；无钩子则 subpage-1 | 开关 BLE 配置 |
| KEY_DOWN | 模块"下一项"钩子（`subpage_next`，相册=下一张、名言=下一条）；无钩子则 subpage+1 | 切换下一个启用模块（`module_registry_next_enabled`，按住3秒即触发，并保存当前模块） |

模块可在 `module_descriptor_t` 注册 `subpage_next/subpage_prev` 自定义按键行为
（`gallery` 注册为上一张/下一张；**Lua 模块在脚本中定义同名函数即可注册**，
如名言 `subpage_next/subpage_prev`；其他模块默认 subpage ±1，互不影响）。
统一休眠模型下，按键唤醒**不执行模块 setup**：系统记录唤醒按键，由 `check_btn`
判定短按/长按后执行对应动作并休眠（短按 → 上一项/下一项钩子，长按 → 切模块/开 BLE）。
Lua 按键钩子被调用时若模块未加载（按键唤醒跳过了 setup），系统会**只加载脚本、
不执行 setup 绘制**，再由钩子自行绘制。

### BLE 状态字段

`get_status` 回传 `"ink"` 为墨水屏色数（6 / 4 / 2），小程序据此选择图片格式
（6色：240x240 4bpp / 28,800 字节；4色：200x200 2bpp / 10,000 字节；
黑白：200x200 1bpp / 5,000 字节）。

**电量字段**：状态 JSON 只回传 `"battery_mv"`（分压后电压，分压系数2），
百分比由前端自行换算（3300mV~4200mV → 0-100%，`bluetooth.js applyBatteryPercent()`）。
首次状态请求时触发一次 ADC 采样。

---

## 墨水屏驱动（三选一）与统一接口

三种屏型通过 `include/common.h` 里的宏切换，调用点统一走 `include/eink_display.h`
暴露的接口，不再各自写条件编译：

| 屏型 | 宏 | 帧缓冲 | 驱动实现 |
|------|-----|--------|----------|
| 4 色 200x200 2bpp | 无 | `BlackImage[10000]` | `eink.cpp` + `Display_EPD_W21*.cpp` |
| 6 色 240x240 4bpp | `INK6` | `BlackImage[28800]` | `eink6.cpp` |
| 黑白 200x200 1bpp | `INK_BW` | `BlackImage[5000]` | `eink_bw.cpp`、`eink_bw.h` |

统一接口（各驱动的 `.cpp` 内实现，见 `include/eink_display.h`）：

- `eink_display_init()`：初始化 SPI 与面板，`setup()` 调用一次
- `eink_display_frame()`：把 `BlackImage` 整帧刷到屏幕（**异步**：发完刷新命令即返回）
- `eink_display_white()`：全屏刷白
- `extern unsigned char BlackImage[ALLSCREEN_BYTES]`：当前屏型的画布缓冲

### 异步刷屏调度层（`include/epd_async.h` + `src/epd_async.cpp`）

三种屏共用同一套“发完刷新命令就休眠”的省电机制，驱动只实现两个钩子：

| 钩子 | 作用 |
|------|------|
| `epdPanelIsIdle()` | 刷新是否已结束（读 BUSY 空闲电平） |
| `epdPanelPowerOff()` | 关闭面板电源 + 面板深度休眠（假定刷新已结束） |

调度层提供：

| 接口 | 时机 | 行为 |
|------|------|------|
| `epdAsyncMarkStarted()` | 驱动发完刷新命令后 | 置 RTC 标志，函数立即返回 |
| `epdAsyncIsPending()` | 各处判断 | 是否有刷新进行中（跨深度休眠保持） |
| `epdAsyncWaitPrevious()` | 新一帧开始前 | 上一帧未刷完先等它结束，避免 reset 打断刷新 |
| `epdAsyncPowerOffNow()` | 定时唤醒补断电 / 进 BLE 前 | 断电 → 面板休眠（**不等 BUSY**，几十毫秒返回） |
| `epdAsyncMaintain()` | 主循环 | 未休眠时兜底：刷新结束且过了延迟才断电（**非阻塞**，不拖慢 BLE） |
| `epdAsyncPrepareSleep()` | 进深度休眠前 | 刷新中则 `gpio_hold` 住面板控制脚，见下 |
| `epdAsyncReleasePins()` | 开机初始化墨水屏前 | 释放上次休眠保持的引脚 |

完整流程（三个屏型一致）：

1. 模块 `setup` 绘制 → `eink_display_frame()` 发出刷新命令 → 置标志后立即返回；
2. `enter_deep_sleep()` 发现标志：只启用 `EPD_ASYNC_REFRESH_SLEEP_S` 秒定时唤醒
   （禁用 GPIO 唤醒，避免按键中途唤醒打断刷新），MCU 立刻休眠；
3. 定时唤醒后 `setup()` 走快路径：`eink_display_init()` 只初始化总线
   （**pending 时不 reset 面板**）→ `epdAsyncPowerOffNow()` 直接断电
   （唤醒时刻按刷新时长选取，**不再依赖 BUSY 判空闲**）→ 清标志 → 继续休眠；
4. 若没休眠（BLE 交互 / 上电待机）：主循环 `epdAsyncMaintain()` 在刷新结束、
   且超过 `EPD_PANEL_POWEROFF_DELAY_MS` 后补一次断电；BUSY 读不到空闲电平时，
   到达 `EPD_ASYNC_FORCE_POWEROFF_MS` 也会兜底断电（非阻塞，不会卡住 loop）。

进入 BLE 前（`ble_config_init`）：面板没在刷新就立即断电 + 面板休眠（按键唤醒进
BLE 时面板刚 init 过，不断电会持续耗电）。这里**也不能等 BUSY**：面板处于睡眠/非
初始化态时 BUSY 一直是忙电平（实测进 BLE 时 BUSY=0，白等 3 秒），所以直接走
`epdAsyncPowerOffNow()`；正在刷新则交给主循环补断电（4 色要 13.5 秒、6 色要 30 秒，
阻塞会让 BLE 迟迟不广播）。

同理，6 色驱动断电阶段的 BUSY 等待上限从 10 秒收到 `POWEROFF_WAIT_MS`（2 秒）兜底。

注意：异步刷新窗口内**只启用定时唤醒、禁用 GPIO 唤醒**，这段时间（黑白 6 秒 /
4 色 22 秒 / 6 色 30 秒）按键不会被响应——这是为了避免按键打断刷新，代价与
6 色屏原有行为一致。

各屏参数（定义在各自驱动头文件，调度层取默认值兜底）：

| 屏型 | 全刷耗时 | `EPD_ASYNC_REFRESH_SLEEP_S` | `EPD_PANEL_POWEROFF_DELAY_MS` | `EPD_ASYNC_WAIT_TIMEOUT_MS` |
|------|---------|------------------------------|-------------------------------|-----------------------------|
| 6 色 240x240 | 20~30s | 30 | 3000 | 5000 |
| 4 色 200x200 | ~13.5s（实测） | 22 | 13000 | 3000 |
| 黑白 200x200 | ~2s | 6 | 3000 | 3000 |

（`EPD_ASYNC_FORCE_POWEROFF_MS`：未休眠时的兜底断电时间，6 色 40s / 4 色 20s / 黑白 6s）

**BUSY 只是优化，时间才是保证**：定时唤醒路径完全不等 BUSY；主循环兜底路径优先看
BUSY，读不到空闲电平则按时间兜底。所以即便某块屏 BUSY 引脚不可读，也不会出现长时间
卡住或面板一直通电。

**四色板实测（`probe` 命令 + 唤醒探针）**：醒着刷一次全白 **13.5 秒**完成
（BUSY 忙=0 / 空闲=1，极性正确、引脚正常）；但 MCU 深度休眠期间面板控制 IC 会
进入非初始化状态，唤醒后 BUSY 一直为 0，**而屏幕内容显示正确**（墨水屏双稳态，
图像不依赖 IC 供电/状态）。因此四色屏的补断电流程改为：

1. 唤醒后不等 BUSY，也不做整屏初始化（`lcd_chkstatus()` 是死等，绝不能进）；
2. `EPD_reset_only()` 只做一次复位（不等 BUSY），把控制 IC 拉回已知状态；
3. `EPD_poweroff_sleep()` 发 0x02 断电 + 0x07 面板深睡。

复位不会影响已显示画面（双稳态），但能保证断电/深睡命令不被忽略。四色刷新窗口据此
放宽到 22 秒（实测 13.5 秒 + 余量）。

`EPD_ASYNC_WAIT_TIMEOUT_MS` 是“等 BUSY 变空闲”的兜底上限：唤醒时刻本就选在刷新完成
之后，这里只防意外，超时后**照常断电**（面板会在下一帧初始化时重新复位，不会损坏）。

### 深度休眠期间保持面板控制脚（`gpio_hold`）

MCU 深度休眠时 GPIO 会浮空：EPD 的 RST/CS/SCK/MOSI 浮空可能被干扰，甚至把面板拉进
复位态，表现就是**唤醒后 BUSY 一直报忙**、补断电流程只能走到超时（曾出现 60 秒延迟）。
因此在异步刷屏进入休眠前（`epdAsyncPrepareSleep()`）把 RST/DC/CS/SCK/MOSI 拉到空闲电平
并 `gpio_hold_en()` + `gpio_deep_sleep_hold_en()` 固定住，开机时 `epdAsyncReleasePins()`
（`main.cpp` 里，早于 SPI 初始化）释放。

补断电 / 进 BLE 前都不再等 BUSY，因此日常日志不会再出现 `wait idle`。唯一还会
等 BUSY 的地方是“新一帧开始前，上一帧仍在刷新”的同步点（`epdAsyncWaitPrevious()`），
它同样带短超时并打印原始电平：

```
[EPD] sync: waiting previous refresh to finish
[EPD] wait idle: BUSY raw=0                 # 进入等待时 BUSY 读数
[EPD] wait idle timeout (3000 ms), BUSY raw=0 -> power off anyway
```

### BUSY 时间线探针（临时诊断）

四色板实测“唤醒后 BUSY 恒为 0”（尽管画面已刷新完成），需要用探针确认 BUSY 的
真实空闲极性与刷新时长。两处探针由 `EPD_PROBE_BUSY_ON_BOOT` 控制，**默认已置 0**
（日常运行不打印、不白屏刷屏）：

- 置 1 时：上电冷启动自动跑一次时间线，并且每次唤醒打印唤醒后 BUSY 原始电平
  （在任何 GPIO/SPI 初始化前、初始化后各一次）；
- **串口命令 `probe`**：不受开关影响，随时手动重跑时间线（换新屏幕板卡时用来确认
  刷新时长与 BUSY 极性）。

输出形如：

```
[EPD] probe: cold boot, measuring BUSY timeline
[EPD] probe: t=0 ms BUSY=0 idle=0
[EPD] probe: t=1000 ms BUSY=0 (stable)
...
[EPD] probe: t=12000 ms BUSY=1 idle=1     ← 翻转点 = 真实刷新时长 + 空闲电平
[EPD] probe: BUSY timeline end
```

据此可判断：翻转后为 1 → 现有 `epdPanelIsIdle()`（`BUSY==1`）正确，问题只在
“唤醒时刷新确实还没结束”；翻转后为 0 → 该屏空闲电平为低，把 `epdPanelIsIdle()`
反过来；始终不变 → 该脚没有反映刷新状态（走线/引脚不对），保持纯时间兜底。

### 1.54 寸黑白屏（`INK_BW`）

- 控制器 SSD1681 兼容，1bpp（1 字节 8 像素，**bit=1 为白**），帧 5000 字节
- 驱动移植自 `ESP32-C6-ePaper-1.54` 例程 `port_display.cpp`，去掉了 LVGL 与
  `esp_lcd_panel_io`，改用项目统一的 Arduino SPI + GPIO；全刷波形表（159 字节）
  原样保留
- 引脚与 6 色屏硬件一致（MOSI=7 / CLK=6 / BUSY=10 / DC=4 / CS=5 / RST=3），
  定义集中在 `include/eink_bw.h`，换硬件只改这一处
- 刷新流程：`0x12 SWRESET` → 驱动输出/数据入口/窗口/Border/温度/LUT →
  `0x24` 写帧 → `0x22 0xC7 + 0x20` 触发刷新后**立即返回**（约 2 秒刷新期间 MCU
  进入深度休眠，5 秒后唤醒补断电）；`epdPanelPowerOff()` 里 `0x10 0x01` 让面板
  深度休眠
- `gui_drawtext()` 调试命令、"white" 串口命令均已实现

**颜色映射（Lua 颜色值 → 黑白屏）**：`1 白 → 白`、`2 黄 → 浅灰抖动`、
`0/3/4/5 → 黑`。抖动由 `GUI_Paint` 的 Scale=2 分支实现（4x4 Bayer 有序抖动，
中间灰常量 `GRAY_LIGHT/GRAY_MID/GRAY_DARK` 定义在 `include/GUI_Paint.h`），
这样原本用黄色的高亮块在黑白屏上不会直接消失。

**前端已配套（小程序）**：设备上报 `"ink": 2`，图片格式为 200x200 1bpp =
**5000 字节**，`bit=1` 白、`bit=0` 黑。前端统一从 `app.getInkSpec()` 取规格：

| 位置 | 改动 |
|------|------|
| `app.js` | 新增 `getInkSpec()`：按 `ink` 返回 `{width,height,bits,bytes,isMono}`（6→240/4bit/28800，2→200/1bit/5000，4→200/2bit/10000） |
| `pages/image-editor` | 黑白屏用 2 色色板（白 `0x01`/黑 `0x00`）+ 亮度阈值 128 + 误差扩散产生灰阶；按 1bpp 高位在前打包；预览标题为“黑白预览”，隐藏无意义的“冷暖”滑块 |
| `pages/gallery` | 缩略图解码支持 1bpp；缓存键改为按屏型隔离（`gallery_image_cache_v3_ink<色数>`），换屏后不再沿用旧格式缩略图 |

---

## 目录结构

### 设备端 (`d:\Projects\Eink`)

```
src/
├── main.cpp              # 主程序入口，loop和setup
├── ble_config.cpp        # BLE通信核心（NimBLE），包含分块传输
├── cmd_handler.cpp       # 串口命令处理器（含 white/dis 调试命令）
├── module_registry.cpp   # 模块注册表（含 Lua 动态模块加载）
├── sleep_manager.cpp     # 休眠管理（深度睡眠，KEY_UP/KEY_DOWN 唤醒）
├── gallery.cpp           # 相册功能（含 1bpp/2bpp/4bpp 旋转）
├── eink.cpp              # 4色屏驱动封装（INK6/INK_BW 下不编译）
├── eink6.cpp             # 6色屏 JD7601 驱动 + 6色画布 BlackImage
├── eink_bw.cpp           # 黑白屏 SSD1681 驱动 + 1bpp 画布 BlackImage
├── epd_async.cpp         # 异步刷屏调度（RTC pending 标志 / 补断电 / 兜底）
├── lua_hardware_api.cpp  # Lua display API（绘制/中文渲染/刷新）
├── Display_EPD_W21.cpp   # 4色屏底层驱动（PIC_write_ram / EPD_update_async / 断电休眠）
└── GUI/                  # GUI_Paint 绘制库（Scale 2=1bpp, 4=2bpp, 7=4bpp）

include/
├── ble_config.h          # BLE接口定义
├── cmd_handler.h
├── module_registry.h
├── sleep_manager.h
├── gallery.h
├── eink.h                # 4色屏参数（200x200）
├── eink6.h               # 6色屏参数（240x240）+ 颜色常量
├── eink_bw.h             # 黑白屏参数（200x200, 1bpp）+ 引脚
├── eink_display.h        # 三种屏型统一显示接口
├── epd_async.h           # 异步刷屏调度接口 + 各屏参数
├── image.h               # 6色测试图（调试用）
└── GUI_Paint.h

data/
├── gallery.cfg           # 相册配置定义（display_mode, cycle_interval；rotation 已移至模块页全局配置）
├── quotes.cfg            # 名言警句配置定义（interval 切换间隔，分钟）
├── quotes.lua            # 名言警句模块（内置 431 条国内外名言）
├── countdown.lua         # 纪念日倒计时模块
└── fonts/                # GB2312 全量点阵字库（16/24px + 映射表）

platformio.ini            # PlatformIO配置
```

### 前端 (`d:\Projects\jeff\WeChatProjects\pix`)

```
pages/
├── modules/              # 模块列表页（首页TabBar）
├── module-config/        # 模块配置详情页
├── gallery/              # 相册管理页（仅MiniEink设备，TabBar）
├── image-editor/         # 图片编辑页（按设备 ink 字段选黑白/4色/6色格式）
├── device-connect/       # 设备扫描连接页
├── market/               # 模块市场页（TabBar）
└── profile/              # 个人中心页（TabBar）

utils/
├── bluetooth.js          # ⭐ BLE通信核心，统一管理所有BLE交互
└── imageProcessor.js     # 图片处理工具（4色/6色量化与打包）

app.js                    # 全局应用配置
app.json                  # 小程序配置（TabBar等）
```

---

## 技术栈

### 设备端

| 组件 | 版本/说明 |
|------|----------|
| 平台 | PlatformIO |
| 框架 | Arduino (ESP32 5.5.4) |
| BLE库 | NimBLE-Arduino 2.5.0 |
| 显示 | GUI_Paint 绘制库；4色屏 Display_EPD_W21 / 6色屏 eink6 (JD7601) |
| Lua | lua-5.4.7（动态模块脚本） |
| 编译命令 | `d:\.platformio\penv\Scripts\platformio.exe run`（⚠️ 仅编译，不上传） |

### 前端

| 组件 | 说明 |
|------|------|
| 框架 | 微信小程序 |
| 基础库 | 2.9.0+ |
| 权限 | scope.bluetooth, scope.writePhotosAlbum |
| 云开发 | cloud1-d3gxxnyov88776090 |

---

## BLE通信协议

### Service & Characteristics

```
Service UUID:  8C0B8A10-7E3D-4DF7-9A2A-1D8D46F8B100
Config Char:   8C0B8A11-7E3D-4DF7-9A2A-1D8D46F8B100 (WRITE)
Status Char:   8C0B8A12-7E3D-4DF7-9A2A-1D8D46F8B100 (READ | NOTIFY)
```

### 命令格式 (JSON)

#### 小程序 → 设备 (通过 Config Characteristic)

```javascript
// 模块相关
{"manifest": 0}                    // 获取模块0的配置清单
{"module": 0, "config": {...}}     // 设置模块0的配置

// 相册相关（仅MiniEink）
{"gallery_list": true}             // 获取图片列表
{"gallery_upload_start": true, "filename": "...", "size": 28800, "chunk_size": 336} // 开始上传（6色=28800，4色=10000）
{"gallery_upload_chunk": true, "index": 0, "data": "base64..."}  // 上传数据块（带索引，按固定偏移落位）
{"gallery_upload_progress": true}                                // 查询已连续接收的字节数（丢包补齐用）
{"gallery_upload_complete": true}                                // 上传完成（返回真实保存结果 ok/error）
{"gallery_get": true, "filename": "..."}                         // 获取图片
{"gallery_show": true, "filename": "..."}                        // 显示图片
{"gallery_delete": 2}                                            // 删除索引2

// 系统相关
{"get_status": 0}                  // 获取状态（支持分页）
```

`get_status` 响应包含 `"ink"` 字段（4 或 6），小程序端据此区分图片格式与编辑尺寸。

#### 设备 → 小程序 (通过 Status Characteristic)

```javascript
// Manifest响应
{"id": "clock", "name": "时钟", "configs": [...]}

// Gallery列表响应
{"images": [{"filename": "img_123.img", "size": 5000}, ...]}

// Gallery图片响应
{"image_data": "base64encoded..."}

// 上传进度响应（gallery_upload_progress）
{"ok": true, "received": 13104, "size": 28800}  // received=从0起连续收到的字节数

// 上传保存结果回执（gallery_upload_complete，前端 uploadComplete 等待此响应）
{"ok": true}                                   // 保存成功
{"ok": false, "error": "Save failed"}          // 保存失败（如磁盘满）

// 分块传输（大数据自动分块，每块约200字节）
{"chunk": 0, "total": 5, "data": "..."}
{"chunk": 1, "total": 5, "data": "..."}
// ...
{"chunk": 4, "total": 5, "data": "..."}
```

### 分块传输机制

**设备端发送** (`ble_config.cpp::set_status()`):
- 自动检测数据大小
- 超过200字节自动分块
- 每块包含 `chunk`、`total`、`data` 字段

**小程序接收** (`bluetooth.js`):
- 全局监听器统一接收
- 根据数据类型分发到对应callback：
  - `manifestCallback` - 模块配置
  - `galleryListCallback` - 图片列表
  - `galleryImageCallback` - 单张图片
  - `uploadResultCallback` - 图片上传进度/保存结果回执（含 `ok` 字段的状态）
- 自动合并分块，验证完整性

**图片上传（前端 → 设备，`gallery.js uploadImageToDevice`）**:
- 336 字节/块（base64 448 字符 + JSON ≈ 487，适配 512 MTU），每块带 `index`
- 设备端在 **BLE 回调中直通处理**图片上传命令（绕过单槽 `s_pending_cmd` 队列，
  避免高频分块覆盖丢块），按 `index*chunk_size` 固定偏移落位，重复/乱序块忽略
- 前端 15ms 块间隔 + 写失败退避重试；发完后轮询 `gallery_upload_progress`，
  只有进度连续无变化才补发缺失后缀（最多8轮）
- 全部收满后才发 `gallery_upload_complete`，并等待设备端真实保存结果
  （`uploadComplete()`，15 秒超时），失败则提示设备返回的错误
- 预期耗时：28800 字节约 2-3 秒（原 256 字节/100ms 限速版约 11 秒）

---

## 核心架构

### BLE通信架构 ⭐

**关键原则**：所有BLE数据接收统一由 `bluetooth.js` 管理，**页面不得直接注册监听器**。

#### bluetooth.js 核心组件

```javascript
class Bluetooth {
  // 全局监听器（仅此一个）
  setupGlobalListener() {
    wx.onBLECharacteristicValueChange((res) => {
      // 解析数据
      const status = this.parseStatus(res.value);
      
      // 根据数据类型分发
      if (status.chunk !== undefined) {
        // 分块数据，分发给等待的callback
        if (this.manifestCallback) this.manifestCallback(status);
        if (this.galleryListCallback) this.galleryListCallback(status);
        if (this.galleryImageCallback) this.galleryImageCallback(status);
      } else if (status.id && status.configs) {
        // manifest数据
        if (this.manifestCallback) this.manifestCallback(status);
      } else if (status.images) {
        // gallery list数据
        if (this.galleryListCallback) this.galleryListCallback(status);
      } else if (status.image_data) {
        // gallery image数据
        if (this.galleryImageCallback) this.galleryImageCallback(status);
      } else if (status.ok !== undefined && this.uploadResultCallback) {
        // 上传进度/保存结果回执
        this.uploadResultCallback(status);
      } else {
        // 普通状态更新
        app.globalData.deviceStatus = status;
      }
    });
  }
  
  // 统一接口方法
  async getModuleManifest(index) { /* 设置manifestCallback */ }
  async getGalleryList() { /* 设置galleryListCallback */ }
  async getImageFromDevice(filename, onProgress) { /* 设置galleryImageCallback */ }
}
```

#### 页面使用方式

```javascript
// ✅ 正确 - 使用bluetooth.js的统一接口
const result = await bluetooth.getGalleryList();

// ❌ 错误 - 不要在页面直接注册监听器
wx.onBLECharacteristicValueChange((res) => {
  // 这会导致监听器泄漏！
});
```

---

## 重要问题修复记录

### 问题1: 相册模块点击转圈超时

**现象**: 第二次点击相册模块配置时，一直转圈，超时失败。

**根因**: 
- `gallery.js` 在 `loadImageList()` 中直接注册了BLE监听器
- 每次调用都注册新监听器，但从未清除
- 多个监听器同时存在，拦截并消费了manifest数据
- 导致 `module-config.js` 的 `manifestCallback` 收不到数据

**解决方案**:
1. 移除 `gallery.js` 的临时监听器
2. 在 `bluetooth.js` 添加 `getGalleryList()` 方法
3. 使用 `galleryListCallback` 机制统一管理

**修改文件**:
- `utils/bluetooth.js` - 添加 `getGalleryList()` 和 `galleryListCallback`
- `pages/gallery/gallery.js` - 使用 `bluetooth.getGalleryList()`

---

### 问题2: 点击图片占位符后刷新列表失败

**现象**: 
- 点击图片占位符从设备获取图片
- 获取成功后，下拉刷新列表失败
- 模块配置页面也无法加载

**根因**:
- `gallery.js` 的 `fetchImageFromDevice()` 直接注册了BLE监听器
- 每次点击图片都注册新监听器，从未清除
- 监听器累加，干扰了所有后续的BLE通信

**解决方案**:
1. 在 `bluetooth.js` 添加 `getImageFromDevice()` 方法
2. 添加 `galleryImageCallback` 机制
3. 全局监听器识别 `image_data` 字段并分发
4. 重构 `fetchImageFromDevice()` 使用统一接口
5. 添加进度回调支持

**修改文件**:
- `utils/bluetooth.js` - 添加 `getImageFromDevice(filename, onProgress)`
- `pages/gallery/gallery.js` - 重构 `fetchImageFromDevice()`

---

### 问题3: 第二次扫描设备无法发现

**现象**: 
- 第一次扫描能发现17个设备，包括MiniEink
- 返回后第二次扫描只发现2-5个设备，MiniEink不出现
- 但点击历史设备仍能连接

**根因**:
- 微信小程序BLE扫描有多层缓存机制
- `stopBluetoothDevicesDiscovery()` 无法清除所有缓存
- 第二次扫描时，已缓存的设备不会重复回调

**解决方案**:
1. 每次扫描前调用 `wx.closeBluetoothAdapter()` 完全关闭
2. 等待100ms让系统释放资源
3. 重新初始化蓝牙适配器
4. 使用 `allowDuplicatesKey: true, interval: 0` 提高发现率
5. 优化超时定时器管理，防止堆积

**修改文件**:
- `utils/bluetooth.js` - `startScan()` 添加适配器重启逻辑（关闭再打开清除缓存）
- `pages/device-connect/device-connect.js` - 添加 `scanTimeout` 管理

---

### 问题4: 相册图片下载导致设备崩溃（Load access fault）

**现象**: 小程序点击相册图片占位符下载图片时，设备端重启，
日志停在 `Sending image data via set_status (auto-chunked)...`。

**根因**: `gallery_get` 先把 28,800 字节图片拼成 38,400 字符的 Base64 String，
再拼成完整响应 String（峰值约 3×38KB 临时内存），把 ESP32-C3 堆耗尽，
`set_status` 分块时分配 packet 失败 → 空指针 → Load access fault。

**解决方案**: 新增 `send_gallery_image_chunked()` 流式发送——边读文件边
Base64 编码，按 120 字符累积直接 notify 分块，全程不构建大 String
（峰值仅约几百字节）。块格式与 `set_status` 一致，前端无需改动。
BLE 通知做了节流与退避（`notify()` 失败延时重试、成功后 2ms 间隔），
避免连发 300+ 个通知把 NimBLE 队列塞满导致丢包（表现为进度卡在 30-40%
后超时）。小程序端 `getImageFromDevice` 超时同步放宽到 60 秒。

**修改文件**: `src/ble_config.cpp`、`utils/bluetooth.js`

---

### 问题5: 图片上传提速后丢块/花图（单槽命令队列）

**现象**:
- 提速版（0ms 连发）设备只收到 13104/28800 字节（正好 39 块），前端却全写"成功"
- 加上 20ms 限速 + 补发后，墨水屏显示完全错位的花图

**根因**:
- 设备端 `onWrite` 回调只把命令存入**单槽变量** `s_pending_cmd`，由主循环
  `ble_config_update()` 逐个取出处理；高频分块时新命令覆盖旧命令 → 静默丢块
- 前端写 API 只代表"写入链路成功"，不代表设备已处理（丢块无法感知）
- 修复 v2 的"按接收偏移追加"在补发与原始队列交错时错位写入 → 花图

**解决方案**:
1. 设备端：图片上传命令（start/chunk/progress/complete）在 **BLE 回调中直通处理**，
   绕过单槽队列；预分配解码缓冲区，去掉每块 String 拷贝与 malloc/free
2. 分块带 `index`，按 `index*chunk_size` 固定偏移落位，重复/乱序块直接忽略
   （无论补发怎么交错都不会写错位）
3. 新增 `gallery_upload_progress`：返回"从0起连续收到的字节数"（前缀），
   前端轮询进度，只有连续两次无进展才补发缺失后缀
4. 前端 `uploadComplete()` 等待设备真实保存结果，失败提示具体错误

**修改文件**: `src/ble_config.cpp`、`utils/bluetooth.js`、`pages/gallery/gallery.js`

---

## 功能特性

### 1. 设备端特性

#### 相册功能 (gallery.cpp)
- 图片存储：SPIFFS文件系统
- 支持格式：4色屏 2bit（200x200，10,000字节）；6色屏 4bit（240x240，28,800字节）
- 上传：336字节分块 + 索引落位，设备端 BLE 回调直通处理 + 进度校验补发，
  预期 2-3 秒（详见"BLE通信协议 → 图片上传"）
- 旋转：0°/90°/180°/270° 四个方向（**全局配置**，位于模块页“显示方向”，
  通过 `gallery_rotation` 键保存，设备状态含 `rotation` 字段）
  - 相册：显示时旋转帧缓冲（`rotate_image` / `rotate_image6`）
  - Lua 模块（倒计时等）：`display_prepare_canvas()` 统一 `Paint_SetRotate`，
    重绘（退出 BLE / 刷新显示）后整屏生效
- 循环播放：可配置间隔（1分钟~24小时）
- 空态提示：相册无图片时显示"请上传图片：长按上键等待蓝色指示灯亮，通过小程序
  '幻彩抽屉'连接设备上传图片。"（size 4 24x24 中文字库，UTF-8 安全自动换行居中，
  随全局显示方向旋转；有图后正常显示图片）
- 按键切换：KEY_DOWN 下一张 / KEY_UP 上一张（模块钩子 `subpage_next/subpage_prev`，
  按键唤醒跳过 setup，钩子内部懒初始化图片列表后直接切换）。
  `check_btn` 会补发一次短按事件（唤醒键在轮询前已松开时）：
  KEY_DOWN 短按 → 下一张并进入深度休眠；KEY_UP 短按 → 上一张并进入深度休眠；
  长按 KEY_DOWN（3秒）→ 只触发"切换下一个模块"；长按 KEY_UP → 开关 BLE，
  均不执行短按动作。休眠唤醒长按切模块后，等 KEY_DOWN 释放再进入深度休眠
  （`s_sleep_after_wake_switch`，避免按住时立即休眠导致低电平再次唤醒）
- 进入相册模块（上电 / 定时唤醒 / 从其他模块切换）：`gallery_setup` 显示
  "当前应显示的内容"——循环模式每次绘制下一张，固定模式显示已保存 `img_index`
  对应图片；按键唤醒不经 setup，由按键钩子直接切换
- 限制：仅MiniEink设备支持

**配置项** (`data/gallery.cfg`):
JSON 配置定义数组：`display_mode`（显示模式）、`cycle_interval`（循环间隔）。
显示方向不再出现在相册配置页，改由模块页全局设置。

#### 名言警句模块 (quotes.lua)
- 内置 431 条国内外名言警句（中国古代/唐诗宋词/近现代/外国哲理中文/英文原句），
  全部字符经 GB2312 字库映射校验（无缺字）
- 内存优化：数据用**扁平数组**（每条 "名言\t作者" 单字符串对象），避免 431 个
  嵌套小表占用大量 Lua 堆；脚本由设备端**流式加载**（`luaL_loadfile`，不再拼进
  C++ String），修复 ESP32-C3 上 "not enough memory" 问题
- 中文显示：GB2312 映射表（约29KB）在**开机时预加载**（`lua_hardware_preload_gb2312`），
  避免 BLE 会话中首次绘制中文时因堆不足加载失败导致文字全部显示为 `?`
  （加载失败会保持未加载状态，下次绘制自动重试）
- 布局：无标题/分隔线，正文直接用最大字号（size 4：中文 24x24，ASCII 17x24）
  自动换行、整块垂直居中 + 右下红色作者；中英文混排按显示宽度换行
  （英文尽量整词断行），超出行数自动省略
- 切换间隔：`interval` 配置（分钟），选项与相册循环间隔一致（1分钟~24小时，默认1小时）
- 显示顺序：`order` 配置（0=按顺序，1=随机，默认按顺序）
- 按键切换：KEY_DOWN 短按下一条 / KEY_UP 短按上一条（Lua `subpage_next/subpage_prev`
  钩子，首尾回绕；**连续按键可逐步切换**——当前索引经 `sys.set_state/get_state`
  存入 RTC 内存，深度休眠保持、掉电清零；切换后立即休眠，下次定时唤醒按槽位刷新）
- 轮换策略：按顺序模式按 `当前时刻/切换间隔` 取槽位确定条目（纯时间推导，
  深度休眠唤醒后稳定轮换，无需模块持久化状态）；随机模式以当前时刻为随机种子，
  每次唤醒/切换随机取一条
- 休眠定时唤醒：见"休眠管理 → 模块定时唤醒"（`interval` 分钟级）

#### 休眠管理 (sleep_manager.cpp)
- **统一休眠模型**：任何逻辑执行完立即进入深度休眠，只有 BLE 配置模式保持唤醒。
  - **电池保护**：每次开机（上电 / 按键 / 定时唤醒）`setup()` 先测电压
    （`battery.h` 的 `BATTERY_LOW_MV = 3100` mV）；低于阈值时不执行任何模块逻辑
    （不绘制、不进 BLE），直接 `enter_deep_sleep()`；保留按键 + 模块定时唤醒，
    电压恢复后再次开机自动继续正常运行。若由按键唤醒且按键仍按住，
    等释放后再休眠，避免 GPIO 低电平立即再次唤醒形成开机循环
  - 上电 / 定时唤醒：模块 `setup()` 绘制 → 立即 `enter_deep_sleep()`
  - 按键唤醒：跳过 setup，由 `check_btn` 判定 → 短按动作/切模块后休眠，
    长按 KEY_UP 进入 BLE 保持唤醒
  - BLE 开启后 **60 秒无连接**：自动停止广播并休眠（`BLE_NO_CONNECT_TIMEOUT_MS`，
    写死，不依赖配置；连接后取消计时，断开后仍走"会话结束立即休眠"）
  - BLE 会话结束（长按 KEY_UP 退出 / 小程序断开）：立即休眠，不重绘屏幕，
    新配置在下次唤醒的 `setup()` 中生效
- **BLE 省电优化**（A+B+D+E）：
  - **A. light sleep**：`platformio.ini` 的 `custom_sdkconfig` 开启
    `CONFIG_PM_ENABLE=y` + `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`，且必须同时开启
    **蓝牙控制器低功耗**：
    - `CONFIG_BT_CTRL_MODEM_SLEEP=y`（蓝牙 modem sleep，否则控制器以
      `ESP_BT_SLEEP_MODE_NONE` 初始化，使能时持有 `ESP_PM_NO_LIGHT_SLEEP` 锁，
      **直接禁止系统 light sleep**，实测仍 41mA 即此原因）
    - `CONFIG_BT_CTRL_LPCLK_SEL_MAIN_XTAL=y`（主晶振做低功耗时钟，C3 无外部 32k）
    - `CONFIG_BT_CTRL_MAIN_XTAL_PU_DURING_LIGHT_SLEEP=y`（light sleep 期间主晶振
      保持上电，否则 `lpclk_sel==MAIN_XTAL` 时 `no_light_sleep` 仍为 1）
    BLE 广播/空闲时系统可进入 light sleep（C3 支持 BT 唤醒 + BT 断电），广播平均
    电流从 ~41mA 降至数 mA 量级（主晶振保持上电有几百 µA 代价）。⚠️ 自定义 sdkconfig
    会触发一次 Arduino IDF 库全量重编（约 10-30 分钟，含组件下载，Windows 偶发组件
    目录被占用报 WinError 32/145，清理对应 managed_components 子目录重试即可），
    成功后缓存（`sdkconfig.defaults` 写入哈希），之后为增量构建
  - **BLE 模式附加省电**（实测 41→20mA 后再压）：
    - 进入 BLE 前 `epdFinishPowerOff()` + `epdClearRefreshPending()`（仅 INK6）：
      按键唤醒进 BLE 时 6 色面板仍处于 init 后未断电状态，驱动 IC 持续耗电
    - 蓝色指示灯改 LEDC 低占空比 PWM（1kHz、8bit、5%），常亮约 2~8mA → ~0.2mA
  - **B. 广播间隔 100~200ms**：`NimBLEAdvertising::setMinInterval(160)/setMaxInterval(320)`
    （单位 0.625ms），替代默认约 40ms 快速广播
  - **D. TX 功率 0dBm**：`NimBLEDevice::setPower(0)`（近距离连接足够）
  - **E. CPU 80MHz**：进入 BLE 时 `setCpuFrequencyMhz(80)`，`ble_config_stop()` 恢复 160
  - 依赖的平台补丁（pioarduino，机器级）：
    - `component_manager.py`：lib_ignore 剪组件时应用 BT/BLE 保护
    - `espidf.py`：`get_lib_ignore_components` 从 `platformio.ini`（UTF-8）读取
      `lib_deps` 判断 NimBLE 依赖，避免自定义 sdkconfig 重建库时剪掉 bt 组件
    - `src/log_printf_wrap.cpp`：补 `__wrap_log_printf`（core 3.3.8 移除该符号，
      pioarduino 链接脚本仍保留 `-Wl,--wrap=log_printf`）
  - 原 `-DCONFIG_BT_ENABLED=0 -DCONFIG_BLUEDROID_ENABLED=0` 已移除（与 sdkconfig
    冲突，且会导致库构建时自带 BLE 库误判走 Bluedroid 分支）
- 已移除：空闲超时（`sleep_sec` 仅存档，不再控制休眠）、模块常驻 `loop`、
  Lua `sys.wake_source()`（模块 setup 一律绘制，按键唤醒由系统层跳过）
- **模块定时唤醒**：`module_descriptor_t` 的 `wake_interval` 钩子返回秒（0=不启用），
  `enter_deep_sleep()` 按当前模块配置 `esp_sleep_enable_timer_wakeup`
  ⚠️ `wake_interval` 可能被 30 秒断电快路径在模块 `setup` **之前**查询，
  实现必须独立于模块运行时状态（如直接读 NVS 配置，参考相册/Lua 的实现）
  - 相册：循环播放模式下按 `cycle_interval`（分钟）定时唤醒切换
  - Lua 模块：通用实现（`dynamic_lua_wake_interval`）统一读取模块配置 `interval`
    （**分钟**，1~1440，名言/倒计时通用；NVS 键名最长15字符，故配置键须 ≤15），
    `refresh`（小时）仅作旧版倒计时配置兼容（`interval` 未保存时回退），
    定时唤醒后 `setup()` 重绘并立即休眠；异步刷屏期间 MCU 直接休眠，
    面板刷新结束后由定时唤醒完成断电，再按模块周期继续休眠
- **系统级异步刷屏（4色 / 6色 / 黑白屏统一）**：见下方“异步刷屏调度层”
  （`include/epd_async.h`）。面板刷新由控制器内部定时序，`eink_display_frame()`
  发完刷新命令立即返回并置 RTC 标志 `s_refresh_pending`，不依赖任何模块特判。
  - **BUSY 等待省电**：`epdWaitBusy` 轮询间隔从 `delay(1)` 改为 `delay(20)`——
    tickless idle 入睡阈值为 8ms（`FREERTOS_IDLE_TIME_BEFORE_SLEEP=8` @ 1000Hz），
    1ms 轮询让 CPU 永远睡不进 light sleep，等待面板 BUSY（如 30 秒断电快路径最多
    等 40 秒）时 CPU 全程活跃约 15mA；20ms 轮询可进入 light sleep，
    该阶段电流从 ~18mA 降至面板自身的 3~5mA。主循环 `delay(10)` 同步改为 `delay(20)`

**时间与时区**：
- 时间戳（UTC）由 BLE `sync_time` 同步，`TimeCalibration` 存 RTC 内存（深睡保持）
- 时区偏移同时存 RTC 内存与 NVS（`system/tz_offset`），掉电后从 NVS 恢复；
  `sync_time` 命令未携带 `timezone` 字段时不会清零已有时区（避免倒计时差8小时）

### 2. 小程序特性

#### 相册管理 (gallery页面)
- ✅ 图片上传：选择相册/拍照 → 编辑页处理 → 上传；按设备 `ink` 自动转换
  - 编辑页无确定/取消按钮，主按钮为"预览上传"
  - 预览弹窗：抖动算法选择（Floyd/Atkinson/Burkes/Stucki/无抖动）+ 亮度/对比度/冷暖
    滑块（实时预览）+ 右上角"上传"按钮
  - 转换管道：用户调节 → 肤色暖化(warmEnhance) → 色相分类 → 色相门控抖动 → 打包
    （肤色暖化仅对暖色皮肤提红压蓝，避免面部偏冷；色相门控防止误差扩散串色）
  - 上传等待设备真实保存结果，失败显示设备返回的错误（如 Save failed）
- ✅ 图片文件名：`img_年月日时分秒.img`（如 img_20260812114147.img），非毫秒时间戳
- ✅ 图片预览：点击占位符从设备获取，支持进度显示
- ✅ 批量删除：长按进入批量模式
- ✅ 显示到墨水屏：一键推送显示
- ✅ 本地缓存：已获取的图片缓存在本地存储
  - 缓存键 `gallery_image_cache_v2`（v1 按旧名义色板生成，已作废清理）
  - 6色缩略图解码色板与编辑页校准色板一致，避免列表蓝色过艳
- ✅ 下拉刷新：手动刷新列表
- ⚠️ 仅MiniEink设备可用，其他设备会提示

#### 模块管理 (modules页面)
- 模块列表：显示已安装模块
- **电量显示**：顶部设备名右侧显示 🔋 百分比（<20% 变红），前端由 `battery_mv` 换算
- 开关控制：启用/禁用模块
- 配置入口：点击进入详细配置
- 删除模块：卸载模块
- **亮度设置**：仅非MiniEink设备显示（墨水屏无背光）
- **休眠时间**：前端已按设备型号隐藏（`!isMiniEink`），MiniEink 不再展示；
  Bottle 等设备仍保留并使用该配置

#### 模块市场 (market页面)
- 模块列表：云函数 `getModules` 拉取，支持分类/搜索/下拉刷新
- ✅ **按硬件型号过滤**：模块 `supportedModels` 数组（如 `bottlev1`/`bottlev4`，
  未声明或含 `通用` 表示所有设备可见）；连接设备后前端用 `deviceStatus.model`
  （设备 `get_status` 返回的 `DEVICE_MODEL`，如 `Bottle-V1`/`Eink-V1`）归一化
  （小写去分隔符）匹配过滤；未连接/未知型号时显示全部
- 过滤后无可用模块时显示"当前设备暂无可用模块"
- 下载：按 `deviceModel` 传给 `getModuleDetail`，优先型号专用代码，降级通用代码

#### 设备连接 (device-connect页面)
- BLE扫描：自动过滤 BottleLED/MiniEink 设备
- 扫描超时：15秒
- 历史设备：保存已连接设备，快速重连
- 自动清理：扫描完成后清除蓝牙缓存

---

## 常见陷阱与最佳实践

### 1. ⚠️ BLE监听器泄漏

**问题**: 微信小程序的 `wx.onBLECharacteristicValueChange()` 是**累加式注册**，每次调用都添加新监听器。

**解决**: 使用 `bluetooth.js` 的全局单例监听器 + callback机制（见"统一BLE架构"章节）。

**排查方法**:
```bash
# 搜索页面中的监听器注册
grep -rn "onBLECharacteristicValueChange" pages/ --include="*.js"
# 应该返回空，所有监听器都在bluetooth.js中
```

---

### 2. ⚠️ 设备端固件上传限制

**重要**: 根据 `AGENT_HANDOFF.md`，**不要上传固件**到设备，只编译！

```bash
# ✅ 正确 - 仅编译
cd /d/Projects/Eink
/d/.platformio/penv/Scripts/platformio.exe run

# ❌ 错误 - 不要上传
platformio.exe run --target upload
```

---

### 3. ⚠️ 分块数据验证

**问题**: 分块数据可能混入非目标数据。

**示例**: 等待manifest分块时，可能收到status更新的分块。

**解决**: 合并后验证数据结构
```javascript
const manifest = JSON.parse(fullData);
if (!manifest.id || !manifest.configs) {
  // 不是manifest，重置chunks继续等待
  chunks = {};
  return;
}
```

---

### 4. ⚠️ TabBar页面生命周期

**问题**: TabBar页面不会触发 `onUnload`，只触发 `onShow`/`onHide`。

**影响**: 页面级的临时资源无法在 `onUnload` 中清理。

**解决**: 
- 不要在TabBar页面注册临时BLE监听器
- 使用全局单例 + callback机制
- 在 `onHide` 中清理临时状态

---

### 5. ⚠️ 进度条动画重绘

**问题**: 频繁调用 `wx.showLoading()` 会导致loading动画重新开始。

**解决**: 降低更新频率（如：只在进度变化≥5%时更新）。

---

### 6. ⚠️ BLE 单槽命令队列（设备端）

**问题**: 设备端 `onWrite` 只把命令存入单个 `s_pending_cmd` 变量，
主循环逐个处理；高频命令（如图片分块）会覆盖未处理的旧命令 → 静默丢块，
且前端写 API 无法感知（写"成功"≠设备已处理）。

**解决**:
- 高频命令（图片上传）在 BLE 回调中直通处理，不经过单槽队列
- 需要重传语义的命令带 `index`，设备按固定偏移落位、重复块忽略
- 前端以设备实际接收进度为准（`gallery_upload_progress`），不要信任写返回值

---

## 编译与调试

### 设备端编译

**环境**:
- PlatformIO: `D:\.platformio\penv\Scripts\platformio.exe`
- 工作目录: `d:\Projects\Eink`

**命令**:
```bash
cd /d/Projects/Eink
/d/.platformio/penv/Scripts/platformio.exe run
```

**Flash 分区表** (`partitions.csv`，4MB Flash):
- `nvs`(20KB) + `otadata`(8KB) + `app0`(992KB, ota_0) + `modules`(2.96875MB, SPIFFS)
- 已移除 `app1`/OTA 分区（固件 OTA 逻辑已全部注释），把空余空间全部并入 `modules` 分区
- `app0` 按当前固件体积预留约 10% 余量（`0xF8000`=1015808B，当前 firmware.bin 921216B）；
  后续固件若显著增大需同步复核 `partitions.csv`
- ⚠️ 刷入新分区表后 SPIFFS 容量变化会触发格式化，需重新 `uploadfs` 上传 `data/` 目录
  （Lua 模块、字体、配置），否则设备上旧数据失效

**常见错误**:
1. **undefined reference to 'enter_deep_sleep()'**
   - 原因: 缺少头文件
   - 修复: 添加 `#include "sleep_manager.h"`

2. **timerBegin参数错误**
   - 原因: 新版API已变更
   - 新API: `timerBegin(freq)`, `timerAttachInterrupt()`, `timerAlarm()`

---

### 小程序调试

**工具**: 微信开发者工具

**注意事项**:
1. **BLE必须真机调试**，开发者工具无法模拟
2. 打开"调试基础库"选项
3. 使用 `console.log()` 输出到Console

**关键调试位置**:
- `bluetooth.js::setupGlobalListener()` - BLE数据接收
- `bluetooth.js::manifestCallback` - 模块配置响应
- `bluetooth.js::galleryListCallback` - 图片列表响应
- `bluetooth.js::galleryImageCallback` - 单张图片响应
- `bluetooth.js::uploadResultCallback` - 图片上传进度/保存结果回执
- `bluetooth.js::uploadComplete()` - 等待设备端真实保存结果
- `bluetooth.js::getUploadProgress()` - 查询上传接收进度（丢包补齐）

---

## 文档维护指南

### Claude Code 文件写入最佳实践

在使用Claude Code进行文档编写时，遇到过文件写入工具的问题和解决方案：

#### 问题现象
使用 `Write` 工具创建大型文档时出现错误：
```
InputValidationError: Write was called with input that could not be parsed as JSON.
```

#### 根本原因
- **Write工具限制**: 单次写入内容不能超过约4000个token或8000字符
- **路径格式**: Windows路径中的反斜杠需要正确转义
- **大文档**: 超过150行或8000字符的内容会导致API截断

#### 正确的解决方案

**方案1: PowerShell分块写入（推荐用于大文档）**
```powershell
# 第一步：创建文件头
@'
# 文档标题
第一部分内容...
'@ | Out-File -FilePath "path/to/file.md" -Encoding utf8 -NoNewline

# 第二步：追加内容
@'
第二部分内容...
'@ | Add-Content -Path "path/to/file.md" -Encoding utf8 -NoNewline

# 第三步：继续追加
@'
第三部分内容...
'@ | Add-Content -Path "path/to/file.md" -Encoding utf8 -NoNewline
```

**方案2: Bash + heredoc（适用于中等大小文档）**
```bash
cat > /path/to/file.md << 'EOF'
文档内容...
EOF
```

**方案3: Write工具（仅适用于小文件 <150行）**
```
只用于创建小文件或代码文件，不适合大型文档。
```

#### 最佳实践总结

**✅ 推荐做法**:
1. **大型文档（>500行）**: 使用PowerShell分块写入
   - 第一次用 `Out-File` 创建
   - 后续用 `Add-Content` 追加
   - 每块控制在200-300行以内
   - 使用 `-NoNewline` 避免额外换行

2. **中型文档（200-500行）**: 使用Bash heredoc
   - 注意heredoc结束符必须顶格（`EOF`前无空格）
   - 使用单引号 `'EOF'` 避免变量展开

3. **小型文件（<150行）**: 使用Write工具
   - 代码文件
   - 配置文件
   - 短文档

4. **路径处理**:
   - Windows路径优先使用正斜杠: `d:/Projects/Eink/file.md`
   - 或者在Bash中使用: `/d/Projects/Eink/file.md`
   - 避免反斜杠转义问题

**❌ 避免做法**:
- 不要用Write工具一次性写入超过150行的文档
- 不要在PowerShell heredoc中使用复杂的引号嵌套
- 不要在Bash heredoc结束符前添加空格或制表符

#### 实际案例

本项目中创建 `LUA_SCRIPT_GUIDE.md` (1010行) 的成功方案：

```powershell
# 步骤1: 创建前200行
@'
# Lua 模块开发指南
...前200行内容...
'@ | Out-File -FilePath "d:\Projects\Eink\docs\LUA_SCRIPT_GUIDE.md" -Encoding utf8 -NoNewline

# 步骤2: 追加配置系统和API部分（约300行）
@'
### 配置系统
...中间内容...
'@ | Add-Content -Path "d:\Projects\Eink\docs\LUA_SCRIPT_GUIDE.md" -Encoding utf8 -NoNewline

# 步骤3: 追加示例和参考（约500行）
@'
## 示例模块
...后续内容...
'@ | Add-Content -Path "d:\Projects\Eink\docs\LUA_SCRIPT_GUIDE.md" -Encoding utf8 -NoNewline
```

#### 验证方法

创建文档后验证：
```bash
# 检查行数
wc -l /d/Projects/Eink/docs/LUA_SCRIPT_GUIDE.md

# 检查开头
head -20 /d/Projects/Eink/docs/LUA_SCRIPT_GUIDE.md

# 检查结尾
tail -20 /d/Projects/Eink/docs/LUA_SCRIPT_GUIDE.md

# 搜索关键内容验证完整性
grep -c "## " /d/Projects/Eink/docs/LUA_SCRIPT_GUIDE.md
```

---

## 测试流程

### 完整功能测试

#### 1. 设备连接测试
- [ ] 打开小程序
- [ ] 进入设备连接页面
- [ ] 点击"开始扫描"
- [ ] 应该能发现MiniEink设备（15秒内）
- [ ] 返回，再次扫描 - 应该还能发现（验证缓存清除）
- [ ] 点击设备连接
- [ ] 连接成功，跳转到模块页面

#### 2. 模块管理测试
- [ ] 连接MiniEink设备
- [ ] 模块页面**不应显示亮度设置**（墨水屏无背光）
- [ ] MiniEink 模块页**不应显示休眠时间**（Bottle 设备保留）
- [ ] 点击任意模块进入配置页面
- [ ] 配置页面应该正常加载（不转圈）

#### 3. 相册功能测试
- [ ] 点击底部"相册"图标
- [ ] 如果未连接设备，应该显示空状态
- [ ] 如果连接非MiniEink设备，应该提示"功能不可用"并返回
- [ ] 连接MiniEink设备后，应该加载图片列表
- [ ] **上传图片**:
  - [ ] 点击"上传图片"按钮
  - [ ] 选择一张照片
  - [ ] 应该显示 "上传中 X%" 进度
  - [ ] 上传成功后，列表自动刷新
- [ ] **点击占位符**:
  - [ ] 点击未缓存的图片占位符
  - [ ] 应该显示 "加载中 X%" 进度
  - [ ] 加载完成后预览图片
- [ ] **下拉刷新**:
  - [ ] 下拉列表
  - [ ] 应该刷新成功（不会失败）
- [ ] **切换页面**:
  - [ ] 切换到模块页
  - [ ] 再切换回相册页
  - [ ] **不应该自动刷新**（已有数据）

#### 4. 压力测试
- [ ] 快速切换TabBar页面10次
- [ ] 不应该出现加载失败
- [ ] 点击相册模块配置10次
- [ ] 每次都应该正常加载
- [ ] 上传多张图片
- [ ] 每次都应该成功刷新列表

---

## 项目依赖

### 设备端 (platformio.ini)
```ini
[env:pico32]
platform = espressif32
board = adafruit_qtpy_esp32c3
framework = arduino
lib_deps = 
    FastLED @ 3.10.3
    arduinoFFT @ 2.0.4
    NimBLE-Arduino @ 2.5.0
    Lua @ 5.4.7
```

### 小程序 (package.json)
```json
{
  "dependencies": {
    "regenerator-runtime": "^0.13.9"
  }
}
```

**云开发环境**: cloud1-d3gxxnyov88776090

---

## 待办事项

### 已完成 ✅
- [x] 修复BLE监听器泄漏问题
- [x] 修复设备扫描缓存问题
- [x] 统一BLE数据接收架构
- [x] 添加图片旋转功能
- [x] 优化相册页面刷新逻辑
- [x] 统一进度条样式
- [x] MiniEink设备UI优化
- [x] 市场模块按设备型号过滤（supportedModels）
- [x] 创建完整项目交接文档
- [x] 创建Lua开发规范文档

### 待实现 🔲
- [ ] 考虑添加设备端广播间隔配置（提高扫描发现率）
- [ ] 优化分块传输大小（当前200字节）
- [ ] 添加更多设备类型支持
- [ ] 完善错误处理和用户提示
- [ ] 相册图片压缩优化
- [ ] 添加图片轮播预览功能
- [ ] Lua模块市场功能

---

## 联系信息

- **用户**: jeff
- **Git用户**: jeffreyer
- **设备端项目**: d:\Projects\Eink
- **小程序项目**: d:\Projects\jeff\WeChatProjects\pix

---

## 附录：关键代码片段

### A. 设备端分块发送

```cpp
// ble_config.cpp
void set_status(const String &status) {
    const size_t CHUNK_SIZE = 200;
    if (status.length() > CHUNK_SIZE) {
        size_t total = (status.length() + CHUNK_SIZE - 1) / CHUNK_SIZE;
        for (size_t i = 0; i < total; i++) {
            StaticJsonDocument<256> chunk_doc;
            chunk_doc["chunk"] = i;
            chunk_doc["total"] = total;
            chunk_doc["data"] = status.substring(
                i * CHUNK_SIZE, 
                min((i + 1) * CHUNK_SIZE, status.length())
            );
            String chunk_str;
            serializeJson(chunk_doc, chunk_str);
            s_status_char->setValue(chunk_str.c_str());
            s_status_char->notify();
            delay(30);  // 给小程序处理时间
        }
    } else {
        s_status_char->setValue(status.c_str());
        s_status_char->notify();
    }
}
```

### B. 小程序分块接收

```javascript
// bluetooth.js
this.galleryListCallback = (data) => {
  if (data.chunk !== undefined && data.total !== undefined) {
    chunks[data.chunk] = data.data;
    
    if (Object.keys(chunks).length === data.total) {
      let fullData = '';
      for (let i = 0; i < data.total; i++) {
        fullData += chunks[i];
      }
      
      const result = JSON.parse(fullData);
      
      // 验证数据结构
      if (!result.images) {
        chunks = {};  // 不是期望的数据，重置
        return;
      }
      
      resolve({ success: true, images: result.images });
    }
  } else if (data.images) {
    // 非分块数据，直接返回
    resolve({ success: true, images: data.images });
  }
};
```

### C. 图片旋转算法

```cpp
// gallery.cpp
void rotate_image(uint8_t* image, int width, int height, int degrees) {
    uint8_t* temp = (uint8_t*)malloc(total_bytes);
    memcpy(temp, image, total_bytes);
    memset(image, 0, total_bytes);
    
    if (degrees == 90) {
        // 90度顺时针: (x,y) -> (height-1-y, x)
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int new_x = height - 1 - y;
                int new_y = x;
                // 复制像素位...
            }
        }
    }
    // 180度、270度类似...
    
    free(temp);
}
```

---

## Lua 模块开发

### 开发文档
**完整开发规范**: `d:\Projects\Eink\docs\LUA_SCRIPT_GUIDE.md`

### 快速参考

#### 关键特性
- **元数据格式**: `-- @key: value` （单行注释+冒号，与Bottle一致）
- **配置文件**: `module-id.cfg` （JSON数组格式）
- **显示**: 4色墨水屏（黑=0, 白=1, 黄=2, 红=3）
- **刷新**: 约12秒，建议间隔≥30秒
- **屏幕**: 200×200像素

#### API概览
```lua
-- 显示
display.clear()
display.pixel(x, y, color)
display.text(x, y, str, size)
display.show()

-- 时间
local h, m, s = time.get()
time.delay(ms)

-- 配置
local value = CONFIG.key or default
```

#### 重要规则
1. 文件名和 `@id` 使用连字符 `-`，不用下划线 `_`
2. `@description` 不超过128字节（约42个中文字符）
3. 减少刷新频率，避免频繁调用 `display.show()`

#### 项目继承关系
**MiniEink的Lua框架继承自Bottle项目**（`D:\Projects\Bottle`）
- 元数据格式、配置系统、标准库完全一致
- 如需修改底层Lua解析逻辑，参考Bottle项目源码
- 日常开发无需了解Bottle，参考本项目文档即可

---

**文档版本**: v2.1  
**最后更新**: 2025-01-XX  
**维护者**: jeffreyer
