// glink.h: the link cable between two copies of gpSP, over TCP. Header-only C, POSIX sockets, no threads.
//
// gpSP (the GBA emulator in both Delta Link on the iPhone and the Kindle games app) emulates the GBA Wireless
// Adapter and the link cable itself; all it needs from the frontend is the libretro "netpacket" interface:
// a way to send a packet to the other player and to be handed the packets that arrive. This file is that,
// for exactly two players:
//   host   = player 0, listens on GLINK_PORT (Delta Link on her iPhone; on a phone hotspot it is 172.20.10.1)
//   client = player 1, connects to the host (the Kindle)
//
// Wire format (both directions):
//   hello   "GLK1" u8 n, n bytes of the core's netpacket protocol version (must match or the link is refused)
//   packet  u32 little-endian length, u16 little-endian sender id, payload
//
// Use (one link per process; the libretro send/poll callbacks carry no context, so the state is a global):
//   glink_set_core(&iface)          once, with the struct the core passed to RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE
//   glink_host(port) / glink_join(ip, port)
//   glink_pump()                    every frame, before retro_run (accepts, connects, reads, flushes)
//   glink_state()                   GLINK_OFF / GLINK_WAITING / GLINK_LINKED
//   glink_close()
#ifndef GLINK_H
#define GLINK_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <poll.h>

#ifndef GLINK_NO_LIBRETRO_TYPES
#include "libretro.h"
#endif

#define GLINK_PORT 5739
enum { GLINK_OFF = 0, GLINK_WAITING = 1, GLINK_LINKED = 2 };

#define GLINK_RMAX (1u << 17)
#define GLINK_WMAX (1u << 20)

typedef struct {
  const struct retro_netpacket_callback *cb;
  int lfd, fd, host, state, connecting, started, hello_in;
  uint8_t rbuf[GLINK_RMAX]; size_t rlen;
  uint8_t wbuf[GLINK_WMAX]; size_t wlen;
  char err[96];
} glink_t;

static glink_t glink_g = { .lfd = -1, .fd = -1 };

static void glink_set_core(const struct retro_netpacket_callback *cb) { glink_g.cb = cb; }
static int glink_state(void) { return glink_g.state; }
static const char *glink_error(void) { return glink_g.err; }

