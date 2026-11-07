# Requirements — v2

What must be observably
true of this guide, and what it is not. Reconstructed from the repository on 2026-11-03 by Claude Opus 5.5
([SE1/W12](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w12)), and **confirmed by the client, all of it, on 2026-11-04**, after a review in session. A change in meaning is a new
version. This one changes the mission, the fundamental shape, the pain, and CHK-1, and adds a glossary of this
repository's own terms, ID-1, ADOPT-1, CONF-1, and the assumptions; everything marked *(since v1)* is unchanged from
[v1](REQUIREMENTS_v1.md).

Client: Warren MacEvoy · Proxy for the users: the field reports in GAPS.md, and the fall 2026 course projects
· Date sent: 2026-11-04, confirmed the same day · *Tell us what is wrong with it.*

## Glossary

GLOSSARY.md, at the root, is the glossary for data work. These are the terms of this repository itself.

| Term | Meaning |
| ---- | ------- |
| Principle | One idea with an ID (`F5`), in one chapter, cited as `DM1/F5`. |
| Reframe | What a principle does when it says what an SE1 principle means for data, and links to it. One that does not is new. |
| Release | A git tag `vX.Y` (minor) or `vX.Y.Z` (patch), with a dated section in the CHANGELOG. |
| Copy | A file adopters copy that names its release: AGENTS.md and the templates. They also copy `dm`, `dm.ps1`, and `dm.py`, which a patch release may change. |

## Mission

- **MISSION-1** *(since v2)* *(judged)* People and agents doing data work produce claims worth acting on.
  Judge: Warren MacEvoy, on the guide as it stands, at each minor release. Veto: a field report where following a
  principle made the analysis worse, unanswered at that release.

## Fundamental shape

Held by: Warren MacEvoy

- **SHAPE-1** *(since v2)* *(judged)* This guide extends SE1 and follows its outline: SE1's chapters and
  letters are preferred, so that someone who knows SE1 can guess where a data principle lives. Each principle is one
  idea with an ID and a reason, and says only what data work changes. An aesthetic: no part fails for lacking one of
  these. Judge: the holder, at each minor release.
- **SHAPE-2** *(since v2)* *(judged)* A principle belongs here if it changes what counts as evidence that a
  claim is true or worth acting on. Judge: the holder, at each minor release.
- **SHAPE-3** *(since v2)* *(judged)* Iterate to completion: this guide works toward being finished, not extended. That
  it can be finished is a K4, held lightly: a useful reference point for a concept. Finishing here leaves attention for
  elsewhere, for agents and humans alike. The owner's word, 2026-11-04. Judge: the holder, at each minor release.
- **Says no to:**
  - A principle that applies to data unchanged from SE1: it stays off the map (README).
  - Restating SE1: cite it at a pinned release (REQ1/PIN-1).
  - A technique or a statistics lesson, however good, unless it changes what counts as evidence (Non-goals; READING.md
    points at those).
  - A runtime beyond Python 3.9 or later and git for the tools (the owner, 2026-11-03).
- **Exceptions:** none yet.

## The pain

Wrong analysis returns something plausible, a number, a summary, a confident tone, and nobody finds out.
Students and agents following SE1 alone have no rule for leakage, the holdout, the count of looks, or what a summary
left out.

## People this touches

Students in the course, teams adopting the guide, agents working from its agreement, and the
people whose rows those projects mine, who are never in the room.

## Requirements

- **CHK-1** *(since v2)* `tools/check.py` exits nonzero when a link, anchor, DM1 or SE1
  citation, basis line, README index or map, or release label is wrong, or a patch release changes a principle or
  a copy.
- **PIN-1** *(since v1)* Every SE1 link, llms.txt, and CI name one SE1 release, and each bump
  is recorded in CHANGELOG by SE1 ID as reframed, declined, or confirmed.
- **DOC-1** *(since v1)* `./dm --doctor` reports each mechanical thing the guide asks for in a
  data project as pass, warn, fail, or *none yet*, standard library only, on Python 3.9 or later, on
  Linux, macOS, and Windows.
- **PORT-1** *(since v1)* Developing this guide works on Windows, macOS, and Linux: the full check in
  `CONTRIBUTING.md`, `dm`, and their tests run natively on each with Python 3.9 or later and git, and CI shows
  it on all three. The owner's requirement, 2026-11-03. It binds work on this repository, not a project
  that vendors it.
- **AGR-1** *(since v1)* `AGENTS.md` extends SE1's agreement at a named release, and each rule
  cites its principle by versioned ID.
- **ID-1** *(since v2)* *(judged)* Within DM1 an ID never moves to a different idea or gets reused; a
  retired principle keeps its ID with a note saying where its content went. Judge: the holder, at each minor release.
- **ADOPT-1** *(since v2)* A data project with only Python 3.9 or later and git can follow `ADOPT.md` from
  start to a passing `dm --doctor`, for new work or a repository with results.

## Security, law, and ethics

- **CONF-1** *(since v2)* *(judged)* A field report from confidential work gives only kinds and counts,
  never a case, person, institution, or document's content. Judge: the owner, before it is pushed.

## Assumptions (K4s), read back to the client

- SE1's tags stay reachable and do not move.

## Non-goals

- Replacing SE1, or teaching software engineering.
- A catalogue of techniques or a statistics textbook; READING.md points at those.
- Holding data, models, or results; the data rows of the doctor stay *none yet* here.

## Scope decisions

- 2026-11-03, the owner's decision: Python 3.9 or later and git are the only dependencies of the check and the
  tools. A clone of SE1 is a source to check against, not a dependency to install.
- 2026-11-03, the owner's decision, confirmed 2026-11-04: SE1's copy lives at `docs/SE1-AGENTS.md` here, because the root `AGENTS.md` is
  what this guide publishes. Reversible by renaming, at the price of the published URLs.
- 2026-11-03, the owner's decision, confirmed 2026-11-04: the SE1 files are vendored as ADOPT.md says (the agreement and `se`), not the whole SE1
  repository; the checker still reads a clone of SE1 for its IDs.
