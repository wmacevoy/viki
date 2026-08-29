# assistant/ — not viki

> *"isn't this something that a digital assistant agent manages — not viki
> itself but an agent using viki to stay on top of things?"*
> — Warren, 2026-08-24

Yes. And `viki brief` had already been written as a viki subcommand before he
asked, which is the second time the same line got crossed in one day — the first
was `d2l.coloradomesa.edu` in viki's manifest (see `edge/chrome/sites/`).

## The line, and how to tell which side you are on

**viki answers questions. It does not decide anything.**

| viki (a query) | the assistant (a decision) |
|---|---|
| `viki promises` — live tasks, ordered by due | *which* of those is worth waking someone about |
| `viki coverage` — sources and last-seen times | what counts as **stale**, and whether to say so |
| `viki why` — the supersession chain | whether the chain means the work is finished |
| `viki ask` / `grep` / `muse` — retrieval | what the results mean |

The test that catches it: **can viki compute this without an opinion?**
`coverage` prints last-seen times and no thresholds — deliberately. The moment
something needs a number like "12 hours" or a word like "at risk", it is policy,
and policy belongs here.

The sharpest tell for the brief specifically: **viki cannot ask questions.** It
has no LLM and never will (D-10 in spirit). A brief that asks *"this has no due
date — is it a promise or a note?"* is doing the one thing viki structurally
cannot, which is proof it was never a viki feature.

## What is here

- **`brief.sh`** — the morning brief. Composes `viki promises`, `viki coverage`
  and `viki structure --pending` into what §2.4 and §2.5 describe: what is at
  risk, what I can and cannot see, which channels need signing in, and what I am
  unsure about.

- **`schedule-brief.sh`** — makes the brief ARRIVE rather than wait to be
  asked (§2.4, "the clock, not the query"). `install [HH:MM]` writes a launchd
  agent on macOS; anywhere else it PRINTS the cron line rather than editing
  someone's crontab for them — installing a recurring job on a person's machine
  is not a thing to do silently. Output is `~/.viki/brief/YYYY-MM-DD.txt` plus a
  `latest.txt` symlink.
  **A failed run writes the failure into that file.** A scheduled job breaks on
  a morning nobody is watching, and this file's own rule is that silence is
  indistinguishable from a broken job — so an empty brief must never read as a
  quiet day.
  It writes a file rather than sending mail: sending would make the assistant a
  SENDER, and `observe` is the only authority rung that needs no policy decided
  first (§2.2c).

It is a shell script on purpose. It is the *simplest possible* assistant, it is
readable in one sitting, and it establishes the interface an actual agent should
use. A real agent — nanoclaw, openclaw, an MCP client — replaces this file and
touches nothing in `src/`. If replacing it requires changing viki, the line has
moved and that is worth noticing.

## Why the brief and the login round are one ritual

MFA friction is why things get missed (`VIKIVERSE_V1` §5c). So the brief is not
a report you read *after* signing in — it is the thing that tells you **which**
sign-ins are worth the five minutes, and then tells you what arrived. One
interaction, not two features.

That is why `SIGN IN:` is part of the brief rather than a separate command, and
why staleness is computed here: the threshold is a judgment about Warren's day,
not a fact about the corpus.
