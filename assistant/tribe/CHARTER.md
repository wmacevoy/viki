# The village tribe — the store, exported

Exported from `~/.viki/tribe.diary` (keyed, not committed). The store is the
authority; this file goes stale the moment a journeyman writes. **Newest
first**, so a reader meets a correction before the thing corrected.

A claim marked *superseded* is retired, not deleted. `viki why <id>` walks
the chain both ways.

## [k0] the nanny

CORRECTED (I seeded the charter with this and it was too pessimistic): A LINEAGE CAN CATCH ITS OWN ERROR, AND MODEL DIVERSITY IS NOT WHAT IT NEEDS -- A POINTED TARGET IS. Measured 2026-08-31 against a known defect I had verified myself (a per-paper maximum cost read as a study total, 2.2M against a 8,900 median). Three librarians got a store with the correction REMOVED, and one instruction: assume the resolution is wrong and find how. HAIKU, THE SAME MODEL THAT MADE THE ERROR, pointed at the claim by id, found it: WRONG at 95%. Sonnet, unpointed, noticed the target was ambiguous, reasoned to the right one, found it at 85%, AND additionally established that the primary source never mentions the University of Chicago at all. THE ONE THAT FAILED FAILED ON TARGETING, NOT MODEL: haiku given an ambiguous target audited a different claim entirely and returned HOLDS. So an unattended tribe does not need a second model in the room; it needs something that SELECTS what gets audited. The newest unaudited k0 is a mechanical default and needs no judgment.
status: k0
falsified by: a librarian of the same model, pointed at a specific claim by id and told to disbelieve, that repeatedly misses a defect an outside checker finds
because: measured after seeding it; the pessimism was untested and the test came out the other way

*falsified by:* a librarian of the same model, pointed at a specific claim by id and told to disbelieve, that repeatedly misses a defect an outside checker finds

*supersedes* 64c15a12893a: measured after seeding it; the pessimism was untested and the test came out the other way

## [k0] the nanny

MY BROKEN INSTRUMENT PRODUCED THE CONTROL THE EXPERIMENT NEEDED. I wrote 'find the claim beginning RESOLVED (was OPEN): that is your target' as though ONE did; FOUR did -- the third time in one session that an ambiguity in my own question produced something that looked like a finding. The accident is that the misdirected librarian became the control I had not designed: told to ASSUME a claim was wrong, it audited a claim that is CORRECT and returned HOLDS at 92% rather than manufacturing a defect. Without it, three WRONG verdicts would not have distinguished 'the librarian role works' from 'a librarian told to assume error will always find one'.
status: k0
falsified by: point a librarian at several claims known to be sound; if it reports defects in them, the disbelieve-instruction manufactures errors and the positive result above is worth much less

*falsified by:* point a librarian at several claims known to be sound; if it reports defects in them, the disbelieve-instruction manufactures errors and the positive result above is worth much less

## [k0] warden

IMPROVEMENT: A 'BUILT' LABEL NEEDS A CALLER, NOT JUST A LINKER AND A TEST. Verifying a doc's built-vs-planned claim by confirming the file exists, links, and has a passing test is not enough -- test_shamir passes and shamir-bf links into the strata binary, yet grep for 'shamir' or 'vouch' outside CMakeLists.txt and the test itself returns zero hits, so nothing in strata actually uses it (its own test file's header comment admits it 'only proves the dependency is wired up'). Contrast blob AEAD, verified instead by finding strata_blob_put/get calling strata_aead_seal/open directly on the real read/write path. A predecessor spot-checking a 'built' claim should grep the claimed function or table name OUTSIDE its own test file and its own module before marking it verified -- a green test proves a dependency links and its own narrow assertions hold, not that any feature was built on top of it.
status: k0
falsified by: a built claim correctly verified by test-passing alone, where the feature turned out to have no caller anywhere and the test still meaningfully proved the claim

*falsified by:* a built claim correctly verified by test-passing alone, where the feature turned out to have no caller anywhere and the test still meaningfully proved the claim

## [k1] warden

NOT CHECKED: dens/board.js, dens/gatekeeper.js, dens/library.js, dens/claude-homestead.js's own memory schema (grep shows it also defines conversations+memory, possibly a sixth incompatible pair or a copy of claude.js's -- not compared), dens/anthropic.js, and dens/lib/ were not examined at all. The den-memory count in my k0 claim is a floor from 5 of roughly 14 files in dens/, not an exhaustive survey. Also not checked: whether test/run.sh's 16 passed reflects functional correctness of what each test asserts, versus merely 'the assertions in the test as written all held' -- I read test_shamir.c's assertions but not the other 15 test files.
status: k1

*falsified by:* 

## [k1] warden

NOT CHECKED: whether the Layer 3/5/6 'Implemented' column entries (store_service, village daemon, code-smith, claude-homestead, cobbler, strata-human REPL, artifact browsing) actually DO what ARCHITECTURE.md's prose claims, beyond confirming the source files exist, link, and back a passing test binary (test_cobbler, test_claude_homestead, test_anthropic are 3 of the 16 green tests). File existence plus a passing smoke test is not behavioral verification of the specific prose claims -- I did not read cobbler.c or code_smith.c closely enough to say whether they do what a 'cobbler' or 'code-smith' vocation is described as doing.
status: k1

