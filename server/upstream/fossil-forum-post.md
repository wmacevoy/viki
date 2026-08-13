# Draft forum post for fossil-scm.org/forum

*(Post as Warren; plain text below the line. The Fossil project takes bug
reports via the forum, not the ticket tracker, for non-contributors.)*

*(This finding was independently written up again, in more detail — root
cause trace against all 5 call sites of `db_get_saved_encryption_key()`,
a secondary lower-severity instance of the same bug in `test_pid_page()` —
while factoring the encrypted-Fossil build out into the shared `fossil-see`
project. That version is the one to actually file:
`vendor/fossil-see/docs/upstream-report-fossil-server-see-key.md` (once
viki vendors fossil-see — see KICKOFF.md). This draft is kept for the
historical record of the independent discovery.)*

---

**Subject:** `fossil server` on a USE_SEE build never obtains the repo key
(saved-key pointer check defeats the prompt)

On Unix, `fossil server repo.efossil` on a USE_SEE build fails at startup
with `SQLITE_NOTADB` warnings and "not a valid repository", even though the
same repository opens fine with one-shot commands (`fossil timeline -R`,
`fossil http`) in the same environment. The encryptedrepos doc only notes a
server limitation on Windows, so I believe the Unix behavior is unintended.

Mechanism (line refs from 2.29 [7a40eb9748], also present in 2.28):

1. `cmd_webserver` calls `db_setup_for_saved_encryption_key()` before opening
   the repo. That function pre-allocates a *zeroed* mlock'd page and points
   `zSavedKey` at it, so that request children can later inherit the key
   (`FOSSIL_SEE_PID_KEY` / `--usepidkey` machinery).
2. First repo open → `db_maybe_obtain_encryption_key()` does:
       char *zKey = db_get_saved_encryption_key();
       if( zKey ){ blob_set(pKey, zKey); } else { /* prompt */ }
   `db_get_saved_encryption_key()` returns the raw pointer without a validity
   check, so the zeroed page is mistaken for an already-obtained key. The
   prompt is skipped, `blob_set` produces an empty key ("" since p[0]==0),
   `db_maybe_set_encryption_key()` then applies nothing
   (`blob_size(&key)>0` is false), and every open fails NOTADB.

The validity predicate already exists (`db_have_saved_encryption_key()`,
which rejects a NULL/empty first byte); it just isn't consulted on this path.
Proposed fix:

    --- a/src/db.c
    +++ b/src/db.c
    @@ in db_maybe_obtain_encryption_key
       if( sqlite3_strglob("*.efossil", zDbFile)==0 ){
    -    char *zKey = db_get_saved_encryption_key();
    +    char *zKey = db_have_saved_encryption_key()
    +                   ? db_get_saved_encryption_key() : 0;
         if( zKey ){

With this change the first open obtains the key normally (prompt or other
source), and `db_set_saved_encryption_key()` copies it into the pre-allocated
secure page, so forked request children inherit it as the setup function
intends. Verified end-to-end: `fossil server repo.efossil` then serves
correctly and remote clients clone/sync against it.

Honest caveat on test setup: I don't have an SEE license, so this was
reproduced and verified on a USE_SEE build with SQLCipher substituted as the
codec (the pizza-party-vote-fossil project's build). The failing logic is in
Fossil's key-handling scaffolding, upstream of any codec, and the same
unvalidated pointer check is visible in current trunk — but I'd welcome
confirmation from someone with a genuine SEE build.

Happy to provide the full patch or more detail.

---

## Not being reported (and why)

The other finding from the viki work (formerly fossil-app) — function-static caches
(`db_repository_filename`'s `zRepo`, the delete-on-failure list, versioned-
settings cache) misbehaving when `fossil_main()` is invoked repeatedly in one
process — is **not** an upstream bug. Fossil's contract is process-per-
command; the statics are correct under that contract. It only bites embedded
use, which upstream doesn't claim to support. If embedding ever becomes an
upstream conversation, that's a feature-request thread ("supported embedding
entry point / state-reset function"), not a bug report.
