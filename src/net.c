/* SPDX-License-Identifier: MIT */
/*
 * net.c - "OS-9 Net": serial ports ACIA1..ACIA3 attached to TCP sockets
 *
 * Every network port looks to OS-9 like another PTY style ACIA (control /
 * status at offset +0, data at +1) and is driven by the NPty SCF driver
 * (os9/net/npty.asm).  The host side offers three personalities, chosen with
 * the -net option:
 *
 *   -net N=listen:PORT[,raw][,any]   telnet server.  A connecting telnet
 *        client gets a terminal on /Nn: telnet option negotiation is done and
 *        stripped, the character set follows -kanji, and a shell can be
 *        started on the line with tsmon.  Binds to 127.0.0.1 unless ",any".
 *   -net N=modem[:PORT][,any]        Hayes style modem.  The line starts in
 *        command mode: ATDT host:port dials (opens a TCP connection and
 *        answers CONNECT), ATA answers an incoming call on the listening
 *        port, ATH hangs up, +++ returns to command mode, ATO goes online.
 *        Result codes: OK CONNECT NO CARRIER BUSY ERROR.
 *   -net N=tcp:HOST:PORT[,telnet]    connect at start up, raw byte stream.
 *
 * Status register bits (6850 like): b0 RDRF, b1 TDRE, b2 /DCD (1 = no
 * carrier), b7 HUP: the connection was lost; the bit is cleared by the
 * driver reading the (empty) data register.
 *
 * Credits: the idea of virtual serial ports for NitrOS-9 reachable by
 * telnet / AT commands follows the DriveWire network ports (Boisy Pitre,
 * Aaron Wolfe); nothing of their code is used.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "net.h"
#include "console.h"

enum { NET_OFF = 0, NET_LISTEN, NET_MODEM, NET_TCP };
enum { M_COMMAND = 0, M_DIALING, M_ONLINE };

#define IAC  255
#define DONT 254
#define DO   253
#define WONT 252
#define WILL 251
#define SB   250
#define SE   240

typedef struct kconv kconv_t;
kconv_t *kconv_open(int mode, int to_guest);
size_t   kconv_feed(kconv_t *k, const uint8_t *in, size_t n, uint8_t *out, size_t outsz);
void     kconv_close(kconv_t *k);

typedef struct netchan {
    int mode;
    int fd;                  /* data connection or -1            */
    int lfd;                 /* listening socket or -1           */
    int connecting;          /* non-blocking connect in progress */
    uint64_t conn_deadline;
    int telnet;              /* telnet protocol + charset conversion */
    int raw_any;             /* bind to all interfaces           */
    int port;                /* listen port                      */
    char host[256];
    int  rport;
    int  hup;                /* HUP event pending                 */
    uint64_t start_at;       /* tcp: do not connect before this cycle (see net_config) */
    int  started;
    /* modem */
    int mstate;
    char cmd[160];
    int  cmdlen;
    int  echo, verbose, autoans, ringing;
    uint64_t last_tx_cycle, plus_cycle;
    int  plus_count;
    /* telnet input parser */
    int  iac;                /* 0 none, 1 saw IAC, 2 saw verb, 3 in SB, 4 SB saw IAC, 5 CR seen */
    int  crseen;
    /* charset conversion (per direction) */
    kconv_t *to_guest, *to_host;
    /* tx buffer */
    uint8_t txbuf[2048];
    int  txlen;
} netchan_t;

static netchan_t chan[MAX_ACIA];
static int net_debug = -1;          /* NETDEBUG=1: log modem commands and connection events */
#define NET_DEBUG() (net_debug < 0 ? (net_debug = getenv("NETDEBUG") != NULL) : net_debug)
static machine_t *nm;

/* ------------------------------------------------------------------ */
static uint64_t now_cycles(void) { return nm ? nm->cpu.total_cycles : 0; }

static void rx_text(int idx, const char *s)
{
    acia_t *a = &nm->acia[idx];
    while (*s) console_rx_push(a, (uint8_t)*s++);
}

static void result(int idx, const char *verbose, int code)
{
    netchan_t *c = &chan[idx];
    char buf[64];
    if (c->verbose) snprintf(buf, sizeof buf, "\r\n%s\r\n", verbose);
    else snprintf(buf, sizeof buf, "%d\r", code);
    rx_text(idx, buf);
}

