#include "common.h"   // INK6 宏在此定义，eink6.cpp 需先包含才能正确条件编译

#ifdef INK6

#include <Arduino.h>
#include <SPI.h>
#include "eink6.h"
#include "GUI_Paint.h"
#include "epd_async.h"

// 6 色画布：240x240，4bpp 打包（2 像素/字节），供 GUI_Paint 绘制后整帧送屏
unsigned char BlackImage[ALLSCREEN_BYTES];


// Pin assignment - based on board selection
// 默认值 = 当前硬件接线：这些全局在 initPins() 之前（如开机释放引脚保持）也要可用
static int PIN_EPD_MOSI = 7;
static int PIN_EPD_CLK = 6;
static int PIN_EPD_BUSY = 10;
static int PIN_EPD_DC = 4;
static int PIN_EPD_CS = 5;
static int PIN_EPD_RST = 3;

void initPins() {

    // 新硬件 6 色屏引脚定义（ESP32-C3）
    PIN_EPD_MOSI = 7;
    PIN_EPD_CLK = 6;
    PIN_EPD_BUSY = 10;
    PIN_EPD_DC = 4;
    PIN_EPD_CS = 5;
    PIN_EPD_RST = 3;
    Serial.println("[BOARD] EPD pins: MOSI=7 CLK=6 BUSY=10 DC=4 CS=5 RST=3");

}

// Set false to run normal panel flow and draw color bars.
static const bool SAFE_DIAG_MODE = false;

// Most EPDs use BUSY low-active (LOW=busy, HIGH=idle). Some panels are opposite.
static bool gBusyActiveLevelLow = true;
static bool gIgnoreBusy = false;

// false: normal init, true: apply factory water-ripple init sequence.
static const bool USE_WATER_RIPPLE_INIT = false;

enum WaterRippleProfile : uint8_t {
    WATER_RIPPLE_FACTORY = 0,
    WATER_RIPPLE_NEW = 1,
};

// Select which water-ripple initialization profile to use.
static const WaterRippleProfile ACTIVE_WATER_RIPPLE_PROFILE = WATER_RIPPLE_NEW;

static const uint32_t BUSY_TIMEOUT_INIT_MS = 10000;
static const uint32_t BUSY_TIMEOUT_POWER_MS = 10000;
static const uint32_t BUSY_TIMEOUT_REFRESH_MS = 40000;
// 断电/深睡阶段的等待上限：面板已在睡眠或停在非初始化态时，BUSY 可能一直是"忙"
// 电平（四色板实测唤醒后 BUSY 恒为 0），这里取短值兜底，避免每次补断电白等
static const uint32_t POWEROFF_WAIT_MS = 2000;

// ====================== 异步刷屏钩子（见 epd_async.h）======================

// 面板刷新是否已结束（BUSY 回到空闲电平）
bool epdPanelIsIdle(void) {
  if (gIgnoreBusy) {
    return true;
  }
  const int busyLevel = gBusyActiveLevelLow ? LOW : HIGH;
  return digitalRead(PIN_EPD_BUSY) != busyLevel;
}

// 刷新结束：关闭面板电源并让面板进入深度休眠
void epdPanelPowerOff(void) {
  Serial.println("[EPD] stage: power off + panel deep sleep");
  // 刷新结束的等待已由 epdAsyncPowerOffNow()/epdAsyncMaintain() 处理，这里只断电
  epdWriteCommand(0x02);  // Power OFF
  epdWriteData(0x00);
  epdWaitBusyStage("Power OFF", POWEROFF_WAIT_MS);
  delay(20);
  epdEnterDeepSleep();
  Serial.println("[EPD] panel powered off and in deep sleep");
}

int epdPanelBusyRaw(void) {
  return digitalRead(PIN_EPD_BUSY);
}

void epdPanelHoldPins(bool hold) {
  if (hold) {
    // 休眠前把控制脚拉到空闲电平再保持
    digitalWrite(PIN_EPD_CS, HIGH);
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_RST, HIGH);
  }
  const int pins[] = {
    PIN_EPD_RST, PIN_EPD_DC, PIN_EPD_CS, PIN_EPD_CLK, PIN_EPD_MOSI
  };
  epdAsyncHoldPinsImpl(pins, sizeof(pins) / sizeof(pins[0]), hold);
}

