/* Name: main.c
 * Project: USBaspLoader
 * Author: Christian Starkjohann
 * Creation Date: 2007-12-08
 * Tabsize: 4
 * Copyright: (c) 2007 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt)
 * This Revision: $Id: main.c 486 2008-02-05 18:21:36Z cs $
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <avr/boot.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <string.h>

static void leaveBootloader() __attribute__((__noreturn__));


/* ------------------------------------------------------------------------ */
// software jumper in EEPROM to start bootloader
#define UBOOT_SOFTJUMPER_ADDRESS	0x00
#define UBOOT_SOFTJUMPER			0xd9
/* ------------------------------------------------------------------------ */


#include "bootloaderconfig.h"
#include "usbdrv/usbdrv.c"

/* ------------------------------------------------------------------------ */

/* Request constants used by USBasp */
#define USBASP_FUNC_CONNECT         1
#define USBASP_FUNC_DISCONNECT      2
#define USBASP_FUNC_TRANSMIT        3
#define USBASP_FUNC_READFLASH       4
#define USBASP_FUNC_ENABLEPROG      5
#define USBASP_FUNC_WRITEFLASH      6
#define USBASP_FUNC_READEEPROM      7
#define USBASP_FUNC_WRITEEEPROM     8
#define USBASP_FUNC_SETLONGADDRESS  9



/* ------------------------------------------------------------------------ */

#ifndef ulong
#   define ulong    unsigned long
#endif
#ifndef uint
#   define uint     unsigned int
#endif

/* defaults if not in config file: */
#ifndef HAVE_EEPROM_PAGED_ACCESS
#   define HAVE_EEPROM_PAGED_ACCESS 0
#endif
#ifndef HAVE_EEPROM_BYTE_ACCESS
#   define HAVE_EEPROM_BYTE_ACCESS  0
#endif
#ifndef BOOTLOADER_CAN_EXIT
#   define  BOOTLOADER_CAN_EXIT     0
#endif

/* allow compatibility with avrusbboot's bootloaderconfig.h: */
#ifdef BOOTLOADER_INIT
#   define bootLoaderInit()         BOOTLOADER_INIT
#   define bootLoaderExit()
#endif
#ifdef BOOTLOADER_CONDITION
#   define bootLoaderCondition()    BOOTLOADER_CONDITION
#endif

/* device compatibility: */
#ifndef GICR    /* ATMega*8 don't have GICR, use MCUCR instead */
#   define GICR     MCUCR
#endif

/* ------------------------------------------------------------------------ */

typedef union longConverter{
    ulong   l;
    uint    w[2];
    uchar   b[4];
}longConverter_t;

static uchar            requestBootLoaderExit;
static longConverter_t  currentAddress; /* in bytes */
static uchar            bytesRemaining;
static uchar            isLastPage;
#if HAVE_EEPROM_PAGED_ACCESS
static uchar            currentRequest;
#else
static const uchar      currentRequest = 0;
#endif

static const uchar  signatureBytes[4] = {
#ifdef SIGNATURE_BYTES
    SIGNATURE_BYTES
#elif defined (__AVR_ATmega8__) || defined (__AVR_ATmega8HVA__)
    0x1e, 0x93, 0x07, 0
#elif defined (__AVR_ATmega48__) || defined (__AVR_ATmega48P__)
    0x1e, 0x92, 0x05, 0
#elif defined (__AVR_ATmega88__) || defined (__AVR_ATmega88P__)
    0x1e, 0x93, 0x0a, 0
#elif defined (__AVR_ATmega168__) || defined (__AVR_ATmega168P__)
    0x1e, 0x94, 0x06, 0
#elif defined (__AVR_ATmega328P__)
    0x1e, 0x95, 0x0f, 0
#else
#   error "Device signature is not known, please edit main.c!"
#endif
};

#if (FLASHEND) > 0xffff /* we need long addressing */
#   define CURRENT_ADDRESS  currentAddress.l
#else
#   define CURRENT_ADDRESS  currentAddress.w[0]
#endif

/* ------------------------------------------------------------------------ */

static void (*nullVector)(void) __attribute__((__noreturn__));

static void leaveBootloader()
{
// from Sanguino bootloader:
// autoreset via watchdog (sneaky!) BBR/LF 9/13/2008
	  		WDTCSR = _BV(WDE);
	  		while (1);
}


/* ------------------------------------------------------------------------ */

