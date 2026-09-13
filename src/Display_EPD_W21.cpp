#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"


void delay_xms(unsigned int xms)
{
  delay(xms);
}

void EPD_init2(void)
{
  delay_xms(100);//At least 20ms delay 	
	EPD_W21_RST_0;		// Module reset
	delay_xms(50);//At least 50ms delay 
	EPD_W21_RST_1;
	delay_xms(50);//At least 50ms delay 

  lcd_chkstatus();
	
  EPD_W21_WriteCMD(0x4d);
  EPD_W21_WriteDATA(0x78); 

  EPD_W21_WriteCMD(0x00);
  EPD_W21_WriteDATA(0x0f);
  EPD_W21_WriteDATA(0x29);

  EPD_W21_WriteCMD(0x06);
  EPD_W21_WriteDATA(0x0d);
  EPD_W21_WriteDATA(0x12); 
  EPD_W21_WriteDATA(0x30); 
  EPD_W21_WriteDATA(0x20); 
  EPD_W21_WriteDATA(0x19); 
  EPD_W21_WriteDATA(0x2a);
  EPD_W21_WriteDATA(0x22);

  EPD_W21_WriteCMD(0x30);
  EPD_W21_WriteDATA(0x08); 

  EPD_W21_WriteCMD(0x50);
  EPD_W21_WriteDATA(0x37);

  EPD_W21_WriteCMD(0x61);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0xc8); 
  EPD_W21_WriteDATA(0x00); 
  EPD_W21_WriteDATA(0xc8);

  EPD_W21_WriteCMD(0xE9);
  EPD_W21_WriteDATA(0x01); 

  EPD_W21_WriteCMD(0x04);
  lcd_chkstatus();
}

void EPD_init_Fast2(void)
{
  delay_xms(100);//At least 20ms delay 	
	EPD_W21_RST_0;		// Module reset
	delay_xms(50);//At least 50ms delay 
	EPD_W21_RST_1;
	delay_xms(50);//At least 50ms delay 

  lcd_chkstatus();
	
  EPD_W21_WriteCMD(0x4d);
  EPD_W21_WriteDATA(0x78); 

  EPD_W21_WriteCMD(0x00);
  EPD_W21_WriteDATA(0x0f);
  EPD_W21_WriteDATA(0x29);

  EPD_W21_WriteCMD(0x06);
  EPD_W21_WriteDATA(0x0d);
  EPD_W21_WriteDATA(0x12); 
  EPD_W21_WriteDATA(0x30); 
  EPD_W21_WriteDATA(0x20); 
  EPD_W21_WriteDATA(0x19); 
  EPD_W21_WriteDATA(0x2a);
  EPD_W21_WriteDATA(0x22);

  EPD_W21_WriteCMD(0x30);
  EPD_W21_WriteDATA(0x08); 

  EPD_W21_WriteCMD(0x50);
  EPD_W21_WriteDATA(0x37);

  EPD_W21_WriteCMD(0x61);
  EPD_W21_WriteDATA(0x00);
  EPD_W21_WriteDATA(0xc8); 
  EPD_W21_WriteDATA(0x00); 
  EPD_W21_WriteDATA(0xc8);

  EPD_W21_WriteCMD(0xE9);
  EPD_W21_WriteDATA(0x01); 

  EPD_W21_WriteCMD(0x04);
  lcd_chkstatus();

  EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02);
		
	EPD_W21_WriteCMD(0xE6);
	EPD_W21_WriteDATA(0x5D);
	 
	EPD_W21_WriteCMD(0xA5);
	EPD_W21_WriteDATA(0x00);
	lcd_chkstatus(); 
}

void EPD_init(void)
{
  delay_xms(100);//At least 20ms delay 	
	EPD_W21_RST_0;		// Module reset
	delay_xms(50);//At least 50ms delay 
	EPD_W21_RST_1;
	delay_xms(50);//At least 50ms delay 
	
  EPD_W21_WriteCMD(0xE9);
  EPD_W21_WriteDATA(0x01); 

  EPD_W21_WriteCMD(0x04);
  lcd_chkstatus();   
}
//Fast full screen update initialization
void EPD_init_Fast(void)	
{
  delay_xms(20);//At least 20ms delay 	
	EPD_W21_RST_0;		// Module reset
	delay_xms(50);//At least 50ms delay 
	EPD_W21_RST_1;
	delay_xms(50);//At least 50ms delay 
	
  EPD_W21_WriteCMD(0xE9);
  EPD_W21_WriteDATA(0x01);  
	
  EPD_W21_WriteCMD(0xEF);
	EPD_W21_WriteDATA(0x01);       
	
	EPD_W21_WriteCMD(0xF6);
	EPD_W21_WriteDATA(0x24);        

	EPD_W21_WriteCMD(0xEF);
	EPD_W21_WriteDATA(0x00);           
	
	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02);

	EPD_W21_WriteCMD(0xE6);
	EPD_W21_WriteDATA(92);

	EPD_W21_WriteCMD(0xA5);
	lcd_chkstatus(); 

  EPD_W21_WriteCMD(0x04);
  lcd_chkstatus(); 	
}	
void EPD_sleep(void)
{   
	EPD_W21_WriteCMD(0X02);  	//power off
	EPD_W21_WriteDATA(0x00);
	lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal  
 
	EPD_W21_WriteCMD(0X07);  	//deep sleep
	EPD_W21_WriteDATA(0xA5);
}
void EPD_update(void)
{   
  EPD_W21_WriteCMD(0x12); //Display Update Control
  EPD_W21_WriteDATA(0x00);
  lcd_chkstatus();   
}

