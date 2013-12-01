# Name: Makefile
# Project: USBaspLoader
# Author: Christian Starkjohann
# Creation Date: 2007-12-10
# Tabsize: 4
# Copyright: (c) 2007 by OBJECTIVE DEVELOPMENT Software GmbH
# License: GNU GPL v2 (see License.txt)
# This Revision: $Id: Makefile 487 2008-02-05 18:23:57Z cs $

###############################################################################
# Configure the following variables according to your AVR.
# Program the device with
#     make fuse    # to set the clock generator, boot section size etc.
#     make flash   # to load the boot loader into flash


#F_CPU = 12000000
F_CPU = 20000000
#DEVICE = atmega16
DEVICE = atmega644p

ifeq ($(DEVICE),atmega644)
	FUSEOPT = -U lfuse:w:0xf7:m -U hfuse:w:0xdc:m -U efuse:w:0xfc:m
	BOOTLOADER_ADDRESS = F800
	DEFINES = -D__ATMEGA644__=1 -DTEST=$(TEST) -DDEVICE=$(DEVICE)
else ifeq ($(DEVICE),atmega644p)
	FUSEOPT = -U lfuse:w:0xf7:m -U hfuse:w:0xdc:m -U efuse:w:0xfc:m
	BOOTLOADER_ADDRESS = F800
	DEFINES = -D__ATMEGA644p__=1 -DTEST=$(TEST) -DDEVICE=$(DEVICE)
else													#atmega16
	FUSEOPT = -U lfuse:w:0x2f:m -U hfuse:w:0xc8:m   #brown out detection 4.0V enabled
	BOOTLOADER_ADDRESS = 3800
	DEFINES = -DTEST=$(TEST) -DDEVICE=$(DEVICE)#-DDEBUG_LEVEL=2
endif

PROGRAMMER = -c usbasp -P usb
#PROGRAMMER = -c stk500 -P /dev/cu.U*
#PROGRAMMER = -c stk500v2 -P avrdoper

###############################################################################

# Tools:
AVRDUDE = avrdude $(PROGRAMMER) -p $(DEVICE)
CC = avr-gcc

# Options:
#-DDEBUG_LEVEL=2
#CFLAGS = -Wall -Os -I. -mmcu=$(DEVICE) -DF_CPU=$(F_CPU) $(DEFINES)
#LDFLAGS = -Wl,--section-start=.text=$(BOOTLOADER_ADDRESS)
CFLAGS = -Wall -Os -fno-move-loop-invariants -fno-tree-scev-cprop -fno-inline-small-functions -I. -mmcu=$(DEVICE) -DF_CPU=$(F_CPU) $(DEFINES)
LDFLAGS = -Wl,--relax,--gc-sections -Wl,--section-start=.text=$(BOOTLOADER_ADDRESS)

OBJECTS =  usbdrv/usbdrvasm.o  main.o

# symbolic targets:
all: main.hex

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

.S.o:
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

.c.s:
	$(CC) $(CFLAGS) -S $< -o $@

flash:	all
	$(AVRDUDE) -U flash:w:main.hex:i

readflash:
	$(AVRDUDE) -U flash:r:read.hex:i

fuse:
	$(AVRDUDE) $(FUSEOPT)

lock:
	$(AVRDUDE) $(LOCKOPT)

read_fuses:
	$(UISP) --rd_fuses

clean:
	rm -f main.hex main.bin *.o usbdrv/*.o main.s usbdrv/oddebug.s usbdrv/usbdrv.s

# file targets:
main.bin:	$(OBJECTS)
	$(CC) $(CFLAGS) -o main.bin $(OBJECTS) $(LDFLAGS)

main.hex:	main.bin
	rm -f main.hex main.eep.hex
	avr-objcopy -j .text -j .data -O ihex main.bin main.hex
	avr-size main.hex

disasm:	main.bin
	avr-objdump -d main.bin

cpp:
	$(CC) $(CFLAGS) -E main.c

# Special rules for generating hex files for various devices and clock speeds
ALLHEXFILES = hexfiles/mega8_12mhz.hex hexfiles/mega8_15mhz.hex hexfiles/mega8_16mhz.hex \
	hexfiles/mega88_12mhz.hex hexfiles/mega88_15mhz.hex hexfiles/mega88_16mhz.hex \
	hexfiles/mega168_12mhz.hex hexfiles/mega168_15mhz.hex hexfiles/mega168_16mhz.hex

allhexfiles: $(ALLHEXFILES)
	$(MAKE) clean
	avr-size hexfiles/*.hex

$(ALLHEXFILES):
	@[ -d hexfiles ] || mkdir hexfiles
	@device=`echo $@ | sed -e 's|.*/mega||g' -e 's|_.*||g'`; \
	clock=`echo $@ | sed -e 's|.*_||g' -e 's|mhz.*||g'`; \
	addr=`echo $$device | sed -e 's/\([0-9]\)8/\1/g' | awk '{printf("%x", ($$1 - 2) * 1024)}'`; \
	echo "### Make with F_CPU=$${clock}000000 DEVICE=atmega$$device BOOTLOADER_ADDRESS=$$addr"; \
	$(MAKE) clean; \
	$(MAKE) main.hex F_CPU=$${clock}000000 DEVICE=atmega$$device BOOTLOADER_ADDRESS=$$addr DEFINES=-DUSE_AUTOCONFIG=1
	mv main.hex $@
