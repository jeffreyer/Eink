#include "common.h"

// 1.54" 黑白墨水屏驱动（200x200，SSD1681 兼容控制器）
// 移植自 ESP32-C6-ePaper-1.54 例程的 port_display.cpp（协议与波形表原样保留），
// 去掉了 LVGL 相关代码，并改为项目统一的“整帧刷新 + 面板深度休眠”模型。

#ifdef INK_BW

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include "esp_rom_crc.h"
#include "eink_bw.h"
#include "GUI_Paint.h"
#include "epd_async.h"

// 1bpp 画布：1 字节 8 像素，bit=1 为白，与面板 0x24 RAM 的位定义一致
unsigned char BlackImage[ALLSCREEN_BYTES];

// 全刷波形表（159 字节：前 153 字节为 LUT 数据，后 6 字节为配套寄存器值）
static const uint8_t WF_Full_1IN54[159] = {
  0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x80, 0x48, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x40, 0x48, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xA,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x8, 0x1, 0x0, 0x8, 0x1, 0x0, 0x2,
  0xA, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0,
  0x0, 0x0, 0x22, 0x17, 0x41, 0x0, 0x32, 0x20
};

// 局部刷波形表（原厂 WF_PARTIAL_1IN54，同样 159 字节）
static const uint8_t WF_Partial_1IN54[159] = {
  0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x80, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x40, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0xF, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1, 0x1, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0, 0x02, 0x17, 0x41, 0xB0, 0x32, 0x28,
};

// 上一帧缓存：放在 RTC 内存（深度休眠后仍保留、掉电丢失）。
// 局部刷需要"上一帧"作为面板 0x26 的基准图；本项目每次刷完都深度休眠，
// 普通 RAM 会丢，所以放这里（ESP32-C3 RTC 数据区约 8KB，本工程其余只用了几十字节）。
// 用标记 + CRC 校验：掉电/异常复位后 RTC 内存可能丢失或残缺，校验不过就全刷
#define EPD_BW_PREV_MARKER 0xE10D5A17u
RTC_DATA_ATTR static uint8_t s_prev_frame[EPD_FRAME_BYTES];
RTC_DATA_ATTR static uint32_t s_prev_crc = 0;
RTC_DATA_ATTR static uint32_t s_prev_marker = 0;  // 掉电后为 0 → 首帧自动全刷
RTC_DATA_ATTR static uint8_t s_partial_run = 0;   // 连续局部刷次数（到上限强制全刷）

static uint32_t epdBWFrameCrc(const uint8_t* frame) {
  return esp_rom_crc32_le(0, frame, EPD_FRAME_BYTES);
}

// 上一帧基准是否可信（RTC 内存跨深度休眠保留，掉电或残缺时校验不过）
static bool epdBWPrevFrameUsable() {
  return s_prev_marker == EPD_BW_PREV_MARKER && s_prev_crc == epdBWFrameCrc(s_prev_frame);
}

static void epdBWStorePrevFrame(const uint8_t* frame) {
  memcpy(s_prev_frame, frame, EPD_FRAME_BYTES);
  s_prev_crc = epdBWFrameCrc(s_prev_frame);
  s_prev_marker = EPD_BW_PREV_MARKER;
}

// ====================== 底层收发 ======================

static void epdBWWriteCommand(uint8_t cmd) {
  digitalWrite(EPD_BW_DC_PIN, LOW);
  digitalWrite(EPD_BW_CS_PIN, LOW);
  SPI.transfer(cmd);
  digitalWrite(EPD_BW_CS_PIN, HIGH);
}

static void epdBWWriteData(uint8_t data) {
  digitalWrite(EPD_BW_DC_PIN, HIGH);
  digitalWrite(EPD_BW_CS_PIN, LOW);
  SPI.transfer(data);
  digitalWrite(EPD_BW_CS_PIN, HIGH);
}

static void epdBWWriteBytes(const uint8_t* data, size_t len) {
  digitalWrite(EPD_BW_DC_PIN, HIGH);
  digitalWrite(EPD_BW_CS_PIN, LOW);
  SPI.writeBytes(data, len);
  digitalWrite(EPD_BW_CS_PIN, HIGH);
}

// 屏忙检测：BUSY 高电平表示忙，低电平表示空闲。返回是否在超时前空闲
static bool epdBWWaitBusyTimeout(uint32_t timeout_ms) {
  uint32_t start = millis();
  while (digitalRead(EPD_BW_BUSY_PIN) == HIGH) {
    // 轮询间隔 ≥ tickless idle 入睡阈值，等待期间允许 CPU 进入 light sleep
    delay(10);
    if (millis() - start > timeout_ms) {
      Serial.println("[EPD-BW] wait busy timeout");
      return false;
    }
  }
  return true;
}

