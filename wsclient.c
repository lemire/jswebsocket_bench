// wsclient.c: a minimal, low-overhead WebSocket client for relay-server benchmarks (Linux).
//
// Connections are paired: client A sends to the server, the server relays to
// client B, B replies, the server relays back to A. That is one round trip
// (four WebSocket messages). The client checks every message it receives, so a
// server that echoes instead of relaying fails immediately.
//
// The client does as little as possible per message: frames are prebuilt
// (zero masking key, so masking is free), sends are a single write(), and in
// the default mode sockets are non-blocking and polled in a busy loop so the
// client never sleeps.
//
// Modes:
//   pingpong (default): one pair, strictly sequential round trips; reports
//                       the rate and the per-round-trip latency (p50, p99).
//   load:               N pairs, each with one message in flight, spread over
//                       T threads (one per CPU in the process affinity mask);
//                       reports the total rate.
//
// build: cc -O2 -o wsclient wsclient.c -lpthread
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int port = 8080;
static int size = 16;     // payload bytes
static int spin = 1;      // busy-poll instead of blocking
static long warmup = 100000;
static long rounds = 200000;
static int trials = 5;
static int pairs = 1;
static int threads = 1;
static double warmup_s = 1.0;
static double duration_s = 2.0;

typedef struct conn {
  int fd;
  int is_a;
  unsigned char *frame; // our outgoing frame
  size_t frame_len;
  const unsigned char *expect; // payload we must receive (the peer's)
  unsigned char *buf;
  size_t have, cap;
} conn;

typedef struct {
  conn a, b;
} pair_t;

static void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

static inline uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}

// Masked client frame with a zero masking key, so the payload is sent as is.
static void make_frame(conn *c, char tag, unsigned id) {
  size_t hdr = size < 126 ? 6 : 8;
  c->frame_len = hdr + size;
  c->frame = calloc(1, c->frame_len);
  c->frame[0] = 0x81; // FIN, text
  if (size < 126) {
    c->frame[1] = 0x80 | size;
  } else {
    c->frame[1] = 0x80 | 126;
    c->frame[2] = size >> 8;
    c->frame[3] = size & 0xff;
  }
  unsigned char *p = c->frame + hdr;
  char text[16];
  snprintf(text, sizeof(text), "%c%08x", tag, id);
  for (int i = 0; i < size; i++) {
    p[i] = i < 9 ? text[i] : 'x';
  }
}

static void send_all(conn *c, const unsigned char *data, size_t len) {
  while (len > 0) {
    ssize_t n = write(c->fd, data, len);
    if (n > 0) {
      data += n;
      len -= n;
    } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      continue;
    } else {
      die("write failed: %s", strerror(errno));
    }
  }
}

// Consumes complete frames from the buffer. Returns 1 when a data frame was
// consumed (and validated), 0 if more bytes are needed.
static int try_frame(conn *c) {
  for (;;) {
    if (c->have < 2) {
      return 0;
    }
    unsigned char *b = c->buf;
    if (b[1] & 0x80) {
      die("server frame is masked");
    }
    size_t hdr = 2, len = b[1] & 0x7f;
    if (len == 126) {
      if (c->have < 4) {
        return 0;
      }
      len = ((size_t)b[2] << 8) | b[3];
      hdr = 4;
    } else if (len == 127) {
      if (c->have < 10) {
        return 0;
      }
      len = 0;
      for (int i = 2; i < 10; i++) {
        len = (len << 8) | b[i];
      }
      hdr = 10;
    }
    if (hdr + len > c->cap) {
      die("frame too large: %zu bytes", len);
    }
    if (c->have < hdr + len) {
      return 0;
    }
    int opcode = b[0] & 0x0f;
    int is_data = 0;
    if (opcode == 1 || opcode == 2) {
      if (!(b[0] & 0x80)) {
        die("fragmented messages are not supported");
      }
      if (len != (size_t)size || memcmp(b + hdr, c->expect, size) != 0) {
        die("client %c received '%.*s', expected '%.*s' (does the server echo instead of relaying?)",
            c->is_a ? 'A' : 'B', (int)(len < 64 ? len : 64), b + hdr, size < 64 ? size : 64, c->expect);
      }
      is_data = 1;
    } else if (opcode == 9) { // ping: answer with a pong
      unsigned char pong[6 + 125];
      if (len > 125) {
        die("bad ping");
      }
      pong[0] = 0x8a;
      pong[1] = 0x80 | len;
      memset(pong + 2, 0, 4);
      memcpy(pong + 6, b + hdr, len);
      send_all(c, pong, 6 + len);
    } else if (opcode == 8) {
      die("server closed the connection");
    } else if (opcode != 10) {
      die("unexpected opcode %d", opcode);
    }
    c->have -= hdr + len;
    memmove(c->buf, c->buf + hdr + len, c->have);
    if (is_data) {
      return 1;
    }
  }
}

