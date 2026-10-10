// Relay server built on the 'ws' npm package. Runs under Node.js, Bun and Deno.
// Connections are paired in arrival order; every message goes to the peer
// (never echoed back).
import { WebSocketServer, WebSocket } from 'ws';

const port = Number(process.env.PORT ?? 8080);
const server = new WebSocketServer({ port });
let waiting = null;

server.on('connection', function (socket) {
  socket.on('error', console.error);
  if (waiting !== null) {
    socket.peer = waiting;
    waiting.peer = socket;
    waiting = null;
  } else {
    waiting = socket;
  }
  socket.on('message', function (data, isBinary) {
    const peer = socket.peer;
    if (peer !== undefined && peer.readyState === WebSocket.OPEN) {
      peer.send(data, { binary: isBinary });
    }
  });
  socket.on('close', function () {
    if (waiting === socket) {
      waiting = null;
    }
  });
});
server.on('listening', () => console.log(`ws server listening on ${port}`));
