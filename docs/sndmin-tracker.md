# sndmin tracker text

An ASCII song has at most 32 patterns, 64 rows per pattern, 8 channels and
128 order entries. Blank lines and comments beginning with `#` are ignored.
`02_song.snd` is a complete 47-line example (38 seconds plus a two-second tail).

```
tempo 120 0.12
rows 8
patch 1 bass
patch 2 gated
arp 0 12 7
order 0 0
pattern 0
C2:1:75:0 ---
--- C3:2:65:0
```

`tempo BPM SWING` uses sixteenth-note rows. Swing, in `[0, 0.5)`, delays odd
rows and shortens them to preserve the duration of each pair. `rows N` is
the length of every pattern; omitted trailing rows are silent. Each
`pattern ID` starts a new pattern; each following line advances one row.
`order` lists patterns in playback order. Unreferenced patterns are allowed.

Every column is a tracker channel. A cell is `NOTE:PATCH:VOLUME:EFFECT` or
`---` for no trigger. Notes use `C0` through `B8`, with `#` for sharps;
volume is 0–100. Effect is an integer semitone transpose from -24 to +24.
`arp CHANNEL A B` replaces each trigger on that zero-based channel with
three equally spaced triggers at offsets 0, A and B semitones. Zero offsets
disable it. Envelopes and releases can overlap; silence does not stop a note.

`patch ID PRESET` chooses `bass`, `pad`, `lead`, `kick`, `snare`, `gated`,
`hat` or `tom`. IDs are 1–31. Patches are built before playback, then
immutable. `sndmin_make_patch` exposes the full synth controls for game code.
Unknown directives, fields, presets, missing patterns and invalid ranges
reject the file. Songs play their finite order list; repeat the play call
to arrange longer playback. The parser emits commands, never runs in audio.
