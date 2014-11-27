/*
 * Simulate the peripherals of PIC32 microcontroller.
 *
 * Copyright (C) 2014 Serge Vakulenko, <serge@vak.ru>
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
#include <unistd.h>
#include "globals.h"
#include "pic32mz.h"

#define STORAGE(name) case name: *namep = #name;
#define READONLY(name) case name: *namep = #name; goto readonly
#define WRITEOP(name) case name: *namep = #name; goto op_##name;\
                      case name+4: *namep = #name"CLR"; goto op_##name;\
                      case name+8: *namep = #name"SET"; goto op_##name;\
                      case name+12: *namep = #name"INV"; op_##name: \
                      VALUE(name) = write_op (VALUE(name), data, address)
#define WRITEOPX(name,label) \
		      case name: *namep = #name; goto op_##label;\
                      case name+4: *namep = #name"CLR"; goto op_##label;\
                      case name+8: *namep = #name"SET"; goto op_##label;\
                      case name+12: *namep = #name"INV"; goto op_##label
#define WRITEOPR(name,romask) \
                      case name: *namep = #name; goto op_##name;\
                      case name+4: *namep = #name"CLR"; goto op_##name;\
                      case name+8: *namep = #name"SET"; goto op_##name;\
                      case name+12: *namep = #name"INV"; op_##name: \
                      VALUE(name) &= romask; \
                      VALUE(name) |= write_op (VALUE(name), data, address) & ~(romask)

static uint32_t *bootmem;       // image of boot memory

static unsigned syskey_unlock;	// syskey state

static void update_irq_status()
{
    /* Assume no interrupts pending. */
    int cause_ripl = 0;
    int vector = 0;
    VALUE(INTSTAT) = 0;

    if ((VALUE(IFS0) & VALUE(IEC0)) ||
        (VALUE(IFS1) & VALUE(IEC1)) ||
        (VALUE(IFS2) & VALUE(IEC2)) ||
        (VALUE(IFS3) & VALUE(IEC3)) ||
        (VALUE(IFS4) & VALUE(IEC4)) ||
        (VALUE(IFS5) & VALUE(IEC5)))
    {
        /* Find the most prioritive pending interrupt,
         * it's vector and level. */
        int irq;
        for (irq=0; irq<=PIC32_IRQ_LAST; irq++) {
            int n = irq >> 5;

            if (((VALUE(IFS(n)) & VALUE(IEC(n))) >> (irq & 31)) & 1) {
//printf ("-- irq %u is pending\n", irq);
                /* Interrupt is pending. */
                int level = VALUE(IPC(irq >> 2));
                level >>= 2 + (irq & 3) * 8;
                level &= 7;
                if (level > cause_ripl) {
                    vector = irq;
                    cause_ripl = level;
                }
            }
        }
        VALUE(INTSTAT) = vector | (cause_ripl << 8);
//printf ("-- vector = %d, level = %d\n", vector, cause_ripl);
    }
//else printf ("-- no irq pending\n");

//printf ("   IFS=%08x %08x %08x %08x %08x %08x\n", VALUE(IFS0), VALUE(IFS1), VALUE(IFS2), VALUE(IFS3), VALUE(IFS4), VALUE(IFS5));
//printf ("   IEC=%08x %08x %08x %08x %08x %08x\n", VALUE(IEC0), VALUE(IEC1), VALUE(IEC2), VALUE(IEC3), VALUE(IEC4), VALUE(IEC5));

    eic_level_vector (cause_ripl, vector);
}

/*
 * Set interrupt flag status
 */
void irq_raise (int irq)
{
    if (VALUE(IFS(irq >> 5)) & (1 << (irq & 31)))
        return;
//printf ("-- %s() irq = %d\n", __func__, irq);
    VALUE(IFS(irq >> 5)) |= 1 << (irq & 31);
    update_irq_status();
}

/*
 * Clear interrupt flag status
 */
void irq_clear (int irq)
{
    if (! (VALUE(IFS(irq >> 5)) & (1 << (irq & 31))))
        return;
//printf ("-- %s() irq = %d\n", __func__, irq);
    VALUE(IFS(irq >> 5)) &= ~(1 << (irq & 31));
    update_irq_status();
}

static void gpio_write (int gpio_port, unsigned lat_value)
{
    /* Control SD card 0 */
    if (gpio_port == sdcard_gpio_port0 && sdcard_gpio_cs0) {
        sdcard_select (0, ! (lat_value & sdcard_gpio_cs0));
    }
    /* Control SD card 1 */
    if (gpio_port == sdcard_gpio_port1 && sdcard_gpio_cs1) {
        sdcard_select (1, ! (lat_value & sdcard_gpio_cs1));
    }
}

/*
 * Perform an assign/clear/set/invert operation.
 */
static inline unsigned write_op (a, b, op)
{
    switch (op & 0xc) {
    case 0x0: a = b;   break;   // Assign
    case 0x4: a &= ~b; break;   // Clear
    case 0x8: a |= b;  break;   // Set
    case 0xc: a ^= b;  break;   // Invert
    }
    return a;
}