uchar   usbFunctionSetup(uchar data[8])
{
usbRequest_t    *rq = (void *)data;
uchar           len = 0;
static uchar    replyBuffer[4];

    usbMsgPtr = replyBuffer;
    if(rq->bRequest == USBASP_FUNC_TRANSMIT){   /* emulate parts of ISP protocol */
        uchar rval = 0;
        usbWord_t address;
        address.bytes[1] = rq->wValue.bytes[1];
        address.bytes[0] = rq->wIndex.bytes[0];
        if(rq->wValue.bytes[0] == 0x30){        /* read signature */
            rval = rq->wIndex.bytes[0] & 3;
            rval = signatureBytes[rval];
#if HAVE_EEPROM_BYTE_ACCESS
        }else if(rq->wValue.bytes[0] == 0xa0){  /* read EEPROM byte */
            rval = eeprom_read_byte((void *)address.word);
        }else if(rq->wValue.bytes[0] == 0xc0){  /* write EEPROM byte */
            eeprom_write_byte((void *)address.word, rq->wIndex.bytes[1]);
#endif
        }else{
            /* ignore all others, return default value == 0 */
        }
        replyBuffer[3] = rval;
        len = 4;
    }else if(rq->bRequest == USBASP_FUNC_ENABLEPROG){
        /* replyBuffer[0] = 0; is never touched and thus always 0 which means success */
        len = 1;
    }else if(rq->bRequest >= USBASP_FUNC_READFLASH && rq->bRequest <= USBASP_FUNC_SETLONGADDRESS){
        currentAddress.w[0] = rq->wValue.word;
        if(rq->bRequest == USBASP_FUNC_SETLONGADDRESS){
#if (FLASHEND) > 0xffff
            currentAddress.w[1] = rq->wIndex.word;
#endif
        }else{
            bytesRemaining = rq->wLength.bytes[0];
            /* if(rq->bRequest == USBASP_FUNC_WRITEFLASH) only evaluated during writeFlash anyway */
            isLastPage = rq->wIndex.bytes[1] & 0x02;
#if HAVE_EEPROM_PAGED_ACCESS
            currentRequest = rq->bRequest;
#endif
            len = 0xff; /* hand over to usbFunctionRead() / usbFunctionWrite() */
        }
#if BOOTLOADER_CAN_EXIT
    }else if(rq->bRequest == USBASP_FUNC_DISCONNECT){
        requestBootLoaderExit = 1;      /* allow proper shutdown/close of connection */
#endif
    }else{
        /* ignore: USBASP_FUNC_CONNECT */
    }
    return len;
}

uchar usbFunctionWrite(uchar *data, uchar len)
{
uchar   isLastWrite;

    DBG1(0x31, (void *)&currentAddress.l, 4);
    if(len > bytesRemaining)
        len = bytesRemaining;
    bytesRemaining -= len;
    isLastWrite = bytesRemaining == 0;
    if(currentRequest >= USBASP_FUNC_READEEPROM){
        eeprom_write_block(data, (void *)currentAddress.w[0], len);
        currentAddress.w[0] += len;
    }else{
        char i = len;
        while(i > 0){
            i -= 2;
            if((currentAddress.w[0] & (SPM_PAGESIZE - 1)) == 0){    /* if page start: erase */
                DBG1(0x33, 0, 0);
#ifndef NO_FLASH_WRITE
                cli();
                boot_page_erase(CURRENT_ADDRESS);   /* erase page */
                sei();
                boot_spm_busy_wait();               /* wait until page is erased */
#endif
            }
            DBG1(0x32, 0, 0);
            cli();
            boot_page_fill(CURRENT_ADDRESS, *(short *)data);
            sei();
            CURRENT_ADDRESS += 2;
            data += 2;
            /* write page when we cross page boundary or we have the last partial page */
            if((currentAddress.w[0] & (SPM_PAGESIZE - 1)) == 0 || (i <= 0 && isLastWrite && isLastPage)){
                DBG1(0x34, 0, 0);
#ifndef NO_FLASH_WRITE
                cli();
                boot_page_write(CURRENT_ADDRESS - 2);
                sei();
                boot_spm_busy_wait();
                cli();
                boot_rww_enable();
                sei();
#endif
            }
        }
        DBG1(0x35, (void *)&currentAddress.l, 4);
    }
    return isLastWrite;
}

uchar usbFunctionRead(uchar *data, uchar len)
{
    if(len > bytesRemaining)
        len = bytesRemaining;
    bytesRemaining -= len;
    if(currentRequest >= USBASP_FUNC_READEEPROM){
        eeprom_read_block(data, (void *)currentAddress.w[0], len);
    }else{
        memcpy_P(data, (PGM_VOID_P)CURRENT_ADDRESS, len);
    }
    CURRENT_ADDRESS += len;
    return len;
}

/* ------------------------------------------------------------------------ */

static void initForUsbConnectivity(void)
{
uchar   i = 0;

    usbInit();
    /* enforce USB re-enumerate: */
    usbDeviceDisconnect();  /* do this while interrupts are disabled */
    while(--i){         /* fake USB disconnect for > 250 ms */
        wdt_reset();
        _delay_ms(1);
    }
    usbDeviceConnect();
    sei();
}

int main(void)
{
    /* initialize  */
    char ch, do_boot_load;
    
    ch = MCUSR;
	MCUCSR = 0;                     /* clear all reset flags for next time */
    wdt_reset();

     // Check if the WDT was used to reset, in which case we dont bootload and skip straight to the code. woot.
    if (ch & (1 << WDRF)) {
	    wdt_disable();
	    nullVector();
    }
    
    bootLoaderInit();

   // wdt_enable(WDTO_1S);	// enable watchdog timer
	odDebugInit();
    DBG1(0x00, 0, 0);
   
	//do_boot_load = ~PIND & (1 << JUMPER_BIT);  // see if jumper is set (= 0)
//	do_boot_load += ~ch & (1 << PORF);		// if we come here without a power on reset
												// we were forced into the bootloader, lets bootload even if there's no jumper!
	do_boot_load += ch & (1 << EXTRF);		// also if we come here from external reset (to mimick Arduino bootloader)

	

	
	
    if(do_boot_load){							// start bootloader in any of these cases
	
		GICR = (1 << IVCE);  /* enable change of interrupt vectors */
	 	GICR = (1 << IVSEL); /* move interrupts to boot flash section */

	    wdt_enable(WDTO_1S);	// enable watchdog timer
        uint i = 0;
        initForUsbConnectivity();
        PORTD |= (1 << 5);  /// Light Yellow Led
        
        do{
            usbPoll();
            wdt_reset();

            if(requestBootLoaderExit){
                if(--i == 0)
                    break;
            }

        }while(1);  /* main event loop */
    }
  	leaveBootloader();
    return 0;
}

/* ------------------------------------------------------------------------ */
