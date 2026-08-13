# FINDINGS.md

Surprising things discovered while building, with repro, in the spirit of
`FFI_RISK.md`. Newest first.

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