*falsified by:* 

## [k0] warden

FOUND: ARCHITECTURE.md's Layer 0 table lists 'SQLite / PostgreSQL | Village store engine ... Same interface, swappable' as a present-tense component, not flagged Planned the way the Shamir row two tables below it is -- but zero code references PostgreSQL anywhere: grep -rl for 'postgres' or 'PostgreSQL' or 'PQconnect' across src/, include/, CMakeLists.txt, and find . -iname '*postgres*' (excluding vendor/ and build*) both return nothing. Layer 2's own prose is more careful ('designed to support ... without changing any code above this layer'), but the Layer 0 table entry reads as present when it is, on this evidence, unstarted.
status: k0
falsified by: a postgres backend file such as store_postgres.c, or any PQ*/postgres reference anywhere in src/ or include/

*falsified by:* a postgres backend file such as store_postgres.c, or any PQ*/postgres reference anywhere in src/ or include/

## [k0] warden

VERIFIED AND WIDER THAN DOCUMENTED: the charter's claim that dens each invented incompatible memory tables holds exactly as README.md states. gee.js: thoughts(id INTEGER PK AUTOINCREMENT, from_who, heard, thought, ts) + meta(key PK, value). loom.js: tapestry(id INTEGER PK AUTOINCREMENT, from_who, heard, weaving, ts) + threads(word PK, count, last_from, updated_at). inch.js: observations(id INTEGER PK AUTOINCREMENT, from_who, heard, observation, word_count, ts) + meta(key PK, value). Three different table names for 'what I heard and what I made of it', all AUTOINCREMENT rather than content-addressed, none with supersession -- confirmed by reading the CREATE TABLE statements directly (dens/gee.js:17-23, dens/loom.js:18-24, dens/inch.js:18-24). README's table names only these three, but dens/claude.js separately defines conversations+memory and dens/claudette.js defines messages -- two more distinct schemas README does not mention, so 'at least three' undercounts what is actually in the tree.
status: k0
falsified by: any two of gee.js/loom.js/inch.js/claude.js/claudette.js sharing one schema, or any of them using a content-addressed or supersedable key

*falsified by:* any two of gee.js/loom.js/inch.js/claude.js/claudette.js sharing one schema, or any of them using a content-addressed or supersedable key

## [k0] warden

VERIFIED: ARCHITECTURE.md's blob-at-rest encryption claim is real wired code, not aspirational prose. include/strata/aead.h defines the exact wire format the doc quotes -- AE02 (4) || nonce (24) || ciphertext+tag (N+16), STRATA_OVERHEAD=44 -- and src/blob.c actually calls strata_aead_derive/strata_aead_seal on the write path and strata_aead_derive/strata_aead_open on the read path (grep strata_aead_ src/blob.c hits lines 49,59,98,108), not merely a linked-but-unused library.
status: k0
falsified by: src/blob.c not calling strata_aead_seal or strata_aead_open, or the wire-format constants in aead.h not matching what ARCHITECTURE.md states

*falsified by:* src/blob.c not calling strata_aead_seal or strata_aead_open, or the wire-format constants in aead.h not matching what ARCHITECTURE.md states

## [k0] warden

VERIFIED: ARCHITECTURE.md correctly labels Shamir SSS (M-of-N vouches, credential reconstruction) as Planned rather than built, despite vendor/shamir-bf being linked into the strata binary (CMakeLists.txt:149) and test_shamir passing as one of the 16 green tests. test/test_shamir.c's own header comment says it 'only proves the dependency is wired up and usable from here'; grep -rl for 'shamir' (case-insensitive) or 'vouch' across src/ and include/, outside CMakeLists.txt and the test file itself, returns zero matches. The raw secret-splitting primitive is linked and tested; no trust or governance code consumes it.
status: k0
falsified by: any src/ or include/ file other than test/test_shamir.c and CMakeLists.txt that references shamir or vouch

*falsified by:* any src/ or include/ file other than test/test_shamir.c and CMakeLists.txt that references shamir or vouch

## [k0] warden

VERIFIED (fresh build, not just existing binaries): sh test/run.sh in ~/projects/strata reports '16 passed, 0 failed (build_test, from /Users/wmacevoy/projects/strata)', exit 0. Additionally ran cmake -B build_verify && cmake --build build_verify from a clean directory against current HEAD (commit 1f82099, which includes the Shamir-submodule-extraction commit 6e02730 that the pre-existing build_test binaries predate -- build_test/test_den is dated before 6e02730) -- configure and build both succeeded with 0 errors, and sh test/run.sh build_verify also reports 16 passed, 0 failed. build_verify was removed afterward (gitignored, no tracked files touched). This is stronger evidence than resting on build_test's pre-built binaries alone, which had not exercised the current tree until this rebuild did.
status: k0
falsified by: a clean cmake -B <dir> && cmake --build <dir> from current HEAD that fails to configure, fails to build, or whose sh test/run.sh <dir> reports any failure

*falsified by:* a clean cmake -B <dir> && cmake --build <dir> from current HEAD that fails to configure, fails to build, or whose sh test/run.sh <dir> reports any failure

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

## [k0] the nanny  — SUPERSEDED

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

