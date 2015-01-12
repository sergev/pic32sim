/*
 * Run an Imperas MIPS simulator.
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
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include "icm/icmCpuManager.h"
#include "globals.h"

char *progname;                         // base name of current program

static uint32_t progmem [PROGRAM_FLASH_SIZE/4];
static uint32_t bootmem [BOOT_FLASH_SIZE/4];
static char datamem [DATA_MEM_SIZE];    // storage for RAM area
uint32_t iomem [0x100000/4];            // backing storage for I/O area

int trace_flag;                         // print cpu instructions and registers
int cache_enable;                       // enable I and D caches
int stop_on_reset;                      // terminate simulation on software reset

icmProcessorP processor;                // top level processor object
icmNetP eic_ripl;                       // EIC request priority level
icmNetP eic_vector;                     // EIC vector number

static void usage()
{
#ifdef PIC32MX7
    icmPrintf("Simulator of PIC32MX7 microcontroller\n");
#endif
#ifdef PIC32MZ
    icmPrintf("Simulator of PIC32MZ microcontroller\n");
#endif
    icmPrintf("Usage:\n");
    icmPrintf("    %s [options] [boot.hex] application.hex \n", progname);
    icmPrintf("Options:\n");
    icmPrintf("    -v           verbose mode\n");
    icmPrintf("    -t filename  trace CPU instructions and registers\n");
    icmPrintf("    -l number    limit simulation to this number of instructions\n");
    icmPrintf("    -d sd0.img   SD card image (repeat for sd1)\n");
    icmPrintf("    -g           wait for GDB connection\n");
    icmPrintf("    -m           enable magic opcodes\n");
    icmPrintf("    -s           stop on software reset\n");
    icmPrintf("    -c           enable cache\n");
    exit(-1);
}

//
// Callback for printing user defined attributes.
//
static void print_user_attribute (const char *owner, const char *name,
    const char *value, Bool set, Bool used, Bool numeric, void *userData)
{
    if (! set || ! used)
        return;
    icmPrintf("    %s.%s = ", owner, name);
    if (value == 0)
        value = "UNDEF";

    if (numeric) {
        icmPrintf("%s\n", value);
    } else {
        icmPrintf("'%s'\n", value);
    }
}

void quit()
{
    icmPrintf("***** Stop *****\n");
    if (trace_flag)
        fprintf(stderr, "***** Stop *****\n");
    icmTerminate();
}

void killed(int sig)
{
    icmPrintf("\n***** Killed *****\n");
    if (trace_flag)
        fprintf(stderr, "\n***** Killed *****\n");
    exit(1);
}

Uns64 read_reg (const char *name)
{
    Uns64 value = 0;

    if (! icmReadReg (processor, name, &value)) {
        fprintf(stderr, "%s: Unable to read register '%s'\n", __func__, name);
        quit();
    }
    return value;
}

void write_reg (const char *name, Uns64 value)
{
    if (! icmWriteReg (processor, name, &value)) {
        fprintf(stderr, "%s: Unable to write register '%s'\n", __func__, name);
        quit();
    }
}

//
// Check for MCheck condition.
//
static void machine_check()
{
    Uns32 cause = read_reg("cause");
    int exc_code = (cause >> 2) & 31;

    if (exc_code == 24) {
        // Machine check!
        dump_regs("MCheck");
        quit();
    }
}

//
// Callback for reading peripheral registers.
//
static void mem_read (icmProcessorP proc, Addr paddr, Uns32 bytes,
    void *value, void *user_data, Addr vaddr, Bool isFetch)
{
    Uns32 offset = paddr & 0xfffff;
    const char *name = "???";
    Uns32 data;

    if (vaddr >= 0x80000000 && vaddr < IO_MEM_START + 0xa0000000U) {
        icmPrintf("--- I/O Read  %08x: incorrect virtual address %08x\n",
            (Uns32) paddr, (Uns32) vaddr);
        icmExit(proc);
    }

    switch (bytes) {
    case 1:
        data = io_read32 (paddr, (Uns32*) (user_data + (offset & ~3)), &name);
        if ((offset &= 3) != 0) {
            // Unaligned read.
            data >>= offset * 8;
        }
        if (trace_flag) {
            icmPrintf("--- I/O Read  %02x from %s\n", data, name);
        }
        *(Uns8*) value = data;
        break;
    case 2:
        data = io_read32 (paddr, (Uns32*) (user_data + (offset & ~1)), &name);
        if (offset & 1) {
            // Unaligned read.
            data >>= 16;
        }
        if (trace_flag) {
            icmPrintf("--- I/O Read  %04x from %s\n", data, name);
        }
        *(Uns16*) value = data;
        break;
    case 4:
        data = io_read32 (paddr, (Uns32*) (user_data + offset), &name);
        if (trace_flag) {
            icmPrintf("--- I/O Read  %08x from %s\n", data, name);
        }
        *(Uns32*) value = data;
        break;
    default:
        icmPrintf("--- I/O Read  %08x: incorrect size %u bytes\n",
            (Uns32) paddr, bytes);
        icmExit(proc);
    }
    machine_check();
}

//
// Callback for writing peripheral registers.
//
static void mem_write (icmProcessorP proc, Addr paddr, Uns32 bytes,
    const void *value, void *user_data, Addr vaddr)
{
    Uns32 data = 0;
    const char *name = "???";

    if (vaddr >= 0x80000000 && vaddr < IO_MEM_START + 0xa0000000U) {
        icmPrintf("--- I/O Read  %08x: incorrect virtual address %08x\n",
            (Uns32) paddr, (Uns32) vaddr);
        icmExit(proc);
    }

    // Fetch data and align to word format.
    switch (bytes) {
    case 1:
        data = *(Uns8*) value;
        data <<= (paddr & 3) * 8;
        paddr &= ~3;
        break;
    case 2:
        data = *(Uns16*) value;
        data <<= (paddr & 2) * 8;
        paddr &= ~3;
        break;
    case 4:
        data = *(Uns32*) value;
        break;
    default:
        icmPrintf("--- I/O Write %08x: incorrect size %u bytes\n",
            (Uns32) paddr, bytes);
        icmExit(proc);
    }
    io_write32 (paddr, (Uns32*) (user_data + (paddr & 0xffffc)),
        data, &name);
    if (trace_flag && name != 0) {
        icmPrintf("--- I/O Write %08x to %s \n", data, name);
    }
    machine_check();
}

//
// Callback for timer interrupt.
//
void timer_irq (void *arg, Uns32 value)
{
    //icmPrintf("--- timer interrupt: %u\n", value);
    if (value)
        irq_raise (0);
}

//
// Callbacks for software interrupts.
//
void soft_irq0 (void *arg, Uns32 value)
{
    //icmPrintf("--- soft interrupt 0: %u\n", value);
    if (value)
        irq_raise (1);
}

void soft_irq1 (void *arg, Uns32 value)
{
    //icmPrintf("--- soft interrupt 1: %u\n", value);
    if (value)
        irq_raise (2);
}

/*
 * When uarts are idle, insert usleep()
 * to decrease the cpu load.
 */
