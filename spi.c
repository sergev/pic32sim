/*
 * SPI ports.
 *
 * Copyright (C) 2014 Serge Vakulenko <serge@vak.ru>
 *
 * Permission to use, copy, modify, and distribute this software
 * and its documentation for any purpose and without fee is hereby
 * granted, provided that the above copyright notice appear in all
 * copies and that both that the copyright notice and this
 * permission notice and warranty disclaimer appear in supporting
 * documentation, and that the name of the author not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 *
 * The author disclaim all warranties with regard to this
 * software, including all implied warranties of merchantability
 * and fitness.  In no event shall the author be liable for any
 * special, indirect or consequential damages or any damages
 * whatsoever resulting from loss of use, data or profits, whether
 * in an action of contract, negligence or other tortious action,
 * arising out of or in connection with the use or performance of
 * this software.
 */
#include "globals.h"

#ifdef PIC32MX7
#   include "pic32mx.h"
#   define NUM_SPI      4               // number of SPI ports
#endif

#ifdef PIC32MZ
#   include "pic32mz.h"
#   define NUM_SPI      6               // number of SPI ports
#endif

#define SPI_IRQ_FAULT   0               // error irq offset
#define SPI_IRQ_TX      1               // transmitter irq offset
#define SPI_IRQ_RX      2               // receiver irq offset

static unsigned spi_buf[NUM_SPI][4];    // SPI transmit and receive buffer
static unsigned spi_rfifo[NUM_SPI];     // SPI read fifo counter
static unsigned spi_wfifo[NUM_SPI];     // SPI write fifo counter

unsigned sdcard_spi_port;               // SPI port number of SD card

static unsigned spi_irq[NUM_SPI] = {    // SPI interrupt numbers
    PIC32_IRQ_SPI1E,
    PIC32_IRQ_SPI2E,
    PIC32_IRQ_SPI3E,
    PIC32_IRQ_SPI4E,
#ifdef PIC32MZ
    PIC32_IRQ_SPI5E,
    PIC32_IRQ_SPI6E,
#endif
};

static unsigned spi_con[NUM_SPI] = {    // SPIxCON address
    SPI1CON, 
    SPI2CON, 
    SPI3CON, 
    SPI4CON, 
#ifdef PIC32MZ
    SPI5CON, 
    SPI6CON, 
#endif
};

static unsigned spi_stat[NUM_SPI] = {	// SPIxSTAT address
    SPI1STAT, 
    SPI2STAT, 
    SPI3STAT, 
    SPI4STAT, 
#ifdef PIC32MZ
    SPI5STAT, 
    SPI6STAT, 
#endif
};

unsigned spi_readbuf (int unit)
{
    unsigned result = spi_buf[unit][spi_rfifo[unit]];
    
    if (VALUE(spi_con[unit]) & PIC32_SPICON_ENHBUF) {
	spi_rfifo[unit]++;
	spi_rfifo[unit] &= 3;
    }
    if (VALUE(spi_stat[unit]) & PIC32_SPISTAT_SPIRBF) {
	VALUE(spi_stat[unit]) &= ~PIC32_SPISTAT_SPIRBF;
	//irq_clear (spi_irq[unit] + SPI_IRQ_RX);  
    }
    return result;
}

