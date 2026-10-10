#!/usr/bin/env python3
"""Combine one or more bench_c.sh CSV files into a Markdown table.

Rates are in thousands of round trips per second: the median over the files,
with the range across files in parentheses. Latencies are medians in
microseconds.

usage: summarize_c.py results_c1.csv [results_c2.csv ...]
"""
import csv
import statistics
import sys

servers, cells = [], {}
for path in sys.argv[1:]:
    with open(path) as f:
        for row in csv.DictReader(f):
            if row['server'] not in servers:
                servers.append(row['server'])
            cells.setdefault((row['server'], row['mode']), []).append(row)


def values(server, mode, key):
    return [float(r[key]) for r in cells.get((server, mode), []) if r[key] not in ('', 'failed')]


def rate(server, mode):
    v = values(server, mode, 'median_round_trips_per_s')
    if not v:
        return 'n/a'
    return f'{statistics.median(v) / 1000:.0f} ({min(v) / 1000:.0f}–{max(v) / 1000:.0f})'


def micros(server, key):
    v = values(server, 'pingpong', key)
    return f'{statistics.median(v):.1f}' if v else 'n/a'


print('| server | ping-pong | p50 (µs) | p99 (µs) | load, 16 B | load, 4 KiB |')
print('|---|---|---|---|---|---|')
for s in servers:
    print(f'| {s} | {rate(s, "pingpong")} | {micros(s, "p50_us")} | {micros(s, "p99_us")} '
          f'| {rate(s, "load16")} | {rate(s, "load4096")} |')
