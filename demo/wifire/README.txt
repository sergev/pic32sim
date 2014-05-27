This example simulates the chipKIT WiFire board with PIC32 MZ processor
installed. The code reads valus of CPU configuration registers, and
prints them to UART port.

File boot.hex contains a standard chipKIT WiFire bootloader,
slightly modified to speed up the simulation. File uart.hex contains
the bunary of the application. See uart.c for sources.

Some features, like Trace port (and CDMMBase register), Watch registers
and Performance Counter registers are not supported by simulator,
so warning messages are printed.

For example:

$ ./run-mz.sh


OVPsim (32-Bit) v20140430.0 Open Virtual Platform simulator from www.OVPworld.org.
Copyright (c) 2005-2014 Imperas Software Ltd.  Contains Imperas Proprietary Information.
Licensed Software, All Rights Reserved.
Visit www.IMPERAS.com for multicore debug, verification and analysis solutions.

OVPsim started: Tue May 27 00:27:59 2014


Flash file 'boot.hex', 6916 bytes
Flash file 'uart.hex', 5092 bytes

***** Start 'microAptivP' *****
-
Status  = 01000000
IntCtl  = 00000020
SRSCtl  = 1c000000
Cause   = 00800000
PRId    = 00019e28
EBase   = 9d000000
Info (MIPS32_IAS_COP0_READ) 0x9d0011d0 read from unsupported COP0 register 15 sel 2
CDMMBase= 00000000
Config  = 80000483
Config1 = 9e000002
Config2 = 80000000
Config3 = 8022bc60
Config4 = 80000000
Config5 = 00000001
Config7 = 80040000
Info (MIPS32_IAS_COP0_READ) 0x9d001250 read from unsupported COP0 register 19 sel 0
WatchHi = 00000000
Info (MIPS32_IAS_COP0_READ) 0x9d001260 read from unsupported COP0 register 19 sel 1
WatchHi1= 00000000
Info (MIPS32_IAS_COP0_READ) 0x9d001270 read from unsupported COP0 register 19 sel 2
WatchHi2= 00000000
Info (MIPS32_IAS_COP0_READ) 0x9d001280 read from unsupported COP0 register 19 sel 3
WatchHi3= 00000000
Debug   = 02028000
Info (MIPS32_IAS_COP0_READ) 0x9d0012a0 read from unsupported COP0 register 25 sel 0
PerfCtl0= 00000000
Info (MIPS32_IAS_COP0_READ) 0x9d0012b0 read from unsupported COP0 register 25 sel 2
PerfCtl1= 00000000
Info (MIPS32_AVP) AVP test PASS
***** Stop *****

OVPsim finished: Tue May 27 00:27:59 2014


OVPsim (32-Bit) v20140430.0 Open Virtual Platform simulator from www.OVPworld.org.
Visit www.IMPERAS.com for multicore debug, verification and analysis solutions.