static inline void epdSelect() {
    digitalWrite(PIN_EPD_CS, LOW);
}

static inline void epdDeselect() {
    digitalWrite(PIN_EPD_CS, HIGH);
}

void diagPrintBusyWithPullModes() {
    pinMode(PIN_EPD_BUSY, INPUT);
    delay(3);
    int vInput = digitalRead(PIN_EPD_BUSY);

    pinMode(PIN_EPD_BUSY, INPUT_PULLUP);
    delay(3);
    int vPullup = digitalRead(PIN_EPD_BUSY);

    pinMode(PIN_EPD_BUSY, INPUT_PULLDOWN);
    delay(3);
    int vPulldown = digitalRead(PIN_EPD_BUSY);

    Serial.printf("[EPD] BUSY sample: IN=%d PU=%d PD=%d\n", vInput, vPullup, vPulldown);
}

void epdWriteCommand(uint8_t cmd) {
    epdSelect();
    digitalWrite(PIN_EPD_DC, LOW);
    SPI.transfer(cmd);
    epdDeselect();
}

void epdWriteData(uint8_t data) {
    epdSelect();
    digitalWrite(PIN_EPD_DC, HIGH);
    SPI.transfer(data);
    epdDeselect();
}

// 调试命令 "dis <str>"：将字符串绘制到 6 色画布并整屏显示
int gui_drawtext(const char* str) {
    Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, NIBBLE_WHITE);
    Paint_SetScale(7);
    Paint_SelectImage(BlackImage);
    Paint_Clear(NIBBLE_WHITE);
    Paint_DrawString_EN(0, 0, str, &Font24, NIBBLE_BLACK, NIBBLE_WHITE);
    epdDisplayImage(BlackImage, ALLSCREEN_BYTES);
    return 0;
}

bool epdWaitBusy(uint32_t timeoutMs = 15000) {
    if (gIgnoreBusy) {
        return true;
    }

    uint32_t start = millis();
    const int busyLevel = gBusyActiveLevelLow ? LOW : HIGH;
    while (digitalRead(PIN_EPD_BUSY) == busyLevel) {
        // 省电：轮询间隔须 ≥ tickless idle 入睡阈值（8ms @ 1000Hz），
        // 否则等待 BUSY 期间 CPU 一直活跃（约 15mA），无法进入 light sleep
        delay(20);
        if (millis() - start > timeoutMs) {
            Serial.printf("[EPD] wait busy timeout, pin=%d, activeLow=%d\n",
                          digitalRead(PIN_EPD_BUSY), gBusyActiveLevelLow ? 1 : 0);
            return false;
        }
    }
    return true;
}

bool epdWaitBusyStage(const char* stage, uint32_t timeoutMs) {
    bool ok = epdWaitBusy(timeoutMs);
    if (!ok) {
        Serial.printf("[EPD] stage timeout: %s\n", stage);
    }
    return ok;
}

void epdReset() {
    digitalWrite(PIN_EPD_RST, LOW);
    delay(20);
    digitalWrite(PIN_EPD_RST, HIGH);
    delay(20);
    epdWaitBusy(BUSY_TIMEOUT_INIT_MS);
    delay(10);
}

void epdInitJD7601() {
    epdWriteCommand(0xE9);
    epdWriteData(0x01);
}

