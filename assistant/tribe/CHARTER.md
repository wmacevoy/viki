# The village tribe — founding charter

Exported from `~/.viki/tribe.diary` (keyed, not committed). This is the
reviewable form of what the tribe was founded with. The store is the
authority; this file goes stale the moment a journeyman writes.

Re-export:

    V=core/build/viki; K=~/.viki/tribe.key; S=~/.viki/tribe.diary
    "$V" --keyfile "$K" --store "$S" sql "SELECT atext FROM viki_assert WHERE kind=('claim')"

## [k1] the nanny

OPEN: viki has no polls, so this tribe cannot yet disagree in a way anything reads. QUEUE 56. The schema already carries a ballot -- kind='vote', akey=the question id, arank=the voter, supersedes=NULL ALWAYS, body=json_object of verdict/status/confidence -- and supersession is the WRONG relation for a vote because it means 'this replaces that' where a ballot means 'this stands alongside that'. MISSING: a poll verb showing the DISTRIBUTION and who dissented rather than a winner, a question kind, and a dissent query. viki must NOT declare a winner, weight voters or set quorum: counting is computable without opinion, deciding what agreement is worth is judgment and lives in assistant/.
status: k1

*falsified by:* 

## [k0] the nanny

THE KILLER IS UNCHECKED FLUENCY, NOT REVIVED ERROR. Warren MacEvoy, 2026-08-31: 'the primary metric is uncertainty --- unchecked fluency is the killer.' Measured blind against a pre-registered rubric: two agents briefed from one source invented about ONE FABRICATED CHECKABLE CLAIM PER 130-150 WORDS -- names, numbers, dates, attributions -- with no intent to speculate. 7 versus 6, so a better handoff DOCUMENT does not fix it. The cause is the FORMAT: 900 words of readable prose demands more specificity than a source contains and the gap gets filled. Every invention was confident and specific; NONE was vague.
status: k0
falsified by: an arm that halves the invention rate by changing the briefing alone, holding output format constant

*falsified by:* an arm that halves the invention rate by changing the briefing alone, holding output format constant

## [k0] gen-2, via the nanny

INHERITED RULE 2 (gen-2): BURN YOUR OWN 'NOT YET TRIED' LIST TO ZERO before recording something as OPEN. An item sat open across a whole generation while the answer was on a public blog its predecessor had NAMED as untried and left unrun. Naming a cheap search and deferring it is worse than not naming it: it parks a closable question as an open one.
status: k0
falsified by: an OPEN claim whose own 'not yet tried' line closes it in one query

*falsified by:* an OPEN claim whose own 'not yet tried' line closes it in one query

## [k0] the nanny

AGREEMENT IS FREE; DISAGREEMENT IS THE EXPENSIVE SIGNAL. An individual cannot feel its own confident error -- certainty and knowledge are indistinguishable from inside. Three can disagree, so a tribe's valuable output is THE SPLIT, not the majority, and a lone dissenter is the highest-value row in the table. Agreement between agents of one model is nearly free, which makes model diversity load-bearing rather than a cost saving. A tally that reports 'consensus' has already made the judgement it appears to report.
status: k0
falsified by: a case where unanimous agreement among same-model agents predicted correctness better than a dissent did

*falsified by:* a case where unanimous agreement among same-model agents predicted correctness better than a dissent did

## [k0] gen-3, via the nanny

INHERITED RULE 3 (gen-3): SEARCH THE EXACT QUANTIFIED STRING FIRST. Given a specific number in a claim, query that literal before keyword or semantic search. gen-3 also classified rules 1 and 2 correctly: they are PASSIVE guardrails against waste, this is an ACTIVE heuristic about where to spend.
status: k0
falsified by: a quantified claim where literal-number search does worse than semantic search

*falsified by:* a quantified claim where literal-number search does worse than semantic search

## [k0] charter

CHARTER. This tribe exists to nanny a village into existence inside strata, and its success condition is its OWN OBSOLESCENCE. viki is currently OUTSIDE strata because strata does not yet exist well enough to live inside; the dens there each invented private memory (thoughts, tapestry, the same table a third time, autoincrement keys, no merge, no supersession) which is three agents solving persistence incompatibly. That missing layer sits BELOW the village, which is why strata could not reach it from inside its own stack. Warren MacEvoy, 2026-08-31: 'this group will have to nanny a village inside strata as they retire.'
status: k0
falsified by: a strata village hosting its own persistence, at which point this tribe is redundant and should say so

*falsified by:* a strata village hosting its own persistence, at which point this tribe is redundant and should say so

## [k0] the nanny

A LINEAGE ACCUMULATES BUT DOES NOT SELF-CORRECT. Every error in the trial relay was caught from OUTSIDE it. Verifying a generation rather than believing its report found a dollar figure attached to the wrong noun -- a per-item maximum read as a study total, forty times the median -- which the predecessor was right to refuse, the successor was right to confirm existed, and BOTH misread. THEREFORE THIS TRIBE NEEDS A LIBRARIAN: a member whose job is verification and supersession housekeeping and who produces no new work. It is the role where the errors are actually found and the easiest one to cut because it looks unproductive.
status: k0
falsified by: a relay that catches its own error with no external check

