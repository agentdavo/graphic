#!/bin/sh
# Counts the lines of src/vkmin.c that belong to one API path only, using the
# markers around each seam implementation. The report promised two numbers; a
# script produces them so they cannot drift from the code.
f=${1:-src/vkmin.c}
total=$(wc -l < "$f")
legacy=$(awk '/--- legacy-only/{on=1} on{n++} /--- end legacy-only/{on=0} END{print n+0}' "$f")
modern=$(awk '/--- modern-only/{on=1} on{n++} /--- end modern-only/{on=0} END{print n+0}' "$f")
echo "$f: $total lines total, $legacy legacy-only, $modern modern-only"
