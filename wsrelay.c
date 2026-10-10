// wsrelay.c: a minimal single-threaded WebSocket relay server in C (Linux, epoll).
// It is a baseline for the JavaScript servers, not a general server: it only
// accepts the fixed Sec-WebSocket-Key that wsclient sends, it does not support
// fragmented messages, and it assumes small frames.
//
// Connections are paired in arrival order; every message goes to the peer.
//
// build: cc -O2 -o wsrelay wsrelay.c
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define CAP 65536

typedef struct conn {
  int fd;
  int upgraded;
  struct conn *peer;
  unsigned char buf[CAP];
  size_t have;
} conn;

static conn *waiting = NULL;

static void close_conn(conn *c) {
  if (waiting == c) {
    waiting = NULL;
  }
  if (c->peer) {
    c->peer->peer = NULL;
  }
  close(c->fd);
  free(c);
}

static int send_all(int fd, const unsigned char *p, size_t len) {
  while (len > 0) {
    ssize_t n = write(fd, p, len);
    if (n > 0) {
      p += n;
      len -= n;
    } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      continue;
    } else {
      return -1;
    }
  }
  return 0;
}

static const char *response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                              "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n";

// Returns -1 if the connection must be closed.
static int handle(conn *c) {
  ssize_t n = read(c->fd, c->buf + c->have, CAP - c->have);
  if (n <= 0) {
    return (n < 0 && errno == EAGAIN) ? 0 : -1;
  }
  c->have += n;
  if (!c->upgraded) {
    char *end = memmem(c->buf, c->have, "\r\n\r\n", 4);
    if (end == NULL) {
      return c->have == CAP ? -1 : 0;
    }
    if (memmem(c->buf, c->have, "dGhlIHNhbXBsZSBub25jZQ==", 24) == NULL) {
      return -1;
    }
    if (send_all(c->fd, (const unsigned char *)response, strlen(response)) != 0) {
      return -1;
    }
    c->upgraded = 1;
    size_t used = end + 4 - (char *)c->buf;
    c->have -= used;
    memmove(c->buf, c->buf + used, c->have);
    if (waiting != NULL) {
      c->peer = waiting;
      waiting->peer = c;
      waiting = NULL;
    } else {
      waiting = c;
    }
  }
  size_t off = 0;
  while (c->have - off >= 2) {
    unsigned char *b = c->buf + off;
    size_t hdr = 2, len = b[1] & 0x7f;
    if (len == 126) {
      if (c->have - off < 4) {
        break;
      }
      len = ((size_t)b[2] << 8) | b[3];
      hdr = 4;
    } else if (len == 127) {
      return -1;
    }
    if (!(b[1] & 0x80)) {
      return -1; // client frames must be masked
    }
    unsigned char *mask = b + hdr;
    hdr += 4;
    if (hdr + len > CAP - 16) {
      return -1;
    }
    if (c->have - off < hdr + len) {
      break;
    }
    int opcode = b[0] & 0x0f;
    if (opcode == 8) {
      return -1;
    }
    if (opcode == 1 || opcode == 2) {
      // Rewrite in place as an unmasked server frame: 2 or 4 header bytes, then the payload.
      unsigned char *payload = b + hdr;
      size_t ohdr = len < 126 ? 2 : 4;
      unsigned char *out = payload - ohdr;
      for (size_t i = 0; i < len; i++) {
        payload[i] ^= mask[i & 3];
      }
      out[0] = 0x80 | opcode;
      if (len < 126) {
        out[1] = len;
      } else {
        out[1] = 126;
        out[2] = len >> 8;
        out[3] = len & 0xff;
      }
      if (c->peer != NULL && send_all(c->peer->fd, out, ohdr + len) != 0) {
        return -1;
      }
    }
    off += hdr + len;
  }
  c->have -= off;
  memmove(c->buf, c->buf + off, c->have);
  return 0;
}

int main(int argc, char **argv) {
  int port = argc > 1 ? atoi(argv[1]) : 8080;
  int lfd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(lfd, 1024) != 0) {
    perror("bind/listen");
    return 1;
  }
  int ep = epoll_create1(0);
  struct epoll_event ev = {.events = EPOLLIN, .data.ptr = NULL};
  epoll_ctl(ep, EPOLL_CTL_ADD, lfd, &ev);
  printf("wsrelay listening on %d\n", port);
  fflush(stdout);
  struct epoll_event evs[256];
  for (;;) {
    int n = epoll_wait(ep, evs, 256, -1);
    for (int i = 0; i < n; i++) {
      conn *c = evs[i].data.ptr;
      if (c == NULL) {
        int fd = accept4(lfd, NULL, NULL, SOCK_NONBLOCK);
        if (fd < 0) {
          continue;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        c = calloc(1, sizeof(conn));
        c->fd = fd;
        struct epoll_event cev = {.events = EPOLLIN, .data.ptr = c};
        epoll_ctl(ep, EPOLL_CTL_ADD, fd, &cev);
      } else if (handle(c) != 0) {
        close_conn(c);
      }
    }
  }
}
