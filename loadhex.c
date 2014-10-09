/*
 * Load SREC and HEX files into memory.
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
#include <ctype.h>
#include "globals.h"

/* Macros for converting between hex and binary. */
#define NIBBLE(x)       (isdigit(x) ? (x)-'0' : tolower(x)+10-'a')
#define HEX(buffer)     ((NIBBLE((buffer)[0])<<4) + NIBBLE((buffer)[1]))

void store_byte (char *progmem, char *bootmem, unsigned address, unsigned char byte)
{
    if (IN_PROGRAM_MEM(address)) {
        //printf("Store %02x to program memory %08x\n", byte, address);
        progmem[address & 0xfffff] = byte;
    }
    else if (IN_BOOT_MEM(address)) {
        //printf("Store %02x to boot memory %08x\n", byte, address);
        bootmem[address & 0xffff] = byte;
    }
}

unsigned virt_to_phys (unsigned address)
{
    if (address >= 0xa0000000 && address <= 0xbfffffff)
        return address - 0xa0000000;
    if (address >= 0x80000000 && address <= 0x9fffffff)
        return address - 0x80000000;
    return address;
}

/*
 * Read the S record file.
 */
int load_srec (void *progmem, void *bootmem, const char *filename)
{
    FILE *fd;
    unsigned char buf [256];
    unsigned char *data;
    unsigned address;
    int bytes, output_len;

    fd = fopen (filename, "r");
    if (! fd) {
        perror (filename);
        exit (1);
    }
    output_len = 0;
    while (fgets ((char*) buf, sizeof(buf), fd)) {
        if (buf[0] == '\n')
            continue;
        if (buf[0] != 'S') {
            if (output_len == 0)
                break;
            printf("%s: bad file format\n", filename);
            exit (1);
        }

        /* Starting an S-record.  */
        if (! isxdigit (buf[2]) || ! isxdigit (buf[3])) {
            printf("%s: bad record: %s\n", filename, buf);
            exit (1);
        }
        bytes = HEX (buf + 2);

        /* Ignore the checksum byte.  */
        --bytes;

        address = 0;
        data = buf + 4;
        switch (buf[1]) {
        case '7':
            address = HEX (data);
            data += 2;
            --bytes;
            /* Fall through.  */
        case '8':
            address = (address << 8) | HEX (data);
            data += 2;
            --bytes;
            /* Fall through.  */
        case '9':
            address = (address << 8) | HEX (data);
            data += 2;
            address = (address << 8) | HEX (data);
            data += 2;
            bytes -= 2;
            if (bytes == 0) {
                //printf("%s: start address = %08x\n", filename, address);
                //TODO: set start address
            }
            goto done;

        case '3':
            address = HEX (data);
            data += 2;
            --bytes;
            /* Fall through.  */
        case '2':
            address = (address << 8) | HEX (data);
            data += 2;
            --bytes;
            /* Fall through.  */
        case '1':
            address = (address << 8) | HEX (data);
            data += 2;
            address = (address << 8) | HEX (data);
            data += 2;
            bytes -= 2;

            address = virt_to_phys (address);
            if (! IN_PROGRAM_MEM(address) && ! IN_BOOT_MEM(address)) {
#define PROGRAM_MEM_START   0x1d000000
#define PROGRAM_MEM_SIZE    (512*1024)          // 512 kbytes
#define BOOT_MEM_START      0x1fc00000
#define BOOT_MEM_SIZE       (12*1024)           // 12 kbytes

                printf("%s: incorrect address %08X, must be %08X-%08X or %08X-%08X\n",
                    filename, address, PROGRAM_MEM_START,
                    PROGRAM_MEM_START + PROGRAM_MEM_SIZE - 1,
                    BOOT_MEM_START, BOOT_MEM_START + BOOT_MEM_SIZE - 1);
                exit (1);
            }
            output_len += bytes;
            while (bytes-- > 0) {
                store_byte (progmem, bootmem, address++, HEX (data));
                data += 2;
            }
            break;
        }
    }
done:
    fclose (fd);
    return output_len;
}