static int fill(conn *c) {
  ssize_t n = read(c->fd, c->buf + c->have, c->cap - c->have);
  if (n > 0) {
    c->have += n;
    return 1;
  }
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
    return 0;
  }
  die(n == 0 ? "server closed the connection" : "read failed: %s", strerror(errno));
  return 0;
}

static void recv_frame(conn *c) {
  uint64_t deadline = 0;
  while (!try_frame(c)) {
    if (!fill(c)) {
      uint64_t t = now_ns();
      if (deadline == 0) {
        deadline = t + 5000000000u;
      } else if (t > deadline) {
        die("client %c received nothing for 5 s (does the server echo instead of relaying?)", c->is_a ? 'A' : 'B');
      }
    }
  }
}

static void ws_connect(conn *c) {
  c->fd = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  struct timeval tv = {.tv_sec = 1}; // so that blocking reads can time out
  setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(port)};
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(c->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    die("connect to port %d failed: %s", port, strerror(errno));
  }
  char req[256];
  int n = snprintf(req, sizeof(req),
                   "GET / HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
                   port);
  send_all(c, (unsigned char *)req, n);
  c->cap = 2 * (size + 14) + 4096;
  c->buf = malloc(c->cap + 1);
  c->have = 0;
  char *end;
  for (;;) {
    fill(c);
    c->buf[c->have] = 0;
    if ((end = memmem(c->buf, c->have, "\r\n\r\n", 4)) != NULL) {
      break;
    }
    if (c->have >= c->cap - 1) {
      die("handshake response too large");
    }
  }
  if (strncmp((char *)c->buf, "HTTP/1.1 101", 12) != 0) {
    die("handshake failed: %.*s", (int)(end - (char *)c->buf), c->buf);
  }
  size_t used = end + 4 - (char *)c->buf;
  c->have -= used;
  memmove(c->buf, c->buf + used, c->have);
}

static int cmp_u64(const void *x, const void *y) {
  uint64_t a = *(const uint64_t *)x, b = *(const uint64_t *)y;
  return a < b ? -1 : a > b;
}

static int cmp_double(const void *x, const void *y) {
  double a = *(const double *)x, b = *(const double *)y;
  return a < b ? -1 : a > b;
}

static double median(double *v, int n) {
  qsort(v, n, sizeof(double), cmp_double);
  return v[n / 2];
}

static void run_pingpong(pair_t *p) {
  conn *a = &p->a, *b = &p->b;
  uint64_t *lat = malloc(sizeof(uint64_t) * (rounds > warmup ? rounds : warmup));
  double *rate = malloc(sizeof(double) * trials);
  double *p50 = malloc(sizeof(double) * trials);
  double *p99 = malloc(sizeof(double) * trials);
  for (int t = -1; t < trials; t++) {
    long n = t < 0 ? warmup : rounds;
    uint64_t start = now_ns(), prev = start;
    for (long i = 0; i < n; i++) {
      send_all(a, a->frame, a->frame_len);
      recv_frame(b);
      send_all(b, b->frame, b->frame_len);
      recv_frame(a);
      uint64_t now = now_ns();
      lat[i] = now - prev;
      prev = now;
    }
    if (t < 0) {
      continue;
    }
    rate[t] = n * 1e9 / (prev - start);
    qsort(lat, n, sizeof(uint64_t), cmp_u64);
    p50[t] = lat[n / 2] / 1000.0;
    p99[t] = lat[n * 99 / 100] / 1000.0;
    printf("round trips per second: %.0f  p50: %.2f us  p99: %.2f us\n", rate[t], p50[t], p99[t]);
    fflush(stdout);
  }
  printf("median: %.0f p50_us: %.2f p99_us: %.2f\n", median(rate, trials), median(p50, trials),
         median(p99, trials));
}

typedef struct {
  _Atomic uint64_t count;
  char pad[56];
} counter_t;

static counter_t *counters;
static pair_t *all_pairs;
static int *cpus;
static int ncpus;

static void *load_thread(void *arg) {
  int tid = (int)(intptr_t)arg;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpus[tid % ncpus], &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
  int ep = epoll_create1(0);
  for (int i = tid; i < pairs; i += threads) {
    conn *cs[2] = {&all_pairs[i].a, &all_pairs[i].b};
    for (int k = 0; k < 2; k++) {
      struct epoll_event ev = {.events = EPOLLIN, .data.ptr = cs[k]};
      epoll_ctl(ep, EPOLL_CTL_ADD, cs[k]->fd, &ev);
    }
  }
  for (int i = tid; i < pairs; i += threads) {
    send_all(&all_pairs[i].a, all_pairs[i].a.frame, all_pairs[i].a.frame_len);
  }
  uint64_t count = 0;
  struct epoll_event evs[256];
  for (;;) {
    int n = epoll_wait(ep, evs, 256, spin ? 0 : -1);
    for (int k = 0; k < n; k++) {
      conn *c = evs[k].data.ptr;
      fill(c);
      while (try_frame(c)) {
        if (c->is_a) {
          count++;
        }
        send_all(c, c->frame, c->frame_len);
      }
    }
    atomic_store_explicit(&counters[tid].count, count, memory_order_relaxed);
  }
  return NULL;
}

