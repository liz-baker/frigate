---
name: porting-fidelity
description: Audit newly-ported game-logic code in src/ against its decomp source in external/007 for unflagged drift from a 1:1 port, per CLAUDE.md's "Porting decomp game code" policy. Run this before considering any porting PR/branch done, whenever src/ changes touch code with a decomp counterpart, or when the user asks to check port fidelity / drift / "did we stray from the decomp".
---

# Porting fidelity audit

This repo's whole point is a *functionally 1:1* port of the GoldenEye 007
decomp (`external/007`) onto libultraship. The failure mode this skill
exists to catch: a change that looks like it works (compiles, runs, no
crash) but silently drops or alters real decomp behavior — a light setup
call, a timing constant, a sound cue, an order-of-operations detail — with
nothing in the diff saying so. CLAUDE.md is explicit that an unflagged gap
is a bug, not a shortcut; a flagged one (`// TODO(<issue-url>): ...`) is a
legitimate, deliberate deferral. This skill's job is to tell those two
apart, mechanically, every time.

## Scope

Default scope is the current diff (`git diff` against the branch's merge
base, or working-tree changes if uncommitted). If the user names a PR
number, branch, or commit range, use that instead. Only look at files
under `src/` that touch ported game logic — model/animation, level/bg,
game/menu, gfx math, and similar. Skip pure build/CI/doc/tooling changes;
they're out of scope for this check.

## Setup

`external/007` is a submodule and may not be checked out in a fresh
session:

```
git submodule status external/007
git submodule update --init external/007   # if it shows a leading '-'
```

## Procedure

1. **List touched functions.** From the diff, identify every changed or
   added function in scope that has (or should have) a decomp
   counterpart — not just new files, but modified existing ones too.

2. **Locate the decomp counterpart** for each one. The port's own function
   names and comments usually name or mirror the original decomp symbol
   (e.g. a comment citing `bgDetermineVisibleRooms`, or a name like
   `BgVisibility::...` tracking `sub_GAME_7F0B7F84`). Search
   `external/007` for it. If a function genuinely has no decomp
   counterpart (new port-only infrastructure, glue code, test scaffolding),
   note that and move on — it's not in scope for a fidelity diff, though it
   *is* in scope for the reuse-before-reimplementing check below.

3. **Diff logic, not just output.** For each pair, compare:
   - Control flow and branch conditions.
   - Constants — magic numbers, angles, timers, distances, buffer sizes.
     Pull the actual literal from the decomp side; don't eyeball it as
     "looks about right."
   - Order of operations, especially anywhere ordering has a visible side
     effect (state set before it's read elsewhere, etc).
   - Side effects: lighting/light setup, camera/viewport setup, sound
     cues, RNG seeding/consumption order, anything that mutates shared
     state.

4. **Classify every difference found:**
   - **Faithful** — matches, or is a no-op simplification with no
     behavioral effect (e.g. dropping a write-only debug counter that
     nothing ever reads). Not a finding.
   - **Flagged deferral** — the port comment says
     `// TODO(<issue-url>): ...` citing a real, resolvable issue and
     describing what's being skipped. Not a finding, but verify the URL
     actually names an issue (not a bare "TODO: fix this later" or a
     citation of this file itself — CLAUDE.md forbids "per CLAUDE.md"-style
     justifications, and forbids hand-waves like "isn't confirmed yet"
     with no tracked issue).
   - **Unflagged gap** — behavior differs and nothing in the diff says why.
     **Finding.**
   - **Mislabeled deferral** — a TODO exists but doesn't cite a real
     issue, or the comment quotes/paraphrases decomp prose instead of
     describing the gap in fresh wording (licensing concern — see
     CLAUDE.md's Licensing section). **Finding.**

5. **Secondary check — reuse before reimplementing.** While in each file,
   note anywhere the change re-derives platform-layer behavior (OS shim,
   config, input, crash handling) that `libultraship-wiiu` already
   provides generically, instead of calling into it. This is a
   lower-severity finding than a behavioral drift but still flag it, per
   CLAUDE.md's design principle section.

6. **Do not fix anything in this pass.** This is a read-only audit. If the
   user wants fixes applied, say what you'd do and let them confirm, same
   as any other review — don't silently patch source to close a finding.

## Reporting

Use the `ReportFindings` tool if available in this session; otherwise
report the same structure as plain text. For each finding give:

- `file`/`line` in the port.
- The corresponding decomp `file:line` (or symbol name if line drifts).
- A one-sentence description of the divergence.
- Whether it's an unflagged gap or a mislabeled deferral (the two finding
  categories above), and, for a mislabeled deferral, what's missing (a
  real issue reference, or non-derivative wording).

If a genuine, careful pass turns up nothing, say so plainly. Do not
manufacture findings to have something to report — a clean pass is a
valid and useful result.

## Notes

- This is a judgment-call audit (matching decomp *behavior*, not just
  text), not a mechanical diff — budget time to actually read both sides
  of each pair rather than pattern-matching on function names.
- For a large diff touching many ported functions, consider delegating
  the read-both-sides comparison work to a background research agent (see
  the Agent tool) the way a full-repo audit would, rather than trying to
  hold every file pair in context at once.
- Never copy decomp prose or comments into port-side comments or this
  skill's own output verbatim beyond a bare fact (e.g. a constant value) —
  `external/007` is unlicensed upstream.
