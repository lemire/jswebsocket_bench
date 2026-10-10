// Relay server using Deno's built-in WebSocket support (Deno.upgradeWebSocket).
// Connections are paired in arrival order; every message goes to the peer.
const port = Number(Deno.env.get('PORT') ?? 8080);
const peers = new Map();
let waiting = null;

Deno.serve({ port, hostname: '127.0.0.1', onListen() { console.log(`Deno.serve listening on ${port}`); } }, (req) => {
  if (req.headers.get('upgrade') !== 'websocket') {
    return new Response('Hello world!');
  }
  const { socket, response } = Deno.upgradeWebSocket(req);
  if (waiting !== null) {
    peers.set(socket, waiting);
    peers.set(waiting, socket);
    waiting = null;
  } else {
    waiting = socket;
  }
  socket.onclose = () => {
    if (waiting === socket) {
      waiting = null;
    }
    peers.delete(socket);
  };
  socket.onmessage = (event) => {
    const peer = peers.get(socket);
    if (peer !== undefined && peer.readyState === WebSocket.OPEN) {
      peer.send(event.data);
    }
  };
  return response;
});
