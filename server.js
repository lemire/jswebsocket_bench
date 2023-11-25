const WebSocket = require('ws');
const server = new WebSocket.Server({
  port: 8080
});

server.on('connection', function(socket) {
  socket.on('error', console.error);
  // broadcast to all clients, except sender
  socket.on('message', function message(data, isBinary) {
    server.clients.forEach(function each(client) {
      if (client !== socket && client.readyState === WebSocket.OPEN) {
        client.send(data, { binary: isBinary });
      }
    });
  });
  // when the user disconnects.. display a message
  socket.on('close', function() {
    console.log('disconnected');
  });
});