/*
 * Read HEX file.
 */
int load_hex (void *progmem, void *bootmem, const char *filename)
{
    FILE *fd;
    unsigned char buf [256], data[16], record_type, sum;
    unsigned address, high;
    int bytes, output_len, i;

    fd = fopen (filename, "r");
    if (! fd) {
        perror (filename);
        exit (1);
    }
    output_len = 0;
    high = 0;
    while (fgets ((char*) buf, sizeof(buf), fd)) {
        if (buf[0] == '\n')
            continue;
        if (buf[0] != ':') {
            if (output_len == 0)
                break;
            printf("%s: bad HEX file format\n", filename);
            exit (1);
        }
        if (! isxdigit (buf[1]) || ! isxdigit (buf[2]) ||
            ! isxdigit (buf[3]) || ! isxdigit (buf[4]) ||
            ! isxdigit (buf[5]) || ! isxdigit (buf[6]) ||
            ! isxdigit (buf[7]) || ! isxdigit (buf[8])) {
            printf("%s: bad record: %s\n", filename, buf);
            exit (1);
        }
	record_type = HEX (buf+7);
	if (record_type == 1) {
	    /* End of file. */
            break;
        }
	bytes = HEX (buf+1);
	if (strlen ((char*) buf) < bytes * 2 + 11) {
            printf("%s: too short hex line\n", filename);
            exit (1);
        }
	address = high << 16 | HEX (buf+3) << 8 | HEX (buf+5);
        if (address & 3) {
            printf("%s: odd address\n", filename);
            exit (1);
        }

	sum = 0;
	for (i=0; i<bytes; ++i) {
            data [i] = HEX (buf+9 + i + i);
	    sum += data [i];
	}
	sum += record_type + bytes + (address & 0xff) + (address >> 8 & 0xff);
	if (sum != (unsigned char) - HEX (buf+9 + bytes + bytes)) {
            printf("%s: bad hex checksum\n", filename);
            printf("Line %s", buf);
            exit (1);
        }

	if (record_type == 5) {
	    /* Start address. */
            if (bytes != 4) {
                printf("%s: invalid length of hex start address record: %d bytes\n",
                    filename, bytes);
                exit (1);
            }
	    address = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
            //printf("%s: start address = %08x\n", filename, address);
            //TODO: set start address
	    continue;
	}
	if (record_type == 4) {
	    /* Extended address. */
            if (bytes != 2) {
                printf("%s: invalid length of hex linear address record: %d bytes\n",
                    filename, bytes);
                exit (1);
            }
	    high = data[0] << 8 | data[1];
	    continue;
	}
	if (record_type != 0) {
            printf("%s: unknown hex record type: %d\n",
                filename, record_type);
            exit (1);
        }

        /* Data record found. */
        address = virt_to_phys (address);
        if (! IN_PROGRAM_MEM(address) && ! IN_BOOT_MEM(address)) {
            printf("%s: incorrect address %08X, must be %08X-%08X or %08X-%08X\n",
                filename, address, PROGRAM_MEM_START,
                PROGRAM_MEM_START + PROGRAM_MEM_SIZE - 1,
                BOOT_MEM_START, BOOT_MEM_START + BOOT_MEM_SIZE - 1);
            exit (1);
        }
        output_len += bytes;
        for (i=0; i<bytes; i++) {
            store_byte (progmem, bootmem, address++, data [i]);
        }
    }
    fclose (fd);
    return output_len;
}

int load_file(void *progmem, void *bootmem, const char *filename)
{
    int memory_len = load_srec (progmem, bootmem, filename);
    if (memory_len == 0) {
        memory_len = load_hex (progmem, bootmem, filename);
        if (memory_len == 0) {
            return 0;
        }
    }
    printf("Load file: '%s', %d bytes\n", filename, memory_len);
    return 1;
}
