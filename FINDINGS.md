# FINDINGS.md

Surprising things discovered while building, with repro, in the spirit of
`FFI_RISK.md`. Newest first.

---

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
