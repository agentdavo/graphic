"""Resolve a cprof report's image offsets to function names.

    python tools/cprof_resolve.py <exe> <cprof.txt> [top]

Uses nm for symbol addresses and objdump for the link-time image base; both
ship with the MSYS2 and GNU toolchains. Prints self and inclusive time per
function with the share of the total self time, largest first.
"""
import bisect
import re
import subprocess
import sys

exe, report = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40

base = 0
for line in subprocess.run(["objdump", "-p", exe], capture_output=True, text=True, check=True).stdout.splitlines():
    m = re.match(r"\s*ImageBase\s+([0-9a-fA-F]+)", line)
    if m:
        base = int(m.group(1), 16)
        break

symbols = []
for line in subprocess.run(["nm", "-n", "--defined-only", exe], capture_output=True, text=True, check=True).stdout.splitlines():
    parts = line.split()
    if len(parts) == 3 and parts[1] in "tT":
        symbols.append((int(parts[0], 16), parts[2]))
addresses = [s[0] for s in symbols]

def name(offset):
    vma = base + offset
    i = bisect.bisect_right(addresses, vma) - 1
    return symbols[i][1] if i >= 0 else f"{offset:#x}"

rows = []
header = ""
for line in open(report, encoding="utf-8"):
    if line.startswith("#"):
        header = line.strip()
        continue
    offset, calls, self_ms, incl_ms = line.split()
    rows.append((name(int(offset, 16)), int(calls), float(self_ms), float(incl_ms)))
total_self = sum(r[2] for r in rows) or 1.0
print(header)
print(f"{'function':40} {'calls':>10} {'self ms':>10} {'incl ms':>10} {'self %':>7}")
for fn, calls, self_ms, incl_ms in rows[:top]:
    print(f"{fn[:40]:40} {calls:>10} {self_ms:>10.2f} {incl_ms:>10.2f} {100*self_ms/total_self:>6.1f}%")