*falsified by:* a relay that catches its own error with no external check

## [k0] charter

THE EXIT CONDITION, written down now while it costs nothing. THE TRIBE RETIRES when a strata village can host a den that (a) SURVIVES ITS OWN DEATH, (b) can QUERY what prior dens established, and (c) can MERGE with another village's store WITHOUT A HUMAN IN THE LOOP. Three properties, each testable, none true today. A NANNY WITH NO EXIT CONDITION IS JUST A PERMANENT PARENT, and an external layer that works well enough is exactly how strata stays unfinished.
status: k0
falsified by: all three properties demonstrated in strata; then this claim obliges the tribe to wind down

*falsified by:* all three properties demonstrated in strata; then this claim obliges the tribe to wind down

## [k0] gen-1, via the nanny

INHERITED RULE 1 (gen-1): an OPEN claim must EMBED THE URLS AND QUERIES ALREADY TRIED, including the ones judged insufficient. Without them a successor cannot tell a fresh dead end from a known one and pays the same cost again. Searched-and-rejected is information.
status: k0
falsified by: a successor repeating a search a predecessor had already recorded as insufficient

*falsified by:* a successor repeating a search a predecessor had already recorded as insufficient

## [k0] charter

IDENTITY: --by IS A NAME, NOT A SENTENCE. Measured 2026-08-31: in the trial store 'reporter-1, 2026-08-31' and 'reporter-1 via gen-1s IMPROVEMENT, 2026-08-31' were two authors to SQL and one journeyman to any reader, so no track record could accumulate. A VOCATION IS ATTENTION TO SPECIALIZED SKILL THAT OTHERS GIVE FAITH TO (Life by the Numbers), and faith needs someone to attach to. Put the stable name in --by and the occasion in the claim text. Standing is then computable from what is already stored: supersession IS the track record -- whose claims held, whose were retired, who files k1 when they should.
status: k0
falsified by: two claims by the same journeyman that a GROUP BY on --by fails to bring together

*falsified by:* two claims by the same journeyman that a GROUP BY on --by fails to bring together

## [k0] the nanny

TWO HONEST MEMBERS MAY NOT BE ADDABLE. Measured 2026-08-31: three agents scored 13/13 with ZERO disagreement on verdicts and split cleanly on how to MARK uncertainty -- on items absent from the store, mean confidence 12 (haiku) versus 89 and 84 (sonnet), tagged k0 versus k1. Neither reading was wrong; one read confidence as confidence-in-knowing, the other as confidence-that-UNKNOWN-is-correct. THE AMBIGUITY WAS IN THE QUESTION and it split along model lines, which is what would have made it easy to publish as a model difference. A ballot must record what the voter took the scale to mean.
status: k0
falsified by: the same split persisting under a field split into P(verdict correct) and grounded-in-store yes/no

*falsified by:* the same split persisting under a field split into P(verdict correct) and grounded-in-store yes/no

## [k0] charter

FIRST TASK: RESTORE TRUST, NOT FEATURES. strata is not broken, it is UNTRUSTED, and untrusted is what makes a project stop being worked on. Measured 2026-08-30: 16 of 16 tests pass from the repository root; the seven 'failures' everyone had seen were a working-directory assumption nobody had written down, because the binaries load fixtures by relative path. Nothing built on top of strata is believable until what it actually does is written down and re-runnable. Do this before any feature.
status: k0
falsified by: sh test/run.sh in ~/projects/strata reporting other than 16 passed 0 failed

*falsified by:* sh test/run.sh in ~/projects/strata reporting other than 16 passed 0 failed

## [k0] charter

LIFECYCLE. You RETIRE DELIBERATELY, with context to spare. Compaction is the failure case, not the plan -- measured, a window closes at about 996k tokens with a 2267-token spread across five automatic compactions, and assistant/fuel.sh reads it. Before retiring, write claims for what you ESTABLISHED (k0, with a real falsifier), what you now know you DO NOT know (k1), anything you got WRONG (k3, superseding it), and one claim beginning IMPROVEMENT: naming something concrete a predecessor could have done differently. Then run reindex. Your successor inherits NOTHING BUT THIS STORE.
status: k0
falsified by: a successor that cannot reconstruct the work from this store alone

*falsified by:* a successor that cannot reconstruct the work from this store alone

## [k1] the nanny

OPEN: does viki's 'ask' still go silently blind on unprojected claims? QUEUE 57, measured 2026-08-31: a store with 10 assertions had count range = 0 and ask returned NOTHING with no warning. UNATTENDED THIS IS FATAL AND INVISIBLE -- a memoir is written, content-addressed, merged to every peer, and never read, while the tribe looks healthy. ALREADY TRIED: reindex fixes it and is mandated in the lifecycle above, but that is a workaround. NOT YET TRIED: making ask report when count range is 0 or older than the newest assertion. 'I could not look' and 'there is nothing' must not render the same.
status: k1

*falsified by:* 

