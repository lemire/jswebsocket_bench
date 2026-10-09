#!/usr/bin/env python3
"""Combine one or more bench.sh CSV files into a Markdown table.

Each cell is the median over the files of the per-run medians, in thousands
of round trips per second, with the range across files in parentheses.

usage: summarize.py results1.csv [results2.csv ...]
"""
import csv
import statistics
import sys

servers, clients, cells = [], [], {}
for path in sys.argv[1:]:
    with open(path) as f:
        for row in csv.DictReader(f):
            s, c, v = row['server'], row['client'], row['median_round_trips_per_s']
            if s not in servers:
                servers.append(s)
            if c not in clients:
                clients.append(c)
            if v.replace('.', '').isdigit():
                cells.setdefault((s, c), []).append(float(v) / 1000)


def fmt(values):
    if not values:
        return 'n/a'
    m = statistics.median(values)
    if len(values) == 1:
        return f'{m:.0f}'
    return f'{m:.0f} ({min(values):.0f}–{max(values):.0f})'


print('| server \\ client | ' + ' | '.join(clients) + ' |')
print('|---' * (len(clients) + 1) + '|')
for s in servers:
    print(f'| {s} | ' + ' | '.join(fmt(cells.get((s, c), [])) for c in clients) + ' |')
