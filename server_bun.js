// Relay server using Bun's built-in WebSocket support (Bun.serve).
// Connections are paired in arrival order; every message goes to the peer.
const port = Number(process.env.PORT ?? 8080);
let waiting = null;

Bun.serve({
  port,
  websocket: {
    message(ws, message) {
      const peer = ws.data.peer;
      if (peer !== null) {
        peer.send(message);
      }
    },
    open(ws) {
      if (waiting !== null) {
        ws.data.peer = waiting;
        waiting.data.peer = ws;
        waiting = null;
      } else {
        waiting = ws;
      }
    },
    close(ws) {
      if (waiting === ws) {
        waiting = null;
      }
      const peer = ws.data.peer;
      if (peer !== null) {
        peer.data.peer = null;
      }
    },
  },
  fetch(req, server) {
    if (server.upgrade(req, { data: { peer: null } })) {
      return undefined;
    }
    return new Response('Hello world!');
  },
});
console.log(`Bun.serve listening on ${port}`);
