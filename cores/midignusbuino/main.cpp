#include <Arduino.h>
#include <avr/wdt.h>
#include "midignusb.h"
#include "usbdrv.h"
#include "../../../../libraries/GnusbuinoMIDI/GnusbuinoMIDI.h"	// not very clean, I know



// ------------------------------------------------------------------------------
// - Status Leds
// ------------------------------------------------------------------------------
// 							(on means  set to 0 as we sink the LEDs )
 
 

unsigned long blinkstop;

#if defined(__AVR_ATtiny85__)
void statusLedOff(StatusLeds led) 		{}
void statusLedOn(StatusLeds led) 		{}
void statusLedToggle(StatusLeds led)	{}
void statusLedBlink(StatusLeds led) {}

#else
void statusLedOff(StatusLeds led) 		{PORTD &= ~(1 << led); }
void statusLedOn(StatusLeds led) 		{PORTD |= (1 << led);}
void statusLedToggle(StatusLeds led)	{PORTD ^= 1 << led;}
void statusLedBlink(StatusLeds led) {
		statusLedOn(led);
		blinkstop = millis()+50;
}
#endif

// ------------------------------------------------------------------------------
// - Start Bootloader
// ------------------------------------------------------------------------------
// dummy function doing the jump to bootloader section (Adress 1C00 on Atmega644)

void (*jumpToBootloader)(void) = (void (*)(void))0xF800; __attribute__ ((unused))

void startBootloader(void) {
		
		
		MCUCSR &= ~(1 << PORF);			// clear power on reset flag
										// this will hint the bootloader that it was forced
	
		cli();							// turn off interrupts
		wdt_disable();					// disable watchdog timer
		usbDeviceDisconnect(); 			// disconnect gnusb from USB bus
		
		ADCSRA &= ~(( 1 << ADIE) | ( 1 << ADEN));	// disable ADC interrupts
													// disable ADC (turn off ADC power)

		statusLedOff(StatusLed_Yellow);		
		statusLedOff(StatusLed_Green);

		jumpToBootloader();
}






// ------------------------------------------------------------------------------
// - usbFunctionDescriptor
// ------------------------------------------------------------------------------

unsigned char usbFunctionDescriptor(usbRequest_t * rq)
{

	if (rq->wValue.bytes[1] == USBDESCR_DEVICE) {
		usbMsgPtr = (unsigned char *) deviceDescrMIDI;
		return sizeof(deviceDescrMIDI);
	} else {		/* must be config descriptor */
		usbMsgPtr = (unsigned char *) configDescrMIDI;
		return sizeof(configDescrMIDI);
	}
}

// ------------------------------------------------------------------------------
// - usbFunctionSetup
// ------------------------------------------------------------------------------
// this function gets called when the usb driver receives a non standard request
// that is: our own requests defined in ../common/gnusb_cmds.h
unsigned char usbFunctionSetup(unsigned char data[8])
{
	void *pVoid = data;
	usbRequest_t    *rq = static_cast<usbRequest_t*>(pVoid);

	if ((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {	/* class request type */

		/*  Prepare bulk-in endpoint to respond to early termination   */
		if ((rq->bmRequestType & USBRQ_DIR_MASK) ==
		    USBRQ_DIR_HOST_TO_DEVICE) {}
	}
	switch (data[1]) {
// 								----------------------------   Start Bootloader for reprogramming the gnusb    		
		case GNUSBCORE_CMD_START_BOOTLOADER:

			startBootloader();
			break;
				
		default:
			break;
	}
	return 0xff;
}


// ---------------------------------------------------------------------------
//  usbFunctionRead                                                          
// ---------------------------------------------------------------------------

unsigned char usbFunctionRead(unsigned char * data, unsigned char len)
{

//	statusLedToggle(StatusLed_Yellow);

//???? thats from http://cryptomys.de/horo/V-USB-MIDI/index.html
	data[0] = 0;
	data[1] = 0;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;
	data[5] = 0;
	data[6] = 0;

	return 7;
}


/*---------------------------------------------------------------------------*/
/* usbFunctionWrite                                                          */
/*---------------------------------------------------------------------------*/

unsigned char usbFunctionWrite(unsigned char * data, unsigned char len)
{
	return 1;
}

/*---------------------------------------------------------------------------*/
/* usbFunctionWriteOut                                                       */
/*                                                                           */
/* this Function is called if a MIDI Out message (from PC) arrives.          */
/*                                                                           */
/*---------------------------------------------------------------------------*/
void usbFunctionWriteOut(unsigned char * data, unsigned char len)
{

	statusLedBlink(StatusLed_Yellow);

	while (len >= sizeof(midi_msg)) {
	
		midi_msg* msg = (midi_msg*)data;
		
		MIDI.receiveMIDI(msg->byte[0],msg->byte[1],msg->byte[2]);

		data += sizeof(midi_msg);
		len -= sizeof(midi_msg);
	}
}



// ------------------------------------------------------------------------------
// - doPeriodical
// ------------------------------------------------------------------------------
// stuff that has do be done often in the loop and not forgotten while delaying


void doPeriodical(void) {

		if (blinkstop) {
			if (millis() >= blinkstop) {
				statusLedOff(StatusLed_Yellow);
				blinkstop = 0;
			}
		}	
		
		usbPoll();
		MIDI.sendMIDI();        


		wdt_reset();
}		




// ------------------------------------------------------------------------------
// --------------------- Init AD Converter
// ------------------------------------------------------------------------------
void adInit(void){



	ADCSRA |= (1 << ADEN);				// enable ADC (turn on ADC power)
	ADCSRA &= ~(1 << ADATE);			// default to single sample convert mode
										// Set ADC-Prescaler (-> precision vs. speed)

	ADCSRA = ((ADCSRA & ~ADC_PRESCALE_MASK) | ADC_PRESCALE_DIV64); // Set ADC Reference Voltage to AVCC
	
	#if defined(__AVR_ATtiny85__)
		ADMUX = 0;	// make sure we don't have AREF on PB0 which is used as a usb pullup
	#else			// for bigger chips, use AREF with capacitor
		ADMUX |= (1 << REFS0);		
		ADMUX &= ~(1 << REFS1);
	#endif

	ADCSRA &= ~(1 << ADLAR);				// set to right-adjusted result//	sbi(ADCSRA, ADIE);				// enable ADC interrupts
	ADCSRA &= ~(1 << ADIE);				// disable ADC interrupts
//	ad_initialized = 1;
}


// ------------------------------------------------------------------------------
// - main
// ------------------------------------------------------------------------------

int main(void)
{
	MCUCSR = (1 << PORF);			// set power on reset flag just to be sure

	init();
	//adInit();

	wdt_enable(WDTO_1S);	// enable watchdog timer
	
	DDRD = (1 << 5) | (1 << 6); 	// LEDS = output


	unsigned char   i = 0;

    usbInit();
  
    // enforce USB re-enumerate: 

	cli();    
    usbDeviceDisconnect();  // do this while interrupts are disabled 
    while(--i){         // fake USB disconnect for > 250 ms 
        wdt_reset();
        delay(1);
    }

    usbDeviceConnect();
    sei();
	

	statusLedOn(StatusLed_Green);
	
	setup();

	for (;;) {
		loop();	
		doPeriodical();
	}
        
	return 0;
}

