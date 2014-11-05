/*
 * Virtual console TTY.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 * Copyright (C) yajin 2008 <yajinzhou@gmail.com>
 * Copyright (C) 2014 Serge Vakulenko <serge@vak.ru>
 *
 * "Interactive" part idea by Mtve.
 * TCP console added by Mtve.
 * Serial console by Peter Ross (suxen_drol@hotmail.com)
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
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include "globals.h"

/* Number of ports instantiated */
#define VTTY_NUNITS 6

/*
 * 4 Kb should be enough for a keyboard buffer
 */
#define VTTY_BUFFER_SIZE    4096

/*
 * VTTY connection states (for TCP)
 */
enum {
    VTTY_STATE_TCP_INVALID,     /* connection is not working */
    VTTY_STATE_TCP_WAITING,     /* waiting for incoming connection */
    VTTY_STATE_TCP_RUNNING,     /* character reading/writing ok */
};

/*
 * VTTY input states
 */
enum {
    VTTY_INPUT_TEXT,
    VTTY_INPUT_VT1,
    VTTY_INPUT_VT2,
    VTTY_INPUT_REMOTE,
    VTTY_INPUT_TELNET,
    VTTY_INPUT_TELNET_IYOU,
    VTTY_INPUT_TELNET_SB1,
    VTTY_INPUT_TELNET_SB2,
    VTTY_INPUT_TELNET_SB_TTYPE,
    VTTY_INPUT_TELNET_NEXT
};

/*
 * Virtual TTY structure
 */
typedef struct virtual_tty vtty_t;
struct virtual_tty {
    char *name;
    int state;
    int tcp_port;
    int terminal_support;
    int input_state;
    int telnet_cmd, telnet_opt, telnet_qual;
    int fd, accept_fd, *select_fd;
    FILE *fstream;
    u_char buffer[VTTY_BUFFER_SIZE];
    u_int read_ptr, write_ptr;
    pthread_mutex_t lock;
};

static vtty_t unittab[VTTY_NUNITS];
static pthread_t vtty_thread;

#define VTTY_LOCK(tty)      pthread_mutex_lock(&(tty)->lock);
#define VTTY_UNLOCK(tty)    pthread_mutex_unlock(&(tty)->lock);

static struct termios tios, tios_orig;

/*
 * Send Telnet command: WILL TELOPT_ECHO
 */
static void vtty_telnet_will_echo (vtty_t * vtty)
{
    u_char cmd[] = { IAC, WILL, TELOPT_ECHO };
    if (write (vtty->fd, cmd, sizeof (cmd)) < 0)
        perror ("vtty_telnet_will_echo");
}

/*
 * Send Telnet command: Suppress Go-Ahead
 */
static void vtty_telnet_will_suppress_go_ahead (vtty_t * vtty)
{
    u_char cmd[] = { IAC, WILL, TELOPT_SGA };
    if (write (vtty->fd, cmd, sizeof (cmd)) < 0)
        perror ("vtty_telnet_will_suppress_go_ahead");
}

/*
 * Send Telnet command: Don't use linemode
 */
static void vtty_telnet_dont_linemode (vtty_t * vtty)
{
    u_char cmd[] = { IAC, DONT, TELOPT_LINEMODE };
    if (write (vtty->fd, cmd, sizeof (cmd)) < 0)
        perror ("vtty_telnet_dont_linemode");
}

/*
 * Send Telnet command: does the client support terminal type message?
 */
static void vtty_telnet_do_ttype (vtty_t * vtty)
{
    u_char cmd[] = { IAC, DO, TELOPT_TTYPE };
    if (write (vtty->fd, cmd, sizeof (cmd)) < 0)
        perror ("vtty_telnet_do_ttype");
}

/*
 * Restore TTY original settings
 */
static void vtty_term_reset (void)
{
    tcsetattr (STDIN_FILENO, TCSANOW, &tios_orig);
}

/*
 * Initialize real TTY
 */
