# 项目交接文档 - MiniEink墨水屏设备

## 项目概述

这是一个基于ESP32-C3的墨水屏设备项目，包含设备端固件和微信小程序前端。设备通过BLE与小程序通信，支持模块化配置和相册管理功能。

**设备型号**: MiniEink (Adafruit QT Py ESP32-C3)  
**显示屏**: 两个版本
- 4色屏（默认）：200x200（黑、白、黄、红）
- 6色屏（`INK6` 宏）：240x240 JD7601（黑、白、黄、红、蓝、绿），1.54寸
**刷新时间**: 4色约12秒；6色约30-40秒（驱动内 BUSY 超时上限 40 秒）  
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
- 刷新：`display.show()` → `epdDisplayImage()`（写帧 0x10 → 刷新 0x12 → 休眠 0x07）
- 4色屏代码在 `INK6` 下通过 `#ifndef INK6` 整体跳过（`eink.cpp`）

### Lua 颜色值

`0=黑, 1=白, 2=黄, 3=红, 4=蓝, 5=绿`（4色屏仅支持 0-3）。
`WIDTH` / `HEIGHT` 全局量由固件注册，`INK6` 下自动为 240。

### 按键功能（`main.cpp check_btn`）

| 按键 | 短按 | 长按(3秒) |
|------|------|------|
| KEY_UP | 上一页（subpage-1，触发模块重绘） | 开关 BLE 配置 |
| KEY_DOWN | 下一页（subpage+1，触发模块重绘） | 进入深度休眠 |

### BLE 状态字段

`get_status` 新增 `"ink": 6`（4色屏为 4），小程序据此选择图片格式
（6色：240x240 4bpp / 28,800 字节；4色：200x200 2bpp / 10,000 字节）。

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
├── gallery.cpp           # 相册功能（含 2bpp/4bpp 旋转）
├── eink.cpp              # 4色屏驱动封装（INK6 下不编译）
├── eink6.cpp             # 6色屏 JD7601 驱动 + 6色画布 BlackImage
├── lua_hardware_api.cpp  # Lua display API（绘制/中文渲染/刷新）
├── Display_EPD_W21.cpp   # 4色屏底层驱动
└── GUI/                  # GUI_Paint 绘制库（Scale 4=2bpp, Scale 7=4bpp）

include/
├── ble_config.h          # BLE接口定义
├── cmd_handler.h
├── module_registry.h
├── sleep_manager.h
├── gallery.h
├── eink.h                # 4色屏参数（200x200）
├── eink6.h               # 6色屏参数（240x240）+ 颜色常量
├── image.h               # 6色测试图（调试用）
└── GUI_Paint.h

data/
├── gallery.cfg           # 相册配置定义（display_mode, cycle_interval；rotation 已移至模块页全局配置）
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
├── image-editor/         # 图片编辑页（按设备 ink 字段选 4色/6色格式）
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
{"gallery_upload_start": true, "filename": "...", "size": 28800} // 开始上传（6色=28800，4色=10000）
{"gallery_upload_chunk": true, "data": "base64..."}              // 上传数据块
{"gallery_upload_complete": true}                                // 上传完成
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
- 自动合并分块，验证完整性

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

## 功能特性

### 1. 设备端特性

#### 相册功能 (gallery.cpp)
- 图片存储：SPIFFS文件系统
- 支持格式：4色屏 2bit（200x200，10,000字节）；6色屏 4bit（240x240，28,800字节）
- 上传：分块上传，每块256字节
- 旋转：0°/90°/180°/270° 四个方向（**全局配置**，位于模块页“显示方向”，
  通过 `gallery_rotation` 键保存，设备状态含 `rotation` 字段）
  - 相册：显示时旋转帧缓冲（`rotate_image` / `rotate_image6`）
  - Lua 模块（倒计时等）：`display_prepare_canvas()` 统一 `Paint_SetRotate`，
    重绘（退出 BLE / 刷新显示）后整屏生效
- 循环播放：可配置间隔（1分钟~24小时）
- 限制：仅MiniEink设备支持

**配置项** (`data/gallery.cfg`):
JSON 配置定义数组：`display_mode`（显示模式）、`cycle_interval`（循环间隔）。
显示方向不再出现在相册配置页，改由模块页全局设置。

#### 休眠管理 (sleep_manager.cpp)
- 深度休眠：定时器唤醒
- 配置：30秒~30分钟，或永不休眠
- 自动保存配置到NVS

### 2. 小程序特性

#### 相册管理 (gallery页面)
- ✅ 图片上传：选择相册/拍照，按设备 `ink` 自动转换为 4色2bit 或 6色4bit（含蓝/绿）
- ✅ 图片预览：点击占位符从设备获取，支持进度显示
- ✅ 批量删除：长按进入批量模式
- ✅ 显示到墨水屏：一键推送显示
- ✅ 本地缓存：已获取的图片缓存在本地存储
- ✅ 下拉刷新：手动刷新列表
- ⚠️ 仅MiniEink设备可用，其他设备会提示

#### 模块管理 (modules页面)
- 模块列表：显示已安装模块
- 开关控制：启用/禁用模块
- 配置入口：点击进入详细配置
- 删除模块：卸载模块
- **亮度设置**：仅非MiniEink设备显示（墨水屏无背光）
- **休眠时间**：所有设备都支持

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
- `nvs`(20KB) + `otadata`(8KB) + `app0`(1.125MB, ota_0) + `modules`(2.8125MB, SPIFFS)
- 已移除 `app1`/OTA 分区（固件 OTA 逻辑已全部注释），把空余空间全部并入 `modules` 分区
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
- [ ] 应该显示休眠时间设置
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