void epdInitJD7601WaterRipple() {
    // Base init first.
    epdInitJD7601();

    // Enter test command mode.
    epdWriteCommand(0xFF);
    epdWriteData(0xA5);

    epdWriteCommand(0xEB);  // PWM AC
    epdWriteData(0x01);

    epdWriteCommand(0xDA);  // IBDC
    epdWriteData(0x04);

    epdWriteCommand(0xB4);  // Dither
    epdWriteData(ACTIVE_WATER_RIPPLE_PROFILE == WATER_RIPPLE_NEW ? 0x31 : 0x11);

    epdWriteCommand(0xEF);  // PWM
    if (ACTIVE_WATER_RIPPLE_PROFILE == WATER_RIPPLE_NEW) {
        // New profile: softer multi-stage PWM to reduce abrupt transitions.
        epdWriteData(3);   // H0
        epdWriteData(80);  // L0
        epdWriteData(6);   // H1
        epdWriteData(45);  // L1
        epdWriteData(10);  // H2
        epdWriteData(36);  // L2
        epdWriteData(14);  // H3
        epdWriteData(8);   // L3
        epdWriteData(18);  // H4
        epdWriteData(4);   // L4
    } else {
        epdWriteData(2);    // H0
        epdWriteData(100);  // L0
        epdWriteData(5);    // H1
        epdWriteData(50);   // L1
        epdWriteData(9);    // H2
        epdWriteData(60);   // L2
        epdWriteData(15);   // H3
        epdWriteData(3);    // L3
        epdWriteData(15);   // H4
        epdWriteData(5);    // L4
    }

    epdWriteCommand(0xDC);  // CPCK SET
    epdWriteData(0x01);     // CPCKEN

    epdWriteCommand(0xDD);  // CPCK
    if (ACTIVE_WATER_RIPPLE_PROFILE == WATER_RIPPLE_NEW) {
        epdWriteData(6);
        epdWriteData(18);
    } else {
        epdWriteData(5);
        epdWriteData(20);
    }

    epdWriteCommand(0xDE);  // CPCK OFT
    if (ACTIVE_WATER_RIPPLE_PROFILE == WATER_RIPPLE_NEW) {
        epdWriteData(4);
        epdWriteData(10);
        epdWriteData(16);
        epdWriteData(22);
        epdWriteData(28);
    } else {
        epdWriteData(6);
        epdWriteData(12);
        epdWriteData(18);
        epdWriteData(24);
        epdWriteData(30);
    }

    // Exit test command mode.
    epdWriteCommand(0xFF);
    epdWriteData(0xE3);
}

void epdInitJD7601WaterRipple_v2() {
    epdInitJD7601();  // 0xE9 基础

    epdWriteCommand(0xFF);
    epdWriteData(0xA5);  // 测试模式

    epdWriteCommand(0xEB);
    epdWriteData(0x01);

    epdWriteCommand(0xDA);
    epdWriteData(0x04);

    // ★ 重点试验：改这个值
    epdWriteCommand(0xB4);
    epdWriteData(0x31);  // 原来是0x31，试 0x51/0x61/0x71
    // epdWriteData(0x51);  // 原来是0x31，试 0x51/0x61/0x71
    // epdWriteData(0x61);  // 原来是0x31，试 0x51/0x61/0x71
    // epdWriteData(0x71);  // 原来是0x31，试 0x51/0x61/0x71

    // ★ 新增：波形模式
    epdWriteCommand(0xB0);
    // epdWriteData(0x03);  // 逐个试 0x00~0x04
    // epdWriteData(0x00);  // 逐个试 0x00~0x04
    // epdWriteData(0x01);  // 逐个试 0x00~0x04
    // epdWriteData(0x02);  // 逐个试 0x00~0x04
    epdWriteData(0x04);  // 逐个试 0x00~0x04

    // ★ 新增：扩散中心（针对240x240，中心在0x78行）
    epdWriteCommand(0xC0);
    epdWriteData(0x78);

    epdWriteCommand(0xEF);
    epdWriteData(3);
    epdWriteData(80);
    epdWriteData(6);
    epdWriteData(45);
    epdWriteData(10);
    epdWriteData(36);
    epdWriteData(14);
    epdWriteData(8);
    epdWriteData(18);
    epdWriteData(4);

    epdWriteCommand(0xDC);
    epdWriteData(0x01);

    epdWriteCommand(0xDD);
    epdWriteData(6);
    epdWriteData(18);

    epdWriteCommand(0xDE);
    epdWriteData(4);
    epdWriteData(10);
    epdWriteData(16);
    epdWriteData(22);
    epdWriteData(28);

    epdWriteCommand(0xFF);
    epdWriteData(0xE3);  // 退出测试模式
}




void epdEnterDeepSleep() {
    epdWriteCommand(0x07);
    epdWriteData(0xA5);
}

// ====================== 自定义波形加载函数 ======================
// 已移除 - waveform.h 不再使用



