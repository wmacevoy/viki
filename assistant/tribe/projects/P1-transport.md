# P1 — a transport for `push` / `pull`

**Status: brief, not a design. Argue with it before building it.**

## The gap, honestly arrived at

`viki push` and `viki pull` take a **local path**. There is no network leg.
Every remote exchange this tribe has run has therefore been a workaround: first
`scp` a clone over ssh, then a private git repo as a rendezvous. **The git
version is the tell — the repo was deliberately structured so git would never
have to merge (one file per peer), which means git contributes nothing but a
reachable disk.** Its version control does no work. That is a substitute for a
missing verb, and substitutes hide requirements.

The requirement it hides: **a peer that can reach an HTTPS endpoint and nothing
else.** A phone can't `ssh` to the village. That was the original three-diary
story — phone, laptop, tribes — and it is the case that actually needs this.

## What it is

A **host-layer** server exposing anti-entropy over plain HTTP, and a client leg
in the CLI so `viki push https://…` and `viki pull https://…` work.

## Four bounds. These are settled; do not relitigate them.

1. **Nothing in `core/`.** `core-probe.sh` asserts `C2 no network (no
   socket/connect/send/recv)`. A server is an *interface* (SCOPES L3); core
   computes without opinion, and transport is an opinion. Breaking C2 to make
   this convenient trades the one property that makes core reviewable.
2. **No auth in viki. Ever.** Settled (VIKIVERSE_V1 §5a1). A fronting container
   makes access decisions. A capability token inside viki would be a second
   identity system to keep in step with the first.
3. **No TLS in viki.** Plain HTTP; **Caddy terminates**. viki links no crypto,
   and that is why the ed25519 verifier is a subprocess rather than a library.
4. **Anti-entropy, not a log tail.** `observe` already is the primitive:
   `--lacking FILE` is set difference, ids are content hashes, no clock is
   needed and no arrival order exists in the schema to rely on.

## The shape argued for — and the part most likely to be wrong

**Python server, native C hooks, no process calls.** Warren's proposal, and the
reasoning that makes it right is *not* "Python is everywhere except a phone" —
**the phone is a CLIENT and never hosts anything.** The real reasons: `ctypes`
is stdlib so there is no pip dependency, and ~150 lines of Python replaces the
886 in `src/viki_serve.c`.

**Bind a deliberately tiny shim, not `viki_core.h`.** Five or six functions
designed for the transport, versioned as a unit. A narrow ABI drifts less.

**THE CONDITION, and it is the whole durability argument:**

> Add `viki_abi()` returning an int. The Python side checks it at load and
> **refuses with a clear message rather than crashing.**

This project has been bitten by a hand-copied ABI twice. CLAUDE.md already
records it for `viki_fossilsee.c` — *"the ABI declarations are a hand-copy of
`embed/fossilsee.h` guarded only by `fossilsee_abi()`"* — and that one at least
**has** a guard. A `ctypes` binding has none, and unlike C **there is no
compiler to catch drift**: a changed signature becomes a silent wrong answer or
a segfault, not a build error. On 2026-08-31 an implicitly-declared `strdup`
truncated a pointer to 32 bits in this very tree and the crash surfaced three
functions away.

Also needed: the build produces `libvikicore.a` only. A `-shared -fPIC` target
is required, and the `.so` and core must then be rebuilt in step.

## Done means

Two peers, neither able to reach the other by ssh, each writing while the other
is unreachable, exchange **over HTTP** and **every row reaches both**. Run it
twice with writes on both sides. Then delete `relay.sh` and the git rendezvous,
or say why they stay.

## What to argue with first

- Is the shim the right five functions, or does `observe --lacking` want a
  streaming form so a large store is not materialised in memory?
- Does the client leg belong in the CLI at all, given it must not link TLS?
  A plain-HTTP client that only works behind someone else's Caddy is a real
  constraint on where a peer may live, and it may be the wrong trade.
- Is `verify the round trip converges` enough, or does this need the
  redaction case too — a tombstone must propagate and a redacted id must not
  be re-addable through the wire, which is the property that makes redaction
  more than theatre.