unsigned io_read32 (unsigned address, unsigned *bufp, const char **namep)
{
    switch (address) {
    /*-------------------------------------------------------------------------
     * Interrupt controller registers.
     */
    STORAGE (INTCON); break;	// Interrupt Control
    STORAGE (INTSTAT); break;   // Interrupt Status
    STORAGE (IFS0); break;	// IFS(0..2) - Interrupt Flag Status
    STORAGE (IFS1); break;
    STORAGE (IFS2); break;
    STORAGE (IFS3); break;
    STORAGE (IFS4); break;
    STORAGE (IFS5); break;
    STORAGE (IEC0); break;	// IEC(0..2) - Interrupt Enable Control
    STORAGE (IEC1); break;
    STORAGE (IEC2); break;
    STORAGE (IEC3); break;
    STORAGE (IEC4); break;
    STORAGE (IEC5); break;
    STORAGE (IPC0); break;	// IPC(0..11) - Interrupt Priority Control
    STORAGE (IPC1); break;
    STORAGE (IPC2); break;
    STORAGE (IPC3); break;
    STORAGE (IPC4); break;
    STORAGE (IPC5); break;
    STORAGE (IPC6); break;
    STORAGE (IPC7); break;
    STORAGE (IPC8); break;
    STORAGE (IPC9); break;
    STORAGE (IPC10); break;
    STORAGE (IPC11); break;
    STORAGE (IPC12); break;
    STORAGE (IPC13); break;
    STORAGE (IPC14); break;
    STORAGE (IPC15); break;
    STORAGE (IPC16); break;
    STORAGE (IPC17); break;
    STORAGE (IPC18); break;
    STORAGE (IPC19); break;
    STORAGE (IPC20); break;
    STORAGE (IPC21); break;
    STORAGE (IPC22); break;
    STORAGE (IPC23); break;
    STORAGE (IPC24); break;
    STORAGE (IPC25); break;
    STORAGE (IPC26); break;
    STORAGE (IPC27); break;
    STORAGE (IPC28); break;
    STORAGE (IPC29); break;
    STORAGE (IPC30); break;
    STORAGE (IPC31); break;
    STORAGE (IPC32); break;
    STORAGE (IPC33); break;
    STORAGE (IPC34); break;
    STORAGE (IPC35); break;
    STORAGE (IPC36); break;
    STORAGE (IPC37); break;
    STORAGE (IPC38); break;
    STORAGE (IPC39); break;
    STORAGE (IPC40); break;
    STORAGE (IPC41); break;
    STORAGE (IPC42); break;
    STORAGE (IPC43); break;
    STORAGE (IPC44); break;
    STORAGE (IPC45); break;
    STORAGE (IPC46); break;
    STORAGE (IPC47); break;

    /*-------------------------------------------------------------------------
     * Prefetch controller.
     */
    STORAGE (PRECON); break;	// Prefetch Control
    STORAGE (PRESTAT); break;	// Prefetch Status

    /*-------------------------------------------------------------------------
     * System controller.
     */
    STORAGE (CFGCON); break;	// Configuration Control
    STORAGE (DEVID); break;	// Device Identifier
    STORAGE (SYSKEY); break;	// System Key
    STORAGE (RCON); break;	// Reset Control
    STORAGE (RSWRST);           // Software Reset
        if ((VALUE(RSWRST) & 1) && stop_on_reset) {
            exit(0);
        }
        break;
    STORAGE (OSCCON); break;	// Oscillator Control
    STORAGE (OSCTUN); break;	// Oscillator Tuning
    STORAGE (SPLLCON); break;	// System PLL Control
    STORAGE (PB1DIV); break;	// Peripheral bus 1 divisor
    STORAGE (PB2DIV); break;	// Peripheral bus 2 divisor
    STORAGE (PB3DIV); break;	// Peripheral bus 3 divisor
    STORAGE (PB4DIV); break;	// Peripheral bus 4 divisor
    STORAGE (PB5DIV); break;	// Peripheral bus 5 divisor
    STORAGE (PB7DIV); break;	// Peripheral bus 7 divisor
    STORAGE (PB8DIV); break;	// Peripheral bus 8 divisor

    /*-------------------------------------------------------------------------
     * Peripheral port select registers: input.
     */
    STORAGE (INT1R); break;
    STORAGE (INT2R); break;
    STORAGE (INT3R); break;
    STORAGE (INT4R); break;
    STORAGE (T2CKR); break;
    STORAGE (T3CKR); break;
    STORAGE (T4CKR); break;
    STORAGE (T5CKR); break;
    STORAGE (T6CKR); break;
    STORAGE (T7CKR); break;
    STORAGE (T8CKR); break;
    STORAGE (T9CKR); break;
    STORAGE (IC1R); break;
    STORAGE (IC2R); break;
    STORAGE (IC3R); break;
    STORAGE (IC4R); break;
    STORAGE (IC5R); break;
    STORAGE (IC6R); break;
    STORAGE (IC7R); break;
    STORAGE (IC8R); break;
    STORAGE (IC9R); break;
    STORAGE (OCFAR); break;
    STORAGE (U1RXR); break;
    STORAGE (U1CTSR); break;
    STORAGE (U2RXR); break;
    STORAGE (U2CTSR); break;
    STORAGE (U3RXR); break;
    STORAGE (U3CTSR); break;
    STORAGE (U4RXR); break;
    STORAGE (U4CTSR); break;
    STORAGE (U5RXR); break;
    STORAGE (U5CTSR); break;
    STORAGE (U6RXR); break;
    STORAGE (U6CTSR); break;
    STORAGE (SDI1R); break;
    STORAGE (SS1R); break;
    STORAGE (SDI2R); break;
    STORAGE (SS2R); break;
    STORAGE (SDI3R); break;
    STORAGE (SS3R); break;
    STORAGE (SDI4R); break;
    STORAGE (SS4R); break;
    STORAGE (SDI5R); break;
    STORAGE (SS5R); break;
    STORAGE (SDI6R); break;
    STORAGE (SS6R); break;
    STORAGE (C1RXR); break;
    STORAGE (C2RXR); break;
    STORAGE (REFCLKI1R); break;
    STORAGE (REFCLKI3R); break;
    STORAGE (REFCLKI4R); break;

    /*-------------------------------------------------------------------------
     * Peripheral port select registers: output.
     */
    STORAGE (RPA14R); break;
    STORAGE (RPA15R); break;
    STORAGE (RPB0R); break;
    STORAGE (RPB1R); break;
    STORAGE (RPB2R); break;
    STORAGE (RPB3R); break;
    STORAGE (RPB5R); break;
    STORAGE (RPB6R); break;
    STORAGE (RPB7R); break;
    STORAGE (RPB8R); break;
    STORAGE (RPB9R); break;
    STORAGE (RPB10R); break;
    STORAGE (RPB14R); break;
    STORAGE (RPB15R); break;
    STORAGE (RPC1R); break;
    STORAGE (RPC2R); break;
    STORAGE (RPC3R); break;
    STORAGE (RPC4R); break;
    STORAGE (RPC13R); break;
    STORAGE (RPC14R); break;
    STORAGE (RPD0R); break;
    STORAGE (RPD1R); break;
    STORAGE (RPD2R); break;
    STORAGE (RPD3R); break;
    STORAGE (RPD4R); break;
    STORAGE (RPD5R); break;
    STORAGE (RPD6R); break;
    STORAGE (RPD7R); break;
    STORAGE (RPD9R); break;
    STORAGE (RPD10R); break;
    STORAGE (RPD11R); break;
    STORAGE (RPD12R); break;
    STORAGE (RPD14R); break;
    STORAGE (RPD15R); break;
    STORAGE (RPE3R); break;
    STORAGE (RPE5R); break;
    STORAGE (RPE8R); break;
    STORAGE (RPE9R); break;
    STORAGE (RPF0R); break;
    STORAGE (RPF1R); break;
    STORAGE (RPF2R); break;
    STORAGE (RPF3R); break;
    STORAGE (RPF4R); break;
    STORAGE (RPF5R); break;
    STORAGE (RPF8R); break;
    STORAGE (RPF12R); break;
    STORAGE (RPF13R); break;
    STORAGE (RPG0R); break;
    STORAGE (RPG1R); break;
    STORAGE (RPG6R); break;
    STORAGE (RPG7R); break;
    STORAGE (RPG8R); break;
    STORAGE (RPG9R); break;

    /*-------------------------------------------------------------------------
     * General purpose IO signals.
     */
    STORAGE (ANSELA); break;    // Port A: analog select
    STORAGE (TRISA); break;     // Port A: mask of inputs
    STORAGE (PORTA); break;     // Port A: read inputs
    STORAGE (LATA); break;      // Port A: read outputs
    STORAGE (ODCA); break;      // Port A: open drain configuration
    STORAGE (CNPUA); break;     // Input pin pull-up
    STORAGE (CNPDA); break;     // Input pin pull-down
    STORAGE (CNCONA); break;    // Interrupt-on-change control
    STORAGE (CNENA); break;     // Input change interrupt enable
    STORAGE (CNSTATA); break;   // Input change status

    STORAGE (ANSELB); break;    // Port B: analog select
    STORAGE (TRISB); break;     // Port B: mask of inputs
    STORAGE (PORTB); break;     // Port B: read inputs
    STORAGE (LATB); break;      // Port B: read outputs
    STORAGE (ODCB); break;      // Port B: open drain configuration
    STORAGE (CNPUB); break;     // Input pin pull-up
    STORAGE (CNPDB); break;     // Input pin pull-down
    STORAGE (CNCONB); break;    // Interrupt-on-change control
    STORAGE (CNENB); break;     // Input change interrupt enable
    STORAGE (CNSTATB); break;   // Input change status

    STORAGE (ANSELC); break;    // Port C: analog select
    STORAGE (TRISC); break;     // Port C: mask of inputs
    STORAGE (PORTC); break;     // Port C: read inputs
    STORAGE (LATC); break;      // Port C: read outputs
    STORAGE (ODCC); break;      // Port C: open drain configuration
    STORAGE (CNPUC); break;     // Input pin pull-up
    STORAGE (CNPDC); break;     // Input pin pull-down
    STORAGE (CNCONC); break;    // Interrupt-on-change control
    STORAGE (CNENC); break;     // Input change interrupt enable
    STORAGE (CNSTATC); break;   // Input change status

    STORAGE (ANSELD); break;    // Port D: analog select
    STORAGE (TRISD); break;     // Port D: mask of inputs
    STORAGE (PORTD); break;	// Port D: read inputs
    STORAGE (LATD); break;      // Port D: read outputs
    STORAGE (ODCD); break;      // Port D: open drain configuration
    STORAGE (CNPUD); break;     // Input pin pull-up
    STORAGE (CNPDD); break;     // Input pin pull-down
    STORAGE (CNCOND); break;    // Interrupt-on-change control
    STORAGE (CNEND); break;     // Input change interrupt enable
    STORAGE (CNSTATD); break;   // Input change status

    STORAGE (ANSELE); break;    // Port E: analog select
    STORAGE (TRISE); break;     // Port E: mask of inputs
    STORAGE (PORTE); break;	// Port E: read inputs
    STORAGE (LATE); break;      // Port E: read outputs
    STORAGE (ODCE); break;      // Port E: open drain configuration
    STORAGE (CNPUE); break;     // Input pin pull-up
    STORAGE (CNPDE); break;     // Input pin pull-down
    STORAGE (CNCONE); break;    // Interrupt-on-change control
    STORAGE (CNENE); break;     // Input change interrupt enable
    STORAGE (CNSTATE); break;   // Input change status

    STORAGE (ANSELF); break;    // Port F: analog select
    STORAGE (TRISF); break;     // Port F: mask of inputs
    STORAGE (PORTF); break;     // Port F: read inputs
    STORAGE (LATF); break;      // Port F: read outputs
    STORAGE (ODCF); break;      // Port F: open drain configuration
    STORAGE (CNPUF); break;     // Input pin pull-up
    STORAGE (CNPDF); break;     // Input pin pull-down
    STORAGE (CNCONF); break;    // Interrupt-on-change control
    STORAGE (CNENF); break;     // Input change interrupt enable
    STORAGE (CNSTATF); break;   // Input change status

    STORAGE (ANSELG); break;    // Port G: analog select
    STORAGE (TRISG); break;     // Port G: mask of inputs
    STORAGE (PORTG); break;     // Port G: read inputs
    STORAGE (LATG); break;      // Port G: read outputs
    STORAGE (ODCG); break;      // Port G: open drain configuration
    STORAGE (CNPUG);            // Input pin pull-up
        // Enter critical region
        dump_regs("Enter");
        break;
    STORAGE (CNPDG);            // Input pin pull-down
        // Exit critical region
        dump_regs("Exit");
        break;
    STORAGE (CNCONG); break;    // Interrupt-on-change control
    STORAGE (CNENG); break;     // Input change interrupt enable
    STORAGE (CNSTATG); break;   // Input change status

    /*-------------------------------------------------------------------------
     * UART 1.
     */
    STORAGE (U1RXREG);                          // Receive data
        *bufp = uart_get_char(0);
        break;
    STORAGE (U1BRG); break;                     // Baud rate
    STORAGE (U1MODE); break;                    // Mode
    STORAGE (U1STA);                            // Status and control
        uart_poll_status(0);
        break;
    STORAGE (U1TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U1MODECLR); *bufp = 0; break;
    STORAGE (U1MODESET); *bufp = 0; break;
    STORAGE (U1MODEINV); *bufp = 0; break;
    STORAGE (U1STACLR);  *bufp = 0; break;
    STORAGE (U1STASET);  *bufp = 0; break;
    STORAGE (U1STAINV);  *bufp = 0; break;
    STORAGE (U1BRGCLR);  *bufp = 0; break;
    STORAGE (U1BRGSET);  *bufp = 0; break;
    STORAGE (U1BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 2.
     */
    STORAGE (U2RXREG);                          // Receive data
        *bufp = uart_get_char(1);
        break;
    STORAGE (U2BRG); break;                     // Baud rate
    STORAGE (U2MODE); break;                    // Mode
    STORAGE (U2STA);                            // Status and control
        uart_poll_status(1);
        break;
    STORAGE (U2TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U2MODECLR); *bufp = 0; break;
    STORAGE (U2MODESET); *bufp = 0; break;
    STORAGE (U2MODEINV); *bufp = 0; break;
    STORAGE (U2STACLR);  *bufp = 0; break;
    STORAGE (U2STASET);  *bufp = 0; break;
    STORAGE (U2STAINV);  *bufp = 0; break;
    STORAGE (U2BRGCLR);  *bufp = 0; break;
    STORAGE (U2BRGSET);  *bufp = 0; break;
    STORAGE (U2BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 3.
     */
    STORAGE (U3RXREG);                          // Receive data
        *bufp = uart_get_char(2);
        break;
    STORAGE (U3BRG); break;                     // Baud rate
    STORAGE (U3MODE); break;                    // Mode
    STORAGE (U3STA);                            // Status and control
        uart_poll_status(2);
        break;
    STORAGE (U3TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U3MODECLR); *bufp = 0; break;
    STORAGE (U3MODESET); *bufp = 0; break;
    STORAGE (U3MODEINV); *bufp = 0; break;
    STORAGE (U3STACLR);  *bufp = 0; break;
    STORAGE (U3STASET);  *bufp = 0; break;
    STORAGE (U3STAINV);  *bufp = 0; break;
    STORAGE (U3BRGCLR);  *bufp = 0; break;
    STORAGE (U3BRGSET);  *bufp = 0; break;
    STORAGE (U3BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 4.
     */
    STORAGE (U4RXREG);                          // Receive data
        *bufp = uart_get_char(3);
        break;
    STORAGE (U4BRG); break;                     // Baud rate
    STORAGE (U4MODE); break;                    // Mode
    STORAGE (U4STA);                            // Status and control
        uart_poll_status(3);
        break;
    STORAGE (U4TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U4MODECLR); *bufp = 0; break;
    STORAGE (U4MODESET); *bufp = 0; break;
    STORAGE (U4MODEINV); *bufp = 0; break;
    STORAGE (U4STACLR);  *bufp = 0; break;
    STORAGE (U4STASET);  *bufp = 0; break;
    STORAGE (U4STAINV);  *bufp = 0; break;
    STORAGE (U4BRGCLR);  *bufp = 0; break;
    STORAGE (U4BRGSET);  *bufp = 0; break;
    STORAGE (U4BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 5.
     */
    STORAGE (U5RXREG);                          // Receive data
        *bufp = uart_get_char(4);
        break;
    STORAGE (U5BRG); break;                     // Baud rate
    STORAGE (U5MODE); break;                    // Mode
    STORAGE (U5STA);                            // Status and control
        uart_poll_status(4);
        break;
    STORAGE (U5TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U5MODECLR); *bufp = 0; break;
    STORAGE (U5MODESET); *bufp = 0; break;
    STORAGE (U5MODEINV); *bufp = 0; break;
    STORAGE (U5STACLR);  *bufp = 0; break;
    STORAGE (U5STASET);  *bufp = 0; break;
    STORAGE (U5STAINV);  *bufp = 0; break;
    STORAGE (U5BRGCLR);  *bufp = 0; break;
    STORAGE (U5BRGSET);  *bufp = 0; break;
    STORAGE (U5BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * UART 6.
     */
    STORAGE (U6RXREG);                          // Receive data
        *bufp = uart_get_char(5);
        break;
    STORAGE (U6BRG); break;                     // Baud rate
    STORAGE (U6MODE); break;                    // Mode
    STORAGE (U6STA);                            // Status and control
        uart_poll_status(5);
        break;
    STORAGE (U6TXREG);   *bufp = 0; break;      // Transmit
    STORAGE (U6MODECLR); *bufp = 0; break;
    STORAGE (U6MODESET); *bufp = 0; break;
    STORAGE (U6MODEINV); *bufp = 0; break;
    STORAGE (U6STACLR);  *bufp = 0; break;
    STORAGE (U6STASET);  *bufp = 0; break;
    STORAGE (U6STAINV);  *bufp = 0; break;
    STORAGE (U6BRGCLR);  *bufp = 0; break;
    STORAGE (U6BRGSET);  *bufp = 0; break;
    STORAGE (U6BRGINV);  *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 1.
     */
    STORAGE (SPI1CON); break;                   // Control
    STORAGE (SPI1CONCLR); *bufp = 0; break;
    STORAGE (SPI1CONSET); *bufp = 0; break;
    STORAGE (SPI1CONINV); *bufp = 0; break;
    STORAGE (SPI1STAT); break;                  // Status
    STORAGE (SPI1STATCLR); *bufp = 0; break;
    STORAGE (SPI1STATSET); *bufp = 0; break;
    STORAGE (SPI1STATINV); *bufp = 0; break;
    STORAGE (SPI1BUF);                          // Buffer
        *bufp = spi_readbuf (0);
        break;
    STORAGE (SPI1BRG); break;                   // Baud rate
    STORAGE (SPI1BRGCLR); *bufp = 0; break;
    STORAGE (SPI1BRGSET); *bufp = 0; break;
    STORAGE (SPI1BRGINV); *bufp = 0; break;
    STORAGE (SPI1CON2); break;                   // Control 2
    STORAGE (SPI1CON2CLR); *bufp = 0; break;
    STORAGE (SPI1CON2SET); *bufp = 0; break;
    STORAGE (SPI1CON2INV); *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 2.
     */
    STORAGE (SPI2CON); break;                   // Control
    STORAGE (SPI2CONCLR); *bufp = 0; break;
    STORAGE (SPI2CONSET); *bufp = 0; break;
    STORAGE (SPI2CONINV); *bufp = 0; break;
    STORAGE (SPI2STAT); break;                  // Status
    STORAGE (SPI2STATCLR); *bufp = 0; break;
    STORAGE (SPI2STATSET); *bufp = 0; break;
    STORAGE (SPI2STATINV); *bufp = 0; break;
    STORAGE (SPI2BUF);                          // Buffer
        *bufp = spi_readbuf (1);
        break;
    STORAGE (SPI2BRG); break;                   // Baud rate
    STORAGE (SPI2BRGCLR); *bufp = 0; break;
    STORAGE (SPI2BRGSET); *bufp = 0; break;
    STORAGE (SPI2BRGINV); *bufp = 0; break;
    STORAGE (SPI2CON2); break;                   // Control 2
    STORAGE (SPI2CON2CLR); *bufp = 0; break;
    STORAGE (SPI2CON2SET); *bufp = 0; break;
    STORAGE (SPI2CON2INV); *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 3.
     */
    STORAGE (SPI3CON); break;                   // Control
    STORAGE (SPI3CONCLR); *bufp = 0; break;
    STORAGE (SPI3CONSET); *bufp = 0; break;
    STORAGE (SPI3CONINV); *bufp = 0; break;
    STORAGE (SPI3STAT); break;                  // Status
    STORAGE (SPI3STATCLR); *bufp = 0; break;
    STORAGE (SPI3STATSET); *bufp = 0; break;
    STORAGE (SPI3STATINV); *bufp = 0; break;
    STORAGE (SPI3BUF);                          // SPIx Buffer
        *bufp = spi_readbuf (2);
        break;
    STORAGE (SPI3BRG); break;                   // Baud rate
    STORAGE (SPI3BRGCLR); *bufp = 0; break;
    STORAGE (SPI3BRGSET); *bufp = 0; break;
    STORAGE (SPI3BRGINV); *bufp = 0; break;
    STORAGE (SPI3CON2); break;                   // Control 2
    STORAGE (SPI3CON2CLR); *bufp = 0; break;
    STORAGE (SPI3CON2SET); *bufp = 0; break;
    STORAGE (SPI3CON2INV); *bufp = 0; break;

    /*-------------------------------------------------------------------------
     * SPI 4.
     */
    STORAGE (SPI4CON); break;                   // Control
    STORAGE (SPI4CONCLR); *bufp = 0; break;
    STORAGE (SPI4CONSET); *bufp = 0; break;
    STORAGE (SPI4CONINV); *bufp = 0; break;
    STORAGE (SPI4STAT); break;                  // Status
    STORAGE (SPI4STATCLR); *bufp = 0; break;
    STORAGE (SPI4STATSET); *bufp = 0; break;
    STORAGE (SPI4STATINV); *bufp = 0; break;
    STORAGE (SPI4BUF);                          // Buffer
        *bufp = spi_readbuf (3);
        break;
    STORAGE (SPI4BRG); break;                   // Baud rate
    STORAGE (SPI4BRGCLR); *bufp = 0; break;
    STORAGE (SPI4BRGSET); *bufp = 0; break;
    STORAGE (SPI4BRGINV); *bufp = 0; break;
    STORAGE (SPI4CON2); break;                   // Control 2
    STORAGE (SPI4CON2CLR); *bufp = 0; break;
    STORAGE (SPI4CON2SET); *bufp = 0; break;
    STORAGE (SPI4CON2INV); *bufp = 0; break;

    default:
        fprintf (stderr, "--- Read %08x: peripheral register not supported\n",
            address);
        exit (1);
    }
    return *bufp;
}

static void pps_input_group1 (unsigned address, unsigned data)
{
    // 0000 = RPD1
    // 0001 = RPG9
    // 0010 = RPB14
    // 0011 = RPD0
    // 0101 = RPB6
    // 0110 = RPD5
    // 0111 = RPB2
    // 1000 = RPF3
    // 1001 = RPF13
    // 1011 = RPF2
    // 1100 = RPC2
    // 1101 = RPE8
}

static void pps_input_group2 (unsigned address, unsigned data)
{
    // 0000 = RPD9
    // 0001 = RPG6
    // 0010 = RPB8
    // 0011 = RPB15
    // 0100 = RPD4
    // 0101 = RPB0
    // 0110 = RPE3
    // 0111 = RPB7
    // 1001 = RPF12
    // 1010 = RPD12
    // 1011 = RPF8
    // 1100 = RPC3
    // 1101 = RPE9
}

static void pps_input_group3 (unsigned address, unsigned data)
{
    // 0000 = RPD2
    // 0001 = RPG8
    // 0010 = RPF4
    // 0011 = RPD10
    // 0100 = RPF1
    // 0101 = RPB9
    // 0110 = RPB10
    // 0111 = RPC14
    // 1000 = RPB5
    // 1010 = RPC1
    // 1011 = RPD14
    // 1100 = RPG1
    // 1101 = RPA14
    // 1110 = RPD6
}

static void pps_input_group4 (unsigned address, unsigned data)
{
    // 0000 = RPD3
    // 0001 = RPG7
    // 0010 = RPF5
    // 0011 = RPD11
    // 0100 = RPF0
    // 0101 = RPB1
    // 0110 = RPE5
    // 0111 = RPC13
    // 1000 = RPB3
    // 1010 = RPC4
    // 1011 = RPD15
    // 1100 = RPG0
    // 1101 = RPA15
    // 1110 = RPD7
}

static void pps_output_group1 (unsigned address, unsigned data)
{
    // 0000 = No Connect
    // 0001 = U1TX
    // 0010 = U2RTS
    // 0011 = U5TX
    // 0100 = U6RTS
    // 0101 = SDO1
    // 0110 = SDO2
    // 0111 = SDO3
    // 1000 = SDO4
    // 1001 = SDO5
    // 1011 = OC4
    // 1100 = OC7
    // 1111 = REFCLKO1
}

static void pps_output_group2 (unsigned address, unsigned data)
{
    // 0000 = No Connect
    // 0001 = U1RTS
    // 0010 = U2TX
    // 0011 = U5RTS
    // 0100 = U6TX
    // 0110 = SS2
    // 1000 = SDO4
    // 1010 = SDO6
    // 1011 = OC2
    // 1100 = OC1
    // 1101 = OC9
    // 1111 = C2TX
}

static void pps_output_group3 (unsigned address, unsigned data)
{
    // 0000 = No Connect
    // 0001 = U3TX
    // 0010 = U4RTS
    // 0101 = SDO1
    // 0110 = SDO2
    // 0111 = SDO3
    // 1001 = SDO5
    // 1010 = SS6
    // 1011 = OC3
    // 1100 = OC6
    // 1101 = REFCLKO4
    // 1110 = C2OUT
    // 1111 = C1TX
}

static void pps_output_group4 (unsigned address, unsigned data)
{
    // 0000 = No Connect
    // 0001 = U3RTS
    // 0010 = U4TX
    // 0100 = U6TX
    // 0101 = SS1
    // 0111 = SS3
    // 1000 = SS4
    // 1001 = SS5
    // 1010 = SDO6
    // 1011 = OC5
    // 1100 = OC8
    // 1110 = C1OUT
    // 1111 = REFCLKO3
}

void io_write32 (unsigned address, unsigned *bufp, unsigned data, const char **namep)
{
    unsigned mask;

    switch (address) {
    /*-------------------------------------------------------------------------
     * Interrupt controller registers.
     */
    WRITEOP (INTCON); return;   // Interrupt Control
    READONLY(INTSTAT);          // Interrupt Status
    WRITEOP (IPTMR);  return;   // Temporal Proximity Timer
    WRITEOP (IFS0); goto irq;	// IFS(0..2) - Interrupt Flag Status
    WRITEOP (IFS1); goto irq;
    WRITEOP (IFS2); goto irq;
    WRITEOP (IFS3); goto irq;
    WRITEOP (IFS4); goto irq;
    WRITEOP (IFS5); goto irq;
    WRITEOP (IEC0); goto irq;	// IEC(0..2) - Interrupt Enable Control
    WRITEOP (IEC1); goto irq;
    WRITEOP (IEC2); goto irq;
    WRITEOP (IEC3); goto irq;
    WRITEOP (IEC4); goto irq;
    WRITEOP (IEC5); goto irq;
    WRITEOP (IPC0); goto irq;	// IPC(0..11) - Interrupt Priority Control
    WRITEOP (IPC1); goto irq;
    WRITEOP (IPC2); goto irq;
    WRITEOP (IPC3); goto irq;
    WRITEOP (IPC4); goto irq;
    WRITEOP (IPC5); goto irq;
    WRITEOP (IPC6); goto irq;
    WRITEOP (IPC7); goto irq;
    WRITEOP (IPC8); goto irq;
    WRITEOP (IPC9); goto irq;
    WRITEOP (IPC10); goto irq;
    WRITEOP (IPC11); goto irq;
    WRITEOP (IPC12); goto irq;
    WRITEOP (IPC13); goto irq;
    WRITEOP (IPC14); goto irq;
    WRITEOP (IPC15); goto irq;
    WRITEOP (IPC16); goto irq;
    WRITEOP (IPC17); goto irq;
    WRITEOP (IPC18); goto irq;
    WRITEOP (IPC19); goto irq;
    WRITEOP (IPC20); goto irq;
    WRITEOP (IPC21); goto irq;
    WRITEOP (IPC22); goto irq;
    WRITEOP (IPC23); goto irq;
    WRITEOP (IPC24); goto irq;
    WRITEOP (IPC25); goto irq;
    WRITEOP (IPC26); goto irq;
    WRITEOP (IPC27); goto irq;
    WRITEOP (IPC28); goto irq;
    WRITEOP (IPC29); goto irq;
    WRITEOP (IPC30); goto irq;
    WRITEOP (IPC31); goto irq;
    WRITEOP (IPC32); goto irq;
    WRITEOP (IPC33); goto irq;
    WRITEOP (IPC34); goto irq;
    WRITEOP (IPC35); goto irq;
    WRITEOP (IPC36); goto irq;
    WRITEOP (IPC37); goto irq;
    WRITEOP (IPC38); goto irq;
    WRITEOP (IPC39); goto irq;
    WRITEOP (IPC40); goto irq;
    WRITEOP (IPC41); goto irq;
    WRITEOP (IPC42); goto irq;
    WRITEOP (IPC43); goto irq;
    WRITEOP (IPC44); goto irq;
    WRITEOP (IPC45); goto irq;
    WRITEOP (IPC46); goto irq;
    WRITEOP (IPC47);
irq:    update_irq_status();
        return;

    /*-------------------------------------------------------------------------
     * Prefetch controller.
     */
    WRITEOP (PRECON); return;   // Prefetch Control
    WRITEOP (PRESTAT); return;  // Prefetch Status

    /*-------------------------------------------------------------------------
     * System controller.
     */
    STORAGE (CFGCON);           // Configuration Control
        // TODO: use unlock sequence
        mask = PIC32_CFGCON_DMAPRI | PIC32_CFGCON_CPUPRI |
            PIC32_CFGCON_ICACLK | PIC32_CFGCON_OCACLK |
            PIC32_CFGCON_IOLOCK | PIC32_CFGCON_PMDLOCK |
            PIC32_CFGCON_PGLOCK | PIC32_CFGCON_USBSSEN |
            PIC32_CFGCON_ECC_MASK | PIC32_CFGCON_JTAGEN |
            PIC32_CFGCON_TROEN | PIC32_CFGCON_TDOEN;
        data = (data & mask) | (*bufp & ~mask);
        break;
    READONLY(DEVID);		// Device Identifier
    STORAGE (SYSKEY);		// System Key
	/* Unlock state machine. */
	if (syskey_unlock == 0 && VALUE(SYSKEY) == 0xaa996655)
	    syskey_unlock = 1;
	if (syskey_unlock == 1 && VALUE(SYSKEY) == 0x556699aa)
	    syskey_unlock = 2;
	else
	    syskey_unlock = 0;
	break;
    STORAGE (RCON); break;	// Reset Control
    WRITEOP (RSWRST);		// Software Reset
	if (syskey_unlock == 2 && (VALUE(RSWRST) & 1)) {
            /* Reset CPU. */
            soft_reset();

            /* Reset all devices */
            io_reset();
            sdcard_reset();
        }
	break;
    STORAGE (OSCCON); break;	// Oscillator Control
    STORAGE (OSCTUN); break;	// Oscillator Tuning
    STORAGE (SPLLCON); break;	// System PLL Control
    STORAGE (PB1DIV); break;	// Peripheral bus 1 divisor
    STORAGE (PB2DIV); break;	// Peripheral bus 2 divisor
    STORAGE (PB3DIV); break;	// Peripheral bus 3 divisor
    STORAGE (PB4DIV); break;	// Peripheral bus 4 divisor
    STORAGE (PB5DIV); break;	// Peripheral bus 5 divisor
    STORAGE (PB7DIV); break;	// Peripheral bus 7 divisor
    STORAGE (PB8DIV); break;	// Peripheral bus 8 divisor

    /*-------------------------------------------------------------------------
     * Peripheral port select registers: input.
     */
    STORAGE (INT1R);    pps_input_group1 (address, data); break;
    STORAGE (T4CKR);    pps_input_group1 (address, data); break;
    STORAGE (T9CKR);    pps_input_group1 (address, data); break;
    STORAGE (IC1R);     pps_input_group1 (address, data); break;
    STORAGE (IC6R);     pps_input_group1 (address, data); break;
    STORAGE (U3CTSR);   pps_input_group1 (address, data); break;
    STORAGE (U4RXR);    pps_input_group1 (address, data); break;
    STORAGE (U6RXR);    pps_input_group1 (address, data); break;
    STORAGE (SS2R);     pps_input_group1 (address, data); break;
    STORAGE (SDI6R);    pps_input_group1 (address, data); break;
    STORAGE (OCFAR);    pps_input_group1 (address, data); break;
    STORAGE (REFCLKI3R);pps_input_group1 (address, data); break;

    STORAGE (INT2R);    pps_input_group2 (address, data); break;
    STORAGE (T3CKR);    pps_input_group2 (address, data); break;
    STORAGE (T8CKR);    pps_input_group2 (address, data); break;
    STORAGE (IC2R);     pps_input_group2 (address, data); break;
    STORAGE (IC5R);     pps_input_group2 (address, data); break;
    STORAGE (IC9R);     pps_input_group2 (address, data); break;
    STORAGE (U1CTSR);   pps_input_group2 (address, data); break;
    STORAGE (U2RXR);    pps_input_group2 (address, data); break;
    STORAGE (U5CTSR);   pps_input_group2 (address, data); break;
    STORAGE (SS1R);     pps_input_group2 (address, data); break;
    STORAGE (SS3R);     pps_input_group2 (address, data); break;
    STORAGE (SS4R);     pps_input_group2 (address, data); break;
    STORAGE (SS5R);     pps_input_group2 (address, data); break;
    STORAGE (C2RXR);    pps_input_group2 (address, data); break;

    STORAGE (INT3R);    pps_input_group3 (address, data); break;
    STORAGE (T2CKR);    pps_input_group3 (address, data); break;
    STORAGE (T6CKR);    pps_input_group3 (address, data); break;
    STORAGE (IC3R);     pps_input_group3 (address, data); break;
    STORAGE (IC7R);     pps_input_group3 (address, data); break;
    STORAGE (U1RXR);    pps_input_group3 (address, data); break;
    STORAGE (U2CTSR);   pps_input_group3 (address, data); break;
    STORAGE (U5RXR);    pps_input_group3 (address, data); break;
    STORAGE (U6CTSR);   pps_input_group3 (address, data); break;
    STORAGE (SDI1R);    pps_input_group3 (address, data); break;
    STORAGE (SDI3R);    pps_input_group3 (address, data); break;
    STORAGE (SDI5R);    pps_input_group3 (address, data); break;
    STORAGE (SS6R);     pps_input_group3 (address, data); break;
    STORAGE (REFCLKI1R);pps_input_group3 (address, data); break;

    STORAGE (INT4R);    pps_input_group4 (address, data); break;
    STORAGE (T5CKR);    pps_input_group4 (address, data); break;
    STORAGE (T7CKR);    pps_input_group4 (address, data); break;
    STORAGE (IC4R);     pps_input_group4 (address, data); break;
    STORAGE (IC8R);     pps_input_group4 (address, data); break;
    STORAGE (U3RXR);    pps_input_group4 (address, data); break;
    STORAGE (U4CTSR);   pps_input_group4 (address, data); break;
    STORAGE (SDI2R);    pps_input_group4 (address, data); break;
    STORAGE (SDI4R);    pps_input_group4 (address, data); break;
    STORAGE (C1RXR);    pps_input_group4 (address, data); break;
    STORAGE (REFCLKI4R);pps_input_group4 (address, data); break;

    /*-------------------------------------------------------------------------
     * Peripheral port select registers: output.
     */
    STORAGE (RPA15R);   pps_output_group1 (address, data); break;
    STORAGE (RPB1R);    pps_output_group1 (address, data); break;
    STORAGE (RPB3R);    pps_output_group1 (address, data); break;
    STORAGE (RPC4R);    pps_output_group1 (address, data); break;
    STORAGE (RPC13R);   pps_output_group1 (address, data); break;
    STORAGE (RPD3R);    pps_output_group1 (address, data); break;
    STORAGE (RPD7R);    pps_output_group1 (address, data); break;
    STORAGE (RPD11R);   pps_output_group1 (address, data); break;
    STORAGE (RPD15R);   pps_output_group1 (address, data); break;
    STORAGE (RPE5R);    pps_output_group1 (address, data); break;
    STORAGE (RPF0R);    pps_output_group1 (address, data); break;
    STORAGE (RPF5R);    pps_output_group1 (address, data); break;
    STORAGE (RPG0R);    pps_output_group1 (address, data); break;
    STORAGE (RPG7R);    pps_output_group1 (address, data); break;

    STORAGE (RPB2R);    pps_output_group2 (address, data); break;
    STORAGE (RPB6R);    pps_output_group2 (address, data); break;
    STORAGE (RPB14R);   pps_output_group2 (address, data); break;
    STORAGE (RPC2R);    pps_output_group2 (address, data); break;
    STORAGE (RPD0R);    pps_output_group2 (address, data); break;
    STORAGE (RPD1R);    pps_output_group2 (address, data); break;
    STORAGE (RPD5R);    pps_output_group2 (address, data); break;
    STORAGE (RPE8R);    pps_output_group2 (address, data); break;
    STORAGE (RPF2R);    pps_output_group2 (address, data); break;
    STORAGE (RPF3R);    pps_output_group2 (address, data); break;
    STORAGE (RPF13R);   pps_output_group2 (address, data); break;
    STORAGE (RPG9R);    pps_output_group2 (address, data); break;

    STORAGE (RPA14R);   pps_output_group3 (address, data); break;
    STORAGE (RPB5R);    pps_output_group3 (address, data); break;
    STORAGE (RPB9R);    pps_output_group3 (address, data); break;
    STORAGE (RPB10R);   pps_output_group3 (address, data); break;
    STORAGE (RPC1R);    pps_output_group3 (address, data); break;
    STORAGE (RPC14R);   pps_output_group3 (address, data); break;
    STORAGE (RPD2R);    pps_output_group3 (address, data); break;
    STORAGE (RPD6R);    pps_output_group3 (address, data); break;
    STORAGE (RPD10R);   pps_output_group3 (address, data); break;
    STORAGE (RPD14R);   pps_output_group3 (address, data); break;
    STORAGE (RPF1R);    pps_output_group3 (address, data); break;
    STORAGE (RPF4R);    pps_output_group3 (address, data); break;
    STORAGE (RPG1R);    pps_output_group3 (address, data); break;
    STORAGE (RPG8R);    pps_output_group3 (address, data); break;

    STORAGE (RPB0R);    pps_output_group4 (address, data); break;
    STORAGE (RPB7R);    pps_output_group4 (address, data); break;
    STORAGE (RPB8R);    pps_output_group4 (address, data); break;
    STORAGE (RPB15R);   pps_output_group4 (address, data); break;
    STORAGE (RPC3R);    pps_output_group4 (address, data); break;
    STORAGE (RPD4R);    pps_output_group4 (address, data); break;
    STORAGE (RPD9R);    pps_output_group4 (address, data); break;
    STORAGE (RPD12R);   pps_output_group4 (address, data); break;
    STORAGE (RPE3R);    pps_output_group4 (address, data); break;
    STORAGE (RPE9R);    pps_output_group4 (address, data); break;
    STORAGE (RPF8R);    pps_output_group4 (address, data); break;
    STORAGE (RPF12R);   pps_output_group4 (address, data); break;
    STORAGE (RPG6R);    pps_output_group4 (address, data); break;

    /*-------------------------------------------------------------------------
     * General purpose IO signals.
     */
    WRITEOP (ANSELA); return;	    // Port A: analog select
    WRITEOP (TRISA); return;	    // Port A: mask of inputs
    WRITEOPX(PORTA, LATA);          // Port A: write outputs
    WRITEOP (LATA);                 // Port A: write outputs
        gpio_write (0, VALUE(LATA));
	return;
    WRITEOP (ODCA); return;	    // Port A: open drain configuration
    WRITEOP (CNPUA); return;	    // Input pin pull-up
    WRITEOP (CNPDA); return;	    // Input pin pull-down
    WRITEOP (CNCONA); return;	    // Interrupt-on-change control
    WRITEOP (CNENA); return;	    // Input change interrupt enable
    WRITEOP (CNSTATA); return;	    // Input change status

    WRITEOP (ANSELB); return;	    // Port B: analog select
    WRITEOP (TRISB); return;	    // Port B: mask of inputs
    WRITEOPX(PORTB, LATB);          // Port B: write outputs
    WRITEOP (LATB);		    // Port B: write outputs
        gpio_write (1, VALUE(LATB));
	return;
    WRITEOP (ODCB); return;	    // Port B: open drain configuration
    WRITEOP (CNPUB); return;	    // Input pin pull-up
    WRITEOP (CNPDB); return;	    // Input pin pull-down
    WRITEOP (CNCONB); return;	    // Interrupt-on-change control
    WRITEOP (CNENB); return;	    // Input change interrupt enable
    WRITEOP (CNSTATB); return;	    // Input change status

    WRITEOP (ANSELC); return;	    // Port C: analog select
    WRITEOP (TRISC); return;	    // Port C: mask of inputs
    WRITEOPX(PORTC, LATC);          // Port C: write outputs
    WRITEOP (LATC);                 // Port C: write outputs
        gpio_write (2, VALUE(LATC));
	return;
    WRITEOP (ODCC); return;	    // Port C: open drain configuration
    WRITEOP (CNPUC); return;	    // Input pin pull-up
    WRITEOP (CNPDC); return;	    // Input pin pull-down
    WRITEOP (CNCONC); return;	    // Interrupt-on-change control
    WRITEOP (CNENC); return;	    // Input change interrupt enable
    WRITEOP (CNSTATC); return;	    // Input change status

    WRITEOP (ANSELD); return;	    // Port D: analog select
    WRITEOP (TRISD); return;	    // Port D: mask of inputs
    WRITEOPX(PORTD, LATD);          // Port D: write outputs
    WRITEOP (LATD);		    // Port D: write outputs
        gpio_write (3, VALUE(LATD));
	return;
    WRITEOP (ODCD); return;	    // Port D: open drain configuration
    WRITEOP (CNPUD); return;	    // Input pin pull-up
    WRITEOP (CNPDD); return;	    // Input pin pull-down
    WRITEOP (CNCOND); return;	    // Interrupt-on-change control
    WRITEOP (CNEND); return;	    // Input change interrupt enable
    WRITEOP (CNSTATD); return;	    // Input change status

    WRITEOP (ANSELE); return;	    // Port E: analog select
    WRITEOP (TRISE); return;	    // Port E: mask of inputs
    WRITEOPX(PORTE, LATE);          // Port E: write outputs
    WRITEOP (LATE);		    // Port E: write outputs
        gpio_write (4, VALUE(LATE));
	return;
    WRITEOP (ODCE); return;	    // Port E: open drain configuration
    WRITEOP (CNPUE); return;	    // Input pin pull-up
    WRITEOP (CNPDE); return;	    // Input pin pull-down
    WRITEOP (CNCONE); return;	    // Interrupt-on-change control
    WRITEOP (CNENE); return;	    // Input change interrupt enable
    WRITEOP (CNSTATE); return;	    // Input change status

    WRITEOP (ANSELF); return;	    // Port F: analog select
    WRITEOP (TRISF); return;	    // Port F: mask of inputs
    WRITEOPX(PORTF, LATF);          // Port F: write outputs
    WRITEOP (LATF);		    // Port F: write outputs
        gpio_write (5, VALUE(LATF));
	return;
    WRITEOP (ODCF); return;	    // Port F: open drain configuration
    WRITEOP (CNPUF); return;	    // Input pin pull-up
    WRITEOP (CNPDF); return;	    // Input pin pull-down
    WRITEOP (CNCONF); return;	    // Interrupt-on-change control
    WRITEOP (CNENF); return;	    // Input change interrupt enable
    WRITEOP (CNSTATF); return;	    // Input change status

    WRITEOP (ANSELG); return;	    // Port G: analog select
    WRITEOP (TRISG); return;	    // Port G: mask of inputs
    WRITEOPX(PORTG, LATG);          // Port G: write outputs
    WRITEOP (LATG);		    // Port G: write outputs
        gpio_write (6, VALUE(LATG));
	return;
    WRITEOP (ODCG); return;	    // Port G: open drain configuration
    WRITEOP (CNPUG); return;	    // Input pin pull-up
    WRITEOP (CNPDG); return;	    // Input pin pull-down
    WRITEOP (CNCONG); return;	    // Interrupt-on-change control
    WRITEOP (CNENG); return;	    // Input change interrupt enable
    WRITEOP (CNSTATG); return;	    // Input change status

    /*-------------------------------------------------------------------------
     * UART 1.
     */
    STORAGE (U1TXREG);                              // Transmit
        uart_put_char (0, data);
        break;
    WRITEOP (U1MODE);                               // Mode
        uart_update_mode (0);
        return;
    WRITEOPR (U1STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (0);
        return;
    WRITEOP (U1BRG); return;                        // Baud rate
    READONLY (U1RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 2.
     */
    STORAGE (U2TXREG);                              // Transmit
        uart_put_char (1, data);
        break;
    WRITEOP (U2MODE);                               // Mode
        uart_update_mode (1);
        return;
    WRITEOPR (U2STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (1);
        return;
    WRITEOP (U2BRG); return;                        // Baud rate
    READONLY (U2RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 3.
     */
    STORAGE (U3TXREG);                              // Transmit
        uart_put_char (2, data);
        break;
    WRITEOP (U3MODE);                               // Mode
        uart_update_mode (2);
        return;
    WRITEOPR (U3STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (2);
        return;
    WRITEOP (U3BRG); return;                        // Baud rate
    READONLY (U3RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 4.
     */
    STORAGE (U4TXREG);                              // Transmit
        uart_put_char (3, data);
        break;
    WRITEOP (U4MODE);                               // Mode
        uart_update_mode (3);
        return;
    WRITEOPR (U4STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (3);
        return;
    WRITEOP (U4BRG); return;                        // Baud rate
    READONLY (U4RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 5.
     */
    STORAGE (U5TXREG);                              // Transmit
        uart_put_char (4, data);
        break;
    WRITEOP (U5MODE);                               // Mode
        uart_update_mode (4);
        return;
    WRITEOPR (U5STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (4);
        return;
    WRITEOP (U5BRG); return;                        // Baud rate
    READONLY (U5RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * UART 6.
     */
    STORAGE (U6TXREG);                              // Transmit
        uart_put_char (5, data);
        break;
    WRITEOP (U6MODE);                               // Mode
        uart_update_mode (5);
        return;
    WRITEOPR (U6STA,                                // Status and control
        PIC32_USTA_URXDA | PIC32_USTA_FERR | PIC32_USTA_PERR |
        PIC32_USTA_RIDLE | PIC32_USTA_TRMT | PIC32_USTA_UTXBF);
        uart_update_status (5);
        return;
    WRITEOP (U6BRG); return;                        // Baud rate
    READONLY (U6RXREG);                             // Receive

    /*-------------------------------------------------------------------------
     * SPI.
     */
    WRITEOP (SPI1CON);                              // Control
	spi_control (0);
        return;
    WRITEOPR (SPI1STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI1BUF);                              // Buffer
        spi_writebuf (0, data);
        return;
    WRITEOP (SPI1BRG); return;                      // Baud rate
    WRITEOP (SPI1CON2); return;                     // Control 2

    WRITEOP (SPI2CON);                              // Control
	spi_control (1);
        return;
    WRITEOPR (SPI2STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI2BUF);                              // Buffer
        spi_writebuf (1, data);
        return;
    WRITEOP (SPI2BRG); return;                      // Baud rate
    WRITEOP (SPI2CON2); return;                     // Control 2

    WRITEOP (SPI3CON);                              // Control
	spi_control (2);
        return;
    WRITEOPR (SPI3STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI3BUF);                              // Buffer
        spi_writebuf (2, data);
        return;
    WRITEOP (SPI3BRG); return;                      // Baud rate
    WRITEOP (SPI3CON2); return;                     // Control 2

    WRITEOP (SPI4CON);                              // Control
	spi_control (3);
        return;
    WRITEOPR (SPI4STAT, ~PIC32_SPISTAT_SPIROV);     // Status
        return;                                     // Only ROV bit is writable
    STORAGE (SPI4BUF);                              // Buffer
        spi_writebuf (3, data);
        return;
    WRITEOP (SPI4BRG); return;      // Baud rate
    WRITEOP (SPI4CON2); return;                     // Control 2

    default:
        fprintf (stderr, "--- Write %08x to %08x: peripheral register not supported\n",
            data, address);
        exit (1);
readonly:
        fprintf (stderr, "--- Write %08x to %s: readonly register\n",
            data, *namep);
        *namep = 0;
        return;
    }
    *bufp = data;
}

void io_reset()
{
    /*
     * Prefetch controller.
     */
    VALUE(PRECON) = 0x00000007;

    /*
     * System controller.
     */
    syskey_unlock = 0;
    VALUE(CFGCON) = PIC32_CFGCON_ECC_DISWR | PIC32_CFGCON_TDOEN;
    VALUE(SYSKEY) = 0;
    VALUE(RCON)   = 0;
    VALUE(RSWRST) = 0;
    VALUE(OSCTUN) = 0;
    VALUE(SPLLCON)= 0x01310201;
    VALUE(PB1DIV) = 0x00008801;
    VALUE(PB2DIV) = 0x00008801;
    VALUE(PB3DIV) = 0x00008801;
    VALUE(PB4DIV) = 0x00008801;
    VALUE(PB5DIV) = 0x00008801;
    VALUE(PB7DIV) = 0x00008800;
    VALUE(PB8DIV) = 0x00008801;

    /*
     * General purpose IO signals.
     * All pins are inputs, high, open drains and pullups disabled.
     * No interrupts on change.
     */
    VALUE(ANSELA) = 0xFFFF;		// Port A: analog select
    VALUE(TRISA) = 0xFFFF;		// Port A: mask of inputs
    VALUE(PORTA) = 0xFFCF;		// Port A: read inputs, write outputs
    VALUE(LATA)  = 0xFFFF;		// Port A: read/write outputs
    VALUE(ODCA)  = 0;			// Port A: open drain configuration
    VALUE(CNPUA) = 0;			// Input pin pull-up
    VALUE(CNPDA) = 0;			// Input pin pull-down
    VALUE(CNCONA) = 0;			// Interrupt-on-change control
    VALUE(CNENA) = 0;			// Input change interrupt enable
    VALUE(CNSTATA) = 0;			// Input change status

    VALUE(ANSELB) = 0xFFFF;		// Port B: analog select
    VALUE(TRISB) = 0xFFFF;		// Port B: mask of inputs
    VALUE(PORTB) = 0xFFFF;		// Port B: read inputs, write outputs
    VALUE(LATB)  = 0xFFFF;		// Port B: read/write outputs
    VALUE(ODCB)  = 0;			// Port B: open drain configuration
    VALUE(CNPUB) = 0;			// Input pin pull-up
    VALUE(CNPDB) = 0;			// Input pin pull-down
    VALUE(CNCONB) = 0;			// Interrupt-on-change control
    VALUE(CNENB) = 0;			// Input change interrupt enable
    VALUE(CNSTATB) = 0;			// Input change status

    VALUE(ANSELC) = 0xFFFF;		// Port C: analog select
    VALUE(TRISC) = 0xFFFF;		// Port C: mask of inputs
    VALUE(PORTC) = 0xFFFF;		// Port C: read inputs, write outputs
    VALUE(LATC)  = 0xFFFF;		// Port C: read/write outputs
#ifdef WIFIRE
    VALUE(LATC) ^= 0x1000;		// Disable latc[15] for the cipKIT bootloader
#endif
    VALUE(ODCC)  = 0;			// Port C: open drain configuration
    VALUE(CNPUC) = 0;			// Input pin pull-up
    VALUE(CNPDC) = 0;			// Input pin pull-down
    VALUE(CNCONC) = 0;			// Interrupt-on-change control
    VALUE(CNENC) = 0;			// Input change interrupt enable
    VALUE(CNSTATC) = 0;			// Input change status

    VALUE(ANSELD) = 0xFFFF;		// Port D: analog select
    VALUE(TRISD) = 0xFFFF;		// Port D: mask of inputs
    VALUE(PORTD) = 0xFFFF;		// Port D: read inputs, write outputs
    VALUE(LATD)  = 0xFFFF;		// Port D: read/write outputs
    VALUE(ODCD)  = 0;			// Port D: open drain configuration
    VALUE(CNPUD) = 0;			// Input pin pull-up
    VALUE(CNPDD) = 0;			// Input pin pull-down
    VALUE(CNCOND) = 0;			// Interrupt-on-change control
    VALUE(CNEND) = 0;			// Input change interrupt enable
    VALUE(CNSTATD) = 0;			// Input change status

    VALUE(ANSELE) = 0xFFFF;		// Port E: analog select
    VALUE(TRISE) = 0xFFFF;		// Port E: mask of inputs
    VALUE(PORTE) = 0xFFFF;		// Port E: read inputs, write outputs
    VALUE(LATE)  = 0xFFFF;		// Port E: read/write outputs
    VALUE(ODCE)  = 0;			// Port E: open drain configuration
    VALUE(CNPUE) = 0;			// Input pin pull-up
    VALUE(CNPDE) = 0;			// Input pin pull-down
    VALUE(CNCONE) = 0;			// Interrupt-on-change control
    VALUE(CNENE) = 0;			// Input change interrupt enable
    VALUE(CNSTATE) = 0;			// Input change status

    VALUE(ANSELF) = 0xFFFF;		// Port F: analog select
    VALUE(TRISF) = 0xFFFF;		// Port F: mask of inputs
    VALUE(PORTF) = 0xFFFF;		// Port F: read inputs, write outputs
    VALUE(LATF)  = 0xFFFF;		// Port F: read/write outputs
    VALUE(ODCF)  = 0;			// Port F: open drain configuration
    VALUE(CNPUF) = 0;			// Input pin pull-up
    VALUE(CNPDF) = 0;			// Input pin pull-down
    VALUE(CNCONF) = 0;			// Interrupt-on-change control
    VALUE(CNENF) = 0;			// Input change interrupt enable
    VALUE(CNSTATF) = 0;			// Input change status

    VALUE(ANSELG) = 0xFFFF;		// Port G: analog select
    VALUE(TRISG) = 0xFFFF;		// Port G: mask of inputs
    VALUE(PORTG) = 0xFFFF;		// Port G: read inputs, write outputs
    VALUE(LATG)  = 0xFFFF;		// Port G: read/write outputs
    VALUE(ODCG)  = 0;			// Port G: open drain configuration
    VALUE(CNPUG) = 0;			// Input pin pull-up
    VALUE(CNPDG) = 0;			// Input pin pull-down
    VALUE(CNCONG) = 0;			// Interrupt-on-change control
    VALUE(CNENG) = 0;			// Input change interrupt enable
    VALUE(CNSTATG) = 0;			// Input change status

    uart_reset();
    spi_reset();
}

void io_init (void *bootp, unsigned devcfg0, unsigned devcfg1,
    unsigned devcfg2, unsigned devcfg3, unsigned devid, unsigned osccon)
{
    bootmem = bootp;
    VALUE(DEVID)  = devid;
    VALUE(OSCCON) = osccon;

    BOOTMEM(DEVCFG3) = devcfg3;
    BOOTMEM(DEVCFG2) = devcfg2;
    BOOTMEM(DEVCFG1) = devcfg1;
    BOOTMEM(DEVCFG0) = devcfg0;

    io_reset();
    sdcard_reset();
}
