# FINDINGS.md

Surprising things discovered while building, with repro, in the spirit of
`FFI_RISK.md`. Newest first.

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
