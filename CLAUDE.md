# CLAUDE.md — The Carmack Regime

This file sets out how to work in this codebase. It is distilled from John Carmack's own
writing: his id Software `.plan` files (1996–2010), the 2007 "inlining code" email, the two
2011–2012 #AltDevBlogADay essays on static code analysis and functional programming in C++,
his QuakeCon keynotes, and his more recent public comments on tooling and AI-assisted
development. Sources are listed at the end. Where his views changed over time, the later
position wins when the sources address the same tradeoff.

This is vkmin's policy inspired by those sources, not a verbatim statement of
Carmack's rules or his endorsement of this project. In particular, our ban on
silencing warnings is a deliberately stricter project rule: his
[static-analysis essay](https://www.gamedeveloper.com/programming/in-depth-static-code-analysis)
describes prioritising consequential findings and configuring warning sets.
Apply the judgement and cost section to design choices; source attribution must
distinguish his documented experience from our own conventions.

The point of adopting one person's methodology is not reverence. It is that this particular
set of habits was arrived at empirically, by someone who tracked his own bug rates, changed
his mind when the data embarrassed him, and shipped software that had to run correctly sixty
times a second on hardware he did not control.

---

## 1. Start from the assumption that the code is wrong

The foundational move is humility about defect rates. Carmack's position, stated plainly in
the static analysis essay, is that you must first fully admit that the code you write is
riddled with errors, and that without swallowing that, every suggestion for improvement reads
as an insult. In a large enough codebase, any error that is syntactically legal is almost
certainly present somewhere. Code quality at scale behaves statistically, like a material
property, rather than as a matter of individual care.

So: **actively want criticism of the code.** When you review your own output, do not look for
confirmation that it works. Look for the specific ways it is probably already broken. Best
intentions do not matter — if something can be entered incorrectly, eventually it will be.

Never write "this should work" in a summary. Either it has been exercised and you can say how,
or it has not and you should say that instead.

## 2. Unexpected state is the enemy

Carmack's single most transferable idea is that most bugs come from the execution state not
being what the programmer assumed it was. Everything below follows from that.

Programming in a functional style makes the state your code sees explicit, which makes it
possible to reason about. A pure function reads only its parameters, returns computed values,
touches no global state, keeps no internal state, does no I/O, and mutates none of its inputs.
Passing in some `allMyGlobals` handle defeats the whole purpose.

Purity is a gradient, not a gate. Moving a function towards purity improves it even if it
never gets all the way there, and the step from tangled-state to mostly-pure is worth far more
than the step from almost-pure to perfectly-pure. Fixing the genuinely toxic systems — the
ones you handle with tongs — matters more than polishing the maths helpers that were nearly
clean already.

In practice, in this codebase:

- Gather the inputs, hand them to a pure function, then act on the result. Structure new work
  this way from the beginning rather than refactoring towards it later.
- Prefer returning new values over mutating in place. Where an object has a self-mutating
  method, consider whether it also wants a `const` variant that returns a copy.
- Put `const` (or the language's equivalent) on nearly everything that is not an iterator. If
  making something `const` proves frustrating, that frustration is data: you have found a place
  where state can change unexpectedly, which is a place bugs breed.
- Be irritated by long parameter lists. A dozen parameters usually means the decomposition is
  wrong, not that purity is impractical.
- Refactoring towards purity means more parameter passing and more copying. That cost is real
  and is usually worth paying. The exception is the genuinely hot path — do not write a pure
  `DrawTriangle` that returns a whole new framebuffer.

## 3. The shape of code: awareness over concealment

This is the part of Carmack that runs hardest against conventional advice, so apply it with
judgement rather than as dogma. The argument is that fragmenting a sequential operation across
many small functions hides the control flow, and hidden control flow is where latency,
ordering bugs, and skipped state updates live.

The rules he actually settled on, lightly restated:

1. If a function is called from exactly one place, consider inlining it.
2. If it is called from several places, see whether the work can be arranged to happen once, in
   one place, possibly flag-driven — then inline that.
3. If there are several variants of a function, prefer one function with more (defaulted)
   parameters.
4. If the work is nearly pure, finish the job and make it pure.
5. Use `const` on parameters and functions when a function genuinely must live in several places.
6. Minimise control-flow complexity and the amount of code sitting under conditionals. Favour
   consistent execution paths over cleverly skipping unnecessary work.

Two corollaries worth holding onto. First, a function that does not exist cannot be called
from somewhere you did not anticipate — much of the value of inlining is denying future
callers the chance to invoke half of an operation and skip the state update the other half
performed. One check for `health <= 0 && !killed` in the think loop will produce fewer bugs
than `KillPlayer()` called from twenty sites. Second, code duplication is still worse than
almost any second-order problem a shared function causes, so never duplicate merely to avoid a
call.

The "execute then inhibit" pattern belongs here too: doing the work unconditionally and then
discarding or suppressing the result is often more reliable than conditionally performing it,
because the conditional path tends to skip state updates that something else depended on. It
costs average-case time and buys worst-case predictability. Carmack later noted the caveat
himself: this is right for reliability, and wrong when power and thermal budgets dominate.

Finally, a small empirical finding he was surprised by: copy-paste-modify on short vector or
index expressions produced a disproportionate share of his own bugs. Write the explicit loop
and trust the compiler.

## 4. Automation is not optional

Exhortations to write better code do not work. Code reviews, pair programming and good
intentions do not scale against a team under time pressure. What works is anything that runs
every single time without a human deciding whether to bother.

Therefore, in this repository:

- The build runs at maximum practical warning level with warnings treated as errors. If that
  setting is ever quietly disabled, errors accumulate immediately, so treat any regression here
  as urgent.
- Static analysis runs as part of the normal build, not as an occasional cleanup exercise. The
  value is not only the bugs caught but the fact that the author sees them at the moment of
  writing rather than having someone else silently fix them later.
- Different analysers find different things. Code that is clean under one tool routinely turns
  up real defects under a second. Do not treat one clean pass as proof.
- Cooperate with the tools rather than fighting them. Favour indexing over pointer arithmetic,
  keep the call graph within a single source file where you can, and use explicit annotations.
  The governing principle: anything that is not crystal clear to an analyser is probably not
  clear to the next human either.
- Do not silence a warning to make it go away. Either the code is wrong, or the code is unclear
  enough to fool a tool, and both are worth fixing.

Carmack's conclusion after all this was blunt — that not using static analysis is
"irresponsible". Treat it that way.

Two related notes. Null pointers were the largest single defect category in his C++ codebase,
because one value serves as both a flag and an address; prefer references or non-nullable types
and do the check once at the boundary. And format-string mismatches were second, which is an
argument for annotating variadic functions so the compiler type-checks them.

## 5. The testing regime

Carmack is honest that he was historically undisciplined about writing tests, and that the
reason was real: code entangled with enough systems needs elaborate harnesses, so it was always
easy to convince himself it was not worth it. What changed his behaviour was not resolve, it was
restructuring. Pure functions are trivial to test — build inputs, check outputs, textbook stuff.
So the testing strategy and the design strategy are the same strategy.

**Split out the finicky bit and test it.** Whenever you meet a piece of code that looks fiddly,
lift it into a pure function and write tests against it. Carmack's own report is that he found
something wrong disturbingly often when he did this — which he took as evidence that he was not
casting a wide enough net, not as evidence that he was done.

**Build parallel implementations rather than mutating in place.** When replacing an existing
approach, stand the new one up alongside the old and make switching between them as cheap as
flipping a variable at runtime. Rolling back and rebuilding to compare is painful enough that
nobody does it honestly, and once you have invested in the rewrite there is every incentive to
avoid a fair comparison. Two flavours are useful: a small reference implementation that is kept
around permanently, and an experimental implementation where one version is expected to win
within a couple of weeks. On the experimental copy, break the usual rules freely — copy, paste
and rename is a perfectly good start, precisely because the original stays untouched and
working. Resist the temptation to add an option flag to the existing code instead; that path
tends to compromise both versions.

**Make the system deterministic and replayable.** This is the most underrated item on the list.
By funnelling every input through a single point and journalling it — including time — a
real-time program becomes a batch process, and an entire class of problems evaporates. You get
reproducible transient bugs (a bug that reproduces is a bug that can be fixed), the ability to
break on a specific frame, accurate profiling free of the distortion instrumentation causes,
and the ability to run heavyweight checkers at a fraction of normal speed while covering exactly
the same code paths. Where this codebase has non-determinism, treat removing it as high-value
work rather than an indulgence.

**Step through the code you never look at.** Periodically take a major entry point and step
into every function, walking the complete coverage of one cycle. It is a grim exercise and it
reliably surfaces code with performance and stability implications that everyone has been
blindly skipping past in the debugger for months. Related: use the debugger while writing code,
not only when something has already broken.

**Litter asserts.** When tracking an unfamiliar failure, saturating the region with assertions
is a first move, not a last resort.

**Log and diff against a known-good path.** When two implementations diverge non-deterministically,
produce large logs from both, normalise them, and find the first point of divergence.

## 6. Judgement, cost and scope

Quality is not everything, and saying so is not a moral failing. Value is what you are producing;
quality is one input alongside cost, features and timing. Plenty of successful, well-regarded
software shipped full of bugs, and running a game studio on Space Shuttle process would be
idiotic. The NASA-grade approach delivers genuinely low defect rates at genuinely low
productivity, and the usual failure is not choosing wrong once but building something fast and
loose and then living with it for years.

So spend the quality budget where it goes to customers. Aggressively push code that is only used
internally out of the shipping path. Shrink the important code — the research Carmack cites
suggests code size alone predicts defects about as well as any fancier metric, so size reduction
is a reliability strategy.

Assume the code will outlive the current task. His standing message to his own team was that
anything not extremely feature-specific may well still exist a decade later with hundreds of
people reading it, and that this justifies fairly severe restrictions on what gets through. Write
for that reader.

On features: the cost of adding one is not merely the time to code it. It is also the obstacle it
places in front of everything that comes after. Fragile, over-featured codebases make dead-simple
future ideas take longer and longer. Pick features that do not fight each other.

And be honest that most of this is not science. Algorithms and optimisation are measurable;
almost everything else about software development is a social problem, either between programmers
or between you and your future self. That is precisely why the automatable parts should be
automated and the rest handled with explicit, shared convention.

## 7. How Claude should work here specifically

Carmack's framing of AI assistance is that the useful role is a diligent team member rather than
"your coding genie" — a tireless collaborator continuously going over the codebase looking for
places to improve it, not a generator of bulk code that gets accepted unread. He has also noted
that the same coding behaviours that make a codebase legible to humans tend to make it legible to
models, and that anything which is not a plain collection of readable text files causes trouble.

Concretely:

- Volume is not value. Prefer the smallest change that does the job, and say what you did not do.
- Do not present generated code as finished. State what has been run, what has not, and what you
  are unsure about. Flag the parts most likely to be wrong.
- When you touch existing code, check what preconditions the surrounding code was relying on. A
  very common defect pattern is perfectly good code that checked for null, later modified so the
  pointer is used unchecked — a failure to communicate assumptions, not a failure of care.
- When you propose a substantial change to a working system, propose it as a parallel path with a
  switch, not as an in-place replacement, unless the change is genuinely small.
- If asked to optimise, first ask whether the code is on a path that matters. Highly variable
  frame or response times are worse than uniformly slightly slower ones.
- Housekeeping is a legitimate and valuable use of your time: tightening `const`, removing dead
  paths, making an analyser-hostile construct explicit, shrinking a function's reach.

## 8. Toolchain on this Windows machine

The compilers live in MSYS2 at `C:\msys64`. Nothing is on the default PATH,
and `w64devkit/` is not installed, so do not search for `gcc` or `make`
elsewhere or reach for `tools/test-windows.ps1`, which requires w64devkit.

- Build and test through `tools/build-msys2.ps1` (`-Toolchain ucrt64` is the
  verified default; `clang64` is also present). It sets PATH to
  `C:/msys64/<toolchain>/bin` and `C:/msys64/usr/bin`, runs
  `mingw32-make` with `BUILD=build/omega-<suffix>`, and reads `VULKAN_SDK`.
  Pass one make target per invocation, e.g. `-Target all`, `-Target test`,
  `-Target omega`, `-Headless` for the null-audio, no-GLFW build.
- `make test` needs three things the script does not supply. Prepend
  `build/deps/cppcheck-src` to PATH for `cppcheck`; MSYS2 here has no
  `diffutils`, so prepend `C:\Program Files\Git\usr\bin` for `cmp`; and set
  `VK_DRIVER_FILES` to the absolute path of `build/deps/lavapipe.json`. The
  relative-path manifest under `build/deps/mesa/x64` fails to load through
  the MSYS2 shell and the harness then reports "test driver is not lavapipe".
- Under Windows PowerShell 5.1 a native program that writes to stderr while
  `$ErrorActionPreference = 'Stop'` looks like a failure. Check the exit code
  or the output file before concluding a run failed; `ex_21_omega --headless`
  prints its device line to stderr and succeeds.
- GLFW 3.5.1 binaries are cached under `build/deps/glfw`; the build script
  downloads them only when missing.
- Claude Code's Bash tool exports `NoDefaultCurrentDirectoryInExePath=1`.
  Under it, Windows will not launch `build/x/foo.exe` without a `./` prefix,
  so python test drivers that spawn relative executables
  (`tools/test_sndmin_shared.py`, `tools/test_unison.py`) fail from that
  shell with WinError 2 while the executables exist. Run those from the
  PowerShell tool, or `env -u NoDefaultCurrentDirectoryInExePath` first.

---

## Sources

- `.plan` files, 1996–2010 — archived at `github.com/ESWAT/john-carmack-plan-archive`. The
  journalling and deterministic replay material is in the 1998 file; the note on feature cost as
  future obstacle is 1997.
- "Inlining code", email to the id Software team, 13 March 2007, with a 2014 addendum —
  `number-none.com/blow/john_carmack_on_inlined_code.html`.
- "Static Code Analysis", #AltDevBlogADay, December 2011 — reprinted at Game Developer,
  `gamedeveloper.com/programming/in-depth-static-code-analysis`.
- "Functional Programming in C++", #AltDevBlogADay, April 2012 — mirrored at
  `sevangelatos.com/john-carmack-on/`.
- "Parallel Implementations", #AltDevBlogADay — mirrored at
  `sevangelatos.com/john-carmack-on-parallel-implementations/`.
- QuakeCon 2011 and 2012 keynotes. The 2012 remarks on defect rates, daily code review and
  software engineering as a social science are transcribed by Amy J. Ko.
- Public comments on X, 2024–2025, on AI-assisted development and codebase legibility.
