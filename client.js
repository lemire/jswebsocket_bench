const WebSocket = require('ws');

const ws1 = new WebSocket('ws://127.0.0.1:8080');
const ws2 = new WebSocket('ws://127.0.0.1:8080');

ws1.on('error', console.error);
ws2.on('error', console.error);

ws1.on('open', function open() {
    console.log('client1 starts');

  ws1.send('hello from client 1');
});

var round_trips = 0;
var start_time = process.hrtime();
ws1.on('message', function message(data) {
  ws1.send('client 1 got it');
  round_trips++;
  if(round_trips % 65536 == 0)
  {
    var current_time = process.hrtime();
    var elapsed_time = current_time[0]*1000000000 + current_time[1] - start_time[0]*1000000000 - start_time[1];
    console.log('round trips per second: ' + round_trips * 1000000000.0 / elapsed_time );
    if(round_trips > 1000000){
        ws1.close();
        ws2.close();
      process.exit(0);
    }
  }
});

ws2.on('message', function message(data) {
    ws2.send('client 2 got it');
  });