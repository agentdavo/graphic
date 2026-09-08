# Loaded by GDB, not a standalone Python program. Trace every entered function
# in vkmin.c and its private headers during one complete fixture cycle.
import gdb
import json
import re
from pathlib import Path
seen = {}
class Entry(gdb.Breakpoint):
    def stop(self):
        name = gdb.newest_frame().name()
        seen[name] = seen.get(name, 0) + 1
        return False
owned = False
for line in gdb.execute("info functions", to_string=True).splitlines():
    if line.startswith("File "):
        owned = "/src/vkmin" in line or "src\\vkmin" in line
    if owned:
        match = re.search(r"\b([A-Za-z_]\w*)\([^;]*\);$", line)
        if match:
            Entry(match.group(1), internal=True)
def finished(event):
    required = {"vkmin_init", "vkmin_frame_begin", "vkmin_pass_begin", "vkmin_pass_end", "vkmin_frame_end", "vkmin_shutdown"}
    Path("build/boundaries/debug-functions.json").write_text(json.dumps(seen, indent=2))
    if not required.issubset(seen):
        raise gdb.GdbError("Missing cycle entries: " + str(required-set(seen)))
    print("Complete cycle: " + str(len(seen)) + " distinct vkmin functions entered")
gdb.events.exited.connect(finished)
