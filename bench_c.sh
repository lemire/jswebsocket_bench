#!/usr/bin/env bash
# Runs every server against the C client (wsclient), in three modes:
#   pingpong:   one pair, busy-polling client, latency-bound round trips
#   load16:     64 pairs over 4 client threads, 16-byte messages (server-bound)
#   load4096:   64 pairs over 4 client threads, 4096-byte messages (server-bound)
# The server is pinned to one core and restarted for each mode. The busy time
# of that core (user + system + irq + softirq, from /proc/stat) during the
# measurement is reported as a percentage: on loopback, part of the TCP work
# runs as softirq and is not charged to the server process.
#
#   NODES="/path/to/node24 /path/to/node26" BUN=/path/to/bun DENO=/path/to/deno ./bench_c.sh
set -u
cd "$(dirname "$0")"
SERVER_CPU=${SERVER_CPU:-8}
CLIENT_CPU=${CLIENT_CPU:-10}
LOAD_CPUS=${LOAD_CPUS:-10,12,14,16}
export PORT=${PORT:-8080}
NODES=${NODES:-$(command -v node)}
BUN=${BUN:-$(command -v bun)}
DENO=${DENO:-$(command -v deno)}
OUT=${OUT:-results_c.csv}

cc -O2 -o wsclient wsclient.c -lpthread || exit 1
cc -O2 -o wsrelay wsrelay.c || exit 1

ver() { "$1" --version | head -1 | sed -E 's/^[a-z]* ?v?([0-9][^ ]*).*/\1/'; }

servers=("C wsrelay|./wsrelay $PORT")
for n in $NODES; do
  v=$(ver "$n")
  servers+=("node-$v ws|$n server_ws.js" "node-$v uws|$n server_uws.js")
done
if [ -n "$BUN" ]; then
  v=$(ver "$BUN")
  servers+=("bun-$v ws|$BUN server_ws.js" "bun-$v Bun.serve|$BUN server_bun.js")
fi
if [ -n "$DENO" ]; then
  v=$(ver "$DENO")
  servers+=("deno-$v ws|$DENO run -A server_ws.js" "deno-$v Deno.serve|$DENO run -A server_deno.js")
fi
modes=(
  "pingpong|$CLIENT_CPU|./wsclient pingpong -w 100000 -r 200000 -t 5"
  "load16|$LOAD_CPUS|./wsclient load -n 64 -T 4 -s 16 -W 2 -D 2 -t 5"
  "load4096|$LOAD_CPUS|./wsclient load -n 64 -T 4 -s 4096 -W 2 -D 2 -t 5"
)

wait_port() {
  for _ in $(seq 100); do
    (echo > /dev/tcp/127.0.0.1/$PORT) 2>/dev/null && return 0
    sleep 0.1
  done
  return 1
}
core_ticks() { awk -v c="cpu$1" '$1 == c {print $2 + $3 + $4 + $7 + $8, $2 + $3 + $4 + $5 + $6 + $7 + $8 + $9}' /proc/stat; }

echo "server,mode,median_round_trips_per_s,p50_us,p99_us,server_core_busy_pct" > "$OUT"
for s in "${servers[@]}"; do
  sname=${s%%|*}; scmd=${s#*|}
  for m in "${modes[@]}"; do
    mname=${m%%|*}; rest=${m#*|}; cpus=${rest%%|*}; ccmd=${rest#*|}
    taskset -c "$SERVER_CPU" $scmd > /dev/null 2>&1 < /dev/null &
    spid=$!
    if ! wait_port; then
      echo "$sname: server failed to start" >&2
      kill $spid 2>/dev/null; wait $spid 2>/dev/null
      continue
    fi
    sleep 0.5
    read b0 t0 < <(core_ticks "$SERVER_CPU")
    line=$(timeout 300 taskset -c "$cpus" $ccmd 2>&1 | tail -1)
    read b1 t1 < <(core_ticks "$SERVER_CPU")
    cpu=$(awk -v a="$b0" -v b="$b1" -v x="$t0" -v y="$t1" 'BEGIN {printf "%.0f", 100 * (b - a) / (y - x)}')
    rate=$(echo "$line" | awk '/^median/ {print $2}')
    p50=$(echo "$line" | awk '/^median/ && $3 == "p50_us:" {print $4}')
    p99=$(echo "$line" | awk '/^median/ && $5 == "p99_us:" {print $6}')
    [ -z "$rate" ] && echo "$sname $mname: $line" >&2
    echo "$sname,$mname,${rate:-failed},$p50,$p99,$cpu" | tee -a "$OUT"
    kill $spid; wait $spid 2>/dev/null
    sleep 1
  done
done
