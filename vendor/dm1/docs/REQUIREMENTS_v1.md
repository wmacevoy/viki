# Requirements — v1

What must be observably true of this guide, and what it is not. Reconstructed from the repository on
2026-11-03 by Claude Opus 5.5 ([SE1/W12](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w12)),
so **all of it is *(inferred)* until the client confirms it**, except where a line says it is the owner's. A change in meaning is a new version.

Client: Warren MacEvoy · Proxy for the users: the field reports in GAPS.md, and the fall 2026 course projects
· Date sent: not yet sent · *Tell us what is wrong with it.*

## Glossary

GLOSSARY.md, at the root, is the glossary; this repository adds no terms to it.

## Mission

- **MISSION-1** *(since v1)* *(judged)* *(inferred)* People and agents doing data work produce claims
  worth acting on, by following a written guide that extends SE1 and does not repeat it. Judge: Warren
  MacEvoy, on the guide as it stands, at each release he signs. Veto: a field report where following a
  principle made the analysis worse and the guide does not change.

## Shape

Held by: Warren MacEvoy *(inferred)*

- **SHAPE-1** *(since v1)* *(judged)* *(inferred)* Every principle sits in one of SE1's ten chapters under
  SE1's letter, and says on the line under its heading whether it is new or which SE1 principles it
  reframes. Judge: the holder, at each release.
- **SHAPE-2** *(since v1)* *(judged)* *(inferred)* What SE1 already says is cited at a pinned release, not
  restated. Judge: the holder, at each release.
- **Says no to:** a principle that applies to data unchanged from SE1 (it stays off the map, per the
  README); a tool with a dependency outside the standard library.
- **Exceptions:** none yet.

## The pain

*(inferred)* Wrong analysis returns a plausible number and nobody finds out. Students and agents following
SE1 alone have no rule for leakage, the holdout, or the count of looks.

## People this touches

*(inferred)* Students in the course, teams adopting the guide, agents working from its agreement, and the
people whose rows those projects mine, who are never in the room.

## Requirements

- **CHK-1** *(since v1)* *(inferred)* `tools/check.py` exits nonzero when a link, anchor, DM1 or SE1
  citation, basis line, README index or map, or release label is wrong.
- **PIN-1** *(since v1)* *(inferred)* Every SE1 link, llms.txt, and CI name one SE1 release, and each bump
  is recorded in CHANGELOG by SE1 ID as reframed, declined, or confirmed.
- **DOC-1** *(since v1)* *(inferred)* `./dm --doctor` reports each mechanical thing the guide asks for in a
  data project as pass, warn, fail, or *none yet*, standard library only, on Python 3.9 or later, on
  Linux, macOS, and Windows.
- **PORT-1** *(since v1)* Developing this guide works on Windows, macOS, and Linux: the full check in
  `CONTRIBUTING.md`, `dm`, and their tests run natively on each with Python 3.9 or later and git, and CI shows
  it on all three. The owner's requirement, 2026-11-03. It binds work on this repository, not a project
  that vendors it.
- **AGR-1** *(since v1)* *(inferred)* `AGENTS.md` extends SE1's agreement at a named release, and each rule
  cites its principle by versioned ID.

## Non-goals

- *(inferred)* Replacing SE1, or teaching software engineering.
- *(inferred)* A catalogue of techniques or a statistics textbook; READING.md points at those.
- *(inferred)* Holding data, models, or results; the data rows of the doctor stay *none yet* here.

## Scope decisions

- 2026-11-03, the owner's decision: Python 3.9 or later and git are the only dependencies of the check and the
  tools. A clone of SE1 is a source to check against, not a dependency to install.
- 2026-11-03 *(inferred)*: SE1's copy lives at `docs/SE1-AGENTS.md` here, because the root `AGENTS.md` is
  what this guide publishes. Reversible by renaming, at the price of the published URLs.
- 2026-11-03: the SE1 files are vendored as ADOPT.md says (the agreement and `se`), not the whole SE1
  repository; the checker still reads a clone of SE1 for its IDs.