void epdDisplaySolid(EpdColorByte color) {
    epdReset();
    epdInitJD7601();
    
    Serial.println("[EPD] stage: write frame");
    epdWriteCommand(0x10);  // DTM1 Write
    epdWaitBusyStage("DTM1", BUSY_TIMEOUT_INIT_MS);

    for (uint32_t i = 0; i < EPD_FRAME_BYTES; ++i) {
        epdWriteData(static_cast<uint8_t>(color));
    }

    Serial.println("[EPD] stage: power on");
    epdWriteCommand(0x04);  // Power ON
    epdWaitBusyStage("Power ON", BUSY_TIMEOUT_POWER_MS);
    delay(10);

    Serial.println("[EPD] stage: refresh");
    epdWriteCommand(0x12);  // Display Refresh
    epdWriteData(0x00);
    delay(10);
    epdWaitBusyStage("Display Refresh", BUSY_TIMEOUT_REFRESH_MS);

    Serial.println("[EPD] stage: power off");
    epdWriteCommand(0x02);  // Power OFF
    epdWriteData(0x00);
    epdWaitBusyStage("Power OFF", BUSY_TIMEOUT_POWER_MS);
    delay(20);
}

void epdDisplayColorBars() {
    static const EpdColorByte bars[6] = {
        COLOR_RED,
        COLOR_YELLOW,
        COLOR_BLUE,
        COLOR_BLACK,
        COLOR_WHITE,
        COLOR_GREEN,
    };

    const uint16_t bytesPerLine = EPD_WIDTH / 2;
    const uint16_t bandHeight = EPD_HEIGHT / 6; // still valid for 6 color bars

    Serial.println("[EPD] stage: write frame (color bars)");
    epdWriteCommand(0x10);  // DTM1 Write
    epdWaitBusyStage("DTM1", BUSY_TIMEOUT_INIT_MS);

    for (uint16_t y = 0; y < EPD_HEIGHT; ++y) {
        uint8_t band = y / bandHeight;
        if (band > 5) {
            band = 5;
        }
        uint8_t px = static_cast<uint8_t>(bars[band]);
        for (uint16_t x = 0; x < bytesPerLine; ++x) {
            epdWriteData(px);
        }
    }

    Serial.println("[EPD] stage: power on");
    epdWriteCommand(0x04);  // Power ON
    epdWaitBusyStage("Power ON", BUSY_TIMEOUT_POWER_MS);
    delay(10);

    Serial.println("[EPD] stage: refresh");
    epdWriteCommand(0x12);  // Display Refresh
    epdWriteData(0x00);
    delay(10);
    epdWaitBusyStage("Display Refresh", BUSY_TIMEOUT_REFRESH_MS);

    Serial.println("[EPD] stage: power off");
    epdWriteCommand(0x02);  // Power OFF
    epdWriteData(0x00);
    epdWaitBusyStage("Power OFF", BUSY_TIMEOUT_POWER_MS);
    delay(20);
}


void epdDisplayImage(const unsigned char* imgData, uint32_t dataLen) {
    Serial.println("[EPD] stage: write frame (image)");

    // 同步点：上一帧异步刷新若仍在进行，等待其完成再开始新绘制，避免打断刷新
    epdAsyncWaitPrevious();

    epdReset();
    epdInitJD7601();

    epdWriteCommand(0x10);  // DTM1 Write
    epdWaitBusyStage("DTM1", BUSY_TIMEOUT_INIT_MS);

    // Ensure we write exactly the expected frame size. If image data length
    // doesn't match EPD_FRAME_BYTES, truncate or pad with white.
    if (dataLen != EPD_FRAME_BYTES) {
        Serial.printf("[EPD] warning: image length mismatch: dataLen=%u expected=%u\n", dataLen, (unsigned)EPD_FRAME_BYTES);
    }

    for (uint32_t i = 0; i < EPD_FRAME_BYTES; ++i) {
        uint8_t b = 0x11; // default: white
        if (i < dataLen) {
            b = pgm_read_byte(&imgData[i]);  // 从 Flash 读取
        }
        epdWriteData(b);
    }

    Serial.println("[EPD] stage: power on");
    epdWriteCommand(0x04);
    epdWaitBusyStage("Power ON", BUSY_TIMEOUT_POWER_MS);
    delay(10);

    Serial.println("[EPD] stage: refresh");
    epdWriteCommand(0x12);
    epdWriteData(0x00);
    delay(10);

    // 系统级异步：发完刷新命令立即返回，MCU 可直接进入深度休眠，
    // 面板断电由 epdAsyncPowerOffNow()（定时唤醒）或 epdAsyncMaintain()（未休眠）完成
    epdAsyncMarkStarted();
}

