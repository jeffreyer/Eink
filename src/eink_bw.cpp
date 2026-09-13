#include "common.h"

// 1.54" 黑白墨水屏驱动（200x200，SSD1681 兼容控制器）
// 移植自 ESP32-C6-ePaper-1.54 例程的 port_display.cpp（协议与波形表原样保留），
// 去掉了 LVGL 相关代码，并改为项目统一的“整帧刷新 + 面板深度休眠”模型。

#ifdef INK_BW

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include "eink_bw.h"
#include "GUI_Paint.h"

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

// 屏忙检测：BUSY 高电平表示忙，低电平表示空闲
void epdBWWaitBusy() {
  uint32_t start = millis();
  while (digitalRead(EPD_BW_BUSY_PIN) == HIGH) {
    // 轮询间隔 ≥ tickless idle 入睡阈值，等待期间允许 CPU 进入 light sleep
    delay(10);
    if (millis() - start > EPD_BW_BUSY_TIMEOUT_MS) {
      Serial.println("[EPD-BW] wait busy timeout");
      return;
    }
  }
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

// 触发一次全屏刷新并等待完成
static void epdBWTurnOnDisplay() {
  epdBWWriteCommand(0x22);
  epdBWWriteData(0xC7);
  epdBWWriteCommand(0x20);
  epdBWWaitBusy();
}

// ====================== 对外接口 ======================

void epdBWEnterDeepSleep() {
  epdBWWriteCommand(0x10);  // Deep sleep mode
  epdBWWriteData(0x01);
}

void epdBWDisplayImage(const unsigned char* imgData, uint32_t dataLen) {
  epdBWInitPanel();

  epdBWWriteCommand(0x24);  // Write RAM（B/W）

  if (imgData != nullptr && dataLen == EPD_FRAME_BYTES) {
    epdBWWriteBytes(imgData, EPD_FRAME_BYTES);
  } else {
    if (imgData != nullptr) {
      Serial.printf("[EPD-BW] warning: frame size mismatch: %u (expected %u)\n",
                    (unsigned)dataLen, (unsigned)EPD_FRAME_BYTES);
    }
    for (uint32_t i = 0; i < EPD_FRAME_BYTES; i++) {
      epdBWWriteData(0xFF);  // 缺数据时补白，避免花瓶
    }
  }

  epdBWTurnOnDisplay();
  epdBWEnterDeepSleep();  // 刷新完成立即让面板休眠，降低静态功耗
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

  epdBWInitPanel();
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
  return init_eink_bw();
}

void eink_display_frame(void) {
  epdBWDisplayImage(BlackImage, ALLSCREEN_BYTES);
}

void eink_display_white(void) {
  epdBWDisplaySolid(BW_WHITE);
}

#endif  // INK_BW