static int vtty_term_init (void)
{
    int fd = open ("/dev/tty", O_RDWR);
    if (fd < 0) {
        perror ("/dev/tty");
        exit(-1);
    }

    tcgetattr (fd, &tios);

    memcpy (&tios_orig, &tios, sizeof (struct termios));
    atexit (vtty_term_reset);

    tios.c_cc[VTIME] = 0;
    tios.c_cc[VMIN] = 1;

    /* Disable Ctrl-C, Ctrl-S, Ctrl-Q and Ctrl-Z */
    tios.c_cc[VINTR] = 0;
    tios.c_cc[VSTART] = 0;
    tios.c_cc[VSTOP] = 0;
    tios.c_cc[VSUSP] = 0;

    tios.c_lflag &= ~(ICANON | ECHO);
    tios.c_iflag &= ~ICRNL;
    tcsetattr (fd, TCSANOW, &tios);
    tcflush (fd, TCIFLUSH);

    return fd;
}

/*
 * Wait for a TCP connection
 */
static int vtty_tcp_conn_wait (vtty_t * vtty)
{
    struct sockaddr_in serv;
    int one = 1;

    vtty->state = VTTY_STATE_TCP_INVALID;

    if ((vtty->accept_fd = socket (PF_INET, SOCK_STREAM, 0)) < 0) {
        perror ("vtty_tcp_waitcon: socket");
        return (-1);
    }

    if (setsockopt (vtty->accept_fd, SOL_SOCKET, SO_REUSEADDR, &one,
            sizeof (one)) < 0) {
        perror ("vtty_tcp_waitcon: setsockopt(SO_REUSEADDR)");
        goto error;
    }

    memset (&serv, 0, sizeof (serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl (INADDR_ANY);
    serv.sin_port = htons (vtty->tcp_port);

    if (bind (vtty->accept_fd, (struct sockaddr *) &serv, sizeof (serv)) < 0) {
        perror ("vtty_tcp_waitcon: bind");
        goto error;
    }

    if (listen (vtty->accept_fd, 1) < 0) {
        perror ("vtty_tcp_waitcon: listen");
        goto error;
    }

    fprintf (stderr, "%s: waiting connection on tcp port %d (FD %d)\n", vtty->name,
        vtty->tcp_port, vtty->accept_fd);

    vtty->select_fd = &vtty->accept_fd;
    vtty->state = VTTY_STATE_TCP_WAITING;
    return (0);

error:
    close (vtty->accept_fd);
    vtty->accept_fd = -1;
    vtty->select_fd = NULL;
    return (-1);
}

/*
 * Accept a TCP connection
 */
static int vtty_tcp_conn_accept (vtty_t * vtty)
{
    if ((vtty->fd = accept (vtty->accept_fd, NULL, NULL)) < 0) {
        fprintf (stderr,
            "vtty_tcp_conn_accept: accept on port %d failed %s\n",
            vtty->tcp_port, strerror (errno));
        return (-1);
    }

    fprintf (stderr, "%s is now connected (accept_fd=%d, conn_fd=%d)\n",
        vtty->name, vtty->accept_fd, vtty->fd);

    /* Adapt Telnet settings */
    if (vtty->terminal_support) {
        vtty_telnet_do_ttype (vtty);
        vtty_telnet_will_echo (vtty);
        vtty_telnet_will_suppress_go_ahead (vtty);
        vtty_telnet_dont_linemode (vtty);
        vtty->input_state = VTTY_INPUT_TELNET;
    }

    vtty->fstream = fdopen (vtty->fd, "wb");
    if (! vtty->fstream) {
        close (vtty->fd);
        vtty->fd = -1;
        return (-1);
    }

    fprintf (vtty->fstream, "Connected to pic32sim - %s\r\n\r\n", vtty->name);

    vtty->select_fd = &vtty->fd;
    vtty->state = VTTY_STATE_TCP_RUNNING;
    return (0);
}

/*
 * Create a virtual tty
 */
void vtty_create (unsigned unit, char *name, int tcp_port)
{
    vtty_t *vtty = unittab + unit;

    if (unit >= VTTY_NUNITS) {
        fprintf (stderr, "%s: unable to create a virtual tty port #%u.\n",
            name, unit);
        exit(1);
    }

    memset (vtty, 0, sizeof (*vtty));
    vtty->name = name;
    vtty->fd = -1;
    vtty->fstream = NULL;
    vtty->accept_fd = -1;
    pthread_mutex_init (&vtty->lock, NULL);
    vtty->input_state = VTTY_INPUT_TEXT;

    if (tcp_port == 0) {
        vtty->fd = vtty_term_init();
        vtty->select_fd = &vtty->fd;
        vtty->fstream = stdout;
    } else {
        vtty->terminal_support = 1;
        vtty->tcp_port = tcp_port;
        vtty_tcp_conn_wait (vtty);
    }
}

/*
 * Delete a virtual tty
 */
void vtty_delete (unsigned unit)
{
    vtty_t *vtty = unittab + unit;

    if (unit < VTTY_NUNITS && (vtty->fstream || vtty->tcp_port)) {

        if (vtty->fstream && vtty->fstream != stdout) {
            fclose (vtty->fstream);
        }
        vtty->fstream = 0;

        /* We don't close FD 0 since it is stdin */
        if (vtty->fd > 0) {
            fprintf (stderr, "%s: closing FD %d\n", vtty->name, vtty->fd);
            close (vtty->fd);
            vtty->fd = 0;
        }

        if (vtty->accept_fd >= 0) {
            fprintf (stderr, "%s: closing accept FD %d\n",
                vtty->name, vtty->accept_fd);
            close (vtty->accept_fd);
            vtty->accept_fd = -1;
        }
        vtty->tcp_port = 0;
    }
}

/*
 * Store a character in the FIFO buffer
 */
static int vtty_store (vtty_t * vtty, u_char c)
{
    u_int nwptr;

    VTTY_LOCK (vtty);
    nwptr = vtty->write_ptr + 1;
    if (nwptr == VTTY_BUFFER_SIZE)
        nwptr = 0;

    if (nwptr == vtty->read_ptr) {
        VTTY_UNLOCK (vtty);
        return (-1);
    }

    vtty->buffer[vtty->write_ptr] = c;
    vtty->write_ptr = nwptr;
    VTTY_UNLOCK (vtty);
    return (0);
}

/*
 * Read a character from the terminal.
 */
static int vtty_term_read (vtty_t * vtty)
{
    u_char c;

    if (read (vtty->fd, &c, 1) == 1)
        return (c);

    perror ("read from vtty failed");
    return (-1);
}

/*
 * Read a character from the TCP connection.
 */
static int vtty_tcp_read (vtty_t * vtty)
{
    u_char c;

    switch (vtty->state) {
    case VTTY_STATE_TCP_RUNNING:
        if (read (vtty->fd, &c, 1) == 1)
            return (c);

        /* Problem with the connection: Re-enter wait mode */
        shutdown (vtty->fd, 2);
        fclose (vtty->fstream);
        close (vtty->fd);
        vtty->fstream = NULL;
        vtty->fd = -1;
        vtty->select_fd = &vtty->accept_fd;
        vtty->state = VTTY_STATE_TCP_WAITING;
        return (-1);

    case VTTY_STATE_TCP_WAITING:
        /* A new connection has arrived */
        vtty_tcp_conn_accept (vtty);
        return (-1);
    }

    /* Shouldn't happen... */
    return (-1);
}

/*
 * Read a character from the virtual TTY.
 *
 * If the VTTY is a TCP connection, restart it in case of error.
 */
static int vtty_read (vtty_t * vtty)
{
    if (vtty->tcp_port)
        return vtty_tcp_read (vtty);
    return vtty_term_read (vtty);
}

/*
 * Read a character (until one is available) and store it in buffer
 */
static void vtty_read_and_store (int unit)
{
    vtty_t *vtty = unittab + unit;
    int c;

    /* wait until we get a character input */
    c = vtty_read (vtty);

    /* if read error, do nothing */
    if (c < 0)
        return;

    if (! vtty->terminal_support) {
        vtty_store (vtty, c);
        return;
    }

    switch (vtty->input_state) {
    case VTTY_INPUT_TEXT:
        switch (c) {
        case 0x1b:
            vtty->input_state = VTTY_INPUT_VT1;
            return;

            /* Ctrl + ']' (0x1d, 29), or Alt-Gr + '*' (0xb3, 179) */
        case 0x1d:
        case 0xb3:
            vtty->input_state = VTTY_INPUT_REMOTE;
            return;
        case IAC:
            vtty->input_state = VTTY_INPUT_TELNET;
            return;
        case 0:                /* NULL - Must be ignored - generated by Linux telnet */
        case 10:               /* LF (Line Feed) - Must be ignored on Windows platform */
            return;
        default:
            /* Store a standard character */
            vtty_store (vtty, c);
            return;
        }

    case VTTY_INPUT_VT1:
        switch (c) {
        case 0x5b:
            vtty->input_state = VTTY_INPUT_VT2;
            return;
        default:
            vtty_store (vtty, 0x1b);
            vtty_store (vtty, c);
        }
        vtty->input_state = VTTY_INPUT_TEXT;
        return;

    case VTTY_INPUT_VT2:
        switch (c) {
        case 0x41:             /* Up Arrow */
            vtty_store (vtty, 16);
            break;
        case 0x42:             /* Down Arrow */
            vtty_store (vtty, 14);
            break;
        case 0x43:             /* Right Arrow */
            vtty_store (vtty, 6);
            break;
        case 0x44:             /* Left Arrow */
            vtty_store (vtty, 2);
            break;
        default:
            vtty_store (vtty, 0x5b);
            vtty_store (vtty, 0x1b);
            vtty_store (vtty, c);
            break;
        }
        vtty->input_state = VTTY_INPUT_TEXT;
        return;

    case VTTY_INPUT_REMOTE:
        //remote_control(vtty, c);
        vtty->input_state = VTTY_INPUT_TEXT;
        return;

    case VTTY_INPUT_TELNET:
        vtty->telnet_cmd = c;
        switch (c) {
        case WILL:
        case WONT:
        case DO:
        case DONT:
            vtty->input_state = VTTY_INPUT_TELNET_IYOU;
            return;
        case SB:
            vtty->telnet_cmd = c;
            vtty->input_state = VTTY_INPUT_TELNET_SB1;
            return;
        case SE:
            break;
        case IAC:
            vtty_store (vtty, IAC);
            break;
        }
        vtty->input_state = VTTY_INPUT_TEXT;
        return;

    case VTTY_INPUT_TELNET_IYOU:
        vtty->telnet_opt = c;
        /* if telnet client can support ttype, ask it to send ttype string */
        if ((vtty->telnet_cmd == WILL) && (vtty->telnet_opt == TELOPT_TTYPE)) {
            vtty_put_char (unit, IAC);
            vtty_put_char (unit, SB);
            vtty_put_char (unit, TELOPT_TTYPE);
            vtty_put_char (unit, TELQUAL_SEND);
            vtty_put_char (unit, IAC);
            vtty_put_char (unit, SE);
        }
        vtty->input_state = VTTY_INPUT_TEXT;
        return;

    case VTTY_INPUT_TELNET_SB1:
        vtty->telnet_opt = c;
        vtty->input_state = VTTY_INPUT_TELNET_SB2;
        return;

    case VTTY_INPUT_TELNET_SB2:
        vtty->telnet_qual = c;
        if ((vtty->telnet_opt == TELOPT_TTYPE)
            && (vtty->telnet_qual == TELQUAL_IS))
            vtty->input_state = VTTY_INPUT_TELNET_SB_TTYPE;
        else
            vtty->input_state = VTTY_INPUT_TELNET_NEXT;
        return;

    case VTTY_INPUT_TELNET_SB_TTYPE:
#if 0
        /* parse ttype string: first char is sufficient */
        /* if client is xterm or vt, set the title bar */
        if (c=='x' || c=='X' || c=='v' || c=='V') {
            fprintf(vtty->fstream, "\033]0;pic32sim %s\07", vtty->name);
        }
#endif
        vtty->input_state = VTTY_INPUT_TELNET_NEXT;
        return;

    case VTTY_INPUT_TELNET_NEXT:
        /* ignore all chars until next IAC */
        if (c == IAC)
            vtty->input_state = VTTY_INPUT_TELNET;
        return;
    }
}

int vtty_is_full (unsigned unit)
{
    vtty_t *vtty = unittab + unit;

    return (unit < VTTY_NUNITS) && (vtty->read_ptr == vtty->write_ptr);
}

/*
 * Read a character from the buffer (-1 if the buffer is empty)
 */
int vtty_get_char (unsigned unit)
{
    vtty_t *vtty = unittab + unit;
    u_char c;

    if (unit >= VTTY_NUNITS)
        return -1;
    VTTY_LOCK (vtty);

    if (vtty->read_ptr == vtty->write_ptr) {
        VTTY_UNLOCK (vtty);
        return (-1);
    }

    c = vtty->buffer[vtty->read_ptr++];

    if (vtty->read_ptr == VTTY_BUFFER_SIZE)
        vtty->read_ptr = 0;

    VTTY_UNLOCK (vtty);
    return (c);
}

/*
 * Returns TRUE if a character is available in buffer
 */
int vtty_is_char_avail (unsigned unit)
{
    vtty_t *vtty = unittab + unit;
    int res;

    if (unit >= VTTY_NUNITS)
        return 0;
    VTTY_LOCK (vtty);
    res = (vtty->read_ptr != vtty->write_ptr);
    VTTY_UNLOCK (vtty);
    return (res);
}

/*
 * Put char to vtty
 */
void vtty_put_char (unsigned unit, char ch)
{
    vtty_t *vtty = unittab + unit;

    if (unit >= VTTY_NUNITS)
        return;
    if (vtty->tcp_port) {
        if (vtty->state == VTTY_STATE_TCP_RUNNING &&
            fwrite (&ch, 1, 1, vtty->fstream) != 1)
        {
            fprintf (stderr, "%s: put char %#x failed (%s)\n",
                vtty->name, (unsigned char) ch, strerror (errno));
        }
    } else if (vtty->fstream) {
        if (write (vtty->fd, &ch, 1) != 1)
            perror ("write");
    } else {
        fprintf (stderr, "uart%u: not configured\n", unit+1);
    }
}

/*
 * Wait for input and return a bitmask of file descriptors.
 */
int vtty_wait (fd_set *rfdp)
{
    vtty_t *vtty;
    struct timeval tv;
    int fd, fd_max, res, unit;

    /* Build the FD set */
    FD_ZERO (rfdp);
    fd_max = -1;
    for (unit=0; unit<VTTY_NUNITS; unit++) {
	vtty = unittab + unit;
	if (! vtty->select_fd)
	    continue;

	fd = *vtty->select_fd;
	if (fd < 0)
	    continue;

	if (fd > fd_max)
	    fd_max = fd;
	FD_SET (fd, rfdp);
    }
    if (fd_max < 0) {
	/* No vttys created yet. */
	usleep (200000);
	return 0;
    }

    /* Wait for incoming data */
    tv.tv_sec = 0;
    tv.tv_usec = 10000; /* 10 ms */
    res = select (fd_max + 1, rfdp, NULL, NULL, &tv);

    if (res == -1) {
	if (errno != EINTR) {
	    perror ("vtty_thread: select");
	    for (unit=0; unit<VTTY_NUNITS; unit++) {
		vtty = unittab + unit;
		if (vtty->name)
		    fprintf (stderr, "   %s: FD %d\n", vtty->name, vtty->fd);
	    }
	}
	return 0;;
    }
    return 1;
}

/*
 * VTTY thread
 */
static void *vtty_thread_main (void *arg)
{
    vtty_t *vtty;
    int fd, unit;
    fd_set rfds;

    for (;;) {
	if (! vtty_wait (&rfds))
	    continue;

        /* Examine active FDs and call user handlers */
        for (unit=0; unit<VTTY_NUNITS; unit++) {
            vtty = unittab + unit;
            if (! vtty->select_fd)
                continue;

            fd = *vtty->select_fd;
            if (fd < 0)
                continue;

            if (FD_ISSET (fd, &rfds)) {
                vtty_read_and_store (unit);
            }

            /* Flush any pending output */
            if (vtty->fstream)
                fflush (vtty->fstream);
        }
    }
    return NULL;
}

/*
 * Initialize the VTTY thread
 */
void vtty_init (void)
{
    if (pthread_create (&vtty_thread, NULL, vtty_thread_main, NULL)) {
        perror ("vtty: pthread_create");
        exit (1);
    }
}
