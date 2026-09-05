"""Fail closed on external calls reachable from the mixer in GCC's call graph.
This complements runtime CRT interposition: a new syscall/lock helper requires
an explicit audit rather than silently escaping a fixed forbidden-name list.
"""
from pathlib import Path
import re
import sys

dump = Path(sys.argv[1]).read_text()
initial = dump.split("Initial Symbol table:", 1)[1].split("Removing unused symbols:", 1)[0]
nodes = {}
for block in re.split(r"\n(?=\S)", initial):
    match = re.match(r"(\S+/\d+) \(([^)]+)\)", block.strip())
    if not match:
        continue
    node, name = match.groups()
    calls = re.search(r"^  Calls: (.*)$", block, re.M)
    nodes[node] = (name, "Type: function definition" in block,
                   re.findall(r"\S+/\d+", calls.group(1)) if calls else [])
root = next(n for n, (name, _, _) in nodes.items() if name == "sndmin_mix")
allowed = {"memcpy", "memset", "__atomic_load_4", "__atomic_store_4"}
visited = set()
pending = [root]
while pending:
    node = pending.pop()
    if node in visited:
        continue
    visited.add(node)
    name, defined, calls = nodes[node]
    if not defined:
        assert name in allowed, f"Forbidden/unaudited callback external: {name}"
    pending.extend(calls)
print(f"Callback graph: {len(visited)} reachable functions; only memcpy/memset and lock-free atomics leave the translation unit.")
