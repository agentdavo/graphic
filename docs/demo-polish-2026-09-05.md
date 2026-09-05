# Demo polish and replay coverage, 5 September 2026

The five v0.4 examples gained shared controls, restart and gameplay checkpoints.
The public renderer API did not need a genre abstraction to express them.

| Example | Change | Recorded checkpoint |
| --- | --- | --- |
| Shooter | Sliding gate, two targets taking three hits each, muzzle lighting, smoke/impact puffs, actual encounter progress | Gate open, six shots, six hits, both targets cleared at frame 150 |
| RTS | Correct march position, previous/current interpolation, rally marker and sparse dust | One order, selected units and simulation tick at frame 300 |
| Top-down | One floor mesh from the heightfield, darker courtyard, warm moving lantern, nearby props and inspection feedback | At least one successful ID-buffer inspection at frame 300 |
| Platformer | Idle/jump pose tracks, interpolated camera and platforms, landing dust, warm colour grading | Jumps and landings at frame 300 |
| Anime | Three animation states, floating motes, grading shared with platformer, named states and controls | Idle, run and jump all visited by frame 390 |

All five use R to restart and F1 to toggle the controls card. Existing
`r_debug=1` through `r_debug=6` expose renderer diagnostics. `--state-trace FILE`
writes named checkpoints and a canonical scalar hash; it does not serialize
the entire game state. Input demos remain the existing `.vkd` format.

The RTS had been displaying the destination plus an orbit as the unit's
position, despite separately advancing its current location. Rendering now
uses the current location and interpolates consecutive simulation positions.
The fixed-step helper retains input edges across frames without a simulation
tick and consumes each edge once during catch-up.

The broadened tests exposed a renderer-journal crash. World particles and
screen UI share a quad allocation. The screen draw previously passed
`base + world_count * sizeof(Quad)` as its address. The journal recognizes
issued allocation bases, so it did not relocate that derived address. The
screen draw now passes the base and a separate starting index. The vertex
shader adds the index. This preserves the original pixels and works with the
existing journal format. All five games now record and replay full renderer
journals across Vulkan paths, rather than relying on the anime-only case.

The build now emits and includes compiler dependency files. An explicit `-MT`
keeps their target names consistent with Makefile targets on Windows as well
as Unix. Header changes therefore invalidate standalone tools and examples
through their transitive includes.

## Verification scope

A concurrent task, **Build vkmin v0.5 outdoor demo**, was changing core files
in the same checkout. Verification was moved to `build/polish-snapshot`, a
frozen source copy with its own build and test outputs. That copy includes the
outdoor changes present at capture time, but its v0.4 test results do not claim
v0.5 acceptance. The quad fix was applied both there and to the shared checkout.

The five demo goldens were deliberately replaced for the changed scenes,
after visual inspection and cross-path comparisons. The old files are retained
locally in `build/plan-audit/pre-polish-goldens`; git retains their prior versions.
The anime golden now captures frame 390, covering the jump state. Corridor
goldens were not replaced. The executed results are recorded in
[the verification report](verification-2026-09-05.md).

These are playable rendering prototypes. The supplied character is still
CesiumMan; idle/jump are authored pose offsets on that rig, not three imported
clips. The platformer recording includes a fall and restart at the spawn point;
it is not a completed obstacle-course playthrough. There is no enemy AI combat,
production UI toolkit or external developer usability study. Windowed controls
and visual quality at hardware frame rates remain unverified in this headless run.

Read [Using vkmin](using-vkmin.md) for ownership, numeric resource values and
the distinction between input demos and renderer journals.