static uint64_t total_count(void) {
  uint64_t s = 0;
  for (int t = 0; t < threads; t++) {
    s += atomic_load_explicit(&counters[t].count, memory_order_relaxed);
  }
  return s;
}

static void sleep_s(double s) {
  struct timespec ts = {(time_t)s, (long)((s - (time_t)s) * 1e9)};
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
}

static void run_load(void) {
  cpu_set_t set;
  sched_getaffinity(0, sizeof(set), &set);
  cpus = malloc(sizeof(int) * CPU_SETSIZE);
  for (int i = 0; i < CPU_SETSIZE; i++) {
    if (CPU_ISSET(i, &set)) {
      cpus[ncpus++] = i;
    }
  }
  if (ncpus < threads) {
    fprintf(stderr, "warning: %d threads share %d cpus\n", threads, ncpus);
  }
  counters = calloc(threads, sizeof(counter_t));
  pthread_t th;
  for (int t = 0; t < threads; t++) {
    pthread_create(&th, NULL, load_thread, (void *)(intptr_t)t);
  }
  sleep_s(warmup_s);
  double *rate = malloc(sizeof(double) * trials);
  uint64_t c0 = total_count(), t0 = now_ns();
  for (int t = 0; t < trials; t++) {
    sleep_s(duration_s);
    uint64_t c1 = total_count(), t1 = now_ns();
    if (c1 == c0) {
      die("no round trip completed in %.1f s (does the server echo instead of relaying?)", duration_s);
    }
    rate[t] = (c1 - c0) * 1e9 / (t1 - t0);
    printf("round trips per second: %.0f\n", rate[t]);
    fflush(stdout);
    c0 = c1;
    t0 = t1;
  }
  printf("median: %.0f\n", median(rate, trials));
  fflush(stdout);
  _exit(0);
}

static void usage(void) {
  fprintf(stderr,
          "usage: wsclient [pingpong|load] [-p port] [-s payload_bytes] [-b (block, do not spin)]\n"
          "  pingpong: [-w warmup_round_trips] [-r round_trips_per_trial] [-t trials]\n"
          "  load:     [-n pairs] [-T threads] [-W warmup_seconds] [-D seconds_per_trial] [-t trials]\n");
  exit(2);
}

int main(int argc, char **argv) {
  int load = 0;
  if (argc > 1 && argv[1][0] != '-') {
    if (strcmp(argv[1], "load") == 0) {
      load = 1;
    } else if (strcmp(argv[1], "pingpong") != 0) {
      usage();
    }
    argv++;
    argc--;
  }
  int opt;
  while ((opt = getopt(argc, argv, "p:s:bw:r:t:n:T:W:D:")) != -1) {
    switch (opt) {
    case 'p': port = atoi(optarg); break;
    case 's': size = atoi(optarg); break;
    case 'b': spin = 0; break;
    case 'w': warmup = atol(optarg); break;
    case 'r': rounds = atol(optarg); break;
    case 't': trials = atoi(optarg); break;
    case 'n': pairs = atoi(optarg); break;
    case 'T': threads = atoi(optarg); break;
    case 'W': warmup_s = atof(optarg); break;
    case 'D': duration_s = atof(optarg); break;
    default: usage();
    }
  }
  if (size < 1 || size > 65535 || trials < 1 || pairs < 1 || threads < 1 || rounds < 1) {
    usage();
  }
  if (!load) {
    pairs = 1;
  }
  if (threads > pairs) {
    threads = pairs;
  }
  all_pairs = calloc(pairs, sizeof(pair_t));
  // Servers pair connections in arrival order: connect A, then B, one pair at a time.
  for (int i = 0; i < pairs; i++) {
    pair_t *p = &all_pairs[i];
    p->a.is_a = 1;
    make_frame(&p->a, 'A', i);
    make_frame(&p->b, 'B', i);
    p->a.expect = p->b.frame + (p->b.frame_len - size);
    p->b.expect = p->a.frame + (p->a.frame_len - size);
    ws_connect(&p->a);
    ws_connect(&p->b);
  }
  sleep_s(0.1); // let the server finish opening the last connections
  for (int i = 0; i < pairs; i++) {
    if (spin || load) {
      fcntl(all_pairs[i].a.fd, F_SETFL, O_NONBLOCK);
      fcntl(all_pairs[i].b.fd, F_SETFL, O_NONBLOCK);
    }
  }
  if (load) {
    run_load();
  } else {
    run_pingpong(&all_pairs[0]);
  }
  return 0;
}
