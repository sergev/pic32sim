#
# Check for a required build environment.
#
ifndef IMPERAS_HOME
IMPERAS_ERROR   := $(error "Please source 'imperas.environ' to setup Imperas environment.")
endif

#
# Processor: PIC32 MX7
#
CFLAGS_MX7      = -DPIC32MX7
OBJ_MX7		= obj-mx7/mx7.o

#
# Processor: PIC32 MZ
#
CFLAGS_MZ       = -DPIC32MZ
OBJ_MZ		= obj-mz/mz.o

#
# Board types supported:
#	EXPLORER16 - Microchip Explorer16
#	MAXIMITE   - Maximite Computer
#	MAX32      - chipKIT Max32
#	WIFIRE     - chipKIT WiFire
#
CFLAGS_MX7      += -DEXPLORER16
CFLAGS_MZ       += -DWIFIRE

#
# Common options
#
OPTIMIZE        = -O2
OBJ		= loadhex.o main.o sdcard.o spi.o uart.o vtty.o
OBJ_MX7		+= $(addprefix obj-mx7/,$(OBJ))
OBJ_MZ		+= $(addprefix obj-mz/,$(OBJ))

CFLAGS          = -m32 -g -Wall -Werror $(OPTIMIZE) \
                  -I$(IMPERAS_HOME)/ImpPublic/include/common \
                  -I$(IMPERAS_HOME)/ImpPublic/include/host \
                  -I$(IMPERAS_HOME)/ImpProprietary/include/host

LDFLAGS         = -L$(IMPERAS_HOME)/bin/$(IMPERAS_ARCH) -lRuntimeLoader \
                  -lpthread

all:		pic32mx7 pic32mz

pic32mx7:       $(OBJ_MX7)
		$(CC) -m32 -o $@ $^ $(LDFLAGS)

pic32mz:        $(OBJ_MZ)
		$(CC) -m32 -o $@ $^ $(LDFLAGS)

obj-mx7/%.o:    %.c
		@mkdir -p $(@D)
		$(CC) -c -o $@ $< $(CFLAGS) $(CFLAGS_MX7)

obj-mz/%.o:     %.c
		@mkdir -p $(@D)
		$(CC) -c -o $@ $< $(CFLAGS) $(CFLAGS_MZ)

clean:
		rm -rf *.o *~ obj-mx7 obj-mz
###
obj-mx7/loadhex.o obj-mz/loadhex.o: loadhex.c globals.h
obj-mx7/main.o obj-mz/main.o: main.c globals.h
obj-mx7/mx7.o: mx7.c globals.h pic32mx.h
obj-mz/mz.o: mz.c globals.h pic32mz.h
obj-mx7/sdcard.o obj-mz/sdcard.o: sdcard.c globals.h
obj-mx7/spi.o obj-mz/spi.o: spi.c globals.h pic32mx.h pic32mz.h
obj-mx7/uart.o obj-mz/uart.o: uart.c globals.h pic32mx.h pic32mz.h
obj-mx7/vtty.o obj-mz/vtty.o: vtty.c globals.h