void epdBWWaitBusy() {
  epdBWWaitBusyTimeout(EPD_BW_BUSY_TIMEOUT_MS);
}

void epdBWReset() {
  digitalWrite(EPD_BW_RST_PIN, HIGH);
  delay(50);
  digitalWrite(EPD_BW_RST_PIN, LOW);
  delay(20);
  digitalWrite(EPD_BW_RST_PIN, HIGH);
  delay(50);
  epdBWWaitBusy();
}

// 只做硬件复位（不等待 BUSY）：补断电前把控制 IC 拉回已知状态用。
// 面板处于睡眠/非初始化态时 BUSY 不可靠，等它就会白等
static void epdBWResetOnly() {
  digitalWrite(EPD_BW_RST_PIN, HIGH);
  delay(20);
  digitalWrite(EPD_BW_RST_PIN, LOW);
  delay(20);
  digitalWrite(EPD_BW_RST_PIN, HIGH);
  delay(50);
}

// ====================== 面板初始化 ======================

static void epdBWSetWindows(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend) {
  epdBWWriteCommand(0x44);  // SET_RAM_X_ADDRESS_START_END_POSITION
  epdBWWriteData((Xstart >> 3) & 0xFF);
  epdBWWriteData((Xend >> 3) & 0xFF);

  epdBWWriteCommand(0x45);  // SET_RAM_Y_ADDRESS_START_END_POSITION
  epdBWWriteData(Ystart & 0xFF);
  epdBWWriteData((Ystart >> 8) & 0xFF);
  epdBWWriteData(Yend & 0xFF);
  epdBWWriteData((Yend >> 8) & 0xFF);
}

static void epdBWSetCursor(uint16_t Xstart, uint16_t Ystart) {
  epdBWWriteCommand(0x4E);  // SET_RAM_X_ADDRESS_COUNTER
  epdBWWriteData(Xstart & 0xFF);

  epdBWWriteCommand(0x4F);  // SET_RAM_Y_ADDRESS_COUNTER
  epdBWWriteData(Ystart & 0xFF);
  epdBWWriteData((Ystart >> 8) & 0xFF);
}

static void epdBWSetLut(const uint8_t* lut) {
  epdBWWriteCommand(0x32);
  epdBWWriteBytes(lut, 153);
  epdBWWaitBusy();

  epdBWWriteCommand(0x3f);
  epdBWWriteData(lut[153]);

  epdBWWriteCommand(0x03);
  epdBWWriteData(lut[154]);

  epdBWWriteCommand(0x04);
  epdBWWriteData(lut[155]);
  epdBWWriteData(lut[156]);
  epdBWWriteData(lut[157]);

  epdBWWriteCommand(0x2c);
  epdBWWriteData(lut[158]);
}

static void epdBWInitPanel() {
  epdBWReset();

  epdBWWriteCommand(0x12);  // SWRESET
  epdBWWaitBusy();

  epdBWWriteCommand(0x01);  // Driver output control
  epdBWWriteData(0xC7);
  epdBWWriteData(0x00);
  epdBWWriteData(0x01);

  epdBWWriteCommand(0x11);  // Data entry mode：Y 递减、X 递增（与波形表配套）
  epdBWWriteData(0x01);

  epdBWSetWindows(0, EPD_HEIGHT - 1, EPD_WIDTH - 1, 0);

  epdBWWriteCommand(0x3C);  // BorderWaveform
  epdBWWriteData(0x01);

  epdBWWriteCommand(0x18);  // 内部温度传感器
  epdBWWriteData(0x80);

  epdBWWriteCommand(0x22);  // Load temperature and waveform setting
  epdBWWriteData(0xB1);
  epdBWWriteCommand(0x20);

  epdBWSetCursor(0, EPD_HEIGHT - 1);
  epdBWWaitBusy();

  epdBWSetLut(WF_Full_1IN54);
}

// 触发一次全屏刷新（发完即返回，不等待 BUSY）
static void epdBWTurnOnDisplay() {
  epdBWWriteCommand(0x22);
  epdBWWriteData(0xC7);
  epdBWWriteCommand(0x20);
}

// 触发一次局部刷新（0xCF：只驱动与 0x26 基准图有差异的像素）
static void epdBWTurnOnDisplayPart() {
  epdBWWriteCommand(0x22);
  epdBWWriteData(0xCF);
  epdBWWriteCommand(0x20);
}