void spi_writebuf (int unit, unsigned val)
{
    /* Perform SD card i/o on configured SPI port. */
    if (unit == sdcard_spi_port) {
        unsigned result = 0;

        if (VALUE(spi_con[unit]) & PIC32_SPICON_MODE32) {
            /* 32-bit data width */
            result  = (unsigned char) sdcard_io (val >> 24) << 24;
            result |= (unsigned char) sdcard_io (val >> 16) << 16;
            result |= (unsigned char) sdcard_io (val >> 8) << 8;
            result |= (unsigned char) sdcard_io (val);

        } else if (VALUE(spi_con[unit]) & PIC32_SPICON_MODE16) {
            /* 16-bit data width */
            result = (unsigned char) sdcard_io (val >> 8) << 8;
            result |= (unsigned char) sdcard_io (val);

        } else {
            /* 8-bit data width */
            result = (unsigned char) sdcard_io (val);
        }
        spi_buf[unit][spi_wfifo[unit]] = result;
    } else {
        /* No device */
        spi_buf[unit][spi_wfifo[unit]] = ~0;
    }
    if (VALUE(spi_stat[unit]) & PIC32_SPISTAT_SPIRBF) {
        VALUE(spi_stat[unit]) |= PIC32_SPISTAT_SPIROV;
        //irq_raise (spi_irq[unit] + SPI_IRQ_FAULT);
    } else if (VALUE(spi_con[unit]) & PIC32_SPICON_ENHBUF) {
        spi_wfifo[unit]++;
        spi_wfifo[unit] &= 3;
        if (spi_wfifo[unit] == spi_rfifo[unit]) {
            VALUE(spi_stat[unit]) |= PIC32_SPISTAT_SPIRBF;
            //irq_raise (spi_irq[unit] + SPI_IRQ_RX);
        }
    } else {
        VALUE(spi_stat[unit]) |= PIC32_SPISTAT_SPIRBF;
        //irq_raise (spi_irq[unit] + SPI_IRQ_RX);
    }
}

void spi_control (int unit)
{
    if (! (VALUE(spi_con[unit]) & PIC32_SPICON_ON)) {
	irq_clear (spi_irq[unit] + SPI_IRQ_FAULT);
	irq_clear (spi_irq[unit] + SPI_IRQ_RX);
	irq_clear (spi_irq[unit] + SPI_IRQ_TX);
	VALUE(spi_stat[unit]) = PIC32_SPISTAT_SPITBE;
    } else if (! (VALUE(spi_con[unit]) & PIC32_SPICON_ENHBUF)) {
	spi_rfifo[unit] = 0;
	spi_wfifo[unit] = 0;
    }
}

void spi_reset()
{
    VALUE(SPI1CON)  = 0;
    VALUE(SPI1STAT) = PIC32_SPISTAT_SPITBE;     // Transmit buffer is empty
    spi_wfifo[0]    = 0;
    spi_rfifo[0]    = 0;
    VALUE(SPI1BRG)  = 0;

    VALUE(SPI2CON)  = 0;
    VALUE(SPI2STAT) = PIC32_SPISTAT_SPITBE;     // Transmit buffer is empty
    spi_wfifo[1]    = 0;
    spi_rfifo[1]    = 0;
    VALUE(SPI2BRG)  = 0;

    VALUE(SPI3CON)  = 0;
    VALUE(SPI3STAT) = PIC32_SPISTAT_SPITBE;     // Transmit buffer is empty
    spi_wfifo[2]    = 0;
    spi_rfifo[2]    = 0;
    VALUE(SPI3BRG)  = 0;

    VALUE(SPI4CON)  = 0;
    VALUE(SPI4STAT) = PIC32_SPISTAT_SPITBE;     // Transmit buffer is empty
    spi_wfifo[3]    = 0;
    spi_rfifo[3]    = 0;
    VALUE(SPI4BRG)  = 0;
#ifdef PIC32MZ
    VALUE(SPI5CON)  = 0;
    VALUE(SPI5STAT) = PIC32_SPISTAT_SPITBE;     // Transmit buffer is empty
    spi_wfifo[4]    = 0;
    spi_rfifo[4]    = 0;
    VALUE(SPI5BRG)  = 0;

    VALUE(SPI6CON)  = 0;
    VALUE(SPI6STAT) = PIC32_SPISTAT_SPITBE;     // Transmit buffer is empty
    spi_wfifo[5]    = 0;
    spi_rfifo[5]    = 0;
    VALUE(SPI6BRG)  = 0;

    VALUE(SPI1CON2) = 0;
    VALUE(SPI2CON2) = 0;
    VALUE(SPI3CON2) = 0;
    VALUE(SPI4CON2) = 0;
    VALUE(SPI5CON2) = 0;
    VALUE(SPI6CON2) = 0;
#endif
}
