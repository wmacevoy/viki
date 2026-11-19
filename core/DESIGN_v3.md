# core design for REQUIREMENTS_v3

Drafted by Claude Opus 5.5, 2026-11-09. Covers the first slice only: `include/viki_store.h`,
`src/viki_store.c`, `src/viki_search.c`, `src/viki_delta.c`. The older files in `core/` are still there, because
`cli/` builds on them; they go when the CLI moves to this interface.

## Run it

    sh core/test/store-check.sh

Builds with ASan and UBSan and exits nonzero on any red. It needs a C compiler, Python 3, git, the
`vendor/sqlcipher-libressl` submodule (`git submodule update --init vendor/sqlcipher-libressl`), and
LibreSSL at `LIBRESSL_PREFIX` (default `$HOME/libressl`), installed as that submodule's README says.
The check builds SQLCipher once, out of tree, into `core/build-store/sqlcipher`, and links it with no
fallback to the system SQLite (ENV-4). The caller still opens and keys the `sqlite3*`. The tests open
their databases unkeyed, so encryption at rest (N-7) is not exercised here.

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

- **bm25 is an FTS5 table over `assertion`'s own bodies** (external content), turned on per diary by
  the host with `vk_bm25_enable`. Until then the method is blind. It is off by default, because the
  puck's record path should not pay for an index it does not query. Triggers keep it current through
  every write, erase and forget. `secure-delete` makes erased words leave its pages, not only its
  answers. A `contentless_delete` table was tried first and does not do that (FINDINGS.md,
  2026-11-10). Every row is indexed, an erased one as an empty document, because FTS5 checks an
  external-content index row for row. The query's words are each quoted and OR'd: FTS5's MATCH is an
  AND, and its syntax is not the caller's to run. The self-check queries a planted row in a temporary
  index through the same query builder.
- **Reverse-delta storage (DS) is one column and one function.** `assertion.dbase` is NULL when
  `body` is whole. Otherwise `body` holds a delta and `dbase` is the row it rests on. So "erased"
  is still `body IS NULL` everywhere, and only readers of content changed. They read
  `vk_body(body, dbase)`: the cursor, `vk_get`, heads, grep, and the bm25 index (whose content is
  now a view of rebuilt bodies). The codec is `src/viki_delta.c`, pure code: the textbook
  rolling-hash match, 16-byte blocks, on any bytes, and every offset is checked on apply.
  - **When:** a write that supersedes an unlogged row tries to turn that row into a delta from
    itself. A row that arrives after a held row superseding it tries the same on arrival (DS-3). It
    is kept only when the body is at least 256 bytes, the delta is at most 3/4 of it, and no chain
    through it would pass 16 deltas (DS-2). Otherwise the row stays whole, which is always correct.
    All three numbers are invented.
  - **Unlogged only.** A logged row is never a delta and never a base. So a range tombstone
    (W-15) deletes no row another body rests on, and needs no DS code.
  - **Erasure** makes every body resting on the erased row whole first, then erases it (DS-4).
  - **The bm25 triggers** act only when a body is written, erased or deleted. Re-encoding a body,
    whole to delta or back, changes nothing the index sees.
  - **A store made before DS** gets the column on open (`ensure_dbase`). An existing bm25 index
    built over `assertion` directly is not migrated: rebuild that diary.
  - **Measured** on the viki and puck diary: file bodies went from 53 MB to 15.3 MB, and the file
    from 77 MB to 43 MB (the bm25 index is about 19 MB of that). 1,738 of 3,018 rows are deltas.
    Ingest time and search time did not visibly change.
- **An outline (OL-1) is checked by SQLite's JSON functions**, so there is no parser of ours. Its
  reference is the target's id, its akey is `vk_outline_key(target, author)` (a hash, like a range
  tombstone's), and it is never logged. The frame is checked in `shape_error`. The body is checked
  by one query over `json_tree`: a list of objects, each with a text title, integer `lo` and `hi`
  (byte offsets, `hi` exclusive), optional `children` and nothing else, children inside the parent,
  and siblings in order. The body is bound as text, because a BLOB given to the JSON functions may
  be read as JSONB, and an outline is JSON text. A write needs the target held and readable, and
  the ranges are checked against its length. On merge the target may arrive later, so only a held
  target's length is checked. An outline whose target never arrives is kept, and its ranges are
  checked by whoever reads it.
- **Search returns outline nodes (OL-4) from a projection, `vk_onode` and `vk_oseg`.** It holds
  the nodes of every current outline: the heads of each outline akey, valid and not erased. It is
  re-folded for an akey whenever an outline is stored or erased, and for every akey when the grants
  change. Each outline adds a root node (depth 0) for the text outside every top-level node. A node's
  own text is its range less its children's ranges (`vk_oseg`), so every byte of a target is in
  exactly one node's own text.
  - **The reader chooses** whose outlines a search uses (`vk_use_outlines`). The default is none,
    which is the old behaviour.
  - **Grep** returns the smallest node holding the first match.
  - **Regex** cannot say where a match is (the engine returns only yes or no), so it returns the
    deepest node whose span matches by itself. There `^` and `$` mean the span's ends, so a node can
    be chosen for an anchored match the body as a whole does not have.
  - **bm25** indexes each node's own text in the same FTS table as whole bodies, under negative
    rowids. It ranks the reader's author's nodes together with the whole bodies of targets that
    author has not outlined, so their scores compare. The triggers keep node text right when a
    target arrives after its outline, or is erased or deleted. `integrity-check` with `rank` 1
    covers node rows too, through the content view.
  - **A hit's node** comes from `vk_search_hit`. Coverage counts what was searched whole
    (`nWhole`).
  - **Limits:** outlines of logged targets get no searchable node text. A node's text is rebuilt
    from the target's body once per segment, which is slow for a delta-stored target with many
    segments.
