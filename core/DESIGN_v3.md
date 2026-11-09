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

## Testing

- `test/store_test.c`: one test per claim, naming its requirement.
- **Faults:** the test build fails each SQLite prepare and step, then each allocation, in turn, and
  requires a retry to reach the same store. Currently 90 and 3,468 points.
- `test/test_store.py`: through the C interface with ctypes, 200 seeded random histories against a
  separate Python implementation of the id and of resolution. Python is used where a second
  implementation is the point. Faults and memory stay in C, because they need the test build.
- `test/gate.py`: the seed of FIN-3. Prints each in-scope ID as green, red, or not reached.

## Not done

Signatures (so `author` is unauthenticated), grants read from the diary, the wire format,
sync, regex, bm25, the withdraw and seal tiers, keys, references, relations, an ESP32 build.
