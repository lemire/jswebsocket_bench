// Relay server built on the 'ws' npm package. Runs under Node.js, Bun and Deno.
// Every message is forwarded to every other connected client (never echoed back).
import { WebSocketServer, WebSocket } from 'ws';

const port = Number(process.env.PORT ?? 8080);
const server = new WebSocketServer({ port });

server.on('connection', function (socket) {
  socket.on('error', console.error);
  socket.on('message', function (data, isBinary) {
    for (const client of server.clients) {
      if (client !== socket && client.readyState === WebSocket.OPEN) {
        client.send(data, { binary: isBinary });
      }
    }
  });
});
server.on('listening', () => console.log(`ws server listening on ${port}`));
