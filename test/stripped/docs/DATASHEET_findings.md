# Datasheet — FINDINGS.md

**This sheet:** drafted by Claude Opus 5.5 on 2026-11-07, as a proposal, from reading parts of the file and
its git history. It would be shown wrong by an entry whose date and commit disagree, or by a marker
kind `stripped.py` does not parse.

## Origin

`FINDINGS.md` at the root of this repository: viki's ledger of surprises, newest entry first, written
2026-08-13 to 2026-09-02 by the owner and the agents working for him. It is a living file, so every
look records its sha256 (`key.json`'s `source_sha256`, and the ledger's `data`).

## People

Engineering notes. They name the owner, and other people only by role.

## Documents

| Part | What it holds | How it was made | Knowable at | Absent / late / wrong / lying |
| ---- | ------------- | --------------- | ----------- | ----------------------------- |
| preamble (before the first `## `) | the marking convention | written once, by hand | first commit, 2026-08-13 | dropped from every pile |
| entry (`## ` heading to the next) | one claim, with its repro | appended at the top, by hand or by an agent | the commit that added it; a `**Date:**` line when present | an undeclared repeat of another entry: seen as unkeyed overlap |
| correction block (`> **Correction, DATE` / `> **Superseded DATE`) | a later fact about its host entry | inserted into the host, dated | the date in its label, after its host's | a positional pointer can rot: line 3798's "the entry at the top" no longer is |
| strikethrough (`~~…~~ -- superseded …`) | a retracted sentence and what replaced it | edited in place | the commit that struck it | one instance |
| heading pointer (`The entry below ("…")`) | a later entry correcting an earlier one by quoting its heading | written in the later entry | the later entry's commit | the quote may wrap lines, elide with `...`, or carry a locator (`..., item 4`) |
