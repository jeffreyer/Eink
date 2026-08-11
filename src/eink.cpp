#include "common.h"

#ifdef INK6
// 6 色屏模式：4 色驱动与画布不再编译，BlackImage 由 eink6.cpp 提供
#else

#include <SPI.h>
//EPD
#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"
#include "eink.h"
#include "GUI_Paint.h"

unsigned char BlackImage[ALLSCREEN_BYTES];//Define canvas space 

int init_eink(){
    pinMode(2, INPUT);  //BUSY
    pinMode(3, OUTPUT); //RES 
    pinMode(4, OUTPUT); //DC   
    pinMode(5, OUTPUT); //CS   
    //SPI
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0)); 
    SPI.begin(6,-1,7,5);
    // SPI.begin ();  
    return 0;
}

int gui_drawtext(const char* str){
    Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 0, WHITE0); //Set canvas parameters, GUI image rotation, please change 270 to 0/90/180/270.
    Paint_SetScale(4);
    Paint_SelectImage(BlackImage); //Select current settings.
    Paint_Clear(WHITE0);
    Paint_DrawString_EN(0, 0, str, &Font24, BLACK0, WHITE0);
    EPD_init_Fast2(); //Full screen update initialization.
    PIC_display(BlackImage);//display image
    EPD_sleep();
    return 0;
}

int gui_draw(){
    
    Paint_NewImage(BlackImage, EPD_WIDTH, EPD_HEIGHT, 270, WHITE0); //Set canvas parameters, GUI image rotation, please change 270 to 0/90/180/270.
    Paint_SetScale(4);
    Paint_SelectImage(BlackImage); //Select current settings.

    /**************Drawing******************************/	
    Paint_Clear(WHITE0);   
    // Drawing on the image
		//Point.   
    Paint_DrawPoint(5, 10, BLACK0, DOT_PIXEL_1X1, DOT_STYLE_DFT); //point 1x1.
    Paint_DrawPoint(5, 25, BLACK0, DOT_PIXEL_2X2, DOT_STYLE_DFT); //point 2x2.
    Paint_DrawPoint(5, 40, RED0, DOT_PIXEL_3X3, DOT_STYLE_DFT); //point 3x3.
    Paint_DrawPoint(5, 55, YELLOW0, DOT_PIXEL_4X4, DOT_STYLE_DFT); //point 4x4.
		//Line.
    Paint_DrawLine(20, 5, 50, 35, BLACK0, LINE_STYLE_SOLID, DOT_PIXEL_1X1); //1x1line 1.
    Paint_DrawLine(50, 5, 20, 35, BLACK0, LINE_STYLE_SOLID, DOT_PIXEL_1X1); //1x1line 2.
		//Rectangle.
    Paint_DrawRectangle(20, 5, 50, 35, RED0, DRAW_FILL_EMPTY, DOT_PIXEL_1X1); //Hollow rectangle 1.
    Paint_DrawRectangle(70, 5, 100, 35, YELLOW0, DRAW_FILL_FULL, DOT_PIXEL_1X1); //Hollow rectangle 2.
    Paint_DrawRectangle(3, 3, 198, 198, RED0, DRAW_FILL_EMPTY, DOT_PIXEL_1X1); //Hollow rectangle 1.
    Paint_DrawRectangle(2, 2, 199, 199, YELLOW0, DRAW_FILL_EMPTY, DOT_PIXEL_1X1); //Hollow rectangle 1.
    Paint_DrawRectangle(1, 1, 200, 200, BLACK0, DRAW_FILL_EMPTY, DOT_PIXEL_1X1); //Hollow rectangle 1.
    //Circle.
    Paint_DrawCircle(30, 50, 10, RED0, DRAW_FILL_EMPTY, DOT_PIXEL_1X1); //Hollow circle.
    Paint_DrawCircle(80, 50, 10, YELLOW0, DRAW_FILL_FULL, DOT_PIXEL_1X1); //solid circle.
    EPD_init2(); //Full screen update initialization.
    PIC_display(BlackImage);//display image
    EPD_sleep();//EPD_sleep,Sleep instruction is necessary, please do not delete!!!
    // delay(3000); //Delay for 3s.

    /***********String&Number***************************/ 
    // Paint_Clear(WHITE0);
    // Paint_DrawString_EN(5, 10, "Good Display", &Font8, RED0, WHITE0);  //Font8
    // Paint_DrawString_EN(5, 25, "Good Display", &Font12, YELLOW0, BLACK0); //Font12
    // Paint_DrawNum(5, 45, 123456789, &Font16, BLACK0, YELLOW0); //Font16
    // Paint_DrawNum(5, 70, 123456789, &Font20, WHITE0, RED0); //Font20
    // EPD_init2(); //Full screen update initialization.
    // PIC_display(BlackImage);//display image
    // EPD_sleep();//EPD_sleep,Sleep instruction is necessary, please do not delete!!!
    // delay(3000); //Delay for 3s.	
    return 0;
}

int ink_draw(){
    EPD_init_Fast2();
    PIC_display(IMAGE_DATA);
    // delay(5000);

    // Serial.println("begin black");
    // Display_All_Black();
    // Serial.println("end black");
    // delay(5000);
    // Serial.println("begin white");
    // Display_All_White();
    // Serial.println("end white");
    // delay(5000);
    // Serial.println("begin yellow");
    // Display_All_Yellow();
    // Serial.println("end yellow");
    // delay(5000);
    // Serial.println("begin red");
    // Display_All_Red();
    // Serial.println("end red");
    // delay(5000);
    EPD_sleep();
    return 0;
}

int ink_draw_test(){
#if 1//Full screen update demostration.

   /************Full display*******************/
    // EPD_init(); //Full screen update initialization.
    // PIC_display(IMAGE_DATA);//To Display one image using full screen update.
    // EPD_sleep();//Enter the sleep mode and please do not delete it, otherwise it will reduce the lifespan of the screen.
    // delay(5000); //Delay for 5s.
 	/************Fast update mode(12s)*******************/
	#if 1 //Fast update demostration.	
		EPD_init_Fast(); //Fast screen update initialization.
		PIC_display(IMAGE_DATA);//To Display one image using full screen update.
		EPD_sleep();//Enter the sleep mode and please do not delete it, otherwise it will reduce the lifespan of the screen.
    // delay(5000); //Delay for 5s.

    // EPD_init2(); //Fast screen update initialization.
    // PIC_display(IMAGE_DATA);//To Display one image using full screen update.
    // EPD_sleep();//Enter
	#endif	  
#endif 
    return 0;
}

#endif // INK6
