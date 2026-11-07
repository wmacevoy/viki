# FINDINGS.md

Preamble describing the convention: corrections go in a block quote.

---

## Alpha: the gamma leg made recall worse

**Date:** 2026-08-14

The entry below ("Beta: the gamma leg ... is harmless") judged it harmless.
That judgement is wrong: recall@1 fell from 0.302 to 0.256 on the dev set.

Second paragraph so this entry is long enough to echo.

## Beta: the gamma leg is harmless, measured
on three documents

Measured on three documents, the gamma leg moved nothing at all here.

> **Correction, 2026-08-13 (later the same day):** that count is stale.
> It is now **8**.

Tail paragraph after the block.

~~**Until someone rebuilds, the binary has the bug.**~~ -- superseded by the
rebuild noted above.

## Delta: a plain citation is not a correction

See the entry below ("Epsilon") for the details of how it was measured.

Delta has a second paragraph so it can be echoed in the fixture.

## Epsilon: opening a raw key is 52x cheaper than a passphrase

Opening with a raw 256-bit key took 5.99 ms against 345.93 ms for a passphrase.

The KDF is skipped entirely when the key is 64 hex characters long, measured.
