# CONTINUITY.md — the seam experiment

**Status: live in this project only, since 2026-08-30. Reverting takes one
edit and breaks nothing.**

## What problem

A Claude Code session compacts when it fills up: the context is summarised and
the summary starts the next window. Measured in this repo's own session
`ff137b62` — **five boundaries, 4,521,748 tokens dropped**, ~98% discarded each
time. The trace working that session believed there had been one.

Three things go wrong at that seam, all measured rather than supposed:

1. **The summary is stored as `type: "user"` with `model: null`.** A trace's own
   conclusions arrive at its successor wearing the human's voice. That is the
   mechanism behind an agent saying "I decided X" about a thing you decided.
2. **The template has no slot for uncertainty or for corrections.** It records
   intent, files, errors-and-fixes, pending tasks. So a claim a trace later
   found FALSE does not cross the boundary as false — it crosses as furniture.
3. **The `CLAUDE.md` in a session's context is a snapshot from load time.** Edit
   the file mid-session and your own context still shows the old one. A fresh
   agent hit exactly this and would have gone to work in the dead-end tree.

## What the experiment does

`assistant/kin-orient.sh`, wired to `SessionStart` and `PreCompact`. `PreCompact`
stdout is documented as "critical information to preserve", so the block crosses
the boundary verbatim instead of being summarised.

It prints, in about 2 KB:

- how many compaction boundaries are already behind you, and how many tokens went
- the **live** claims from `~/.viki/kin.diary`, newest first, with `[k0..k4]`
  status and who made them — superseded claims are excluded, so a correction
  arrives and the thing it corrected does not
- when `CLAUDE.md` last changed, and a warning that your copy may be behind it

It is **read-only**. A hook that writes during compaction would corrupt state at
the worst possible moment. It touches nothing.

## How to revert

The wiring lives in **one untracked file**, `.claude/settings.json` (`.claude/`
is gitignored, so this is not in version control):

```sh
# full revert — removes the hooks, keeps everything else in the file
python3 - <<'EOF'
import json; p=".claude/settings.json"
s=json.load(open(p)); s.pop("hooks", None); json.dump(s, open(p,"w"), indent=2)
EOF
```

Or just delete `.claude/settings.json` entirely — nothing else in this project
needs it. Or delete `assistant/kin-orient.sh`; the hook then fails, prints
nothing, and Claude Code carries on.

**Nothing else changes.** No viki behaviour, no build, no test, no data. The
script only reads.

## How to tell it is failing

- **It is noise.** If the block never changes what an agent does, it is costing
  context for nothing. Kill it.
- **It is too big.** 2 KB now. If the kin diary grows and the block does too, it
  is eating the context it exists to protect. `KIN_MAX_CLAIMS` caps the claim
  count; lower it.
- **It reports a failure.** It always exits 0 on purpose. A nonzero hook exit
  is an error to Claude Code, and a script that only *reports* must not use the
  exit status as a second channel — `assistant/brief.sh` did exactly that and
  stamped "THE BRIEF FAILED" onto correct output for weeks. Its failure modes
  are printed as text instead.
- **It is silently blind.** It must never print nothing. A missing diary or
  missing binary each say so explicitly, because *"no corrections"* and *"I could
  not look"* rendering the same is the exact failure this repo keeps hitting.
  If you ever see the CORRECTIONS heading with nothing under it, that is a bug.

## What it cannot do

It carries **corrections, not uncertainties.** Seams, live claims and doc
staleness are machine-derivable. What a trace was unsure about is not — that has
to be deliberately written, with `viki claim --status k1`, before the window
closes. The hook is the mechanical half of a handoff; the deliberate half is
still a choice someone has to make while they still have the context to make it.

## Environment

```sh
VIKI_BIN         default ~/projects/viki/core/build/viki
VIKI_KIN_DIARY   default ~/.viki/kin.diary
VIKI_KIN_KEY     default ~/.viki/kin.key
KIN_MAX_CLAIMS   default 6
```

Run it by hand any time:

```sh
CLAUDE_PROJECT_DIR=$PWD sh assistant/kin-orient.sh
```