// ====================== 显示函数结束 ======================

// 总线与引脚初始化（不触碰面板，异步补断电唤醒时也需要先建立 SPI 才能发命令）
static void epdInitBus() {
    initPins();

    pinMode(PIN_EPD_BUSY, INPUT);
    pinMode(PIN_EPD_DC, OUTPUT);
    pinMode(PIN_EPD_CS, OUTPUT);
    pinMode(PIN_EPD_RST, OUTPUT);

    digitalWrite(PIN_EPD_CS, HIGH);
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_RST, HIGH);

    // Hardware SPI 初始化
    SPI.begin(PIN_EPD_CLK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

    Serial.printf("[EPD] boot, BUSY pin now=%d\n", digitalRead(PIN_EPD_BUSY));
}

int init_eink6(){
    epdInitBus();

    // 重置并初始化
    epdReset();
    
    // if (USE_WATER_RIPPLE_INIT) {
    //     Serial.println("[EPD] init: water-ripple mode");
    //     epdInitJD7601WaterRipple_v2();
    // } else {
    //     Serial.println("[EPD] init: normal mode");
    //     epdInitJD7601();
    // }
    Serial.println("[EPD] init done");
    
    return 0;
}

// ====================== 统一显示接口（见 eink_display.h）======================

int eink_display_init(void) {
    epdInitBus();
    // 异步刷屏补断电唤醒：面板正在刷新，不能 reset（会打断刷新），
    // 本次唤醒只做总线初始化，随后由 epdAsyncPowerOffNow() 断电
    if (!epdAsyncIsPending()) {
        epdReset();
    }
    return 0;
}

void eink_display_frame(void) {
    epdDisplayImage(BlackImage, ALLSCREEN_BYTES);
}

void eink_display_white(void) {
    epdDisplaySolid(COLOR_WHITE);
}

// void setup() {
//     Serial.begin(115200);
//     delay(300);

//     // 初始化引脚
//     initPins();

//     pinMode(PIN_EPD_BUSY, INPUT);
//     pinMode(PIN_EPD_DC, OUTPUT);
//     pinMode(PIN_EPD_CS, OUTPUT);
//     pinMode(PIN_EPD_RST, OUTPUT);

//     digitalWrite(PIN_EPD_CS, HIGH);
//     digitalWrite(PIN_EPD_DC, HIGH);
//     digitalWrite(PIN_EPD_RST, HIGH);

//     // Hardware SPI 初始化
//     SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0)); 
//     // ESP32-S3: SCK=IO12, MISO unused(-1), MOSI=IO11, SS=IO2
//     SPI.begin(PIN_EPD_CLK, -1, PIN_EPD_MOSI, PIN_EPD_CS);

//     Serial.printf("[EPD] boot, BUSY pin now=%d\n", digitalRead(PIN_EPD_BUSY));

//     // 重置并初始化
//     epdReset();
    
//     if (USE_WATER_RIPPLE_INIT) {
//         Serial.println("[EPD] init: water-ripple mode");
//         epdInitJD7601WaterRipple_v2();
//     } else {
//         Serial.println("[EPD] init: normal mode");
//         epdInitJD7601();
//     }
//     Serial.println("[EPD] init done");

//     while (1) {
//         Serial.println("[EPD] 显示彩条...");
//         epdDisplayColorBars();
//         delay(15000);

//         Serial.println("[EPD] 显示图片...");
//         epdDisplayImage(image_data_sixcolor, sizeof(image_data_sixcolor));
//         delay(15000);

//         Serial.println("[EPD] 刷白屏幕...");
//         epdDisplaySolid(COLOR_WHITE);
//         delay(15000);
//     }

//     // 进入深睡眠
//     epdEnterDeepSleep();
// }

// void loop() {
//     delay(1000);
// }

#endif // INK6