static void glink__nb(int fd) {
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

static void glink__flush(void) {
  glink_t *g = &glink_g;
  while (g->fd >= 0 && g->wlen) {
#ifdef MSG_NOSIGNAL
    ssize_t n = send(g->fd, g->wbuf, g->wlen, MSG_NOSIGNAL);
#else
    ssize_t n = send(g->fd, g->wbuf, g->wlen, 0);
#endif
    if (n > 0) { memmove(g->wbuf, g->wbuf + n, g->wlen - n); g->wlen -= n; continue; }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
    return;  // a dead socket shows up on the next read
  }
}

static void glink__queue(const void *a, size_t n) {
  glink_t *g = &glink_g;
  // Full: wait (up to ~2 s) for the socket to drain rather than lose a packet; a stuck peer drops the link.
  for (int i = 0; g->fd >= 0 && g->wlen + n > GLINK_WMAX && i < 200; i++) {
    glink__flush();
    if (g->wlen + n <= GLINK_WMAX) break;
    struct pollfd pf = { g->fd, POLLOUT, 0 }; poll(&pf, 1, 10);
  }
  if (g->wlen + n > GLINK_WMAX) return;
  memcpy(g->wbuf + g->wlen, a, n); g->wlen += n;
}

static void RETRO_CALLCONV glink__send(int flags, const void *buf, size_t len, uint16_t client_id) {
  glink_t *g = &glink_g; (void)client_id;
  if (g->state != GLINK_LINKED) return;
  if (buf && len) {
    uint8_t h[6] = { (uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24),
                     (uint8_t)(g->host ? 0 : 1), 0 };
    glink__queue(h, 6); glink__queue(buf, len);
  }
  glink__flush();
  (void)flags;
}

static void glink__drop(const char *why) {
  glink_t *g = &glink_g;
  if (why) { strncpy(g->err, why, sizeof g->err - 1); g->err[sizeof g->err - 1] = 0; }
  if (g->fd >= 0) close(g->fd);
  g->fd = -1; g->rlen = g->wlen = 0; g->hello_in = 0; g->connecting = 0;
  if (g->state == GLINK_LINKED && g->cb) {
    if (g->host) { if (g->cb->disconnected) g->cb->disconnected(1); }
    else if (g->started) { if (g->cb->stop) g->cb->stop(); g->started = 0; }
  }
  g->state = (g->host && g->lfd >= 0) ? GLINK_WAITING : GLINK_OFF;
}

static void RETRO_CALLCONV glink__poll(void);

static void glink__hello(void) {
  const char *v = (glink_g.cb && glink_g.cb->protocol_version) ? glink_g.cb->protocol_version : "";
  uint8_t n = (uint8_t)strlen(v);
  glink__queue("GLK1", 4); glink__queue(&n, 1); glink__queue(v, n); glink__flush();
}

static void glink__linked(void) {
  glink_t *g = &glink_g;
  g->state = GLINK_LINKED;
  if (g->host) {
    if (g->cb && g->cb->connected && !g->cb->connected(1)) { glink__drop("core refused the player"); return; }
  } else if (g->cb && g->cb->start) { g->cb->start(1, glink__send, glink__poll); g->started = 1; }
}

static void glink__read(void) {
  glink_t *g = &glink_g;
  if (g->fd < 0) return;
  for (;;) {
    if (g->rlen == GLINK_RMAX) { glink__drop("packet too big"); return; }
    ssize_t n = recv(g->fd, g->rbuf + g->rlen, GLINK_RMAX - g->rlen, 0);
    if (n > 0) { g->rlen += n; continue; }
    if (n == 0) { glink__drop("the other side closed the link"); return; }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
    glink__drop("link lost"); return;
  }
  size_t p = 0;
  if (!g->hello_in) {
    if (g->rlen < 5) return;
    if (memcmp(g->rbuf, "GLK1", 4)) { glink__drop("not a glink peer"); return; }
    size_t n = g->rbuf[4]; if (g->rlen < 5 + n) return;
    const char *v = (g->cb && g->cb->protocol_version) ? g->cb->protocol_version : "";
    if (n != strlen(v) || memcmp(g->rbuf + 5, v, n)) { glink__drop("emulator versions differ"); return; }
    g->hello_in = 1; p = 5 + n;
    glink__linked();
    if (g->fd < 0) return;
  }
  while (g->rlen - p >= 6) {
    uint32_t len = g->rbuf[p] | g->rbuf[p + 1] << 8 | g->rbuf[p + 2] << 16 | (uint32_t)g->rbuf[p + 3] << 24;
    uint16_t from = g->rbuf[p + 4] | g->rbuf[p + 5] << 8;
    if (len > GLINK_RMAX - 6) { glink__drop("bad packet"); return; }
    if (g->rlen - p - 6 < len) break;
    if (g->cb && g->cb->receive) g->cb->receive(g->rbuf + p + 6, len, from);
    if (g->fd < 0) return;
    p += 6 + len;
  }
  memmove(g->rbuf, g->rbuf + p, g->rlen - p); g->rlen -= p;
}

static void RETRO_CALLCONV glink__poll(void) { glink__flush(); glink__read(); }

// Host: player 0. The core's session starts now (start(0)); the other player joins whenever they connect.
static int glink_host(int port) {
  glink_t *g = &glink_g;
  signal(SIGPIPE, SIG_IGN);
  g->host = 1; g->err[0] = 0;
  g->lfd = socket(AF_INET, SOCK_STREAM, 0); if (g->lfd < 0) return -1;
  int one = 1; setsockopt(g->lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(g->lfd, (struct sockaddr *)&a, sizeof a) || listen(g->lfd, 2)) { close(g->lfd); g->lfd = -1; return -1; }
  fcntl(g->lfd, F_SETFL, fcntl(g->lfd, F_GETFL, 0) | O_NONBLOCK);
  g->state = GLINK_WAITING;
  if (g->cb && g->cb->start) { g->cb->start(0, glink__send, glink__poll); g->started = 1; }
  return 0;
}

// Client: player 1. Non-blocking; glink_pump() finishes the connect. Returns -1 only on a bad address.
static int glink_join(const char *ip, int port) {
  glink_t *g = &glink_g;
  signal(SIGPIPE, SIG_IGN);
  g->host = 0; g->err[0] = 0;
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) return -1;
  g->fd = socket(AF_INET, SOCK_STREAM, 0); if (g->fd < 0) return -1;
  glink__nb(g->fd);
  if (connect(g->fd, (struct sockaddr *)&a, sizeof a) && errno != EINPROGRESS) { glink__drop("could not connect"); return 0; }
  g->connecting = 1; g->state = GLINK_WAITING;
  return 0;
}

// Adopt an already connected socket as the client (the Kindle finds the phone itself, then hands the socket over).
static void glink_join_fd(int fd) {
  glink_t *g = &glink_g;
  signal(SIGPIPE, SIG_IGN);
  g->host = 0; g->err[0] = 0; g->fd = fd; glink__nb(fd);
  g->state = GLINK_WAITING; glink__hello();
}

static void glink_pump(void) {
  glink_t *g = &glink_g;
  if (g->host && g->lfd >= 0 && g->fd < 0) {
    int fd = accept(g->lfd, NULL, NULL);
    if (fd >= 0) { g->fd = fd; glink__nb(fd); g->rlen = g->wlen = 0; g->hello_in = 0; glink__hello(); }
  }
  if (g->connecting) {
    // Writable means the connect finished; SO_ERROR says how.
    struct pollfd pf = { g->fd, POLLOUT, 0 };
    if (poll(&pf, 1, 0) <= 0) return;
    int e = 0; socklen_t l = sizeof e;
    getsockopt(g->fd, SOL_SOCKET, SO_ERROR, &e, &l);
    if (e) { glink__drop("could not connect"); return; }
    g->connecting = 0; glink__hello();
  }
  glink__flush();
  glink__read();
}

static void glink_close(void) {
  glink_t *g = &glink_g;
  glink__drop(NULL);
  if (g->lfd >= 0) { close(g->lfd); g->lfd = -1; }
  if (g->started && g->cb && g->cb->stop) g->cb->stop();
  g->started = 0; g->state = GLINK_OFF;
}
#endif