static void set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void close_conn(int idx, int notify)
{
    netchan_t *c = &chan[idx];
    int was = c->fd >= 0 || c->connecting;
    if (NET_DEBUG()) fprintf(stderr, "net: port %d closed (notify=%d)\n", idx, notify);
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    c->connecting = 0;
    c->txlen = 0;
    c->iac = 0;
    c->crseen = 0;
    if (c->to_guest) { kconv_close(c->to_guest); c->to_guest = NULL; }
    if (c->to_host) { kconv_close(c->to_host); c->to_host = NULL; }
    if (was && notify) {
        c->hup = 1;
        if (c->mode == NET_MODEM) {
            c->mstate = M_COMMAND;
            result(idx, "NO CARRIER", 3);
        }
    }
}

static void conn_established(int idx)
{
    netchan_t *c = &chan[idx];
    int one = 1;
    set_nonblock(c->fd);
    setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    c->hup = 0;
    c->iac = 0;
    c->crseen = 0;
    if (c->telnet) {
        c->to_guest = kconv_open(console_kanji_mode(), 1);
        c->to_host = kconv_open(console_kanji_mode(), 0);
    }
    if (c->mode == NET_LISTEN && c->telnet) {
        static const uint8_t neg[] = { IAC, WILL, 1 /*ECHO*/, IAC, WILL, 3 /*SGA*/, IAC, DO, 3 };
        send(c->fd, neg, sizeof neg, MSG_NOSIGNAL);
    }
}

static int listen_on(int port, int any)
{
    struct sockaddr_in sa;
    int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    sa.sin_addr.s_addr = htonl(any ? INADDR_ANY : INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 4) < 0) {
        fprintf(stderr, "net: cannot listen on port %d: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }
    set_nonblock(fd);
    return fd;
}

static int start_connect(int idx, const char *host, int port)
{
    netchan_t *c = &chan[idx];
    struct addrinfo hints, *res, *ai;
    char ps[16];
    int fd = -1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &res) != 0) return -1;
    for (ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        set_nonblock(fd);
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0 || errno == EINPROGRESS) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;
    c->fd = fd;
    c->connecting = 1;
    c->conn_deadline = now_cycles() + 10ull * (uint64_t)nm->clock_hz;
    return 0;
}

/* ------------------------------------------------------------------ */
/* modem commands                                                      */
/* ------------------------------------------------------------------ */
static void modem_dial(int idx, const char *num)
{
    netchan_t *c = &chan[idx];
    char host[256];
    const char *p = num;
    int port = 23;
    char *colon;
    while (*p == 'T' || *p == 'P' || *p == ' ') p++;
    snprintf(host, sizeof host, "%s", p);
    colon = strrchr(host, ':');
    if (colon) { *colon = 0; port = atoi(colon + 1); }
    if (!*host || port <= 0 || port > 65535) { result(idx, "ERROR", 4); return; }
    if (c->fd >= 0) close_conn(idx, 0);
    if (start_connect(idx, host, port) < 0) { result(idx, "NO CARRIER", 3); return; }
    c->mstate = M_DIALING;
    c->telnet = (port == 23);
}

static void modem_command(int idx, char *line)
{
    netchan_t *c = &chan[idx];
    char *p = line;
    int ok = 1;

    for (char *q = p; *q; q++) *q = (char)toupper((unsigned char)*q);
    if (p[0] != 'A' || p[1] != 'T') {
        if (p[0]) result(idx, "ERROR", 4);
        return;
    }
    p += 2;
    while (*p && ok) {
        switch (*p) {
        case ' ': p++; break;
        case 'Z': c->echo = 1; c->verbose = 1; c->autoans = 0; c->telnet = 0; if (c->fd >= 0) close_conn(idx, 0); p++; break;
        case '&': p++; if (*p) p++; break;
        case 'E': c->echo = (p[1] != '0'); p += (p[1] >= '0' && p[1] <= '9') ? 2 : 1; break;
        case 'V': c->verbose = (p[1] != '0'); p += (p[1] >= '0' && p[1] <= '9') ? 2 : 1; break;
        case 'Q': case 'X': case 'L': case 'M': p += (p[1] >= '0' && p[1] <= '9') ? 2 : 1; break;
        case 'H':
            if (c->fd >= 0 || c->connecting) { close_conn(idx, 0); }
            c->mstate = M_COMMAND;
            p += (p[1] >= '0' && p[1] <= '9') ? 2 : 1;
            break;
        case 'O':
            if (c->fd >= 0) { c->mstate = M_ONLINE; result(idx, "CONNECT", 1); return; }
            result(idx, "NO CARRIER", 3);
            return;
        case 'A':
            if (c->lfd < 0) { result(idx, "ERROR", 4); return; }
            if (c->ringing || c->fd >= 0) { c->ringing = 0; c->mstate = M_ONLINE; result(idx, "CONNECT", 1); return; }
            c->autoans = 2;                    /* answer as soon as someone calls */
            return;
        case 'D':
            modem_dial(idx, p + 1);
            return;                             /* result is sent when connected */
        case 'S': {                              /* S0=n  auto answer */
            int reg = atoi(p + 1);
            char *eq = strchr(p, '=');
            if (eq && reg == 0) c->autoans = atoi(eq + 1) != 0;
            p = p + strlen(p);
            break;
        }
        case '+':                                /* AT+TELNET=n */
            if (!strncmp(p, "+TELNET=", 8)) { c->telnet = atoi(p + 8) != 0; p += strlen(p); }
            else ok = 0;
            break;
        default: ok = 0;
        }
    }
    if (ok) result(idx, "OK", 0);
    else result(idx, "ERROR", 4);
}

