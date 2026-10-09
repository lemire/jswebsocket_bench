// Two clients ping-pong through the relay server: client 1 -> server -> client 2
// -> server -> client 1 counts as one round trip (four WebSocket messages).
//
// usage: client.js [ws|native]
//   ws:     the 'ws' npm package
//   native: the runtime's built-in WebSocket (Node.js 22+, Bun, Deno)
//
// Environment: PORT (8080), WARMUP (100000 round trips), ROUNDS (200000 round
// trips per trial), TRIALS (5).

const impl = (typeof Deno !== 'undefined' ? Deno.args : process.argv.slice(2))[0] ?? 'ws';
const env = (k, d) => Number((typeof Deno !== 'undefined' ? Deno.env.get(k) : process.env[k]) ?? d);
const port = env('PORT', 8080);
const warmup = env('WARMUP', 100000);
const rounds = env('ROUNDS', 200000);
const trials = env('TRIALS', 5);

let WS;
if (impl === 'ws') {
  WS = (await import('ws')).WebSocket;
} else if (impl === 'native') {
  if (typeof globalThis.WebSocket !== 'function') {
    console.error('no built-in WebSocket in this runtime');
    process.exit(2);
  }
  WS = globalThis.WebSocket;
} else {
  console.error(`unknown client implementation: ${impl}`);
  process.exit(2);
}

const url = `ws://127.0.0.1:${port}`;
const ws1 = new WS(url);
const ws2 = new WS(url);
const opened = (s) => new Promise((resolve, reject) => { s.onopen = resolve; s.onerror = reject; });
await Promise.all([opened(ws1), opened(ws2)]);

const results = [];
let count = 0;
let target = warmup;
let start = 0;
let checked = false;

const finish = () => {
  console.log(`median: ${results.slice().sort((a, b) => a - b)[results.length >> 1].toFixed(0)}`);
  ws1.close();
  ws2.close();
  setTimeout(() => (typeof Deno !== 'undefined' ? Deno.exit(0) : process.exit(0)), 10);
};

ws2.onmessage = () => {
  ws2.send('2');
};
ws1.onmessage = (event) => {
  if (!checked) {
    // Guard against a server that echoes to the sender instead of relaying.
    if (String(event.data) !== '2') {
      throw new Error(`client 1 received '${event.data}', expected the message from client 2`);
    }
    checked = true;
  }
  if (++count === target) {
    const now = performance.now();
    if (start !== 0) {
      const rate = rounds * 1000 / (now - start);
      results.push(rate);
      console.log(`round trips per second: ${rate.toFixed(0)}`);
      if (results.length === trials) {
        return finish();
      }
    }
    start = now;
    count = 0;
    target = rounds;
  }
  ws1.send('1');
};
ws1.send('1');
