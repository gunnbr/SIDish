SONG = Comic_Bakery.sng

MCU_TARGET = atmega328p
F_CPU = 16000000L

# The serial port for the Arduino to upload to
ARDUINO_PORT = /dev/tty.usbmodem1411

UPLOADER = avrdude

UPLOADER_FLAGS  = -c arduino
UPLOADER_FLAGS += -p $(MCU_TARGET)
UPLOADER_FLAGS += -P $(ARDUINO_PORT)

QUIET = 

#########################################################################
SONGNAME = $(subst .,_,$(SONG))

CC = avr-gcc
OBJDUMP = avr-objdump
OBJCOPY = avr-objcopy

PROGRAM = sidish

CFLAGS  = -DF_CPU=$(F_CPU)
CFLAGS += -mmcu=$(MCU_TARGET)
CFLAGS += -g
CFLAGS += -Wall
CFLAGS += -std=gnu99
CFLAGS += -funsigned-char
CFLAGS += -funsigned-bitfields
CFLAGS += -fpack-struct
CFLAGS += -fshort-enums
CFLAGS += -Winline
CFLAGS += -fno-move-loop-invariants
CFLAGS += -fno-tree-scev-cprop
CFLAGS += -Os
CFLAGS += -ffunction-sections
CFLAGS += -finline-functions-called-once
CFLAGS += -mcall-prologues
CFLAGS += -save-temps=obj

LDFLAGS  = -Wl,--as-needed
LDFLAGS += -Wl,--gc-sections

LIBS  = -lm

all: sidish.hex sidish.bin
.PHONY: all program

sidish.o: sidish.c goatplayer.c sidish.h Makefile
	$(QUIET)$(CC) -c $(CFLAGS) -Wa,-adhlns=$(@:.o=.al) -o $@ $<

sidish.elf: sidish.o songdata.o
	@echo Linking $<
	$(QUIET)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

sidish.hex: sidish.elf
	$(QUIET)$(OBJCOPY) -j .text -j .data -O ihex -R .eeprom -R .fuse -R .lock $< $@

sidish.bin: sidish.elf
	$(QUIET)$(OBJCOPY) -j .text -j .data -O binary $< $@
	@echo Build complete. $@ is `stat -f '%z' $@` bytes.

test.o: test.c
	gcc -o $@ $<

test: test.o
	gcc -o $@ $<
	./test

program: $(PROGRAM).hex
	$(UPLOADER) $(UPLOADER_FLAGS) -U flash:w:$(PROGRAM).hex

songdata.o: $(SONG)
	@echo "Creating binary song data"
	avr-objcopy --rename-section .data=.progmem.data,contents,alloc,load,readonly,data --redefine-sym _binary_$(SONGNAME)_start=song_start --redefine-sym _binary_$(SONGNAME)_end=song_end --redefine-sym _binary_$(SONGNAME)_size=song_size_sym -I binary -O elf32-avr $< $@

clean:
	rm -rf *.hex *.al *.bin *.elf *.i *.o *.s *~
