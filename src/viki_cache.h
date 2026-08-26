#ifndef VIKI_CACHE_H
#define VIKI_CACHE_H

/* `viki cache push` / `viki cache pull`: distribute the derived layer as
** Fossil unversioned files (D-12), via subprocess calls to `fossil uv`.
** Must be run from within an open Fossil checkout (cwd). Returns 0 on
** success, nonzero (and prints diagnostics to stderr) otherwise.
**
** TWO things travel, not one -- VIKI_DESIGN.md: "Embedding caches -- and
** **the pinned model file itself** -- travel as Fossil unversioned files
** ... Consequence: viki is self-contained -- a fresh clone pulls corpus +
** model + embeddings from one endpoint, no third-party downloads at
** runtime." The stable uv names are:
**
**     viki-cache.db                  the local embedding cache db
**     viki-model/model.onnx          the pinned ONNX model
**     viki-model/vocab.txt           its WordPiece vocabulary
**     viki-model/viki-manifest.json  the epoch pin (model_id, dim, sha256s)
**
** ('/' in a uv name is fine: fossil only requires a simple relative
** pathname -- verified against fossil-see 2.28, see FINDINGS.md.)
**
** Which fossil binary: $VIKI_FOSSIL_BIN if set, else "fossil-see" if
** found on PATH, else "fossil". See FINDINGS.md for why this isn't
** hardcoded to a single name. */
int viki_cmd_cache_push(const char *zCacheDbPath);
int viki_cmd_cache_pull(const char *zCacheDbPath);

/* VIKI_CACHE_NO_MODEL -- publish/fetch the embedding cache ONLY.
**
** Model distribution is the DEFAULT and this is the opt-out, rather than
** the other way around (`--with-model`), because D-12's whole stated
** consequence is that one endpoint is sufficient. An opt-in flag makes
** the default outcome a half-populated hub on which "no third-party
** downloads at runtime" is false, and nothing tells the pusher that --
** exactly the state this repo was in before. The cost of defaulting on
** is bounded: the model is re-published only when the epoch actually
** changes (see the manifest check in viki_cmd_cache_push_opts), so a
** routine `viki cache push` moves the cache and nothing else. */
#define VIKI_CACHE_NO_MODEL 0x01u

/* VIKI_CACHE_REQUIRE_SIG -- refuse a model whose epoch pin is not signed by a
** key listed in the checkout's viki-signers.json.
**
** OFF BY DEFAULT, and the polarity is the whole decision. A REJECTED signature
** is always fatal -- that is evidence, not policy. But "unsigned", "no signer
** list" and "no verifier installed" are states every tribe is in until it
** adopts signing, and defaulting to refusal would break each of them on
** upgrade. So viki always REPORTS the state loudly and acts on it only when
** the caller asks. Whether a tribe requires signatures is the tribe's
** judgment, which makes it exactly the kind of thing that belongs behind a
** flag rather than inside the tool (SCOPES.md 3). */
#define VIKI_CACHE_REQUIRE_SIG 0x02u

int viki_cmd_cache_push_opts(const char *zCacheDbPath, unsigned mFlags);
int viki_cmd_cache_pull_opts(const char *zCacheDbPath, unsigned mFlags);

/* Shared with viki_index.c's wiki/ticket extraction, which also shells
** out to `fossil`. */
const char *viki_fossil_binary(void);

/* $VIKI_FOSSIL_USER if set, else $USER, else "viki" -- ticket commands
** (unlike wiki commands, empirically) refuse to run at all without a
** resolvable user, even for read-only queries. See FINDINGS.md. */
const char *viki_fossil_user(void);

/* $VIKI_MODEL_DIR if set and non-empty, else "build/dist/model"
** (relative to cwd, i.e. inside the checkout).
**
** Lives here, in the module that PUBLISHES that directory, so push/pull
** and the embedder-open path in viki.c cannot drift: `viki cache pull`
** must write the model exactly where `viki ask` will later look for it,
** otherwise a fresh clone pulls a model it then ignores. */
const char *viki_model_dir(void);

#endif

/* REFUSE TO PUBLISH A PRIVATE BLOB. Returns 1 (and explains) when zPath names
** something that must never leave a device -- today that is identity.db and
** its journals.
**
** SYNC.md classes a blob as derived / grow-only / owned / private, and private
** is the only one with no safe sync at any frequency. identity.db holds
** private keys each wrapped under a passphrase, inside a SQLCipher container
** whose key is PUBLIC by design (QUEUE 49: the known key is kept for its
** per-page HMAC, i.e. tamper detection, not for secrecy). Publishing it hands
** every wrapped key to anyone with repo read access, to attack offline at
** leisure and without a rate limit.
**
** A check in code rather than a line in a document, because the document
** already said so and a document cannot refuse.
**
** IT IS A GUARDRAIL, NOT A SECURITY CONTROL, and calling it one would be
** theater (SYNC.md 0b). A tribe member holds the key: they can open the repo
** with SQLCipher directly, link libfossilsee, or just run `fossil uv add` and
** publish whatever they like. This stops a careless script and a confused
** agent -- a real and foreseeable failure -- and it stops nothing else. */
int viki_cache_refuse_private(const char *zPath);
