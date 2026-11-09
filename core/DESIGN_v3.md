# core design for REQUIREMENTS_v3

Drafted by Claude Opus 5.5, 2026-11-09. Covers the first slice only: `include/viki_store.h`,
`src/viki_store.c`, `src/viki_search.c`. The older files in `core/` are still there, because
`cli/` builds on them; they go when the CLI moves to this interface.

## Run it

    sh core/test/store-check.sh

Builds with ASan and UBSan and exits nonzero on any red. It needs a C compiler, system SQLite and
Python 3. It links stock SQLite on purpose: the caller opens and keys the `sqlite3*`, so encryption at
rest (N-7) is not exercised here.

## Decisions

- **C, not C++.** Decided 2026-11-09. C++'s main conveniences are ruled out by ENV-2 or stop at the
  C interface anyway; what remains is RAII and stronger types. C keeps the whole stack in one language
  and easier to review (SHAPE-9). The cost, hand-written failure paths, is paid in testing, below.
- **No per-kind vtable.** `core/`'s `canon`/`key`/`rank`/`text` slots are gone from identity. A kind
  is data, one function frames every row, and a peer can store and merge kinds it has no code for.
- **Search is the one parameterized family.** `VkSearchMethod` is a const table of name, exhaustive
  flag, `open` and `selfcheck`. A method that fails its self-check makes the search blind (Q-3).
  bm25, regex and vector are each a new table.
- **The body enters the frame as its hash.** So the id can still be recomputed after erasure, and
  merge can verify an erased row (SHAPE-3). Knock-on effect: open question 2, guessable content, is
  unchanged by this.
- **Erasure clears the body and keeps the row and its links,** so erasing a correction cannot bring
  back what it corrected, which `core/`'s `apply_redactions` does.
- **A tombstone is an ordinary assertion** of kind `tombstone`; v2's separate framing (A-8) is dropped.
- **Rights are a function of the frame** (D-3), applied in SQL through `vk_rights()` over a small map
  the host supplies at open. Reads of a row by an unentitled author see it as absent; it stays stored
  (R-4a).
- **Bounded memory.** Frames hash as a stream; links are capped at 16 and fields at 4096 bytes
  *(invented)*; merge and cursors work a row at a time. Every allocation goes through SQLite's.
- **The undo statements are prepared at open,** so undoing a failed write needs no new memory.

- **A log is a named sequence of one author's rows** (LOG-1). `(log, pos)` and the skip links are in
  the frame. Skip links are a column, not `edge` rows, because nothing queries them and every extra
  B-tree row costs a card write.
- **A range tombstone removes rows entirely** (W-15, SHAPE-3). A later append at `n` links to
  `n-2^j`, a multiple of `2^j`; if that position has been forgotten it is the largest multiple of
  `2^j` at or below the tombstone's end. Those are the positions got by clearing the end's low set
  bits, so carrying those ids is exactly enough to go on appending, and they chain back over the
  range, so they are also its commitment. It has no Merkle root and no count: the count is
  `to-from+1` positions.
- **A logged row has no entry in an id index** (LOG-4). Its uniqueness is `(log, author, pos, id)`,
  written in position order. `arm`, `edge` and `range_tomb` hold the local `seq` so that following them
  needs no id lookup. `vk_digest` sums a hash per row, because sorting by id would no longer stream.
- **`arm` is a projection of the rows no valid assertion supersedes**, so a head is an index lookup
  (LOG-3). It depends on the grants, so it is rebuilt when their hash in `meta` changes. Its key
  starts with `akey`: any delete by `id` must also name the akey, or it scans the table.
- **The range tombstone's body is binary and fixed-layout**: core has no JSON parser and needs none.
  JSON bodies are the host's choice for its own rows (the puck, Warren 2026-11-09).

- **`vk_get` reads one row by id into a cursor** (Q-8), because a search hit is only an id. It shares
  the cursor's columns. A logged row has no id index, so the statement tries the unlogged rows first,
  and the scan only runs when they hold nothing.

## The first host: `tools/vk.py` (REQ3/SCM, `DOGFOOD_v3.md`)

- **Python through ctypes, not C.** The host shells out to git and keeps a map of every commit's
  files. In C that is a lot of hand-written parsing and memory for something that never runs on the
  ESP32. The binding, `tools/vkstore.py`, mirrors `viki_store.h` field for field, so it is one more
  thing a change to the header must change.
- **The mapping is in vk.py's header.** File versions supersede the version of the same path in
  each parent, so a path changed on two branches is a fork until a merge. A check-in supersedes its
  parents. A deleted path is a `file.gone` row, so that re-adding the path heals rather than forks.
- **Every row is authored by the host's principal.** Git's author is a claim, kept in the body. A
  reader who has not granted that principal sees none of its rows (R-4a). The tests ran into this.
- **Re-ingest reads the whole history each time** and relies on ids to dedupe. viki plus the puck
  (434 commits, 2,800 rows) takes about 2 s. That is fine at this size and not measured larger.
- **Unencrypted, behind an explicit `--plaintext`**, because stock SQLite has no codec. A SQLCipher
  build that exports `sqlite3_key` is the next step for this host.

## Testing

- `test/store_test.c`: one test per claim, naming its requirement.
- **Faults:** the test build fails each SQLite prepare and step, then each allocation, in turn, and
  requires a retry to reach the same store. The counts are printed by the run.
- `test/test_store.py`: through the C interface with ctypes, 200 seeded random histories against a
  separate Python implementation of the id and of resolution. Python is used where a second
  implementation is the point. Faults and memory stay in C, because they need the test build.
- `tools/test_vk.py`: the host against scratch git repositories, with fixed names and dates. It is
  run by the same check.
- `test/gate.py`: the seed of FIN-3. Prints each in-scope ID as green, red, or not reached.

## Not done

Signatures (so `author` is unauthenticated), grants read from the diary, the wire format,
sync, regex, bm25, Q-4's current-or-superseded mark on a hit, the withdraw and seal tiers, keys, references, relations, an ESP32 build.
Reporting two rows at one log position. Merging range tombstones that one range contains. Proving a
row against a range tombstone or walking skip links: nothing reads them yet except the next append.
