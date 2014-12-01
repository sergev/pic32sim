/*
 * UART ports.
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
#include <stdio.h>
#include "globals.h"

#ifdef PIC32MX7
#   include "pic32mx.h"
#endif

#ifdef PIC32MZ
#   include "pic32mz.h"
#endif

#define NUM_UART        6               // number of UART ports
#define UART_IRQ_ERR    0               // error irq offset
#define UART_IRQ_RX     1               // receiver irq offset
#define UART_IRQ_TX     2               // transmitter irq offset

static unsigned uart_irq[NUM_UART] = {  // UART interrupt numbers
    PIC32_IRQ_U1E,
    PIC32_IRQ_U2E,
    PIC32_IRQ_U3E,
    PIC32_IRQ_U4E,
    PIC32_IRQ_U5E,
    PIC32_IRQ_U6E,
};
static int uart_oactive[NUM_UART];      // UART output active
static int uart_odelay[NUM_UART];       // UART output delay count
static unsigned uart_sta[NUM_UART] =    // UxSTA address
    { U1STA, U2STA, U3STA, U4STA, U5STA, U6STA };
static unsigned uart_mode[NUM_UART] =    // UxMODE address
    { U1MODE, U2MODE, U3MODE, U4MODE, U5MODE, U6MODE };

#define OUTPUT_DELAY 3

/*
 * Read of UxRXREG register.
 */
unsigned uart_get_char (int unit)
{
    unsigned value;

    // Read a byte from input queue
    value = vtty_get_char (unit);
    VALUE(uart_sta[unit]) &= ~PIC32_USTA_URXDA;

    if (vtty_is_char_avail (unit)) {
        // One more byte available
        VALUE(uart_sta[unit]) |= PIC32_USTA_URXDA;
    } else {
        irq_clear (uart_irq[unit] + UART_IRQ_RX);
    }
    return value;
}

/*
 * Called before reading a value of UxBRG, U1MODE or U1STA registers.
 */
void uart_poll_status (int unit)
{
    // Keep receiver idle, transmit shift register always empty
    VALUE(uart_sta[unit]) |= PIC32_USTA_RIDLE | PIC32_USTA_TRMT;

    if (vtty_is_char_avail (unit)) {
        // Receive data available
        VALUE(uart_sta[unit]) |= PIC32_USTA_URXDA;
    }
    //printf ("<%x>", VALUE(uart_sta[unit])); fflush (stdout);
}

/*
 * Write to UxTXREG register.
 */
void uart_put_char (int unit, unsigned data)
{
    vtty_put_char (unit, data);
    if ((VALUE(uart_mode[unit]) & PIC32_UMODE_ON) &&
        (VALUE(uart_sta[unit]) & PIC32_USTA_UTXEN) &&
        ! uart_oactive[unit])
    {
        uart_oactive[unit] = 1;
        uart_odelay[unit] = 0;
        VALUE(uart_sta[unit]) |= PIC32_USTA_UTXBF;
    }
}

/*
 * Write to UxMODE register.
 */
void uart_update_mode (int unit)
{
    if (! (VALUE(uart_mode[unit]) & PIC32_UMODE_ON)) {
        irq_clear (uart_irq[unit] + UART_IRQ_RX);
        irq_clear (uart_irq[unit] + UART_IRQ_TX);
        VALUE(uart_sta[unit]) &= ~(PIC32_USTA_URXDA | PIC32_USTA_FERR |
                                   PIC32_USTA_PERR | PIC32_USTA_UTXBF);
        VALUE(uart_sta[unit]) |= PIC32_USTA_RIDLE | PIC32_USTA_TRMT;
    }
}

/*
 * Write to UxSTA register.
 */
