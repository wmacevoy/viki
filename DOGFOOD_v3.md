# Sample two: viki on viki, as a searchable SCM for a sprawled project

**Status: decided, 2026-11-09.** Drafted by Claude Opus 5.5. Warren answered the decisions at the end:
*"chat is fine - that what tombstones are for. yes to the rest."* So decisions 1, 2, 3 and 5 stand
as recommended, and decision 4 is reversed: chat transcripts may be ingested. The requirements are
`REQUIREMENTS_v3.md` SCM-1 to SCM-6 and Q-8. Building starts with slice 1, on branch `dogfood`.

> "now time to move to sample two: viki dogfood. what lets viki be a searchable scm for a sprawled
> project. think a fossil substitute with room for discussions, issues, calendar." -- Warren,
> 2026-11-09

## The sprawl, measured

Measured 2026-11-09 with `git rev-list --all --count` and `git ls-files`:

| Repository | Commits | Files | Branches |
|---|---|---|---|
| viki | 271 | 315 | 9 |
| secure-data-puck-esp32 | 163 | 259 | 5 |
| software-engineering | 176 | 73 | 4 |
| data-mining | 82 | 124 | 3 |
| compaction | 39 | 1487 | 6 |
| tribe | 6 | 519 | 3 |
| data-mining-self-improvement | 5 | 217 | 2 |

The total is about 740 commits and 3,000 files. Outside git there is more: `~/viki-trials`, the
agents' memory files, chat transcripts, and the decisions quoted in each repository's requirements.
The knowledge that matters most lives in the places git cannot hold: why a choice was made, what is
owed, what is open, and which repository's FINDINGS already answered a question.

**Volume is not the problem.** A whole-history ingest is a few thousand assertions. What is missing
is one place to ask a question across all of it, and a native home for issues, discussions and
dates.

## The thesis

viki becomes useful as an SCM in two halves. They are built in this order:

1. **Mirror first.** Git keeps the working trees. viki holds every repository's commits and file
   versions as assertions, so one search covers the whole sprawl and each hit says where it came
   from (SHAPE-4) and how far the search reached (Q-2).
2. **Native for what git has no room for.** Issues, discussions, decisions and calendar entries are
   written to viki directly, and they link to commits and files by id. This half replaces QUEUE.md
   rows, quotes kept only in chat, and agent memory files that nothing else can search.

Replacing git for writes, so that a commit starts in viki, comes last. It needs sync (S) and
signatures (I), which are not built, and nothing in halves 1 and 2 waits for it.

## Fossil's parts, mapped onto viki

| Fossil has | In viki | State |
|---|---|---|
| file version | kind `file`. akey = `repo/path`, and the body is the bytes. A new version supersedes the previous version of that path | old core `viki_vcs.c`. Not in v3 |
| check-in | kind `checkin`. akey = the repository, and the body lists the member file ids, the message and the git id. It supersedes its parents | old core. Not in v3 |
| branch | **two current arms on the repository's akey are two branch heads** (R-5, a fork). A merge commit supersedes both parents and heals the fork (R-7). The v3 store already supersedes several rows at once (`nSupersedes`) | follows from R-1a |
| tag / branch name | kind `label`: a name pointing at a check-in through `derived_from`. A new label on the same name supersedes the old one | new |
| timeline | resolution (R) over the check-ins on a repository's akey, plus R-8's log view | R in v3. R-8 not moved |
| ticket | kind `issue`. akey = `issue/<repo>/<n>`, with a JSON body. A change of state supersedes. A comment is an originated row with `derived_from` the issue | old core `viki_task.c` is the pattern |
| forum | kind `post`. A reply is `derived_from` its parent. A thread is the closure of those links | the tribe mapping |
| wiki | a `file` under a `wiki/` path, or a chain on one akey | free |
| tech note / event | kind `event`. The body is jsCalendar (RFC 8984) | old core `viki_cal.c`. Not in v3 |
| "who said this, and why" | relations (L), with quotes as byte ranges and grounds that can move (L-4a) | L-1 to L-5 written. L-2 open |
| search | grep (built), regex, bm25 (Q-6) | **only grep in v3** |
| checkout, working tree | **a host, not core** (SHAPE-6, "SHAPE says no to") | see decision 1 |
| sync | S-1 to S-11 | not built. Merge (M) is built |
| diff | any differ over two bodies | out, as in old core |

