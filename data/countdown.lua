-- @name: 纪念日倒计时
-- @version: 1.0.0
-- @author: jeffreyer
-- @description: 纪念日倒计时，可配置显示剩余天数或小时数
-- @id: countdown

-- 屏幕尺寸由设备端注册（4色屏 200x200，6色屏 240x240）

local last_update = 0
local update_interval = 3600000  -- 默认 1 小时刷新一次

-- 公历日期转“自 1970-01-01 起的天数”（纯 Lua 实现，不依赖 os 库）
local function days_from_civil(y, m, d)
    y = y - (m <= 2 and 1 or 0)
    local era = math.floor(y / 400)
    local yoe = y - era * 400
    local mp = (m + 9) % 12
    local doy = math.floor((153 * mp + 2) / 5) + d - 1
    local doe = yoe * 365 + math.floor(yoe / 4) - math.floor(yoe / 100) + doy
    return era * 146097 + doe - 719468
end

local function is_leap(y)
    return (y % 4 == 0 and y % 100 ~= 0) or (y % 400 == 0)
end

local function days_in_month(y, m)
    if m == 2 then
        return is_leap(y) and 29 or 28
    elseif m == 4 or m == 6 or m == 9 or m == 11 then
        return 30
    end
    return 31
end

-- 计算下一次纪念日（repeat_yearly=true 时自动滚动到明年）
-- 返回: target_days, year, month, day
local function next_anniversary(now, ty, tm, td, repeat_yearly)
    if tm < 1 or tm > 12 then tm = 1 end
    if td < 1 or td > 31 then td = 1 end
    if td > days_in_month(ty, tm) then td = days_in_month(ty, tm) end

    local today = days_from_civil(now.year, now.month, now.day)
    local target = days_from_civil(ty, tm, td)

    if repeat_yearly then
        local guard = 0
        while target < today and guard < 5 do
            ty = ty + 1
            if td > days_in_month(ty, tm) then td = days_in_month(ty, tm) end
            target = days_from_civil(ty, tm, td)
            guard = guard + 1
        end
    end

    return target, ty, tm, td
end

-- 按字符数截断（中英文混合，避免截断多字节字符）
local function truncate_chars(s, max_chars)
    local i = 1
    local count = 0
    while i <= #s and count < max_chars do
        local b = string.byte(s, i)
        if b >= 0xE0 then
            i = i + 3
        elseif b >= 0xC0 then
            i = i + 2
        else
            i = i + 1
        end
        count = count + 1
    end
    return string.sub(s, 1, i - 1)
end

-- 估算显示宽度（中文按 16/24 点阵，ASCII 按对应字体），color: 0=黑, 1=白, 2=黄, 3=红
local function text_centered(y, text, size, color)
    local ascii_w = {[1] = 5, [2] = 7, [3] = 11, [4] = 17}
    local cn_w = (size >= 4) and 24 or 16
    local w = 0
    local i = 1
    while i <= #text do
        local b = string.byte(text, i)
        if b < 0x80 then
            w = w + (ascii_w[size] or 7)
            i = i + 1
        else
            w = w + cn_w
            if b >= 0xE0 then
                i = i + 3
            elseif b >= 0xC0 then
                i = i + 2
            else
                i = i + 1
            end
        end
    end
    local x = math.floor((WIDTH - w) / 2)
    if x < 0 then x = 0 end
    display.text(x, y, text, size, color or 0)
end

local function draw_screen()
    display.clear()

    local now = time.now()
    if not now or not now.year or now.year < 2000 then
        -- 时间未校准
        text_centered(80, "TIME NOT SET", 3)
        text_centered(112, "SYNC TIME VIA APP", 2)
        display.show()
        return
    end

    -- 读取配置（含默认值）
    local title = CONFIG.title or "ANNIVERSARY"
    if title == nil or #title == 0 then
        title = "ANNIVERSARY"
    end
    title = truncate_chars(title, 12)

    -- 配置数值统一转为整数，避免浮点误差和小数显示
    local target_year = math.floor(tonumber(CONFIG.target_year) or 2026)
    local target_month = math.floor(tonumber(CONFIG.target_month) or 1)
    local target_day = math.floor(tonumber(CONFIG.target_day) or 1)
    local repeat_yearly = true
    if CONFIG["repeat"] ~= nil then
        repeat_yearly = CONFIG["repeat"]
    end
    local unit = CONFIG.unit or "day"
    local use_hours = (unit == "hour")

    local target, ty, tm, td = next_anniversary(now, target_year, target_month, target_day, repeat_yearly)
    local today = days_from_civil(now.year, now.month, now.day)
    local diff_days = target - today

    if diff_days < 0 then
        diff_days = 0  -- 单次模式且已过期
    end

    local number, unit_text
    if use_hours then
        number = diff_days * 24 - now.hour
        if number < 0 then number = 0 end
        unit_text = "HOURS"
    else
        number = diff_days
        unit_text = "DAYS"
    end

    -- 标题（红）
    text_centered(24, title, 4, 3)
    -- 目标日期
    text_centered(54, string.format("%04d-%02d-%02d", ty, tm, td), 2, 0)
    -- 大数字（黑）
    text_centered(88, string.format("%d", number), 4, 0)
    -- 单位（红）
    text_centered(132, unit_text, 3, 3)
    -- 重复提示
    text_centered(168, repeat_yearly and "EVERY YEAR" or "ONCE", 2, 0)

    display.show()
end

function setup()
    last_update = 0
    -- 按键唤醒时墨水屏仍保留上次画面，跳过重绘避免无谓刷新
    if sys.wake_source() ~= 1 then
        draw_screen()
    end
end

function loop()
    local interval = (math.floor(tonumber(CONFIG.refresh) or 6)) * 1000 * 3600
    if interval < 30000 then interval = 30000 end

    local now_ms = time.millis()
    if now_ms - last_update >= interval then
        last_update = now_ms
        local ok, err = pcall(draw_screen)
        if not ok then
            print("Countdown error:", err)
        end
    end

    time.delay(10)
end

function unload()
    print("Countdown module unloaded")
end
