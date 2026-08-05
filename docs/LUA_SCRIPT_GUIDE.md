# Lua 模块开发指南

本文档为 AI 助手和开发者提供快速开发 Lua 模块的完整指南。

## 目录
- [硬件环境](#硬件环境)
- [文件结构](#文件结构)
- [核心 API](#核心-api)
- [模块开发模式](#模块开发模式)
- [常见问题](#常见问题)
- [最佳实践](#最佳实践)

---

## 硬件环境

### 墨水屏显示
- **设备型号**: MiniEink (基于 ESP32-C3)
- **屏幕尺寸**: 200 宽 × 200 高
- **全局变量**: `WIDTH` 和 `HEIGHT` 由设备端注册，值为200
- **坐标系**: 左上角为 (0, 0)，右下角为 (199, 199)
- **显示模式**: 4色显示（黑、白、黄、红）
- **刷新特性**: 
  - 刷新时间：约12秒
  - 建议刷新间隔：≥30秒

---

## 文件结构

### 模块文件命名
```
module-id.lua           # 主脚本文件
module-id.cfg          # 配置文件（可选，JSON格式）
```

**命名规则**:
- 使用**连字符** (`-`)，不用下划线
- ✅ 正确：`clock-simple.lua`, `weather-mini.lua`
- ❌ 错误：`clock_simple.lua`, `weather_mini.lua`

### 必需的文件头元数据
```lua
-- @name: 时钟显示
-- @version: 1.0.0
-- @author: Your Name
-- @description: 在墨水屏上显示大字时钟
-- @id: clock-simple
```

**重要**: 
- 使用 `-- @key: value` 格式（单行注释+冒号+空格）
- `@id` 和文件名必须一致，都使用连字符
- `@description` 必须控制在 128 字节以内
- 中文字符约占 3 字节，建议不超过 42 个中文字符

### 脚本结构模板

```lua
-- @name: 时钟显示
-- @version: 1.0.0
-- @author: Your Name
-- @description: 在墨水屏上显示大字时钟
-- @id: clock-simple

-- 全局状态
local last_minute = -1

function setup()
    print("模块初始化")
    display.clear()
    display.show()
end

function loop()
    local h, m, s = time.get()
    
    -- 每分钟刷新一次
    if m ~= last_minute then
        last_minute = m
        
        display.clear()
        local time_str = string.format("%02d:%02d", h, m)
        display.text(50, 80, time_str, 3)
        display.show()
    end
    
    time.delay(1000)
end

function unload()
    print("模块卸载")
    display.clear()
    display.show()
end
```

### 标准三函数结构

#### `setup()`
- 模块加载时调用一次
- 用于初始化状态、加载配置
- 可选函数

#### `loop()`
- 主循环，持续调用
- 实现模块的主要逻辑
- 必需函数

#### `unload()`
- 模块卸载时调用
- 用于清理资源、保存状态
- 可选函数

### 配置系统

#### 配置文件格式

创建 `[module-id].cfg`（JSON 数组格式）：

```json
[
  {
    "key": "time_format",
    "type": "select",
    "label": "时间格式",
    "options": [
      {"label": "24小时制", "value": "24h"},
      {"label": "12小时制", "value": "12h"}
    ],
    "default": "24h",
    "desc": "选择时间显示格式"
  },
  {
    "key": "refresh_interval",
    "type": "slider",
    "label": "刷新间隔",
    "min": 30,
    "max": 3600,
    "step": 30,
    "default": 60,
    "desc": "屏幕刷新间隔（秒）"
  },
  {
    "key": "show_seconds",
    "type": "switch",
    "label": "显示秒数",
    "default": false,
    "desc": "是否显示秒数"
  },
  {
    "key": "message",
    "type": "text",
    "label": "显示消息",
    "default": "Hello",
    "maxlength": 20,
    "desc": "自定义显示文本"
  }
]
```

#### 配置类型

| 类型 | 说明 | 必需字段 |
|------|------|----------|
| `select` | 下拉选择 | `options` (label, value) |
| `slider` | 滑动条 | `min`, `max`, `step` |
| `switch` | 开关 | - |
| `text` | 文本输入 | `maxlength` (可选) |
| `color` | 颜色选择器 | - |

#### 读取配置

配置通过全局 `CONFIG` 表访问：

```lua
-- 读取配置（提供默认值）
local format = CONFIG.time_format or "24h"
local interval = CONFIG.refresh_interval or 60
local message = CONFIG.message or "Hello"

-- 正确处理布尔值
local show_seconds = false
if CONFIG.show_seconds ~= nil then
    show_seconds = CONFIG.show_seconds
end
```

---

## 核心 API

### 1. display - 显示控制

#### display.clear()
清空显示缓冲区（全白）。

```lua
display.clear()
```

#### display.pixel(x, y, color)
设置单个像素。

```lua
display.pixel(100, 100, 0)  -- 设置 (100, 100) 为黑色
display.pixel(50, 50, 1)    -- 设置 (50, 50) 为白色
display.pixel(20, 20, 2)    -- 设置 (20, 20) 为黄色
display.pixel(10, 10, 3)    -- 设置 (10, 10) 为红色
```

**参数**:
- `x`: 横坐标 (0-199)
- `y`: 纵坐标 (0-199)  
- `color`: 0=黑色, 1=白色, 2=黄色, 3=红色

#### display.line(x0, y0, x1, y1, color)
绘制直线。

```lua
display.line(0, 0, 199, 199, 1)  -- 绘制对角线
```

#### display.rect(x, y, w, h, color)
绘制矩形边框。

```lua
display.rect(10, 10, 100, 50, 1)  -- 绘制矩形框
```

#### display.fill_rect(x, y, w, h, color)
绘制填充矩形。

```lua
display.fill_rect(20, 20, 80, 40, 1)  -- 绘制实心矩形
```

#### display.circle(x, y, r, color)
绘制圆形边框。

```lua
display.circle(100, 100, 50, 1)  -- 绘制空心圆
```

#### display.fill_circle(x, y, r, color)
绘制填充圆形。

```lua
display.fill_circle(100, 100, 30, 1)  -- 绘制实心圆
```

#### display.text(x, y, text, size)
显示文本（仅支持ASCII）。

```lua
display.text(10, 50, "Hello", 2)     -- 小字
display.text(10, 100, "World", 3)    -- 中字
display.text(10, 150, "!", 4)        -- 大字
```

**参数**:
- `x, y`: 文本起始坐标
- `text`: 字符串内容（仅ASCII字符）
- `size`: 字体大小 (1-4)
  - 1: 6×8 像素
  - 2: 12×16 像素
  - 3: 18×24 像素
  - 4: 24×32 像素

#### display.show()
将缓冲区内容刷新到墨水屏。

```lua
display.show()  -- 执行刷新，约12秒
```

**重要**: 
- 调用此函数后才能看到显示效果
- 刷新约需12秒
- 建议刷新间隔≥30秒

### 2. time - 时间管理

#### time.get()
获取当前时间。

```lua
local hour, minute, second = time.get()
print(string.format("%02d:%02d:%02d", hour, minute, second))
```

**返回**: `hour` (0-23), `minute` (0-59), `second` (0-59)

#### time.delay(ms)
延迟指定毫秒数。

```lua
time.delay(1000)  -- 延迟1秒
```

**参数**: `ms` - 毫秒数

**注意**: 
- 不要在 `loop()` 中使用过长延迟
- 建议单次延迟 ≤ 5000ms

#### time.millis()
获取系统运行时间（毫秒）。

```lua
local start = time.millis()
-- 执行某些操作
local elapsed = time.millis() - start
print("耗时: " .. elapsed .. "ms")
```

### 3. 标准 Lua 库

#### math 库
```lua
local angle = math.rad(45)      -- 角度转弧度
local x = math.sin(angle)       -- 正弦
local y = math.cos(angle)       -- 余弦
local r = math.sqrt(x*x + y*y)  -- 平方根
local n = math.random(1, 100)   -- 随机数 [1, 100]
local pi = math.pi              -- 圆周率
local abs = math.abs(-5)        -- 绝对值
```

#### string 库
```lua
local s = string.format("%02d:%02d", 9, 5)  -- "09:05"
local len = string.len("Hello")             -- 5
local upper = string.upper("hello")         -- "HELLO"
local lower = string.lower("HELLO")         -- "hello"
local sub = string.sub("Hello", 1, 3)       -- "Hel"
```

#### table 库
```lua
local t = {1, 2, 3}
table.insert(t, 4)        -- {1, 2, 3, 4}
local v = table.remove(t) -- v=4, t={1, 2, 3}
table.sort(t)             -- 排序
```

### 4. print - 调试输出

输出到串口（用于调试）。

```lua
print("调试信息")
print("值:", 42)
print("坐标:", x, y)
```

---

## 模块开发模式

### 模式1: 静态显示
适用于不需要频繁更新的内容。

```lua
-- @name: 静态Logo
-- @version: 1.0.0
-- @description: 显示静态图案
-- @id: logo-static

function setup()
    display.clear()
    
    -- 绘制静态内容
    display.text(50, 80, "MiniEink", 3)
    display.rect(10, 10, 180, 180, 1)
    
    display.show()
end

function loop()
    -- 保持休眠，节省电量
    time.delay(60000)  -- 1分钟
end
```

**特点**:
- 仅在 `setup()` 中绘制一次
- `loop()` 保持长延迟
- 适合：Logo、标语、固定图案

### 模式2: 定时刷新
适用于需要周期性更新的内容。

```lua
-- @name: 定时时钟
-- @version: 1.0.0
-- @description: 每分钟更新时间
-- @id: clock-timer

local last_update = 0
local update_interval = 60000  -- 60秒

function setup()
    display.clear()
    display.show()
end

function loop()
    local now = time.millis()
    
    if now - last_update >= update_interval then
        last_update = now
        
        -- 更新显示
        display.clear()
        local h, m, s = time.get()
        display.text(50, 90, string.format("%02d:%02d", h, m), 3)
        display.show()
    end
    
    time.delay(1000)  -- 每秒检查一次
end
```

**特点**:
- 使用 `time.millis()` 计时
- 到达间隔才刷新
- 适合：时钟、倒计时

### 模式3: 条件触发
适用于状态变化时更新。

```lua
-- @name: 变化时钟
-- @version: 1.0.0
-- @description: 分钟变化时更新
-- @id: clock-change

local last_minute = -1

function setup()
    display.clear()
    display.show()
end

function loop()
    local h, m, s = time.get()
    
    -- 仅当分钟变化时更新
    if m ~= last_minute then
        last_minute = m
        
        display.clear()
        display.text(50, 90, string.format("%02d:%02d", h, m), 3)
        display.show()
    end
    
    time.delay(1000)
end
```

**特点**:
- 检测状态变化
- 仅在需要时刷新
- 适合：时钟、日期显示

### 模式4: 多页轮播
适用于需要显示多屏内容。

```lua
-- @name: 信息轮播
-- @version: 1.0.0
-- @description: 多页内容轮播
-- @id: info-carousel

local page = 0
local pages = 3
local page_interval = 30000  -- 30秒切换
local last_switch = 0

function setup()
    show_page(0)
end

function loop()
    local now = time.millis()
    
    if now - last_switch >= page_interval then
        page = (page + 1) % pages
        show_page(page)
        last_switch = now
    end
    
    time.delay(1000)
end

function show_page(p)
    display.clear()
    
    if p == 0 then
        display.text(60, 90, "Page 1", 3)
    elseif p == 1 then
        display.text(60, 90, "Page 2", 3)
    else
        display.text(60, 90, "Page 3", 3)
    end
    
    display.show()
end
```

---

## 最佳实践

### 1. 优化刷新策略
```lua
-- ✅ 好：按需刷新
if content_changed then
    display.clear()
    -- 绘制内容
    display.show()
end

-- ❌ 差：无条件刷新
display.clear()
-- 绘制内容
display.show()  -- 每次loop都刷新
```

### 2. 使用局部变量
```lua
-- ✅ 好：缓存重复计算
local center_x = WIDTH / 2
local center_y = HEIGHT / 2

for i = 1, 10 do
    display.circle(center_x, center_y, i * 10, 1)
end

-- ❌ 差：重复计算
for i = 1, 10 do
    display.circle(WIDTH / 2, HEIGHT / 2, i * 10, 1)
end
```

### 3. 合理使用延迟
```lua
-- ✅ 好：根据刷新间隔调整
function loop()
    if should_update() then
        update_display()
    end
    time.delay(1000)  -- 每秒检查
end

-- ❌ 差：无延迟
function loop()
    update_display()  -- 无延迟，刷新过频
end
```

### 4. 文本居中显示
```lua
function text_centered(y, text, size)
    -- 估算文本宽度（6像素 * size * 字符数）
    local char_width = 6 * size
    local text_width = string.len(text) * char_width
    local x = (WIDTH - text_width) / 2
    display.text(x, y, text, size)
end

-- 使用
text_centered(90, "Hello", 3)
```

### 5. 使用配置默认值
```lua
-- ✅ 好：提供默认值
local interval = CONFIG.refresh_interval or 60
local format = CONFIG.time_format or "24h"

-- ❌ 差：直接使用可能为nil
local interval = CONFIG.refresh_interval  -- 可能为nil
```

### 6. 避免频繁创建表
```lua
-- ✅ 好：复用表
local buffer = {}

function loop()
    -- 清空复用
    for k in pairs(buffer) do buffer[k] = nil end
    -- 使用buffer
end

-- ❌ 差：每次创建新表
function loop()
    local buffer = {}  -- 每次分配新内存
end
```

### 7. 错误处理
```lua
-- ✅ 好：保护关键代码
function loop()
    local success, err = pcall(function()
        update_display()
    end)
    
    if not success then
        print("错误:", err)
        display.clear()
        display.text(10, 90, "Error", 2)
        display.show()
    end
    
    time.delay(1000)
end
```

---

## 示例模块

### 示例1: 大字时钟

```lua
-- @name: 大字时钟
-- @version: 1.0.0
-- @author: MiniEink
-- @description: 全屏显示时间
-- @id: clock-large

local last_minute = -1

function setup()
    display.clear()
    display.show()
end

function loop()
    local h, m, s = time.get()
    
    if m ~= last_minute then
        last_minute = m
        
        -- 读取配置
        local format = CONFIG.time_format or "24h"
        
        -- 12小时制转换
        if format == "12h" then
            h = h % 12
            if h == 0 then h = 12 end
        end
        
        display.clear()
        
        -- 居中显示时间
        local time_str = string.format("%02d:%02d", h, m)
        display.text(40, 85, time_str, 4)  -- 大字
        
        display.show()
    end
    
    time.delay(1000)
end
```

**配置文件** `clock-large.cfg`:
```json
[
  {
    "key": "time_format",
    "type": "select",
    "label": "时间格式",
    "options": [
      {"label": "24小时", "value": "24h"},
      {"label": "12小时", "value": "12h"}
    ],
    "default": "24h",
    "desc": "选择时间显示格式"
  }
]
```

### 示例2: 简单计数器

```lua
-- @name: 计数器
-- @version: 1.0.0
-- @author: MiniEink
-- @description: 每秒递增计数器
-- @id: counter-simple

local counter = 0
local last_update = 0

function setup()
    counter = CONFIG.start or 0
    display.clear()
    display.show()
end

function loop()
    local interval = (CONFIG.interval or 1) * 1000
    local now = time.millis()
    
    if now - last_update >= interval then
        last_update = now
        counter = counter + 1
        
        display.clear()
        display.text(70, 85, tostring(counter), 4)
        display.show()
    end
    
    time.delay(100)
end
```

**配置文件** `counter-simple.cfg`:
```json
[
  {
    "key": "start",
    "type": "slider",
    "label": "起始值",
    "min": 0,
    "max": 1000,
    "step": 1,
    "default": 0,
    "desc": "计数器起始值"
  },
  {
    "key": "interval",
    "type": "slider",
    "label": "间隔(秒)",
    "min": 1,
    "max": 60,
    "step": 1,
    "default": 1,
    "desc": "递增间隔"
  }
]
```

### 示例3: 状态指示器

```lua
-- @name: 运行时间
-- @version: 1.0.0
-- @author: MiniEink
-- @description: 显示系统运行时间
-- @id: uptime-display

function setup()
    display.clear()
    display.show()
end

function loop()
    local uptime = time.millis() / 1000  -- 转为秒
    local minutes = math.floor(uptime / 60)
    local seconds = math.floor(uptime % 60)
    
    display.clear()
    
    -- 标题
    display.text(50, 60, "Uptime", 2)
    
    -- 时间
    local time_str = string.format("%d:%02d", minutes, seconds)
    display.text(50, 100, time_str, 3)
    
    display.show()
    
    time.delay(1000)
end
```

---

## 常见问题

### 模块无法加载

**症状**: 小程序中看不到模块

**排查**:
1. 检查文件头元数据格式：
   - 必须使用 `-- @key: value` 格式
   - `@name:`, `@version:`, `@id:` 必须存在
2. 确认文件名和 `@id` 一致，都使用连字符
   - ✅ `clock-simple.lua` + `@id: clock-simple`
   - ❌ `clock_simple.lua` + `@id: clock_simple`
3. 检查 Lua 语法错误
   - 使用 `print()` 在 `setup()` 中输出调试信息
   - 查看串口输出

### 配置不生效

**症状**: 修改配置后模块行为不变

**排查**:
1. 确认配置文件已上传到设备
2. 检查配置文件名与 Lua 文件名一致
3. 验证 JSON 格式正确（使用 JSON 校验工具）
4. 确认 Lua 中正确读取 `CONFIG` 表：
   ```lua
   local value = CONFIG.key_name or default_value
   ```
5. 对于布尔值，使用正确的读取方式：
   ```lua
   local flag = false
   if CONFIG.flag ~= nil then
       flag = CONFIG.flag
   end
   ```

### 显示不更新

**症状**: 调用了绘图函数但屏幕没变化

**排查**:
1. 确认调用了 `display.show()`
   ```lua
   display.clear()
   display.text(50, 90, "Hello", 3)
   display.show()  -- 必须调用
   ```
2. 检查坐标是否在范围内 (0-199)
3. 验证颜色值 (0=黑, 1=白, 2=黄, 3=红)
4. 确认在 `display.clear()` 后重新绘制了内容

### 刷新太频繁

**症状**: 屏幕频繁刷新，设备响应慢

**原因**: `loop()` 中每次都调用 `display.show()`

**解决**:
```lua
-- ❌ 错误：每次loop都刷新
function loop()
    display.clear()
    display.text(50, 90, "Hello", 3)
    display.show()  -- 每次都刷新
    time.delay(100)
end

-- ✅ 正确：添加条件判断
local last_update = 0
local interval = 60000  -- 60秒

function loop()
    local now = time.millis()
    
    if now - last_update >= interval then
        last_update = now
        display.clear()
        display.text(50, 90, "Hello", 3)
        display.show()
    end
    
    time.delay(1000)
end
```

### 性能问题

**症状**: 模块运行缓慢或卡顿

**优化方法**:
1. 减少刷新频率（建议≥30秒）
2. 避免在 `loop()` 中做复杂计算
3. 使用局部变量缓存结果
4. 增加 `time.delay()` 延迟
5. 避免创建大量表

---

## API 快速参考

### 显示 API

| 函数 | 参数 | 说明 |
|------|------|------|
| `display.clear()` | - | 清空屏幕 |
| `display.pixel(x, y, c)` | x, y, color | 绘制像素(0=黑,1=白,2=黄,3=红) |
| `display.line(x0, y0, x1, y1, c)` | 起点, 终点, color | 绘制直线 |
| `display.rect(x, y, w, h, c)` | 位置, 尺寸, color | 绘制矩形框 |
| `display.fill_rect(x, y, w, h, c)` | 位置, 尺寸, color | 填充矩形 |
| `display.circle(x, y, r, c)` | 圆心, 半径, color | 绘制圆形框 |
| `display.fill_circle(x, y, r, c)` | 圆心, 半径, color | 填充圆形 |
| `display.text(x, y, str, size)` | 位置, 文本, 大小 | 显示文本(ASCII) |
| `display.show()` | - | 刷新显示(约12秒) |

### 时间 API

| 函数 | 返回值 | 说明 |
|------|--------|------|
| `time.get()` | hour, minute, second | 获取当前时间 |
| `time.millis()` | 毫秒数 | 系统运行时间 |
| `time.delay(ms)` | - | 延迟毫秒 |

### 配置 API

| 变量 | 类型 | 说明 |
|------|------|------|
| `CONFIG.key` | any | 读取配置值 |
| `WIDTH` | number | 屏幕宽度 (200) |
| `HEIGHT` | number | 屏幕高度 (200) |

### 标准库

| 库 | 常用函数 |
|----|----|
| `math` | `sin`, `cos`, `sqrt`, `abs`, `floor`, `ceil`, `random`, `pi` |
| `string` | `format`, `len`, `upper`, `lower`, `sub` |
| `table` | `insert`, `remove`, `sort` |
| `print` | 调试输出到串口 |

---

## 开发检查清单

### 模块创建
- [ ] 文件名使用连字符 `-`，不使用下划线 `_`
- [ ] 文件头包含完整元数据 (`@name:`, `@version:`, `@id:`, `@description:`)
- [ ] `@id` 与文件名一致
- [ ] `@description` 不超过 128 字节（约42个中文字符）

### 代码质量
- [ ] 实现了 `loop()` 函数
- [ ] 合理使用 `time.delay()` 避免空转
- [ ] 使用局部变量而非全局变量
- [ ] 为配置项提供默认值
- [ ] 添加了必要的错误处理

### 显示优化
- [ ] 避免频繁刷新（建议≥30秒）
- [ ] 每次刷新前调用 `display.clear()`
- [ ] 仅在内容变化时刷新
- [ ] 坐标在有效范围内 (0-199)

### 配置文件
- [ ] 配置文件名与 Lua 文件名一致
- [ ] JSON 格式正确
- [ ] 配置项有合理的默认值
- [ ] 配置说明清晰（`desc` 字段）

### 测试
- [ ] 在设备上测试模块加载
- [ ] 验证配置修改生效
- [ ] 检查显示是否正常
- [ ] 确认无性能问题
- [ ] 测试长时间运行稳定性

---

## 故障排查

### 调试技巧

1. **使用 print() 输出**
   ```lua
   function setup()
       print("模块启动")
       print("配置值:", CONFIG.key)
   end
   
   function loop()
       local h, m, s = time.get()
       print("时间:", h, m, s)
       time.delay(1000)
   end
   ```

2. **检查配置加载**
   ```lua
   function setup()
       for k, v in pairs(CONFIG) do
           print("配置:", k, "=", v)
       end
   end
   ```

3. **显示错误信息**
   ```lua
   function loop()
       local success, err = pcall(function()
           -- 你的代码
       end)
       
       if not success then
           print("错误:", err)
           display.clear()
           display.text(10, 90, "Error", 2)
           display.show()
       end
   end
   ```

---

**版本**: 1.0.0  
**最后更新**: 2025-01-XX  
**适用设备**: MiniEink (ESP32-C3 + 200×200 墨水屏)  
**基于项目**: Bottle Lua Framework