- **Regex is SQLite's `ext/misc/regexp.c`** (REQUIREMENTS_v3 Not yet answered 7), included whole by
  `src/viki_regexp.c`, so the vendored file is unedited and nothing is copied. viki registers its
  own `vk_regexp(pattern, body)` and not the file's `regexp()`, for two measured reasons: `regexp()`
  reads its input as a C string, and the matcher takes a zero byte as end of input. Either would end
  a body at its first NUL, and regex would stop being exhaustive without saying so. `vk_regexp` passes
  the length, and matches a body holding a NUL as a same-length copy with each NUL as 0x80, which
  the matcher reads as U+FFFD. The self-check's planted body has a NUL before its match and is also
  probed with an anchor that must refuse. A pattern that does not compile is refused at open.
  The build needs `-I vendor/sqlcipher-libressl/ext/misc -I vendor/sqlcipher-libressl/src`.
- **`vk_standing` answers Q-4 from `arm`**: whether a row is a head, and how many heads its akey has.
- **A merge source of the wrong shape is refused whole (E-4), before anything is ingested.** One
  pass over the source checks that every stored id, rank, body hash, skip list and link has the
  width and type a frame needs, so a tampered or foreign database adds nothing rather than part of
  itself. Until 2026-11-14 the cursor copied those columns by their fixed widths without looking, so
  a short id was read past (inside SQLite's page buffer, where ASan does not see it). The cursor now
  refuses such a row too, which is what `vk_verify` and `vk_get` meet on a tampered store. A row of
  the right shape whose frame does not make its id is still quarantined, row by row (M-7).
- **A build without vectors (ENV-3)** leaves out `src/viki_vector.c` and defines `VK_OMIT_VECTOR`,
  which drops `vk_cosine`'s registration and the vector branch of coverage. A flag, not lazy
  registration at search time: SQLite refuses to redefine a function while any statement runs, so
  registering at first use would fail whenever the host held a cursor open.
- **`vk_derivatives` reads V-1's other direction**: the rows whose `derived_from` names an id, by the
  `edge` key `(parent, rel)`. `edge` is filled from each frame as it is stored, never copied from a
  source (V-1a).
- **The host passes two ports to `vk_open`: a clock (C-3) and a sync policy (D-1).** Neither has a
  default, and a missing one fails the open. Core calls the clock only to bound a `ts` as it
  arrives, and keeps nothing it returns. The policy is checked in `vk_merge` against the *source*:
  a `never` source is refused, and any other merges only into a store whose principal it names.
  Both are host-supplied for now, as grants are, until the diary carries them under signatures.
- **A `ts` is checked in two steps.** A fixed-shape check in C covers 19 positions, an optional
  fraction, and `Z` or `+00:00`. SQLite then confirms it is a real moment, by a round trip through
  the epoch, and checks the bound. `strftime` alone prints `02-30` and hour 24 back as given (seen
  on 3.40 and on 3.53). A leap second (`:60`) is refused. An empty `ts` is allowed: a writer with
  no wall clock says so. Writes refuse a bad `ts`; merge quarantines it (C-3c).
- **Coverage counts what it could not read (E-2).** `nUnread` is the readable assertions with a
  body that the method holds nothing for. For vector, that is a row with no vector under the
  chosen model; for bm25, a row the index lacks. grep and regex read the bodies, so theirs is 0.

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
- **Keyed with `--keyfile`, or unencrypted only behind an explicit `--plaintext`** (Warren,
  2026-11-10). The key file holds exactly 64 hex characters, a raw key used with no KDF (N-7). A
  passphrase is refused rather than stretched, because N-8 keeps passwords on a separate path. This
  is stricter than the old CLI, which took either. A file that group or others can access is refused,
  as ssh refuses a private key. The key is applied before any other statement, and a real read
  follows, so a wrong key is reported plainly. The host zeroes its copy once applied; Python strings
  made along the way are not guaranteed gone. `vk.py keygen` writes a new key to a new file, created
  mode 600 with `O_EXCL`, so the key is never readable by others and never overwritten. The diary
  file itself is created with the process umask (0644 here): it is ciphertext, but its size and
  timing are visible.
- **Superseded versions are hidden by default, and the number hidden is said.** Without that, a phrase
  that lived through ten commits returned ten rows. `--all` shows them, each marked (Q-4).
- **`ask` combines bm25 and grep (Q-7).** Hits are merged by id in bm25's order, then grep's. Each
  hit names the methods that found it, and each method's coverage gets its own line, because a
  ranked "not found" and an exhaustive one are different claims.
- **The host's ports** (`tools/vkstore.py`): the clock is `time.time()`. The policy is `domain`,
  naming the principals the host entitles, which in practice are the owner's own copies. A git
  commit's `ts` is its commit time in UTC; the offset git reports is not kept.
- **Everything printed from the diary goes through `printable()`** (V-5): bodies, paths, outline
  titles, refs and names are someone else's text, and a terminal runs the escape sequences it is sent
  (OSC 52 writes the clipboard). Control characters and the bidi overrides are spelled out as `\xNN`
  and `\uNNNN`, tab excepted.
- **`find` ranks whole files.** Nothing is chunked, so a long document that mentions every query word
  in passing outranks a short one that answers. Common words match nearly everything, and the snippet
  is the first line holding any query word, not the best one.

- **Outlines are asked of a judge outside viki** (SHAPE-7). The command, `vk.py outline` when this was written,
  is now `la.py outline` in `../viki-agent`, moved 2026-11-10 with the librarian; the notes below describe it.
  - **Who writes:** each model writes under its own principal (`vk.judge.sonnet`, `vk.judge.haiku`),
    and the host entitles them beside the ingester, so a reader chooses by author (OL-2).
  - **Which versions:** by default it outlines current file versions with no outline from that judge
    yet, so a rerun asks nothing new. With `--history`, it does every version in arrival order.
  - **The protocol** is the one measured in `test/chunking/outline.py`. A version whose predecessor
    has an outline is sent with that version and its outline, with a preference to keep it, and the
    new outline names the one it carried (OL-3). Otherwise it gets a fresh outline by concept.
  - **Conversion:** the judge answers in lines. The host clips the tree to the file and to each
    parent, drops overlapping siblings, and converts to OL-1's byte ranges.
  - **Skips:** binary bodies, and anything over 60,000 bytes for the call's versions together
    (Not yet answered 14).
  - **Testing:** `--judge-cmd` replaces the `claude` CLI, so the tests run a fake judge that outlines
    by markdown headings. Calls are sequential.
  - **Cost on the dogfood diary:** three Sonnet calls on puck files took 17 s. About 550 current text
    files would take roughly 50 minutes at that rate, and the money is not measured here.

- **Conversations go in as logs (SCM-7).** `vk.py ingest-transcript` reads a Claude Code transcript
  (JSONL) and appends each user and assistant record to `chat/<session>` as readable text: text,
  tool calls, tool results, and thinking where the transcript kept it readably. A thinking block
  kept only as a signature is counted, not stored. Records that are not messages are skipped
  (attachments, modes, request metadata).
  - **Rows:** written by their own principal, `vk.ingest.chat`, which holds `x` so it can forget them.
  - **Reruns:** how many messages have been consumed is kept in a `chat-ingest/<session>` chain, so a
    forgotten message is not ingested again, and a transcript that shrank is refused. This assumes
    transcripts are append-only.
  - **Forgetting:** `vk.py forget-chat SESSION [--from N --to M]` is one range tombstone.
  - **The hook** that ran this on PreCompact and Stop is the compaction experiment's, and lives in
    `../compaction` (`hooks/vk-chat-hook.sh`, moved 2026-11-10). viki provides only the command.
  - **This session's transcript:** 1,245 messages in 0.4 s, a 3.4 MB encrypted diary. 240 thinking
    blocks had no readable text.

## Testing

- `test/store_test.c`: one test per claim, naming its requirement.
- **Faults:** the test build fails each SQLite prepare and step, then each allocation, in turn, and
  requires a retry to reach the same store. The counts are printed by the run.
- `test/test_store.py`: through the C interface with ctypes, 200 seeded random histories against a
  separate Python implementation of the id and of resolution. Python is used where a second
  implementation is the point. Faults and memory stay in C, because they need the test build.
- `tools/test_vk.py`: the host against scratch git repositories, with fixed names and dates. It is
  run by the same check.
- `test/build-check.sh`: what a build contains, read from its objects. C-1: core imports no clock
  function and its SQL asks SQLite for no time. V-4a: core imports no socket, process or loader
  function, so it cannot call a judge. ENV-3: core links without vectors into a program
  that stores and searches (`test/novector.c`), and no object references an embedding runtime.
- `test/refusal-check.py`: E-3, read from the source. Every literal that can reach `vk_refuse` begins
  with a requirement ID, and `vk_refuse` is given nothing but a literal or `zBad`.
- `test/gate.py`: the seed of FIN-3. Prints each in-scope ID as green, red, or not reached.

## Not done

Signatures (so `author` is unauthenticated), grants and the sync policy read from the diary,
W-9's reason codes (the list is not yet named), the wire format,
sync, Q-4's relations (L is not built), OL-5's flag, a host command that asks a judge for
an outline, compressing deltas or whole bodies, measuring the DS
thresholds, the withdraw tier, keys, references, relations, an ESP32 build.
Reporting two rows at one log position. Merging range tombstones that one range contains. Proving a
row against a range tombstone or walking skip links: nothing reads them yet except the next append.
