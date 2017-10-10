
/* STM32F100: MMCv3/SDv1/SDv2 (SPI mode) control module                    */
/*------------------------------------------------------------------------*/
/*
/   Copyright (C) 2014, ChaN, all right reserved.
/
/ * This software is a free software and there is NO WARRANTY.
/ * No restriction on use. You can use, modify and redistribute it for
/    personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
/ * Redistributions of source code must retain the above copyright notice.
/
/-------------------------------------------------------------------------*/

#include "diskio.h"

#define  MMC_WP  0  /* Write protected (yes:true, no:false, default:false) */
#define  MMC_CD  1  /* Card detect (yes:true, no:false, default:true) */


/* MMC/SD command */
#define CMD0    (0)        /* GO_IDLE_STATE */
#define CMD1    (1)        /* SEND_OP_COND (MMC) */
#define ACMD41  (0x80+41)  /* SEND_OP_COND (SDC) */
#define CMD8    (8)        /* SEND_IF_COND */
#define CMD9    (9)        /* SEND_CSD */
#define CMD10   (10)       /* SEND_CID */
#define CMD12   (12)       /* STOP_TRANSMISSION */
#define ACMD13  (0x80+13)  /* SD_STATUS (SDC) */
#define CMD16   (16)       /* SET_BLOCKLEN */
#define CMD17   (17)       /* READ_SINGLE_BLOCK */
#define CMD18   (18)       /* READ_MULTIPLE_BLOCK */
#define CMD23   (23)       /* SET_BLOCK_COUNT (MMC) */
#define ACMD23  (0x80+23)  /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24   (24)       /* WRITE_BLOCK */
#define CMD25   (25)       /* WRITE_MULTIPLE_BLOCK */
#define CMD32   (32)       /* ERASE_ER_BLK_START */
#define CMD33   (33)       /* ERASE_ER_BLK_END */
#define CMD38   (38)       /* ERASE */
#define CMD55   (55)       /* APP_CMD */
#define CMD58   (58)       /* READ_OCR */

#define INIT_TIMEOUT  1000
#define READ_TIMEOUT  200
#define WRITE_TIMEOUT 500
#define CMD_TIMEOUT   600


static volatile DSTATUS Stat = STA_NOINIT;  /* Physical drive status */
static BYTE CardType;      /* Card type flags */