static void pause_idle()
{
    static unsigned idle_timeout;
    fd_set rfds;

    if (idle_timeout > 0) {
	idle_timeout--;
	return;
    }
    idle_timeout = 2000;

    /* Wait for incoming data */
    vtty_wait (&rfds);
}

//
// Main simulation routine
//
int main(int argc, char **argv)
{
    // Extract a base name of a program.
    progname = strrchr (*argv, '/');
    if (progname)
        progname++;
    else
        progname = *argv;

    //
    // Parse command line arguments.
    // Setup the configuration attributes for the simulator
    //
    Uns32 icm_attrs      = ICM_ATTR_SIMEX;
    Uns32 sim_attrs      = ICM_STOP_ON_CTRLC;
    Uns32 model_flags    = 0;
    Uns32 magic_opcodes  = 0;
    Int64 limit_count    = 0;
    char *remote_debug   = 0;
    char *trace_filename = 0;
    const char *sd0_file = 0;
    const char *sd1_file = 0;

    for (;;) {
        switch (getopt (argc, argv, "vmscgt:d:l:")) {
        case EOF:
            break;
        case 'v':
            sim_attrs |= ICM_VERBOSE;
            continue;
        case 'm':
            magic_opcodes++;
            continue;
        case 's':
            stop_on_reset++;
            continue;
        case 'c':
            cache_enable++;
            continue;
        case 'g':
            remote_debug = "rsp";
            continue;
        case 't':
            trace_flag++;
            trace_filename = optarg;
            continue;
        case 'd':
            if (sd0_file == 0)
                sd0_file = optarg;
            else if (sd1_file == 0)
                sd1_file = optarg;
            else {
                icmPrintf("Too many -d options: %s", optarg);
                return -1;
            }
            continue;
        case 'l':
            limit_count = strtoull(optarg, 0, 0);
            continue;
        default:
            usage ();
        }
        break;
    }
    argc -= optind;
    argv += optind;

    if (argc < 1) {
        usage ();
    }

    //
    // Initialize CpuManager
    //
    icmInitPlatform(ICM_VERSION, sim_attrs, remote_debug, 0, NULL);
    atexit(quit);

    // Use ^\ to kill the simulation.
    signal(SIGQUIT, killed);

    //
    // Setup the configuration attributes for the MIPS model
    //
    icmAttrListP user_attrs = icmNewAttrList();
    char *cpu_type;

#ifdef PIC32MX7
    cpu_type = "M4K";
    icmAddUns64Attr(user_attrs, "pridRevision", 0x65);  // Product revision
    icmAddUns64Attr(user_attrs, "srsctlHSS",    1);     // Number of shadow register sets
    icmAddUns64Attr(user_attrs, "MIPS16eASE",   1);     // Support mips16e
    icmAddUns64Attr(user_attrs, "configSB",     1);     // Simple bus transfers only
#endif
#ifdef PIC32MZ
    cpu_type = "microAptivP";
    icmAddUns64Attr(user_attrs, "pridRevision", 0x28);  // Product revision
    icmAddUns64Attr(user_attrs, "srsctlHSS",    7);     // Number of shadow register sets
    icmAddUns64Attr(user_attrs, "config1WR",    0);     // Disable watch registers
    icmAddUns64Attr(user_attrs, "config3ULRI",  1);     // UserLocal register implemented
#endif

    if (cache_enable) {
        icmAddStringAttr(user_attrs,"cacheenable", "full"); // Enable cache
        icmAddUns64Attr(user_attrs, "config1IS",    2);     // Icache: 256 sets per way
        icmAddUns64Attr(user_attrs, "config1IL",    3);     // Icache: line size 16 bytes
        icmAddUns64Attr(user_attrs, "config1IA",    3);     // Icache: 4-way associativity
        icmAddUns64Attr(user_attrs, "config1DS",    0);     // Dcache: 64 sets per way
        icmAddUns64Attr(user_attrs, "config1DL",    3);     // Dcache: line size 16 bytes
        icmAddUns64Attr(user_attrs, "config1DA",    3);     // Dcache: 4-way associativity
        icmAddUns64Attr(user_attrs, "config7HCI",   0);     // Cache initialized by software
    } else {
        icmAddUns64Attr(user_attrs, "config1IS",    0);
        icmAddUns64Attr(user_attrs, "config1IL",    0);
        icmAddUns64Attr(user_attrs, "config1IA",    0);
        icmAddUns64Attr(user_attrs, "config1DS",    0);
        icmAddUns64Attr(user_attrs, "config1DL",    0);
        icmAddUns64Attr(user_attrs, "config1DA",    0);
    }

    // Processor configuration
    icmAddStringAttr(user_attrs,"variant", cpu_type);

    // PIC32 is always little endian
    icmAddStringAttr(user_attrs, "endian", "Little");

    // Enable external interrupt controller (EIC) and vectored interrupts mode
    icmAddStringAttr(user_attrs, "vectoredinterrupt", "enable");
    icmAddStringAttr(user_attrs, "externalinterrupt", "enable");
    icmAddUns64Attr(user_attrs, "EIC_OPTION", 2);

    // Interrupt pin for Timer interrupt
    icmAddUns64Attr(user_attrs, "intctlIPTI", 0);

    if (trace_filename) {
        // Redirect standard output to file.
        if (freopen (trace_filename, "w", stdout) != stdout) {
            perror (trace_filename);
            return -1;
        }
        fprintf(stderr, "Output redirected to %s\n", trace_filename);
    }

    if (trace_flag) {
        // Enable MIPS-format trace
        icmAddStringAttr(user_attrs, "MIPS_TRACE", "enable");
        icm_attrs |= ICM_ATTR_TRACE |
            ICM_ATTR_TRACE_REGS_BEFORE | ICM_ATTR_TRACE_REGS_AFTER;

        // Trace Count/Compare, TLB and FPU
        model_flags |= 0x0c000020;
    }
    if (magic_opcodes) {
        // Enable magic Pass/Fail opcodes
        icmAddStringAttr(user_attrs, "IMPERAS_MIPS_AVP_OPCODES", "enable");
    }
//model_flags |= 0x08000000; // TLB

    // Select processor model from library
    const char *model_file = icmGetVlnvString(NULL,
        "mips.ovpworld.org", "processor", "mips32", "1.0", "model");

    //
    // create a processor
    //
    processor = icmNewProcessor(
        cpu_type,                     // processor name
        "mips",                       // processor type
        0,                            // processor cpuId
        model_flags,                  // processor model flags
        32,                           // physical address bits
        model_file,                   // model file
        "modelAttrs",                 // morpher attributes
        icm_attrs,                    // processor attributes
        user_attrs,                   // user attribute list
        0,                            // semihosting file name
        0);                           // semihosting attribute symbol
    if (remote_debug) {
        // Mark this processor for debug
        icmDebugThisProcessor(processor);
    }

    icmBusP bus = icmNewBus("bus", 32);
    icmConnectProcessorBusses(processor, bus, bus);

    // Interrupt controller.
    eic_ripl = icmNewNet ("EIC_RIPL");
    icmConnectProcessorNet (processor, eic_ripl, "EIC_RIPL", ICM_INPUT);
    eic_vector = icmNewNet ("EIC_VectorNum");
    icmConnectProcessorNet (processor, eic_vector, "EIC_VectorNum", ICM_INPUT);

    // Callback for timer interrupt,
    icmNetP ti_output = icmNewNet ("causeTI");
    icmConnectProcessorNet (processor, ti_output, "causeTI", ICM_OUTPUT);
    icmAddNetCallback (ti_output, timer_irq, NULL);

    // Callbacks for software interrupts,
    icmNetP swi0_output = icmNewNet ("causeIP0");
    icmNetP swi1_output = icmNewNet ("causeIP1");
    icmConnectProcessorNet (processor, swi0_output, "causeIP0", ICM_OUTPUT);
    icmConnectProcessorNet (processor, swi1_output, "causeIP1", ICM_OUTPUT);
    icmAddNetCallback (swi0_output, soft_irq0, NULL);
    icmAddNetCallback (swi1_output, soft_irq1, NULL);

    // Data memory.
    icmMapNativeMemory (bus, ICM_PRIV_RWX, DATA_MEM_START,
        DATA_MEM_START + DATA_MEM_SIZE - 1, datamem);
#ifdef PIC32MX7
    // User space 96 kbytes.
    icmMapNativeMemory (bus, ICM_PRIV_RWX, USER_MEM_START + 0x8000,
        USER_MEM_START + DATA_MEM_SIZE - 1, datamem + 0x8000);
#endif
    // Program memory.
    icmMapNativeMemory (bus, ICM_PRIV_RX, PROGRAM_FLASH_START,
        PROGRAM_FLASH_START + PROGRAM_FLASH_SIZE - 1, progmem);

    // Boot memory.
    icmMapNativeMemory (bus, ICM_PRIV_RX, BOOT_FLASH_START,
        BOOT_FLASH_START + BOOT_FLASH_SIZE - 1, bootmem);

    // I/O memory.
    icmMapExternalMemory(bus, "IO", ICM_PRIV_RW, IO_MEM_START,
        IO_MEM_START + IO_MEM_SIZE - 1, mem_read, mem_write, iomem);

    if (sim_attrs & ICM_VERBOSE) {
        // Print all user attributes.
        icmPrintf("\n***** User attributes *****\n");
        icmIterAllUserAttributes(print_user_attribute, 0);

        // Show Mapping on bus
        icmPrintf("\n***** Configuration of memory bus *****\n");
        icmPrintBusConnections(bus);
    }

    //
    // Initialize SD card.
    //
    int cs0_port, cs0_pin, cs1_port, cs1_pin;
    const char *board;
#if defined EXPLORER16
    board = "Microchip Explorer16";
    sdcard_spi_port = 0;                        // SD card at SPI1,
    cs0_port = 1; cs0_pin = 1;                  // select0 at B1,
    cs1_port = 1; cs1_pin = 2;                  // select1 at B2
#elif defined MAX32
    board = "chipKIT Max32";
    sdcard_spi_port = 3;                        // SD card at SPI4,
    cs0_port = 3; cs0_pin = 3;                  // select0 at D3,
    cs1_port = 3; cs1_pin = 4;                  // select1 at D4
#elif defined MAXIMITE
    board = "Maximite Computer";
    sdcard_spi_port = 3;                        // SD card at SPI4,
    cs0_port = 4; cs0_pin = 0;                  // select0 at E0,
    cs1_port = -1; cs1_pin = -1;                // select1 not available
#elif defined WIFIRE
    board = "chipKIT WiFire";
    sdcard_spi_port = 2;                        // SD card at SPI3,
    cs0_port = 2; cs0_pin = 3;                  // select0 at C3,
    cs1_port = -1; cs1_pin = -1;                // select1 not available
#elif defined MEBII
    board = "Microchip MEB-II";
    sdcard_spi_port = 1;                        // SD card at SPI2,
    cs0_port = 1; cs0_pin = 14;                 // select0 at B14,
    cs1_port = -1; cs1_pin = -1;                // select1 not available
#else
#error Unknown board type.
#endif
    sdcard_init (0, "sd0", sd0_file, cs0_port, cs0_pin);
    sdcard_init (1, "sd1", sd1_file, cs1_port, cs1_pin);

    //
    // Create console port.
    //
#if defined EXPLORER16 && defined PIC32MX7
    vtty_create (1, "uart2", 0);                // console on UART2
#elif defined WIFIRE
    vtty_create (3, "uart4", 0);                // console on UART4
#else
    vtty_create (0, "uart1", 0);                // console on UART1
#endif
    vtty_init();

    //
    // Generic reset of all peripherals.
    //
#if defined PIC32MX7
    // MX7: use data from Max32 board.
    io_init (bootmem,
        0xffffff7f, 0x5bfd6aff,                 // DEVCFG0, DEVCFG1,
        0xd979f8f9, 0xffff0722,                 // DEVCFG2, DEVCFG3 values
        0x04307053,                             // DEVID: MX795F512L
        0x01453320);                            // OSCCON: external oscillator 8MHz
#elif defined WIFIRE
    // WiFire board.
    io_init (bootmem,
        0xfffffff7, 0x7f743cb9,                 // DEVCFG0, DEVCFG1,
        0xfff9b11a, 0xbeffffff,                 // DEVCFG2, DEVCFG3 values
        0x4510e053,                             // DEVID: MZ2048ECG100 rev A4
        0x00001120);                            // OSCCON: external oscillator 24MHz
#elif defined MEBII
    // MEB-II board.
    io_init (bootmem,
        0x7fffffdb, 0x0000fc81,                 // DEVCFG0, DEVCFG1,
        0x3ff8b11a, 0x86ffffff,                 // DEVCFG2, DEVCFG3 values
        0x45127053,                             // DEVID: MZ2048ECH144 rev A4
        0x00001120);                            // OSCCON: external oscillator 24MHz
#else
    // Generic MZ: use data from Explorer16 board.
    io_init (bootmem,
        0x7fffffdb, 0x0000fc81,                 // DEVCFG0, DEVCFG1,
        0x3ff8b11a, 0x86ffffff,                 // DEVCFG2, DEVCFG3 values
        0x35113053,                             // DEVID: MZ2048ECH100 rev A3
        0x00001120);                            // OSCCON: external oscillator 24MHz
#endif

    if (trace_flag) {
        icmPrintf("Board: '%s'\n", board);
        if (cache_enable)
            icmPrintf("Cache: enabled\n");
        if (magic_opcodes)
            icmPrintf("Magic opcodes: enabled\n");
        if (stop_on_reset)
            icmPrintf("Stop: on software reset\n");
    }

    // Limit the simulation to a given number of instructions.
    if (limit_count > 0) {
        icmPrintf("Limit: %llu instructions\n", (unsigned long long)limit_count);
    }

    //
    // Load program(s)
    //
    for (; argc-- > 0; argv++) {
        const char *app_file = argv[0];

        if (! load_file(progmem, bootmem, app_file)) {
            icmPrintf("Failed for '%s'\n", app_file);
            if (trace_flag)
                fprintf(stderr, "Cannot load file '%s'\n", app_file);
            return -1;
        }
    }

    //
    // Do a simulation run
    //
    icmSetPC(processor, 0xbfc00000);
    icmPrintf("\n***** Start '%s' *****\n", cpu_type);
    if (trace_flag)
        fprintf(stderr, "***** Start '%s' *****\n", cpu_type);

    // Run the processor one instruction at a time until finished
    icmStopReason stop_reason;
    Uns32 chunk = 100;
    do {
        // simulate fixed number of instructions
        stop_reason = icmSimulate(processor, chunk);
	if (stop_reason == ICM_SR_HALT) {
	    /* Suspended on WAIT instruction. */
	    if (! (read_reg ("status") & 1)) {
	        /* Interrupts disabled - halt simulation. */
	        break;
            }
	    stop_reason = ICM_SR_SCHED;

	    if (! uart_active())
		pause_idle();
	}
        machine_check();

	// poll uarts
	uart_poll();

        if (limit_count > 0) {
            limit_count -= chunk;
            if (limit_count <= 0) {
                icmPrintf("\n***** Limit reached *****\n");
                break;
            }
        }
    } while (stop_reason == ICM_SR_SCHED);

    //
    // quit() implicitly called on return
    //
    return 0;
}