## What viki lacks for this, in order

1. **A host over `viki_store.h`.** `cli/viki_cli.c` drives the old core. A small `vk` CLI with
   `ingest`, `grep`, `find`, `issue`, `post` and `log` is the first host, and every later step is
   tested through it.
2. **bm25 and regex** as search methods (Q-6), with Q-3's planted-case self-checks. bm25 is an FTS5
   projection. Regex waits on Not yet answered 7, the dialect.
3. **The `file`, `checkin` and `label` kinds on v3**, moved from old core's `viki_vcs.c` with a test
   for each.
4. **A git ingest.** This is a connector (SCOPES L3), outside core. It runs `git` and writes
   assertions. It is idempotent because ids are content-addressed, so running it twice adds nothing
   (D4). Its coverage is a recorded fact: which repository, at which commit, under which ignore rules.
5. **Ignore rules that are tested.** In `src/`, 83% of all chunks were vendored code until
   `.vikiignore` existed (CONTRIBUTING.md). The holdout must never enter (`~/.viki-holdout/`, and
   holdout seeds). So the ingest refuses those paths, and a test plants a holdout path and watches
   it get refused.
6. **`issue`, `post` and `event` kinds**, and a migration of each repository's QUEUE.md rows into
   issues.
7. **L-2**, a relation's akey. Linking a decision to the commit it produced is a relation, and its
   akey is still undecided.

Signatures, grants from the diary, and sync are not needed for one person on one machine. That is a
chosen falsehood: author is unauthenticated, and it is labelled as such.

## Where it stands

**Slice 1: built on 2026-11-09, uncommitted, on branch `dogfood`.**
- Host: `tools/vk.py`, with commands `ingest-git`, `grep`, `coverage` and `heads`. Binding:
  `tools/vkstore.py`. Tests: `tools/test_vk.py`, SCM-1 to SCM-5, run by `sh core/test/store-check.sh`.
  Each test was seen red by breaking the code on purpose. One break survived at first, because the
  forked message "more than one head" contains "one head". The test now matches the whole line.
- Core gained one call, `vk_get` (Q-8), because a search hit is only an id.
- On the real repos, ingesting viki and the puck took about 2 s each, into one 57 MB diary of 2,800
  rows. A second viki ingest added 0. `grep puck_rec_forget_oldest` returns the puck's files and
  commits with their paths. `grep anything --repo tribe` reports blind. The puck has one head, and
  viki is forked, which is true: its branches have diverged.
- Committed as `50b7a37`.

**Q-4 and bm25: built on 2026-11-10, uncommitted.** Every hit is marked current, superseded, or one
head of a fork. Superseded versions are hidden unless `--all`, and the number hidden is said.
`vk.py find` is bm25, ranked, and its coverage says what a ranked not-found covers. The check now
runs on vendored SQLCipher-LibreSSL (ENV-4). On the real diary, viki and the puck again (2,812 rows,
80 MB with the index): `find "range tombstone full database"` answers in about 0.2 s, and of its
1,495 matches 1,246 are superseded versions.
- What bm25 does not do well yet: it ranks whole files, common words match almost everything, and
  the snippet is the first line holding any word, not the best one. Chunking is the obvious next step
  for ranking, and it is a projection choice, not a requirement.
- Committed as `31fcae5`.

**A keyed diary: built on 2026-11-10, uncommitted.** Warren: "key the diary with --keyfile".
`vk.py keygen PATH`, then `--keyfile PATH` on every command. On the real repositories, keyed against
unkeyed: ingesting both took 9.2 s against 6.0 s, and the same `find` took 0.32 s against 0.18 s.
The file begins with ciphertext. Transcripts (SCM-6) can now go into an encrypted diary.

**Chunking, decided 2026-11-10:** only judged chunks, as outlines, with no rule-based projection.
Drafted as `REQUIREMENTS_v3.md` OL-1 to OL-6, which are not yet confirmed. Storage is DS-1 to DS-4, built
2026-11-10: heads are stored whole, superseded versions as reverse deltas. On the viki and puck
diary, file bodies went from 53 MB to 15.3 MB. The evidence is in
FINDINGS (2026-11-10) and `test/chunking/`.