//------------------------------------------------------------------------------
BYTE waitTimeout(UINT start, UINT ms) {
  return (tickMS() - start) > ms;
}
/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static int wait_ready(UINT waitMS) {
  BYTE d;

  UINT start = tickMS();
  do {
    d = xchg_spi(0xFF);
    /* This loop takes a time. Insert rot_rdq() here for multitask envilonment. */
  } while(d != 0xFF && !waitTimeout(start, waitMS));  /* Wait for card goes ready or timeout */

  return (d == 0xFF) ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/

static void deselect(void) {
  deactivate();
  xchg_spi(0xFF);  /* Dummy clock (force DO hi-z for multiple slave SPI) */
}


/*-----------------------------------------------------------------------*/
/* Select card                                                           */
/*-----------------------------------------------------------------------*/

static void select() {
  activate();
  xchg_spi(0xFF);  /* Dummy clock (force DO enabled) */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                     */
/*-----------------------------------------------------------------------*/

static int rcvr_datablock(BYTE *buff, UINT btr) {
  BYTE token;

  UINT start = tickMS();
  do {              /* Wait for DataStart token in timeout of 200ms */
    token = xchg_spi(0xFF);
    /* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
  } while ((token == 0xFF) && !waitTimeout(start, READ_TIMEOUT));
  if(token != 0xFE) return 0;    /* Function fails if invalid DataStart token or timeout */

  rcvr_spi_multi(buff, btr);    /* Store trailing data to the buffer */
  xchg_spi(0xFF); xchg_spi(0xFF);      /* Discard CRC */

  return 1;            /* Function succeeded */
}



/*-----------------------------------------------------------------------*/
/* Send a data packet to the MMC                                         */
/*-----------------------------------------------------------------------*/

#ifndef FF_FS_READONLY
static int xmit_datablock(const BYTE *buff, BYTE token) {
  BYTE resp;

  if (!wait_ready(WRITE_TIMEOUT)) return 0;    /* Wait for card ready */

  xchg_spi(token);          /* Send token */
  if (token != 0xFD) {        /* Send data if token is other than StopTran */
    xmit_spi_multi(buff, 512);    /* Data */
    xchg_spi(0xFF); xchg_spi(0xFF);  /* Dummy CRC */

    resp = xchg_spi(0xFF);        /* Receive data resp */
    if ((resp & 0x1F) != 0x05) return 0;  /* Function fails if the data packet was not accepted */
  }
  return 1;
}
#endif


/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                       */
/*-----------------------------------------------------------------------*/

static BYTE send_cmd_wait(BYTE cmd, DWORD arg, BYTE wait) {
  BYTE n, res;

  if (cmd & 0x80) {  /* Send a CMD55 prior to ACMD<n> */
    cmd &= 0x7F;
    res = send_cmd_wait(CMD55, 0, wait);
    if (res > 1) return res;
  }
  
  /* Select the card and wait for ready except to stop multiple block read */
  if (cmd != CMD12) {
    deselect();
    select();
    if (!wait_ready(CMD_TIMEOUT) && wait) {
      deselect();
      return 0xFF;
    }
  }

  /* Send command packet */
  xchg_spi(0x40 | cmd);        /* Start + command index */
  xchg_spi((BYTE)(arg >> 24));    /* Argument[31..24] */
  xchg_spi((BYTE)(arg >> 16));    /* Argument[23..16] */
  xchg_spi((BYTE)(arg >> 8));      /* Argument[15..8] */
  xchg_spi((BYTE)arg);        /* Argument[7..0] */
  n = 0x01;              /* Dummy CRC + Stop */
  if (cmd == CMD0) n = 0x95;      /* Valid CRC for CMD0(0) */
  if (cmd == CMD8) n = 0x87;      /* Valid CRC for CMD8(0x1AA) */
  xchg_spi(n);

  /* Receive command resp */
  if (cmd == CMD12) xchg_spi(0xFF);  /* Discard following one byte when CMD12 */
  n = 10;                /* Wait for response (10 bytes max) */
  do {
    res = xchg_spi(0xFF);
  } while ((res & 0x80) && --n);

  return res;              /* Return received response */
}

static BYTE send_cmd(BYTE cmd, DWORD arg) {
  return send_cmd_wait(cmd, arg, 1);
}

static BYTE send_cmd_nowait(BYTE cmd, DWORD arg) {
  return send_cmd_wait(cmd, arg, 0);
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(BYTE drv) {
  BYTE n, cmd, ty, ocr[4];


  if (drv) return STA_NOINIT;      /* Supports only drive 0 */
  if (!fatfs_spi_init()) return STA_NOINIT;              /* Initialize SPI */

  if (Stat & STA_NODISK) return Stat;  /* Is card existing in the soket? */

  for (n = 10; n; n--) xchg_spi(0xFF);  /* Send 80 dummy clocks */
  ty = 0;
  if (send_cmd_nowait(CMD0, 0) == 1) {      /* Put the card SPI/Idle state */
    UINT start = tickMS(); /* Initialization timeout = 1 sec */
    if (send_cmd(CMD8, 0x1AA) == 1) {  /* SDv2? */
      for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);  /* Get 32 bit return value of R7 resp */
      if (ocr[2] == 0x01 && ocr[3] == 0xAA) {        /* Is the card supports vcc of 2.7-3.6V? */
        while (!waitTimeout(start, INIT_TIMEOUT) && send_cmd(ACMD41, 1UL << 30)) ;  /* Wait for end of initialization with ACMD41(HCS) */
        if (!waitTimeout(start, INIT_TIMEOUT) && send_cmd(CMD58, 0) == 0) {    /* Check CCS bit in the OCR */
          for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
          ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;  /* Card id SDv2 */
        }
      }
    } else {  /* Not SDv2 card */
      if (send_cmd(ACMD41, 0) <= 1)    {  /* SDv1 or MMC? */
        ty = CT_SD1; cmd = ACMD41;  /* SDv1 (ACMD41(0)) */
      } else {
        ty = CT_MMC; cmd = CMD1;  /* MMCv3 (CMD1(0)) */
      }
      while (!waitTimeout(start, INIT_TIMEOUT) && send_cmd(cmd, 0)) ;    /* Wait for end of initialization */
      if (waitTimeout(start, INIT_TIMEOUT) || send_cmd(CMD16, 512) != 0)  /* Set block length: 512 */
        ty = 0;
    }
  }
  CardType = ty;  /* Card type */
  deselect();

  if (ty) {      /* OK */
    fatfs_post_spi_init();      /* Set fast clock */
    Stat &= ~STA_NOINIT;  /* Clear STA_NOINIT flag */
  } else {      /* Failed */
    Stat = STA_NOINIT;
  }

  return Stat;
}



/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE drv) {
  if (drv) return STA_NOINIT;    /* Supports only drive 0 */

  return Stat;  /* Return disk status */
}



/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                         */
/*-----------------------------------------------------------------------*/

DRESULT disk_read(BYTE drv, BYTE *buff, DWORD sector, UINT count) {
  if (drv || !count) return RES_PARERR;    /* Check parameter */
  if (Stat & STA_NOINIT) return RES_NOTRDY;  /* Check if drive is ready */

  if (!(CardType & CT_BLOCK)) sector *= 512;  /* LBA ot BA conversion (byte addressing cards) */

  if (count == 1) {  /* Single sector read */
    if ((send_cmd(CMD17, sector) == 0)  /* READ_SINGLE_BLOCK */
      && rcvr_datablock(buff, 512)) {
      count = 0;
    }
  }
  else {        /* Multiple sector read */
    if (send_cmd(CMD18, sector) == 0) {  /* READ_MULTIPLE_BLOCK */
      do {
        if (!rcvr_datablock(buff, 512)) break;
        buff += 512;
      } while (--count);
      send_cmd(CMD12, 0);        /* STOP_TRANSMISSION */
    }
  }
  deselect();

  return count ? RES_ERROR : RES_OK;  /* Return result */
}



/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#ifndef FF_FS_READONLY
DRESULT disk_write(BYTE drv, const BYTE *buff, DWORD sector, UINT count) {
  if (drv || !count) return RES_PARERR;    /* Check parameter */
  if (Stat & STA_NOINIT) return RES_NOTRDY;  /* Check drive status */
  if (Stat & STA_PROTECT) return RES_WRPRT;  /* Check write protect */

  if (!(CardType & CT_BLOCK)) sector *= 512;  /* LBA ==> BA conversion (byte addressing cards) */

  if (count == 1) {  /* Single sector write */
    if ((send_cmd(CMD24, sector) == 0)  /* WRITE_BLOCK */
      && xmit_datablock(buff, 0xFE)) {
      count = 0;
    }
  }
  else {        /* Multiple sector write */
    if (CardType & CT_SDC) send_cmd(ACMD23, count);  /* Predefine number of sectors */
    if (send_cmd(CMD25, sector) == 0) {  /* WRITE_MULTIPLE_BLOCK */
      do {
        if (!xmit_datablock(buff, 0xFC)) break;
        buff += 512;
      } while (--count);
      if (!xmit_datablock(0, 0xFD)) count = 1;  /* STOP_TRAN token */
    }
  }
  deselect();

  return count ? RES_ERROR : RES_OK;  /* Return result */
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buff) {
  DRESULT res;
  BYTE n, csd[16];
  DWORD *dp, st, ed, csize;

  if (drv) return RES_PARERR;          /* Check parameter */
  if (Stat & STA_NOINIT) return RES_NOTRDY;  /* Check if drive is ready */

  res = RES_ERROR;

  switch (cmd) {
  case CTRL_SYNC :    /* Wait for end of internal write process of the drive */
    select();
    if (wait_ready(CMD_TIMEOUT)) res = RES_OK;
    break;

  case GET_SECTOR_COUNT :  /* Get drive capacity in unit of sector (DWORD) */
    if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
      if ((csd[0] >> 6) == 1) {  /* SDC ver 2.00 */
        csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
        *(DWORD*)buff = csize << 10;
      } else {          /* SDC ver 1.XX or MMC ver 3 */
        n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
        csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
        *(DWORD*)buff = csize << (n - 9);
      }
      res = RES_OK;
    }
    break;

  case GET_BLOCK_SIZE :  /* Get erase block size in unit of sector (DWORD) */
    if (CardType & CT_SD2) {  /* SDC ver 2.00 */
      if (send_cmd(ACMD13, 0) == 0) {  /* Read SD status */
        xchg_spi(0xFF);
        if (rcvr_datablock(csd, 16)) {        /* Read partial block */
          for (n = 64 - 16; n; n--) xchg_spi(0xFF);  /* Purge trailing data */
          *(DWORD*)buff = 16UL << (csd[10] >> 4);
          res = RES_OK;
        }
      }
    } else {          /* SDC ver 1.XX or MMC */
      if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {  /* Read CSD */
        if (CardType & CT_SD1) {  /* SDC ver 1.XX */
          *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
        } else {          /* MMC */
          *(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
        }
        res = RES_OK;
      }
    }
    break;

  case CTRL_TRIM :  /* Erase a block of sectors (used when _USE_ERASE == 1) */
    if (!(CardType & CT_SDC)) break;        /* Check if the card is SDC */
    if (disk_ioctl(drv, MMC_GET_CSD, csd)) break;  /* Get CSD */
    if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break;  /* Check if sector erase can be applied to the card */
    dp = buff; st = dp[0]; ed = dp[1];        /* Load sector block */
    if (!(CardType & CT_BLOCK)) {
      st *= 512; ed *= 512;
    }
    if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000)) {  /* Erase sector block */
      res = RES_OK;  /* FatFs does not check result of this command */
    }
    break;

  default:
    res = RES_PARERR;
  }

  deselect();

  return res;
}