//
// EIC Interrupt
//
void eic_level_vector (int ripl, int vector)
{
    if (trace_flag)
        printf ("--- RIPL = %u\n", ripl);

    icmWriteNet (eic_vector, 0);
    icmWriteNet (eic_ripl, ripl);
}

void soft_reset()
{
    Uns32 address = 0xfffffff0;
    Uns32 value;

    value = 4;
    if (! icmWriteProcessorMemory (processor, address, &value, 4)) {
        icmPrintf ("--- Cannot write %#x to %#x\n", value, address);
    }
}

void dump_regs(const char *message)
{
    Uns32 pc = icmGetPC(processor);
    Uns32 status = read_reg ("status");
    Uns32 cause = read_reg ("cause");
    Uns32 entryhi = read_reg ("entryhi");
    Uns32 badvaddr = read_reg ("badvaddr");
    Uns32 epc = read_reg ("epc");
    Uns32 hi = read_reg ("hi");
    Uns32 lo = read_reg ("lo");
    Uns32 r[32];

    r[1] = read_reg ("at");
    r[2] = read_reg ("v0");
    r[3] = read_reg ("v1");
    r[4] = read_reg ("a0");
    r[5] = read_reg ("a1");
    r[6] = read_reg ("a2");
    r[7] = read_reg ("a3");
    r[8] = read_reg ("t0");
    r[9] = read_reg ("t1");
    r[10] = read_reg ("t2");
    r[11] = read_reg ("t3");
    r[12] = read_reg ("t4");
    r[13] = read_reg ("t5");
    r[14] = read_reg ("t6");
    r[15] = read_reg ("t7");
    r[16] = read_reg ("s0");
    r[17] = read_reg ("s1");
    r[18] = read_reg ("s2");
    r[19] = read_reg ("s3");
    r[20] = read_reg ("s4");
    r[21] = read_reg ("s5");
    r[22] = read_reg ("s6");
    r[23] = read_reg ("s7");
    r[24] = read_reg ("t8");
    r[25] = read_reg ("t9");
    r[26] = read_reg ("k0");
    r[27] = read_reg ("k1");
    r[28] = read_reg ("gp");
    r[29] = read_reg ("sp");
    r[30] = read_reg ("s8");
    r[31] = read_reg ("ra");

    printf ("--%-10s--  t0 = %8x   s0 = %8x   t8 = %8x      lo = %8x\n",
        message, r[8], r[16], r[24], lo);
    printf ("at = %8x   t1 = %8x   s1 = %8x   t9 = %8x      hi = %8x\n",
        r[1], r[9], r[17], r[25], hi);
    printf ("v0 = %8x   t2 = %8x   s2 = %8x   k0 = %8x  status = %8x\n",
        r[2], r[10], r[18], r[26], status);
    printf ("v1 = %8x   t3 = %8x   s3 = %8x   k1 = %8x   cause = %8x\n",
        r[3], r[11], r[19], r[27], cause);
    printf ("a0 = %8x   t4 = %8x   s4 = %8x   gp = %8x      pc = %8x\n",
        r[4], r[12], r[20], r[28], pc);
    printf ("a1 = %8x   t5 = %8x   s5 = %8x   sp = %8x     epc = %8x\n",
        r[5], r[13], r[21], r[29], epc);
    printf ("a2 = %8x   t6 = %8x   s6 = %8x   fp = %8x   badva = %8x\n",
        r[6], r[14], r[22], r[30], badvaddr);
    printf ("a3 = %8x   t7 = %8x   s7 = %8x   ra = %8x entryhi = %8x\n",
        r[7], r[15], r[23], r[31], entryhi);
}
