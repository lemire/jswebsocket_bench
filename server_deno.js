// Relay server using Deno's built-in WebSocket support (Deno.upgradeWebSocket).
const connections = new Set();
const port = Number(Deno.env.get('PORT') ?? 8080);

Deno.serve({ port, hostname: '127.0.0.1', onListen() { console.log(`Deno.serve listening on ${port}`); } }, (req) => {
  if (req.headers.get('upgrade') !== 'websocket') {
    return new Response('Hello world!');
  }
  const { socket, response } = Deno.upgradeWebSocket(req);
  socket.onopen = () => connections.add(socket);
  socket.onclose = () => connections.delete(socket);
  socket.onmessage = (event) => {
    for (const other of connections) {
      if (other !== socket) {
        other.send(event.data);
      }
    }
  };
  return response;
});
