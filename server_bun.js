// Relay server using Bun's built-in WebSocket support (Bun.serve).
const connections = new Set();
const port = Number(process.env.PORT ?? 8080);

Bun.serve({
  port,
  websocket: {
    message(ws, message) {
      for (const other of connections) {
        if (other !== ws) {
          other.send(message);
        }
      }
    },
    open(ws) {
      connections.add(ws);
    },
    close(ws) {
      connections.delete(ws);
    },
  },
  fetch(req, server) {
    if (server.upgrade(req)) {
      return undefined;
    }
    return new Response('Hello world!');
  },
});
console.log(`Bun.serve listening on ${port}`);