/* ------------------------------------------------------------------ */
/* guest -> network                                                    */
/* ------------------------------------------------------------------ */
static void flush_tx(int idx)
{
    netchan_t *c = &chan[idx];
    int off = 0;
    if (c->fd < 0 || c->connecting || !c->txlen) { if (c->fd < 0) c->txlen = 0; return; }
    while (off < c->txlen) {
        ssize_t n = send(c->fd, c->txbuf + off, (size_t)(c->txlen - off), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close_conn(idx, 1);
            return;
        }
        off += (int)n;
    }
    if (off < c->txlen) memmove(c->txbuf, c->txbuf + off, (size_t)(c->txlen - off));
    c->txlen -= off;
}

static void queue_tx(int idx, uint8_t b)
{
    netchan_t *c = &chan[idx];
    if (c->txlen >= (int)sizeof c->txbuf) flush_tx(idx);
    if (c->txlen < (int)sizeof c->txbuf) c->txbuf[c->txlen++] = b;
}

void net_tx(machine_t *m, int idx, uint8_t ch)
{
    netchan_t *c = &chan[idx];
    uint64_t now = m->cpu.total_cycles;
    (void)m;

    if (NET_DEBUG()) fprintf(stderr, "net_tx[%d] %02x mode=%d\n", idx, ch, c->mode);
    if (c->mode == NET_MODEM && c->mstate != M_ONLINE) {
        if (ch == '\r' || ch == '\n') {
            if (c->echo && ch == '\r') rx_text(idx, "\r");
            c->cmd[c->cmdlen] = 0;
            if (c->cmdlen) modem_command(idx, c->cmd);
            c->cmdlen = 0;
        } else if (ch == 8 || ch == 127) {
            if (c->cmdlen) c->cmdlen--;
            if (c->echo) { console_rx_push(&m->acia[idx], 8); }
        } else {
            if (c->echo) console_rx_push(&m->acia[idx], ch);
            if (c->cmdlen < (int)sizeof c->cmd - 1) c->cmd[c->cmdlen++] = (char)ch;
        }
        return;
    }
    if (c->fd < 0 || c->connecting) return;       /* nobody listening: drop */

    if (c->mode == NET_MODEM) {                   /* escape sequence detection */
        uint64_t guard = (uint64_t)m->clock_hz;   /* 1 second */
        if (ch == '+' && (c->plus_count > 0 || now - c->last_tx_cycle >= guard)) {
            c->plus_count++;
            c->plus_cycle = now;
        } else c->plus_count = 0;
        c->last_tx_cycle = now;
    }

    if (c->telnet && c->to_host) {
        uint8_t out[16];
        size_t k = kconv_feed(c->to_host, &ch, 1, out, sizeof out), i;
        for (i = 0; i < k; i++) {
            if (out[i] == IAC) queue_tx(idx, IAC);
            queue_tx(idx, out[i]);
        }
    } else {
        queue_tx(idx, ch);
    }
    if (ch == '\r' || ch == '\n' || c->txlen > 256) flush_tx(idx);
}