void lcd_chkstatus(void)
{ 
  while(1)
  {  //=0 BUSY
     if(isEPD_W21_BUSY==1) break;
  }  
}


void Display_All_Black(void)
{
  unsigned long i; 

  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0x00);
    }
  } 
  EPD_update();  
  
}

void Display_All_White(void)
{
  unsigned long i;
 
  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0x55);
    }
  } 
   EPD_update(); 
}

void Display_All_Yellow(void)
{
  unsigned long i;
 
  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0xaa);
    }
  }
   EPD_update(); 
}


void Display_All_Red(void)
{
  unsigned long i;
 
  EPD_W21_WriteCMD(0x10);
  {
    for(i=0;i<ALLSCREEN_BYTES;i++)
    {
      EPD_W21_WriteDATA(0xff);
    }
  } 
   EPD_update(); 
}



unsigned char Color_get(unsigned char color)
{
  unsigned datas;
  switch(color)
  {
    case 0x00:
      datas=white;  
      break;    
    case 0x01:
      datas=yellow;
      break;
    case 0x02:
      datas=red;
      break;    
    case 0x03:
      datas=black;
      break;      
    default:
      break;      
  }
   return datas;
}



void PIC_display(const unsigned char* picData)
{
  PIC_write_ram(picData);
  EPD_update();
}

// 只写 RAM（0x10 + 整帧数据），不触发刷新
void PIC_write_ram(const unsigned char* picData)
{
  unsigned int i,j;
  unsigned char temp1;
  unsigned char data_H1,data_H2,data_L1,data_L2,data;

  EPD_W21_WriteCMD(0x10);
  for(i=0;i<Gate_BITS;i++)  //Source_BITS*Gate_BITS/4
  {
    for(j=0;j<Source_BITS/4;j++)
    {
      temp1=picData[i*Source_BITS/4+j];

      data_H1=Color_get(temp1>>6&0x03)<<6;
      data_H2=Color_get(temp1>>4&0x03)<<4;
      data_L1=Color_get(temp1>>2&0x03)<<2;
      data_L2=Color_get(temp1&0x03);

      data=data_H1|data_H2|data_L1|data_L2;
      EPD_W21_WriteDATA(data);
    }
  }
}

// 触发刷新（0x12 + 0x00），发完立即返回，不等待 BUSY
void EPD_update_async(void)
{
  EPD_W21_WriteCMD(0x12); //Display Update Control
  EPD_W21_WriteDATA(0x00);
}

// 带超时等待面板空闲（BUSY 高 = 空闲）；返回是否在超时前空闲
bool EPD_wait_idle(uint32_t timeout_ms)
{
  uint32_t start = millis();
  while(isEPD_W21_BUSY==0)
  {
    // 轮询间隔 ≥ tickless idle 入睡阈值，等待期间允许 CPU 进入 light sleep
    delay(10);
    if(millis() - start > timeout_ms)
    {
      return false;
    }
  }
  return true;
}

// 只做硬件复位（不等待 BUSY、不发初始化序列）：
// 用于异步补断电前把控制 IC 拉回已知状态，避免它停在非初始化态导致断电命令被忽略
void EPD_reset_only(void)
{
  EPD_W21_RST_0;
  delay_xms(20);
  EPD_W21_RST_1;
  delay_xms(50);
}

// 断电源 + 面板深度休眠（假定刷新已结束）
void EPD_poweroff_sleep(void)
{
  EPD_W21_WriteCMD(0X02);  	//power off
  EPD_W21_WriteDATA(0x00);
  delay(20);
  EPD_W21_WriteCMD(0X07);  	//deep sleep
  EPD_W21_WriteDATA(0xA5);
}
