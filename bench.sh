#!/usr/bin/env bash
# Runs every server/client combination. Each process gets its own core.
# Configure the runtimes through the environment, e.g.
#   NODES="$HOME/.nvm/versions/node/v24.21.0/bin/node ..." BUN=... DENO=... ./bench.sh
set -u
cd "$(dirname "$0")"
SERVER_CPU=${SERVER_CPU:-8}
CLIENT_CPU=${CLIENT_CPU:-10}
export PORT=${PORT:-8080}
NODES=${NODES:-$(command -v node)}
BUN=${BUN:-$(command -v bun)}
DENO=${DENO:-$(command -v deno)}
OUT=${OUT:-results.csv}

ver() { "$1" --version | head -1 | sed -E 's/^[a-z]* ?v?([0-9][^ ]*).*/\1/'; }

servers=()
clients=()
for n in $NODES; do
  v=$(ver "$n")
  servers+=("node-$v ws|$n server_ws.js")
  servers+=("node-$v uws|$n server_uws.js")
  clients+=("node-$v ws|$n client.js ws")
  clients+=("node-$v native|$n client.js native")
done
if [ -n "$BUN" ]; then
  v=$(ver "$BUN")
  servers+=("bun-$v ws|$BUN server_ws.js" "bun-$v Bun.serve|$BUN server_bun.js")
  clients+=("bun-$v ws|$BUN client.js ws" "bun-$v native|$BUN client.js native")
fi
if [ -n "$DENO" ]; then
  v=$(ver "$DENO")
  servers+=("deno-$v ws|$DENO run -A server_ws.js" "deno-$v Deno.serve|$DENO run -A server_deno.js")
  clients+=("deno-$v ws|$DENO run -A client.js ws" "deno-$v native|$DENO run -A client.js native")
fi

wait_port() {
  for _ in $(seq 100); do
    (echo > /dev/tcp/127.0.0.1/$PORT) 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}

echo "server,client,median_round_trips_per_s" > "$OUT"
for s in "${servers[@]}"; do
  sname=${s%%|*}; scmd=${s#*|}
  taskset -c "$SERVER_CPU" $scmd > /dev/null 2>&1 &
  spid=$!
  if ! wait_port; then
    echo "$sname: server failed to start" >&2
    kill $spid 2>/dev/null; wait $spid 2>/dev/null
    continue
  fi
  for c in "${clients[@]}"; do
    cname=${c%%|*}; ccmd=${c#*|}
    m=$(timeout 300 taskset -c "$CLIENT_CPU" $ccmd 2>&1 | awk '/^median/ {print $2}')
    echo "$sname,$cname,${m:-failed}" | tee -a "$OUT"
  done
  kill $spid; wait $spid 2>/dev/null
  sleep 1
done