/* ------------------------------------------------------------------ */
/* network -> guest                                                    */
/* ------------------------------------------------------------------ */
static void rx_byte_filtered(int idx, uint8_t b)
{
    netchan_t *c = &chan[idx];
    acia_t *a = &nm->acia[idx];
    uint8_t out[16];

    if (!c->telnet) { console_rx_push(a, b); return; }
    switch (c->iac) {
    case 1:                                    /* after IAC */
        if (b == IAC) { c->iac = 0; break; }   /* escaped 255 */
        if (b == SB) { c->iac = 3; return; }
        if (b >= WILL && b <= DONT) { c->iac = 2; return; }
        c->iac = 0;
        return;
    case 2: c->iac = 0; return;                /* option byte */
    case 3: if (b == IAC) c->iac = 4; return;  /* inside subnegotiation */
    case 4: c->iac = (b == SE) ? 0 : 3; return;
    default:
        if (b == IAC) { c->iac = 1; return; }
        break;
    }
    /* CR LF and CR NUL both mean a single CR for OS-9 */
    if (c->crseen) {
        c->crseen = 0;
        if (b == '\n' || b == 0) return;
    }
    if (b == '\r') c->crseen = 1;
    if (c->to_guest) {
        size_t k = kconv_feed(c->to_guest, &b, 1, out, sizeof out), i;
        for (i = 0; i < k; i++) console_rx_push(a, out[i]);
    } else {
        console_rx_push(a, b);
    }
}

static void accept_incoming(int idx)
{
    netchan_t *c = &chan[idx];
    struct sockaddr_in sa;
    socklen_t sl = sizeof sa;
    int fd;

    if (c->lfd < 0) return;
    fd = accept(c->lfd, (struct sockaddr *)&sa, &sl);
    if (fd < 0) return;
    if (c->mode == NET_LISTEN) {
        if (c->fd >= 0) {                      /* line busy */
            static const char busy[] = "OS-9 line busy\r\n";
            send(fd, busy, sizeof busy - 1, MSG_NOSIGNAL);
            close(fd);
            return;
        }
        c->fd = fd;
        conn_established(idx);
    } else {                                   /* modem: ring */
        if (c->fd >= 0 || c->ringing) { close(fd); return; }
        c->fd = fd;
        c->telnet = 0;
        conn_established(idx);
        c->ringing = 1;
        rx_text(idx, "\r\nRING\r\n");
        if (c->autoans) {
            c->ringing = 0;
            c->mstate = M_ONLINE;
            rx_text(idx, "\r\nCONNECT\r\n");
        }
    }
}

static void service_channel(int idx)
{
    netchan_t *c = &chan[idx];
    acia_t *a = &nm->acia[idx];
    uint8_t buf[512];

    if (c->mode == NET_OFF) return;
    if (c->mode == NET_TCP && !c->started && now_cycles() >= c->start_at) {
        c->started = 1;
        if (start_connect(idx, c->host, c->rport) < 0) {
            fprintf(stderr, "net: cannot connect to %s:%d\n", c->host, c->rport);
            c->hup = 1;
            return;
        }
    }
    if (c->lfd >= 0) accept_incoming(idx);

    if (c->connecting && c->fd >= 0) {
        struct pollfd p = { c->fd, POLLOUT, 0 };
        if (poll(&p, 1, 0) > 0) {
            int err = 0;
            socklen_t el = sizeof err;
            getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &el);
            c->connecting = 0;
            if (err) {
                close(c->fd); c->fd = -1;
                if (c->mode == NET_MODEM) { c->mstate = M_COMMAND; result(idx, err == ECONNREFUSED ? "BUSY" : "NO CARRIER", err == ECONNREFUSED ? 7 : 3); }
                c->hup = 1;
            } else {
                conn_established(idx);
                if (c->mode == NET_MODEM) { c->mstate = M_ONLINE; result(idx, "CONNECT", 1); }
            }
        } else if (now_cycles() > c->conn_deadline) {
            close(c->fd); c->fd = -1; c->connecting = 0;
            if (c->mode == NET_MODEM) { c->mstate = M_COMMAND; result(idx, "NO CARRIER", 3); }
            c->hup = 1;
        }
        return;
    }

    if (c->fd < 0) return;

    /* modem "+++" guard time after the third plus */
    if (c->mode == NET_MODEM && c->mstate == M_ONLINE && c->plus_count >= 3 &&
        now_cycles() - c->plus_cycle >= (uint64_t)nm->clock_hz) {
        c->plus_count = 0;
        c->mstate = M_COMMAND;
        result(idx, "OK", 0);
    }
    flush_tx(idx);
    if (c->fd < 0) return;

    /* a modem in command mode with an unanswered ring does not read data */
    if (c->mode == NET_MODEM && (c->mstate != M_ONLINE || c->ringing)) return;

    for (int round = 0; round < 4; round++) {
        size_t room = sizeof a->rxq - ((a->rxhead - a->rxtail) % sizeof a->rxq) - 1;
        ssize_t n;
        if (room < sizeof buf / 2 + 16) break;
        n = recv(c->fd, buf, sizeof buf / 2, MSG_DONTWAIT);
        if (n == 0) { close_conn(idx, 1); return; }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
            close_conn(idx, 1);
            return;
        }
        for (ssize_t i = 0; i < n; i++) rx_byte_filtered(idx, buf[i]);
    }
}

