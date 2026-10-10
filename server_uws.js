// Relay server using uWebSockets.js (a native addon, Node.js only).
// Connections are paired in arrival order; every message goes to the peer.
// The package's ESM wrapper points at a missing index.js, so load it as CommonJS.
import { createRequire } from 'node:module';
const uWS = createRequire(import.meta.url)('uWebSockets.js');

const port = Number(process.env.PORT ?? 8080);
const peers = new Map();
let waiting = null;

uWS.App().ws('/*', {
  open(ws) {
    if (waiting !== null) {
      peers.set(ws, waiting);
      peers.set(waiting, ws);
      waiting = null;
    } else {
      waiting = ws;
    }
  },
  message(ws, message, isBinary) {
    const peer = peers.get(ws);
    if (peer !== undefined) {
      peer.send(message, isBinary);
    }
  },
  close(ws) {
    if (waiting === ws) {
      waiting = null;
    }
    const peer = peers.get(ws);
    peers.delete(ws);
    if (peer !== undefined) {
      peers.delete(peer);
    }
  },
}).listen('127.0.0.1', port, (token) => {
  if (!token) {
    console.error(`failed to listen on ${port}`);
    process.exit(1);
  }
  console.log(`uWebSockets.js listening on ${port}`);
});
