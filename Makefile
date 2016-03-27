MCU_TARGET = atmega328p
F_CPU = 16000000L

# The serial port for the Arduino to upload to
ARDUINO_PORT = /dev/tty.usbmodem1411

UPLOADER = avrdude

UPLOADER_FLAGS  = -c arduino
UPLOADER_FLAGS += -p $(MCU_TARGET)
UPLOADER_FLAGS += -P $(ARDUINO_PORT)

QUIET = @

#########################################################################

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

sidish.o: sidish.c Makefile
	$(QUIET)$(CC) -c $(CFLAGS) -Wa,-adhlns=$(@:.o=.al) -o $@ $<

sidish.elf: sidish.o
	$(QUIET)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

sidish.hex: sidish.elf
	$(QUIET)$(OBJCOPY) -j .text -j .data -O ihex -R .eeprom -R .fuse -R .lock $< $@

sidish.bin: sidish.elf
	$(QUIET)$(OBJCOPY) -j .text -j .data -O binary $< $@
	@echo Build complete. $@ is `stat -f '%z' $@` bytes.

program: $(PROGRAM).hex
	$(UPLOADER) $(UPLOADER_FLAGS) -U flash:w:$(PROGRAM).hex

clean:
	rm -rf *.hex *.al *.bin *.elf *.i *.o *.s *~
