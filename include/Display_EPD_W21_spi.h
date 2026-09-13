#ifndef _DISPLAY_EPD_W21_SPI_
#define _DISPLAY_EPD_W21_SPI_
#include "Arduino.h"

//IO settings
//SCK--GPIO23(SCLK)
//SDIN---GPIO18(MOSI)
//引脚与 MiniEink 硬件一致（与 6 色屏同一套接线）
#define EPD_W21_BUSY_PIN 10
#define EPD_W21_RST_PIN  3
#define EPD_W21_DC_PIN   4
#define EPD_W21_CS_PIN   5
#define EPD_W21_SCK_PIN  6
#define EPD_W21_MOSI_PIN 7

#define isEPD_W21_BUSY digitalRead(EPD_W21_BUSY_PIN)  //BUSY
#define EPD_W21_RST_0 digitalWrite(EPD_W21_RST_PIN,LOW)  //RES
#define EPD_W21_RST_1 digitalWrite(EPD_W21_RST_PIN,HIGH)
#define EPD_W21_DC_0  digitalWrite(EPD_W21_DC_PIN,LOW) //DC
#define EPD_W21_DC_1  digitalWrite(EPD_W21_DC_PIN,HIGH)
#define EPD_W21_CS_0 digitalWrite(EPD_W21_CS_PIN,LOW) //CS
#define EPD_W21_CS_1 digitalWrite(EPD_W21_CS_PIN,HIGH)


void SPI_Write(unsigned char value);
void EPD_W21_WriteDATA(unsigned char datas);
void EPD_W21_WriteCMD(unsigned char command);


#endif 