// 切到局部刷新模式：局部波形表 + 显示选项(0x37) + BorderWaveform(0x3C)
// 照搬原厂 EPD_Init_Partial()，只是省掉它前面的复位——本驱动每帧都会整屏初始化
static void epdBWEnterPartialMode() {
  epdBWSetLut(WF_Partial_1IN54);

  epdBWWriteCommand(0x37);  // Display option
  epdBWWriteData(0x00);
  epdBWWriteData(0x00);
  epdBWWriteData(0x00);
  epdBWWriteData(0x00);
  epdBWWriteData(0x00);
  epdBWWriteData(0x40);
  epdBWWriteData(0x00);
  epdBWWriteData(0x00);
  epdBWWriteData(0x00);
  epdBWWriteData(0x00);

  epdBWWriteCommand(0x3C);  // BorderWaveform（局部模式）
  epdBWWriteData(0x80);

  epdBWWriteCommand(0x22);
  epdBWWriteData(0xC0);
  epdBWWriteCommand(0x20);
  epdBWWaitBusy();
}

// 写一帧到指定 RAM 命令（0x24 = 新图 / 0x26 = 基准旧图）；frame 为空时补白
static void epdBWWriteFrameTo(uint8_t cmd, const uint8_t* frame) {
  epdBWWriteCommand(cmd);
  if (frame != nullptr) {
    epdBWWriteBytes(frame, EPD_FRAME_BYTES);
    return;
  }
  for (uint32_t i = 0; i < EPD_FRAME_BYTES; i++) {
    epdBWWriteData(0xFF);
  }
}

// 统计两帧之间变化的像素数（bit 级差异）
static uint32_t epdBWCountChangedPixels(const uint8_t* a, const uint8_t* b) {
  uint32_t changed = 0;
  for (uint32_t i = 0; i < EPD_FRAME_BYTES; i++) {
    changed += (uint32_t)__builtin_popcount((unsigned)(a[i] ^ b[i]));
  }
  return changed;
}

// ====================== 对外接口 ======================

void epdBWEnterDeepSleep() {
  epdBWWriteCommand(0x10);  // Deep sleep mode
  epdBWWriteData(0x01);
}

void epdBWDisplayImage(const unsigned char* imgData, uint32_t dataLen) {
  // 上一帧刷新若仍在进行，先等它结束，避免 reset 打断刷新
  epdAsyncWaitPrevious();

  // 取帧：尺寸不符时按“全部变化”处理（走全刷 + 补白）
  const bool frame_ok = (imgData != nullptr && dataLen == EPD_FRAME_BYTES);
  const uint8_t* frame = frame_ok ? imgData : nullptr;
  if (imgData != nullptr && dataLen != EPD_FRAME_BYTES) {
    Serial.printf("[EPD-BW] warning: frame size mismatch: %u (expected %u)\n",
                  (unsigned)dataLen, (unsigned)EPD_FRAME_BYTES);
  }

  // 能不能局部刷：上一帧可用 + 变化像素不多 + 未到连续局部刷上限
  const uint32_t total_pixels = (uint32_t)EPD_WIDTH * EPD_HEIGHT;
  uint32_t changed_px = total_pixels;
  bool use_partial = false;
  if (frame_ok && epdBWPrevFrameUsable()) {
    changed_px = epdBWCountChangedPixels(frame, s_prev_frame);
    use_partial = (changed_px * 100u <= (uint32_t)EPD_BW_PARTIAL_MAX_DIRTY_PCT * total_pixels) &&
                  (s_partial_run < EPD_BW_PARTIAL_MAX_RUN);
  }

  epdBWInitPanel();

  if (use_partial) {
    // 局部刷：0x24 = 新帧、0x26 = 上一帧（基准），面板只驱动两者不同的像素
    epdBWEnterPartialMode();
    epdBWWriteFrameTo(0x24, frame);
    epdBWWriteFrameTo(0x26, s_prev_frame);
    epdBWTurnOnDisplayPart();
    s_partial_run++;
    Serial.printf("[EPD-BW] partial refresh: %u px changed (%u.%02u%%), run %u/%u\n",
                  (unsigned)changed_px,
                  (unsigned)(changed_px * 100u / total_pixels),
                  (unsigned)((changed_px * 10000u / total_pixels) % 100u),
                  (unsigned)s_partial_run, (unsigned)EPD_BW_PARTIAL_MAX_RUN);
    epdAsyncMarkStartedMs(EPD_BW_PARTIAL_REFRESH_MS);
  } else {
    // 全刷：0x24 与 0x26 都写新帧（把基准图更新为当前画面）
    epdBWWriteFrameTo(0x24, frame);
    epdBWWriteFrameTo(0x26, frame);
    epdBWTurnOnDisplay();
    Serial.printf("[EPD-BW] full refresh: %u px changed (%u.%02u%%), partial_run was %u%s\n",
                  (unsigned)changed_px,
                  (unsigned)(changed_px * 100u / total_pixels),
                  (unsigned)((changed_px * 10000u / total_pixels) % 100u),
                  (unsigned)s_partial_run,
                  frame_ok ? "" : " [bad frame]");
    s_partial_run = 0;
    epdAsyncMarkStartedMs(EPD_BW_FULL_REFRESH_MS);
  }

  // 记录本帧，作为下一轮的局部刷基准（RTC 内存，跨深度休眠保留）
  if (frame_ok) {
    epdBWStorePrevFrame(frame);
  } else {
    s_prev_marker = 0;  // 数据不可信，下一轮强制全刷
  }
}