void uart_update_status (int unit)
{
    if (! (VALUE(uart_sta[unit]) & PIC32_USTA_URXEN)) {
        irq_clear (uart_irq[unit] + UART_IRQ_RX);
        VALUE(uart_sta[unit]) &= ~(PIC32_USTA_URXDA | PIC32_USTA_FERR |
                                   PIC32_USTA_PERR);
    }
    if (! (VALUE(uart_sta[unit]) & PIC32_USTA_UTXEN)) {
        irq_clear (uart_irq[unit] + UART_IRQ_TX);
        VALUE(uart_sta[unit]) &= ~PIC32_USTA_UTXBF;
        VALUE(uart_sta[unit]) |= PIC32_USTA_TRMT;
    }
}

void uart_poll()
{
    int unit;

    for (unit=0; unit<NUM_UART; unit++) {
	if (! (VALUE(uart_mode[unit]) & PIC32_UMODE_ON)) {
	    /* UART disabled. */
    	    uart_oactive[unit] = 0;
            VALUE(uart_sta[unit]) &= ~PIC32_USTA_UTXBF;
	    continue;
	}

	/* UART enabled. */
	if ((VALUE(uart_sta[unit]) & PIC32_USTA_URXEN) && vtty_is_char_avail (unit)) {
	    /* Receive data available. */
	    VALUE(uart_sta[unit]) |= PIC32_USTA_URXDA;

	    /* Activate receive interrupt. */
	    irq_raise (uart_irq[unit] + UART_IRQ_RX);
	    continue;
	}

	if (! (VALUE(uart_sta[unit]) & PIC32_USTA_UTXEN)) {
	    /* Transmitter disabled. */
            uart_oactive[unit] = 0;
            continue;
        }
	if (uart_oactive[unit]) {
	    /* Activate transmit interrupt. */
	    if (++uart_odelay[unit] > OUTPUT_DELAY) {
//printf("uart%u: raise tx irq %u\n", unit, uart_irq[unit] + UART_IRQ_TX);
                irq_raise (uart_irq[unit] + UART_IRQ_TX);
                VALUE(uart_sta[unit]) &= ~PIC32_USTA_UTXBF;
                uart_oactive[unit] = 0;
            }
	}
    }
}

/*
 * Return true when any I/O is active.
 * Check uart output and pending input.
 */
int uart_active()
{
    int unit;

    for (unit=0; unit<NUM_UART; unit++) {
    	if (uart_oactive[unit])
	    return 1;
    	if (vtty_is_char_avail (unit))
	    return 1;
    }
    return 0;
}

void uart_reset()
{
    VALUE(U1MODE)  = 0;
    VALUE(U1STA)   = PIC32_USTA_RIDLE | PIC32_USTA_TRMT;
    VALUE(U1TXREG) = 0;
    VALUE(U1RXREG) = 0;
    VALUE(U1BRG)   = 0;
    VALUE(U2MODE)  = 0;
    VALUE(U2STA)   = PIC32_USTA_RIDLE | PIC32_USTA_TRMT;
    VALUE(U2TXREG) = 0;
    VALUE(U2RXREG) = 0;
    VALUE(U2BRG)   = 0;
    VALUE(U3MODE)  = 0;
    VALUE(U3STA)   = PIC32_USTA_RIDLE | PIC32_USTA_TRMT;
    VALUE(U3TXREG) = 0;
    VALUE(U3RXREG) = 0;
    VALUE(U3BRG)   = 0;
    VALUE(U4MODE)  = 0;
    VALUE(U4STA)   = PIC32_USTA_RIDLE | PIC32_USTA_TRMT;
    VALUE(U4TXREG) = 0;
    VALUE(U4RXREG) = 0;
    VALUE(U4BRG)   = 0;
    VALUE(U5MODE)  = 0;
    VALUE(U5STA)   = PIC32_USTA_RIDLE | PIC32_USTA_TRMT;
    VALUE(U5TXREG) = 0;
    VALUE(U5RXREG) = 0;
    VALUE(U5BRG)   = 0;
    VALUE(U6MODE)  = 0;
    VALUE(U6STA)   = PIC32_USTA_RIDLE | PIC32_USTA_TRMT;
    VALUE(U6TXREG) = 0;
    VALUE(U6RXREG) = 0;
    VALUE(U6BRG)   = 0;
}
