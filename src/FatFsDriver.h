#ifndef FATFSDRIVER_H
#define FATFSDRIVER_H

#include "SPI.h"
#include "ff.h"
#define USE_SPI_DMA 1

class FatFsDriver
{
  public:
    FatFsDriver(int csPin, SPIClass* pSPI, SPISettings fastSettings) {
      FatFsDriver::csPin = csPin;
      FatFsDriver::pSPI = pSPI;
      FatFsDriver::fastSettings = fastSettings;
    }
    
    static int csPin;
    static SPIClass* pSPI;
    static SPISettings fastSettings;
};

#endif
