// Relay server using uWebSockets.js (a native addon, Node.js only).
// The package's ESM wrapper points at a missing index.js, so load it as CommonJS.
import { createRequire } from 'node:module';
const uWS = createRequire(import.meta.url)('uWebSockets.js');

const connections = new Set();
const port = Number(process.env.PORT ?? 8080);

uWS.App().ws('/*', {
  open(ws) {
    connections.add(ws);
  },
  message(ws, message, isBinary) {
    for (const other of connections) {
      if (other !== ws) {
        other.send(message, isBinary);
      }
    }
  },
  close(ws) {
    connections.delete(ws);
  },
}).listen('127.0.0.1', port, (token) => {
  if (!token) {
    console.error(`failed to listen on ${port}`);
    process.exit(1);
  }
  console.log(`uWebSockets.js listening on ${port}`);
});
