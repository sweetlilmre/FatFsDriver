#include "FatFsDriver.h"

FATFS FatFs;
FIL Fil;

SPIClass mySPI(1);
FatFsDriver driver(PA4, &mySPI, SPISettings(1000000UL*4, MSBFIRST, SPI_MODE0));

void setup(){
  Serial.begin(9600);
  while(!Serial) {};
  Serial.println("init start");

  UINT bw;
  FRESULT fr = f_mount(&FatFs, "", 1);
  
  if (fr == FR_OK) {
    if ((fr = f_open(&Fil, "newfile.txt", FA_WRITE | FA_CREATE_ALWAYS)) == FR_OK) {
      fr = f_write(&Fil, "It works!\r\n", 11, &bw);
      f_close(&Fil);

      if (bw == 11) {
        Serial.println("FatFs init and write OK.");
      } else {
        Serial.println("write fail");
      }
    } else {
      Serial.println("create file fail");
    }
  } else {
    Serial.println("mount fail");
  }
}

void loop(){
}