void epdBWDisplaySolid(uint8_t color) {
  uint8_t fill = (color == BW_BLACK) ? 0x00 : 0xFF;
  memset(BlackImage, fill, ALLSCREEN_BYTES);
  epdBWDisplayImage(BlackImage, ALLSCREEN_BYTES);
}

int init_eink_bw() {
  pinMode(EPD_BW_BUSY_PIN, INPUT);
  pinMode(EPD_BW_DC_PIN, OUTPUT);
  pinMode(EPD_BW_CS_PIN, OUTPUT);
  pinMode(EPD_BW_RST_PIN, OUTPUT);

  digitalWrite(EPD_BW_CS_PIN, HIGH);
  digitalWrite(EPD_BW_DC_PIN, HIGH);
  digitalWrite(EPD_BW_RST_PIN, HIGH);

  SPI.begin(EPD_BW_CLK_PIN, -1, EPD_BW_MOSI_PIN, EPD_BW_CS_PIN);
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  Serial.printf("[EPD-BW] init pins: MOSI=%d CLK=%d BUSY=%d DC=%d CS=%d RST=%d\n",
                EPD_BW_MOSI_PIN, EPD_BW_CLK_PIN, EPD_BW_BUSY_PIN,
                EPD_BW_DC_PIN, EPD_BW_CS_PIN, EPD_BW_RST_PIN);
  return 0;
}

// 调试命令 "dis <str>"：把字符串画到画布并整屏显示
int gui_drawtext(const char* str) {
  Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, WHITE);
  Paint_SetScale(2);
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_DrawString_EN(0, 0, str, &Font24, BLACK, WHITE);
  epdBWDisplayImage(BlackImage, ALLSCREEN_BYTES);
  return 0;
}

// ====================== 统一显示接口（见 eink_display.h）======================

int eink_display_init(void) {
  init_eink_bw();
  // 异步刷屏补断电唤醒：面板正在刷新，不能 reset（会打断刷新），
  // 本次唤醒只做总线初始化，随后由 epdAsyncPowerOffNow() 断电
  if (!epdAsyncIsPending()) {
    epdBWInitPanel();
  }
  return 0;
}

void eink_display_frame(void) {
  epdBWDisplayImage(BlackImage, ALLSCREEN_BYTES);
}

void eink_display_white(void) {
  epdBWDisplaySolid(BW_WHITE);
}

// ====================== 异步刷屏钩子（见 epd_async.h）======================

bool epdPanelIsIdle(void) {
  // BUSY 低 = 空闲，刷新已结束
  return digitalRead(EPD_BW_BUSY_PIN) == LOW;
}

int epdPanelBusyRaw(void) {
  return digitalRead(EPD_BW_BUSY_PIN);
}

void epdPanelHoldPins(bool hold) {
  if (hold) {
    // 休眠前把控制脚拉到空闲电平再保持
    digitalWrite(EPD_BW_CS_PIN, HIGH);
    digitalWrite(EPD_BW_DC_PIN, HIGH);
    digitalWrite(EPD_BW_RST_PIN, HIGH);
  }
  static const int pins[] = {
    EPD_BW_RST_PIN, EPD_BW_DC_PIN, EPD_BW_CS_PIN,
    EPD_BW_CLK_PIN, EPD_BW_MOSI_PIN
  };
  epdAsyncHoldPinsImpl(pins, sizeof(pins) / sizeof(pins[0]), hold);
}

void epdPanelPowerOff(void) {
  // MCU 休眠期间控制 IC 可能停在非初始化态、或刷新还没走完，此时直接发深睡命令
  // (0x10 0x01) 会被忽略，面板模拟电路一直通电（表现为刷完电流偏大）。
  // 先复位把它拉回已知状态（只复位、不等 BUSY），再发深睡命令。
  // 墨水屏双稳态，复位不会影响已显示画面。
  epdBWResetOnly();
  epdBWEnterDeepSleep();
  Serial.println("[EPD-BW] panel reset + deep sleep");
}

#endif  // INK_BW
