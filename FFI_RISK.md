# iOS FFI feasibility — experiment results

> **STALE as of 2026-08-13 (later that same day) — read `../fossil-see/embed/README.md`
> instead.** This file is a frozen snapshot from when this experiment lived in `fossil-app`, before it
> was promoted to a shared `embed/` directory in `fossil-sqlcipher-libressl`. The cross-repo bug this
> file's "Required shim rules" section lists as an open risk (`db_repository_filename`'s `zRepo`) is
> now root-caused and fixed there, the exit trap is now portable to Apple's linker (no more GNU-ld
> `--wrap` dependency), and a second, separate bug (`fossil_fatal()` silently swallowing every message
> after the first) was found and fixed along the way. `experiments/harness.c` and `db-embed.patch` in
> this directory are similarly frozen — do not treat either as the current state of the embedding work.
> See AGENTS.md's "Not yet built" section for the full pointer.

**Date:** 2026-08-13 · **Fossil:** 2.29 [7a40eb9748] · **Verdict: FEASIBLE — green light.**

The iOS-blocking question was: can Fossil's client operations run *in-process* —
one process, no fork/exec, `exit()` trapped — invoked repeatedly, the way a
`dart:ffi` shim must call them? Simulated the constraint on Linux by linking
Fossil's objects into a harness (`harness.c`) with `main` renamed, `exit()`
wrapped into a `longjmp`, and `fossil_main()` called like a function.

## Proven

1. **Repeated in-process invocation works.** version → init → open → settings →
   add → commit → commit → timeline → status, all in one process, all rc=0.
   `fossil_main` does `memset(&g,0,…)` on entry — the main global is designed
   to re-init per call.
2. **The process survives `fossil_fatal`.** An intentional failure (empty
   commit) longjmps back; subsequent commands still work.
3. **The full networked lifecycle works in-process over HTTP:** clone →
   open → add → commit (with autosync pull, server check-in lock, push) →
   push/pull/sync. Server verified receiving the artifacts. The sync client
   never forks — fork exists only in the *server* loop, ssh/`file://`
   transports, external-tool launches, and backoffice.

## Bugs found (why this experiment mattered)

**Delete-on-failure carryover — catastrophic class.** `init`/`open` register
"delete this file if we fail" entries (the repo! `.fslckout`!) in a file-static
list that is *never* cleared between in-process calls. A later `fossil_fatal`
fired the stale list and **deleted the repository**. Fix: 12-line patch adding
`db_clear_delete_on_failure()`, called by the shim after every command
(`db-embed.patch`). This alone justifies the harness approach: found in an
hour on Linux instead of in the field on a phone.

## Required shim rules (all verified)

- Call `db_clear_delete_on_failure()` after every command (the patch).
- Call `sqlite3_shutdown()` between commands (fshell precedent).
- Set `backoffice-disable=1` on every repo at open/clone — and compile
  backoffice's `fork()` out entirely on iOS (`#ifdef`) for belt-and-braces.
- No interactive prompts ever: clone with `--save-http-password`, pass
  `--user`, pre-answer everything via flags. (Root cause of two harness
  failures: the "remember password?" prompt silently got EOF.)
- Serialize commands (one at a time, background thread/isolate) — Fossil is
  not re-entrant concurrently.
- One repository per process lifetime for v1. A handful of function-statics
  cache repo identity forever (`db_repository_filename`'s `zRepo`, the
  versioned-settings cache, static prepared statements). Cross-repo switching
  in-process misbehaves (observed: commit consulted the wrong checkout).
  Fine for the app (one PM repo); multi-repo later means writing a
  `fossil_embed_reset()` that clears an auditable, grep-able list of statics.
- Handle check-in lock contention in UX: autosync commit takes a server-side
  parent lock; on "Might fork / parent locked", auto-`update` and retry (or
  `--override-lock` after the ~60s timeout). Observed live; it's a feature.

## iOS-specific remaining work (believed low-risk, standard plumbing)

- **Exit trap without GNU ld:** Apple's linker has no `--wrap`. Since we
  compile Fossil from source, patch `fossil_exit()` to call a registered
  handler (one function) instead of relying on link tricks.
- **Output capture:** fossil prints to stdout; shim redirects per-call to a
  buffer. Alternative worth exploring: Fossil's built-in **JSON command API**
  (`fossil json timeline` etc., already compiled in) as the app's structured
  interface — machine-readable results without scraping.
- Cross-compile arm64-apple-ios: plain C99 + bundled SQLite + zlib (in the
  SDK) + OpenSSL (prebuilt iOS frameworks exist). Termux proves the Android
  NDK path already.
- App Store: no fork/exec used, no JIT, all content is data. Precedent apps:
  iSH, a-Shell, Working Copy.

## Artifacts

- `harness.c` — the experiment (local phase + `--net URL` phase).
- `db-embed.patch` — the delete-on-failure fix against Fossil 2.29 `src/db.c`.

## Estimate to a syncing iOS prototype

Exit-trap + output-capture shim ~1 week; iOS cross-compile plumbing 1–2 weeks;
Dart FFI bindings + minimal Flutter screen ~1 week. Risk is now in the
plumbing, not the concept.
