Simple WebSocket benchmark (JavaScript)
-----------------

Two clients exchange messages through a relay server, one message at a time:
client 1 → server → client 2 → server → client 1. That is one round trip
(four WebSocket messages). The benchmark measures latency-bound throughput
(round trips per second), not how many concurrent connections a server can
handle. Everything runs on one machine over the loopback interface.

Servers:

| file | implementation | runtimes |
|------|----------------|----------|
| `server_ws.js` | [`ws`](https://github.com/websockets/ws) npm package | Node.js, Bun, Deno |
| `server_bun.js` | `Bun.serve` (built in) | Bun |
| `server_deno.js` | `Deno.upgradeWebSocket` (built in) | Deno |
| `server_uws.js` | [uWebSockets.js](https://github.com/uNetworking/uWebSockets.js) | Node.js 22, 24, 26 (uWS.js ships no binary for odd releases) |

Client: `client.js ws` uses the `ws` package; `client.js native` uses the
runtime's built-in `WebSocket` (Node.js 22 and later, Bun, Deno).

The client waits for both connections to open, checks that the server relays
rather than echoes, runs `WARMUP` round trips (default 100000), then `TRIALS`
(default 5) timed batches of `ROUNDS` round trips (default 200000) and prints
the median rate.

## Running

```
npm install
node server_ws.js          # or: bun server_bun.js, deno run -A server_deno.js, ...
```

In another shell:

```
node client.js ws          # or: bun client.js native, deno run -A client.js native, ...
```

To run every server/client combination, with the server pinned to core 8 and
the client to core 10 (Linux, `taskset`):

```
NODES="/path/to/node24 /path/to/node25 /path/to/node26" BUN=/path/to/bun DENO=/path/to/deno ./bench.sh
```

The results go to `results.csv` (set `OUT` to change it). Set `SERVER_CPU`,
`CLIENT_CPU`, `PORT`, `WARMUP`, `ROUNDS` and `TRIALS` as needed.

## Results (October 2026)

Intel Xeon Gold 6548N (Emerald Rapids), Linux, server pinned to core 8 and
client to core 10. Node.js 24.21.0, 25.9.0 and 26.11.1; Bun 1.4.2; Deno 2.9.7;
ws 8.22.0; uWebSockets.js 20.71.0. Thousands of round trips per second: the
median of three full passes of `bench.sh` (50000 warm-up round trips, then five
trials of 100000), with the range across passes in parentheses. Raw data is in
`results/`. Regenerate the table with `./summarize.py results/*.csv`.

| server \ client | node-24.21.0 ws | node-24.21.0 native | node-25.9.0 ws | node-25.9.0 native | node-26.11.1 ws | node-26.11.1 native | bun-1.4.2 ws | bun-1.4.2 native | deno-2.9.7 ws | deno-2.9.7 native |
|---|---|---|---|---|---|---|---|---|---|---|
| node-24.21.0 ws | 20 (20–20) | 20 (20–20) | 20 (20–21) | 20 (20–20) | 21 (21–21) | 20 (20–20) | 34 (32–34) | 35 (34–35) | 20 (20–20) | 20 (20–20) |
| node-24.21.0 uws | 22 (22–22) | 21 (21–21) | 22 (21–22) | 21 (21–21) | 22 (22–22) | 22 (21–22) | 39 (29–39) | 38 (30–39) | 21 (21–21) | 21 (21–21) |
| node-25.9.0 ws | 20 (20–20) | 20 (20–20) | 20 (20–20) | 20 (20–20) | 21 (21–21) | 20 (20–20) | 34 (30–35) | 35 (31–35) | 20 (20–20) | 20 (20–20) |
| node-26.11.1 ws | 20 (20–20) | 20 (20–20) | 20 (20–21) | 20 (20–20) | 21 (21–21) | 20 (20–20) | 34 (30–35) | 32 (30–35) | 20 (20–20) | 20 (20–20) |
| node-26.11.1 uws | 22 (22–22) | 21 (21–21) | 22 (22–22) | 21 (21–21) | 22 (22–22) | 21 (21–22) | 38 (37–39) | 39 (38–39) | 21 (21–21) | 21 (21–21) |
| bun-1.4.2 ws | 28 (28–33) | 30 (26–31) | 32 (29–33) | 30 (28–32) | 33 (31–33) | 32 (30–33) | 38 (38–38) | 38 (38–38) | 26 (24–31) | 24 (21–24) |
| bun-1.4.2 Bun.serve | 29 (29–35) | 33 (33–33) | 35 (35–35) | 33 (32–33) | 36 (36–36) | 35 (35–35) | 41 (41–41) | 41 (41–41) | 33 (32–34) | 33 (32–34) |
| deno-2.9.7 ws | 20 (20–20) | 19 (19–20) | 20 (20–20) | 19 (19–19) | 20 (20–20) | 20 (20–20) | 32 (27–33) | 33 (28–33) | 20 (20–20) | 19 (19–20) |
| deno-2.9.7 Deno.serve | 19 (19–19) | 19 (19–19) | 19 (19–19) | 19 (19–19) | 19 (19–19) | 19 (19–19) | 23 (22–24) | 21 (21–23) | 19 (19–19) | 19 (19–19) |

Cells with a Bun client vary more from pass to pass than the others.

## Note on the 2023 version

The original `bunserver.js` had a bug: it sent each message back to its sender
(`ws.send`) instead of to the other client. Client 1 was talking to itself, so
each "round trip" was two messages instead of four, which inflated the
Bun-specific numbers in the 2023 blog post by about a factor of two. The
client now detects this.
