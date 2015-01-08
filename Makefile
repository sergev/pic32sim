#
# Check for a required build environment.
#
ifndef IMPERAS_HOME
IMPERAS_ERROR   := $(error "Please source 'imperas.environ' to setup Imperas environment.")
endif

#
# Processors supported:
#       PIC32 MX7
#       PIC32 MZ
#
ifeq ($(CPU),mx7)
    DEFINES     = -DPIC32MX7
endif
ifeq ($(CPU),mz)
    DEFINES     = -DPIC32MZ
endif

#
# Board types supported:
#	Microchip Explorer16
#	Maximite Computer
#	chipKIT Max32
#	chipKIT WiFire
#	Microchip MEB-II
#
ifeq ($(BOARD),explorer16)
    DEFINES     += -DEXPLORER16
endif
ifeq ($(BOARD),maximite)
    DEFINES     += -DMAXIMITE
endif
ifeq ($(BOARD),max32)
    DEFINES     += -DMAX32
endif
ifeq ($(BOARD),wifire)
    DEFINES     += -DWIFIRE
endif
ifeq ($(BOARD),meb2)
    DEFINES     += -DMEBII
endif

#
# Common options
#
OBJLIST		= loadhex.o main.o sdcard.o spi.o uart.o vtty.o
OPTIMIZE        = -O2
OBJDIR          = obj-$(CPU)-$(BOARD)
OBJ             = $(OBJDIR)/$(CPU).o \
                  $(addprefix $(OBJDIR)/,$(OBJLIST))

CFLAGS          = -m32 -g -Wall -Werror $(OPTIMIZE) $(DEFINES) \
                  -I$(IMPERAS_HOME)/ImpPublic/include/common \
                  -I$(IMPERAS_HOME)/ImpPublic/include/host \
                  -I$(IMPERAS_HOME)/ImpProprietary/include/host

LDFLAGS         = -m32
LIBS            = -L$(IMPERAS_HOME)/bin/$(IMPERAS_ARCH) \
                  -lRuntimeLoader -lpthread

ifeq ($(CPU),)
all:
		$(MAKE) CPU=mx7 BOARD=explorer16
		$(MAKE) CPU=mx7 BOARD=max32
		$(MAKE) CPU=mx7 BOARD=maximite
		$(MAKE) CPU=mz BOARD=explorer16
		$(MAKE) CPU=mz BOARD=wifire
		$(MAKE) CPU=mz BOARD=meb2
else
all:            pic32$(CPU)-$(BOARD)
endif

pic32$(CPU)-$(BOARD): $(OBJ)
		$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

$(OBJDIR)/%.o:  %.c
		@mkdir -p $(@D)
		$(CC) $(CFLAGS) -c $< -o $@

clean:
		rm -rf *.o *~ obj-* pic32mx7-* pic32mz-*
###
$(OBJDIR)/loadhex.o: loadhex.c globals.h
$(OBJDIR)/main.o: main.c globals.h
$(OBJDIR)/mx7.o: mx7.c globals.h pic32mx.h
$(OBJDIR)/mz.o: mz.c globals.h pic32mz.h
$(OBJDIR)/sdcard.o: sdcard.c globals.h
$(OBJDIR)/spi.o: spi.c globals.h pic32mx.h pic32mz.h
$(OBJDIR)/uart.o: uart.c globals.h pic32mx.h pic32mz.h
$(OBJDIR)/vtty.o: vtty.c globals.h
