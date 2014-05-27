Hot to run RetroBSD demo
~~~~~~~~~~~~~~~~~~~~~~~~
 1. Download the release 860 package from the RetroBSD autobuild server.
    Use the link:
    http://autobuild.majenko.co.uk/download.php?class=current&build=860&file=build-860.zip

    You can use any newer release, in this case you will need to modify
    file names in the run script.

 2. Unpack the archive and put the following two files into the current
    directory:

        explorer16-860.hex
        sdcard-860.rd

 3. Run script `run-mx7.sh'.


Session example
~~~~~~~~~~~~~~~
$ ./run-mx7.sh


OVPsim (32-Bit) v20140430.0 Open Virtual Platform simulator from www.OVPworld.org.
Copyright (c) 2005-2014 Imperas Software Ltd.  Contains Imperas Proprietary Information.
Licensed Software, All Rights Reserved.
Visit www.IMPERAS.com for multicore debug, verification and analysis solutions.

OVPsim started: Tue May 27 00:02:20 2014


Card0 image 'sdcard-860.rd', 329729 kbytes
Flash file 'explorer16-860.hex', 137100 bytes

***** Start 'M4K' *****

2.11 BSD Unix for PIC32, revision 860 build 3:
     Compiled 2013-02-24 by root@autobuild:
     /home/matt/retrobsd/current/sys/pic32/explorer16
cpu: 795F512L 80 MHz, bus 80 MHz
oscillator: HS crystal, PLL div 1:2 mult x20
console: tty1 (5,1)
sd0: port SPI1, select pin B1
sd0: type I, size 329728 kbytes, speed 10 Mbit/sec
phys mem  = 128 kbytes
user mem  = 96 kbytes
root dev  = rd0a (0,1)
root size = 163840 kbytes
swap dev  = rd0b (0,2)
swap size = 2048 kbytes
temp0: allocated 47 blocks
/dev/rd0a: 556 files, 8643 used, 154556 free
temp0: released allocation
Starting daemons: update cron


2.11 BSD UNIX (pic32) (console)

login: root                                             <-- Type "root <Enter>"
Password:                                               <-- Type "<Enter"
Welcome to RetroBSD!
erase, kill ^U, intr ^C
# df                                                    <-- Type "df <Enter>"
Filesystem  1K-blocks     Used    Avail Capacity  Mounted on
/dev/rd0a      163199     8644   154555     5%    /
# halt                                                  <-- Type "halt <Enter>"
killing processes... done
syncing disks... done
halted
                                                        <-- Type "^\"
***** Killed *****
***** Stop *****

OVPsim finished: Tue May 27 00:02:43 2014


OVPsim (32-Bit) v20140430.0 Open Virtual Platform simulator from www.OVPworld.org.
Visit www.IMPERAS.com for multicore debug, verification and analysis solutions.

Quit (core dumped)
