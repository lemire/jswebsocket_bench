
const connections = new Set();
const server = Bun.serve({
  port: 8080,
  websocket: {
    message(ws, message) {
      // send back a message
      for (const tws of connections.keys()) {
        if(tws != ws) {
          ws.send(`${message}`)
        }
      }
    },
    open(ws) {
      connections.add(ws);
      console.log(`opening connection ${ws}`);
    }, 
    close(ws, code, message) {
      connections.delete(ws);
      console.log(`closing connection ${ws}`);
    }, // a socket is closed
  },

  fetch(req, server) {
    const success = server.upgrade(req);
    if (success) {
      // Bun automatically returns a 101 Switching Protocols
      // if the upgrade succeeds
      return undefined;
    }

    // handle HTTP request normally
    return new Response("Hello world!");
  },
});