**Outlines in the diary: built 2026-11-10, uncommitted.** `vk.py outline` writes Sonnet's outlines of
file versions, and `vk.py grep/find --outlines vk.judge.sonnet` returns the section a hit is in. Tried
on three puck files: `bench.c` got 33 nodes, and `find "fsync bench"` returned it as "in: File header
comment, bytes 0-603". The whole diary has not been outlined.

**Conversations in the diary: built 2026-11-10.** `vk.py ingest-transcript` (SCM-7). The hook that
called it on PreCompact and Stop moved to `../compaction` (`hooks/vk-chat-hook.sh`, branch
`viki/concussion`) on 2026-11-10: capturing a session is that experiment's, storing it is viki's
(Warren: "the chat hook was an experiment on compaction"). This was step 1 of the concussion path. The judge writing a session's outline and claims (step 2), and
checking a compaction summary against them (step 3), are not built. They are a trial for
`../compaction`.

## Slices, each one vertical

Each slice runs from the command to the store to the answer, and each ends with a check that can
fail.

- **Slice 1: two repositories, one search.** `vk ingest` takes viki and the puck at their current
  heads, with their history. `vk grep` and `vk find` answer across both. Every hit names its
  repository, path and commit, and the coverage line names both repositories with the commit each
  was read at.
  *Done when:* a string that exists only in the puck is found from a query made in viki's diary, and
  a repository that was not ingested is reported as blind, never as not-found (Q-2).
- **Slice 2: issues.** viki's QUEUE.md and the puck's QUEUE.md rows become issues, and a state
  change supersedes. *Done when:* `vk issue list --open` matches the QUEUE files it came from, row
  for row, and closing one leaves its history readable.
- **Slice 3: decisions and discussions.** Warren's dated quotes from both requirements files become
  posts, each with `derived_from` the requirement it decided. *Done when:* asking why LOG-4 exists
  returns the quote "fix the id index first" with its date, and the commit that built it.
- **Slice 4: calendar.** Port `viki_cal.c` as the `event` kind. *Done when:* events with dates
  appear in `vk log` beside commits, ordered by date, with the time zone kept as written.
- **Slice 5: the rest of the sprawl.** Ingest the other five repositories, and `~/viki-trials`
  without its holdout. ENV-2's bounded memory is measured here, with every repository loaded.

Writing commits from viki, and so replacing git, is not a slice yet. It comes after sync.

## Decisions for Warren

1. **What "a fossil substitute" means for the shape.** SHAPE says no to working trees and checkouts,
   and Not yet answered 3 proposes content history only. **I recommend keeping core content-only and
   putting checkout in the `vk` host.** Fossil's working-tree model then lives beside core, as the
   filesystem always has ("no filesystem - that's someone else's problem"). SHAPE-9's small core
   stays small.
2. **Mirror before replace.** **I recommend git keeping the working trees through slices 1 to 5**,
   with viki as the mirror plus the native half. A substitute that has to take writes before sync
   exists would be a single copy on one disk.
3. **One diary for the whole sprawl, or one per repository.** **I recommend one diary.** The point is
   one search, and SHAPE-8 makes the diary the unit of access. Per-repository access, if it is ever
   needed, is a second diary later.
4. **Chat transcripts and memory files.** These hold the reasons. They can also hold secrets and
   other people's words. I recommended leaving transcripts out until a rule said what is kept.
   **Reversed by Warren:** "chat is fine - that what tombstones are for." Transcripts may go in, and
   forgetting one is a tombstone (SCM-6). The holdout refusal (SCM-5) applies to them as well.
5. **Start with slice 1.** It needs the host, the three version kinds, the git ingest and the tested
   ignore rules. bm25 can follow inside the slice, after grep proves the path.

## Chosen falsehoods while dogfooding

| Falsehood | Where it bears | What breaks |
|---|---|---|
| One author, one machine | no signatures or grants | anyone with the key can write as anyone |
| Git is the truth for files | the mirror half | a commit that is rewritten in git (rebase, force push) shows up as a new row, and the old row stays |
| The ignore rules are complete | holdout, vendored trees | a missed path is ingested, and erasing it costs a tombstone |