void net_poll(machine_t *m)
{
    static uint64_t last;
    if (!nm) return;
    if (m->cpu.total_cycles - last < 500 && m->cpu.total_cycles >= last) return;
    last = m->cpu.total_cycles;
    for (int i = 1; i < MAX_ACIA; i++)
        if (chan[i].mode) service_channel(i);
}

int net_pollfds(machine_t *m, struct pollfd *fds, int max)
{
    int n = 0;
    (void)m;
    for (int i = 1; i < MAX_ACIA && n + 2 <= max; i++) {
        netchan_t *c = &chan[i];
        if (!c->mode) continue;
        if (c->lfd >= 0) { fds[n].fd = c->lfd; fds[n].events = POLLIN; n++; }
        if (c->fd >= 0) { fds[n].fd = c->fd; fds[n].events = c->connecting ? POLLOUT : POLLIN; n++; }
    }
    return n;
}

uint8_t net_status_bits(machine_t *m, int idx)
{
    netchan_t *c = &chan[idx];
    uint8_t st = 0;
    (void)m;
    if (c->fd < 0 || c->connecting) st |= 0x04;      /* /DCD: no carrier */
    if (c->hup) st |= 0x80;
    return st;
}

void net_hup_ack(machine_t *m, int idx)
{
    (void)m;
    chan[idx].hup = 0;
}

/* ------------------------------------------------------------------ */
/* configuration                                                       */
/* ------------------------------------------------------------------ */
int net_config(machine_t *m, const char *spec)
{
    int idx;
    const char *p = spec;
    char buf[512], *flags, *kind, *rest;
    netchan_t *c;

    nm = m;
    if (*p < '1' || *p > '3' || p[1] != '=') return -1;
    idx = *p - '0';
    snprintf(buf, sizeof buf, "%s", p + 2);
    c = &chan[idx];
    memset(c, 0, sizeof *c);
    c->fd = c->lfd = -1;
    c->echo = c->verbose = 1;

    flags = strchr(buf, ',');
    if (flags) { *flags++ = 0; }
    kind = buf;
    rest = strchr(kind, ':');
    if (rest) *rest++ = 0;

    /* flags */
    while (flags && *flags) {
        char *nx = strchr(flags, ',');
        if (nx) *nx++ = 0;
        if (!strcmp(flags, "any")) c->raw_any = 1;
        else if (!strcmp(flags, "raw")) c->telnet = -1;     /* explicit raw */
        else if (!strcmp(flags, "telnet")) c->telnet = 1;
        flags = nx;
    }

    if (!strcmp(kind, "listen")) {
        c->mode = NET_LISTEN;
        c->port = rest ? atoi(rest) : 6809;
        if (c->telnet == 0) c->telnet = 1;
        if (c->telnet < 0) c->telnet = 0;
        c->lfd = listen_on(c->port, c->raw_any);
        if (c->lfd < 0) { c->mode = NET_OFF; return -1; }
    } else if (!strcmp(kind, "modem")) {
        c->mode = NET_MODEM;
        c->mstate = M_COMMAND;
        if (c->telnet < 0) c->telnet = 0;
        if (rest && *rest) {
            c->port = atoi(rest);
            c->lfd = listen_on(c->port, c->raw_any);
            if (c->lfd < 0) { c->mode = NET_OFF; return -1; }
        }
    } else if (!strcmp(kind, "tcp")) {
        char *colon;
        if (!rest) return -1;
        c->mode = NET_TCP;
        snprintf(c->host, sizeof c->host, "%s", rest);
        colon = strrchr(c->host, ':');
        if (!colon) return -1;
        *colon = 0;
        c->rport = atoi(colon + 1);
        if (c->telnet < 0) c->telnet = 0;
        /* The OS-9 kernel probes the I/O page with 16 bit loads while it looks for ROM
         * modules; reading the data register of a port eats a byte.  Connect only after
         * the boot has finished (2 seconds of emulated time). */
        c->start_at = 2ull * (uint64_t)m->clock_hz;
    } else {
        return -1;
    }
    m->acia[idx].kind = ACIA_NET;
    m->net_active = 1;
    return 0;
}

void net_shutdown(machine_t *m)
{
    (void)m;
    for (int i = 1; i < MAX_ACIA; i++) {
        if (!chan[i].mode) continue;
        if (chan[i].fd >= 0) close(chan[i].fd);
        if (chan[i].lfd >= 0) close(chan[i].lfd);
        chan[i].fd = chan[i].lfd = -1;
    }
}
