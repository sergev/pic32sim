This is a simulator of PIC32 processor, based on OVPsim technology.
Currently only Linux platform is supported.

This software is opensource, released under the terms of MIT-like license.
You are free to modify it to your needs. Any enhancements are welcome.


How to build and run
~~~~~~~~~~~~~~~~~~~~
 1) Register at ovpworld.org.
    Link: http://www.ovpworld.org/forum/profile.php?mode=register

 2) Download OVPsim simulator package for Linux, release 20141103.0:
    http://www.ovpworld.org/dlp/?action=dl&dl_id=21193&site=dlp.OVPworld.org

    In case you use different relase of OVPsim, you will need to modify
    file imperas.environ appropriately.

 3) Unpack the downloaded package:
    OVPsim.20141103.0.Linux32.exe

    Install the unpacked directory as: /usr/local/Imperas.20141103
    You can install it to some other place, and modify file
    imperas.environ to use a new path.

 4) Install the required environment variables, using imperas.environ:

        $ source imperas.environ

 5) Get your host ID, needed for a license:

        $ $IMPERAS_HOME/bin/Linux32/lmutil lmhostid
        lmutil - Copyright (c) 1989-2011 Flexera Software, Inc. All Rights Reserved.
        The FLEXnet host ID of this machine is "00123f7c25ed"

 6) Fill a form and send request for OVPsim license.
    Use host ID from previous step.
    Ask for a free license for personal non-commercial usage.
    Link: http://www.ovpworld.org/likey/

 7) When license received by email, put it to the file
    "$IMPERAS_HOME/OVPsim.lic".

 8) Build the pic32 simulator:

        $ make

    It will create six binaries:

        pic32mx7-explorer16 - MX7 processor on Microchip Explorer16 board
        pic32mx7-max32      - MX7 processor on chipKIT Max32 board
        pic32mx7-maximite   - MX7 processor on Maximite board
        pic32mz-explorer16  - MZ processor on Microchip Explorer16 board
        pic32mz-meb2        - MZ processor on Microchip MEB-II board
        pic32mz-wifire      - MZ processor on chipKIT WiFire board

 9) Run the simulator without arguments to get a usage hint:

        $ ./pic32mx7-max32
        Simulator of PIC32MX7 microcontroller
        Usage:
            pic32mx7-max32 [options] [boot.hex] application.hex
        Options:
            -v           verbose mode
            -t filename  trace CPU instructions and registers
            -l number    limit simulation to this number of instructions
            -d sd0.img   SD card image (repeat for sd1)
            -g           wait for GDB connection
            -m           enable magic opcodes
            -s           stop on software reset
            -c           enable cache

10) Run demos in directories demo/boot, demo/wifire and demo/retrobsd.
    See README.txt in these directories.
