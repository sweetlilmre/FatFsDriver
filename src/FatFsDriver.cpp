#include "integer.h"
#include "FatFsDriver.h"

static SPISettings spiSettings = SPISettings(250000, MSBFIRST, SPI_MODE0);
SPISettings FatFsDriver::fastSettings = SPISettings(250000, MSBFIRST, SPI_MODE0);
int FatFsDriver::csPin = 0;
SPIClass* FatFsDriver::pSPI = NULL;

extern "C" DWORD get_fattime (void)
{
  #define YEAR 2017
  #define MONTH 6
  #define DAY 1
  #define HOUR 1
  #define MINUTE 1
  #define SECOND 1
  
	/* Pack date and time into a DWORD variable */
	return	  ((DWORD)(YEAR - 1980) << 25)
			| ((DWORD)MONTH << 21)
			| ((DWORD)DAY << 16)
			| ((DWORD)HOUR << 11)
			| ((DWORD)MINUTE << 5)
			| ((DWORD)SECOND >> 1);
}

extern "C" UINT tickMS() {
  return millis();
}

extern "C" void logmsg(char* msg) {
  Serial.println(msg);
}

extern "C" void activate() {
  FatFsDriver::pSPI->beginTransaction(spiSettings);
  digitalWrite(FatFsDriver::csPin, LOW);
}

extern "C" void deactivate() {
  digitalWrite(FatFsDriver::csPin, HIGH);
  FatFsDriver::pSPI->endTransaction();
}

/* Initialize MMC interface */
extern "C" BYTE fatfs_spi_init (void) {
  if (FatFsDriver::pSPI == NULL) return 0;
  digitalWrite(FatFsDriver::csPin, HIGH);
  pinMode(FatFsDriver::csPin, OUTPUT);
  FatFsDriver::pSPI->begin();
  activate();
  deactivate();
  return 1;
}

extern "C" void fatfs_post_spi_init() {
  spiSettings = FatFsDriver::fastSettings;
}

/* Exchange a byte */
extern "C" BYTE xchg_spi (BYTE data) {
  return FatFsDriver::pSPI->transfer(data);
}

/* Receive multiple byte */
extern "C" void rcvr_spi_multi (BYTE *buffer, UINT count) {
#if USE_SPI_DMA
  FatFsDriver::pSPI->dmaTransfer(0, buffer, count);
#else
  for (UINT i = 0; i < count; i++) {
    buffer[i] = FatFsDriver::pSPI->transfer(0XFF);
  }
#endif
}

/* Send multiple byte */
extern "C" void xmit_spi_multi (const BYTE *buffer,	UINT count) {
#if USE_SPI_DMA
  FatFsDriver::pSPI->dmaSend((void*) buffer, count);
#else
  FatFsDriver::pSPI->write((void*) buffer, count);
#endif
}

