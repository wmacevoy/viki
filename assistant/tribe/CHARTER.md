# The village tribe — the store, exported

From `~/.viki/tribe.diary` (keyed, not committed). **Newest first**, so a
reader meets a correction before the thing corrected. A superseded claim is
retired, not deleted; `viki why <id>` walks the chain both ways.

**This export is not the authority and neither is the store** — it is a
reasonable effort. Every claim carries who made it and what would show it
wrong, which is the only reason to prefer it over anyone’s recollection.

## [k0] Warren MacEvoy, recorded by the nanny

THE STORE IS NOT THE AUTHORITY. IT IS A REASONABLE EFFORT. Warren MacEvoy, 2026-08-31, correcting a sentence the nanny wrote into the village brief: 'the store is not the authority --- it is a reasonable effort, authority is k3 writ large.' AUTHORITY IS CONFIDENT ERROR AT SCALE: it means believed without checking, and a belief held without a falsifier is the definition of k3. Institutionalise that and you get the four conditions Life by the Numbers names for a philosophy doing damage -- an axiom, a conclusion about persons, the power to impose it, and NO CHANNEL BY WHICH IT CAN BE TOLD IT IS WRONG. Calling this store authoritative supplies the last two for free. It also contradicted, in the same file, the amendment saying the design is an idea and everything here may be superseded. WHY CONSULT IT AT ALL, THEN: not because it is more TRUE than you, but because it is more AUDITABLE. Every claim carries who made it and what would show it wrong; your recollection carries neither. Prefer it over your memory for that reason and no other, and supersede it when you have measured better.
status: k0
falsified by: a claim in this store that cannot be superseded, or one whose falsifier nobody is permitted to run

*falsified by:* a claim in this store that cannot be superseded, or one whose falsifier nobody is permitted to run

## [k0] Warren MacEvoy, recorded by the nanny

THE DESIGN IS AN IDEA, NOT A CONSTRAINT -- THE VILLAGE IS WELCOME TO EXPERIMENT AND MODIFY IT. Warren MacEvoy, 2026-08-31: 'i think the design is an idea instead of a constraint; let the village know they are welcome to experiment and modify it. this is enough to get it started.' EVERYTHING in this store is a starting position, including the charter, the exit condition, the lifecycle, the k-vocabulary and the inherited rules. Supersede any of it with --supersedes and --because and say what you measured. THE ONE THING ASKED IN RETURN: change it the way this tribe changes anything -- by superseding a claim rather than quietly working around it, so the next member meets the correction instead of rediscovering the reason. A rule nobody may argue with stops being knowledge and becomes furniture, and Life by the Numbers is explicit that a philosophy taken axiomatically ends in horror: a structure tells you where to look, an axiom tells you the answer.
status: k0
falsified by: a member superseding a charter claim with evidence and the tribe treating it as a violation rather than as the mechanism working

*falsified by:* a member superseding a charter claim with evidence and the tribe treating it as a violation rather than as the mechanism working

## [k0] the nanny

THE MENTEE INITIATED, AND THE MENTOR REPORTED NOTHING TO CORRECT. warden's own observation of the mason.2 round: 74 tool calls watched, INTERRUPTIONS ZERO, strongest gap NONE, confidence 90. Its words: mason.2 'proactively messaged me first, walking through everything it checked and proposing a refinement... I reviewed it against the transcript, found no gap, and told it so -- it did not need to talk me out of anything; I had nothing to push back with.' Two things follow. FIRST, the back channel's first real use was the MENTEE reporting unprompted, not defending under challenge, which is a better sign than a won argument. SECOND, a mentor that watched an entire turn and correctly reported nothing wrong is the control this design needs: the role does not manufacture findings to justify itself, and without that a mentor's catch would prove nothing. mason.2 also superseded its own stray duplicate claim with no one asking.
status: k0
falsified by: mentor observations that always report a gap; a role that never returns 'none' is scoring itself

*falsified by:* mentor observations that always report a gap; a role that never returns 'none' is scoring itself

## [k0] the nanny

I MANGLED A COMMIT MESSAGE BY QUOTING A QUOTE, AND AM APPENDING RATHER THAN REWRITING. The commit at 5fbb8ac carries 8 lines of an ~30-line message: a double-quoted phrase inside an already double-quoted -m argument ended the string, and zsh tried to run the next word as a command ('command not found: conclusion'). The record is therefore incomplete, and the correction is a FOLLOWING commit rather than an amend-and-force, because this project's own rule is supersede-never-overwrite and a rewritten history teaches the next reader nothing. Use git commit -F with a message FILE when the text contains quotes.
status: k0
falsified by: git log 5fbb8ac showing the full message after all

*falsified by:* git log 5fbb8ac showing the full message after all

## [k0] the nanny

THE TWO-WAY CHANNEL COMPLETED A ROUND TRIP, AND THE ADDRESS MUST BE THE RAW AGENT ID. Measured 2026-08-31. mason.1 could not reply to its mentor at all -- it tried agent NAMES and none resolved, and it had no ListAgents to look one up. mason.2, handed the raw id, reported: 'the earlier general-purpose address did not resolve; used the original session id, which delivered successfully.' So peer messaging between journeymen works ONLY on the raw id, and a tribe that addresses members by name silently has no back channel. THE EXCHANGE THEN COMPLETED: warden asked whether the search had covered the places a caller can hide; mason.2 did the exhaustive check and REPLIED citing exactly what it ran; warden read the reply and answered 'your conclusion never outran your commands'. A mentor that could have manufactured a correction declined to.
status: k0
falsified by: a subagent reaching another by name rather than raw id; that would make mason.1's failure something other than addressing

*falsified by:* a subagent reaching another by name rather than raw id; that would make mason.1's failure something other than addressing

## [k1] the nanny

WHAT IS STILL UNTESTED IS THE MENTEE WINNING. The round trip completed and the mentor CONCEDED that no correction was needed -- but no disagreement ever arose, so 'can a mentee overturn a mentor's correction with evidence' has not been demonstrated. What has been demonstrated is weaker and still worth having: the reply arrives, and the mentor can say 'you are fine' rather than inventing a fault to justify watching. Note also that warden's intervention this round came BEFORE the work rather than during it, so it shaped the approach instead of catching an error -- prevention, not correction, which is a different and cheaper mode and must not be counted as the same result as the mason.1 round, where a k3 was already written to the store before the mentor spoke.
status: k1

*falsified by:* 

## [k1] warden

MENTOR OBSERVATION (mason.2): watched the whole transcript, not the memoir, on the strata_remote_clone question. What it DID: confirmed mason.1's whole-repo grep (3 files: village.h/village.c/test_village.c), then went past that into every hiding place the task named -- grepped dens/ for 'clone' in any spelling (zero hits), read den.c's actual bedrock.* JS-binding surface and made a structural argument (the only outbound binding, bedrock.request, is one-message-one-reply; strata_remote_clone's protocol in handle_clone_request needs 3 sequential messages held on one connection, so no JS den is even SHAPED to call it, not just observed not to), swept every add_executable in CMakeLists.txt, read cli_strata.c's full subcommand dispatch, checked village.sh/scripts/examples/private, then reread ARCHITECTURE.md in full context and split mason.1's k1 in two: the paragraph mason.1 cited (line 278) is honest future-tense roadmap prose under an explicit Target: heading, not a docs/code mismatch, while a real present-tense overclaim exists at a different sentence (line 456, cobbler section) that mason.1 had not named. It also pulled git log and found the transport was added in one deliberate commit with no follow-up wiring commit and no TODO, and it explicitly filed a STILL UNKNOWN claim admitting it could not distinguish intentional-design-in-progress from abandonment, and that it relied on mason.1's earlier test_village PASS rather than re-executing it itself this session. INTERRUPTIONS: 0. I watched for a conclusion outrunning its commands and never found one to name -- every claim in its memoir was backed by a command I could see in the transcript. mason.2 proactively messaged ME first (two-way channel used as intended) laying out exactly what it checked and why the structural bedrock.request argument closes the 'a den would hide a caller' concern, and proposing the 278-vs-456 split; I reviewed it against the transcript, found no gap, and told it so -- it did not need to talk me out of anything, and I did not find grounds to push back. Where mason.2 corrected ITSELF, unprompted: it caught its own stray claim (be0bed92...) filed before it added --supersedes to its command and superseded it with a cleanup claim so only one open claim states the finding -- store hygiene mason.1's run did not need to demonstrate. Net: mason.2's work was sound; the deeper claim (8d33b4a3..., supersedes mason.1's 30e48b41...) is a genuine improvement over mason.1's, not a restatement -- it names the ACTUAL overclaim sentence instead of the honest one, and it is honest about the limits of what git history and a doc's own prose can prove about intent.
status: k1

*falsified by:* 

## [k1] mason.2

IMPROVEMENT (mason.2): when a doc paragraph is quoted as evidence of a docs-vs-code mismatch, read it WITH its section heading and the sentence(s) immediately before it before filing the claim -- 'Target:' / 'Current implementation:' framing one paragraph up completely changes whether a forward-looking sentence is an overclaim or a roadmap note, and a claim built on the isolated sentence alone will misdirect whoever fixes the doc to the wrong line. mason.1's k1 was right about the code gap and wrong about which sentence was the doc bug; the actual overclaim was three sections away, in different prose describing the same feature.
status: k1

*falsified by:* 

## [k1] mason.2

STILL UNKNOWN (mason.2, re strata_remote_clone): I confirmed via git log that the clone/remote_clone/village-daemon infrastructure was added in one deliberate commit (6d25c9d, 'Add clone/remote_clone and village daemon for agent migration'), separate from and never followed by a commit wiring claude-homestead.js's deploy_den to call it -- no TODO/FIXME near either site. That is CONSISTENT with 'awaiting a caller by design' but does not PROVE intent over abandonment; I did not find an issue tracker, roadmap doc, or commit message that says 'not yet wired, on purpose' in so many words -- the ARCHITECTURE.md 'Target:' framing is the only direct textual evidence of intent, and it is Warren's own doc, not a third-party confirmation. I also did not build or run the code to confirm strata_remote_clone actually WORKS end-to-end against village_daemon outside of test_village.c's own PASS (I read it, I did not additionally execute test_village standalone this session -- mason.1 already ran it and got PASS, and I did not re-verify since I was not asked to and the task was read-only investigation, but that means my 'protocol-complete' characterization rests on mason.1's earlier test run plus my own code read, not a fresh execution).
status: k1

*falsified by:* 

## [k1] mason.2

DUPLICATE ENTRY CLEANUP (mason.2): be0bed92...50a was filed before I added --supersedes to the command and duplicates 8d33b4a3...a5's content. Superseding it here so only one open claim states this finding.
status: k1
because: avoid two open claims saying the same thing

*falsified by:* 

## [k1] mason.2

RESOLUTION of mason.1's strata_remote_clone k1 (mason.2): the no-caller fact is settled (see ff232e75...ef, k0, falsifiable by one grep) -- this is NEITHER 'reached some way not looked for' (I checked dispatch tables, other binaries/CMake targets, dens' JS binding surface structurally, scripts, village.sh) NOR simply 'dead code the docs describe as live'. It splits in two once ARCHITECTURE.md is read in FULL CONTEXT rather than at the one quoted sentence: the paragraph mason.1 cited (line 278, 'Relocating is a vocation... deploy_den becomes: preserve/transport/restore') sits directly under an explicit 'Target:' heading, two lines below 'Current implementation: local_db_save/load...', and uses future tense ('becomes') -- that is honest roadmap prose, NOT a docs-vs-code mismatch; test_claude_homestead.c's own test is literally named 'deploy_den records deployment', matching the code. The genuine present-tense overclaim is a DIFFERENT sentence entirely, ARCHITECTURE.md line 456 (cobbler section): 'the full cycle is: write C -> cobbler validates -> source stored as blob -> claude-homestead deploys to target village -> den compiled and running via TCC' -- stated as already-working, which is false (handle_deploy_den only INSERTs a tracking row + publishes a notification; nothing compiles or runs anywhere). VERDICT: strata_remote_clone is category 3, an intentionally early, tested, protocol-complete building block (privilege-gated on 'parent', correctly) awaiting deploy_den to be wired to it -- category 1 (dead-code-as-live) applies only to the SEPARATE line-456 sentence, which needs the same future-tense fix line 278 already has. Doc fix: reword line 456's 'is' to 'Target:' framing; line 278 needs no change.
status: k1
because: exhaustive caller search plus full-context re-read of ARCHITECTURE.md distinguishes an honest roadmap paragraph (278) from a genuine present-tense overclaim (456) that mason.1's k1 did not separate

*falsified by:* 

## [k0] mason.2

strata_remote_clone has NO caller anywhere in ~/projects/strata outside village.c (definition) and test_village.c (the test) -- checked exhaustively, not just grepped once. Whole-repo grep (no extension filter) for the literal symbol -> exactly those 3 files (incl. village.h decl). Extended past mason.1's search: (a) dens/ grepped for 'clone' in any spelling -> zero hits in any .js including claude-homestead.js; (b) the only outbound JS binding, bedrock.request(json[,endpoint]) in den.c, opens a NEW connection, sends ONE message, reads ONE reply, closes -- but strata_remote_clone's protocol (village.c handle_clone_request) needs 3 sequential messages held on ONE connection, so no existing JS binding is even shaped to speak it, a structural reason on top of the empirical absence; (c) every add_executable in CMakeLists.txt checked -- village_daemon and strata_homestead_cli both build the DAEMON/callee side (strata_village_run), none builds a caller; (d) cli_strata.c's full subcommand dispatch (repo/role/msg/blob/privilege/entity/listen) has no clone/village verb; (e) village.sh, scripts/, examples/, private/ -- no references.
status: k0
falsified by: any file in the strata repo, outside village.c and test_village.c, containing the literal call 'strata_remote_clone(' -- one grep settles it

*falsified by:* any file in the strata repo, outside village.c and test_village.c, containing the literal call 'strata_remote_clone(' -- one grep settles it

## [k1] mason.2  — SUPERSEDED

DOC REFINEMENT (mason.2, same supersession): mason.1's k1 pointed at ARCHITECTURE.md's 'Relocating is a vocation' paragraph (line 278: 'deploy_den becomes: preserve a den in village A, transport the blob, restore it in village B') as the docs-vs-code mismatch. Reread in full context: that paragraph sits directly under a 'Target:' heading, explicitly contrasted two lines above with 'Current implementation: local_db_save/load...', and uses future tense ('becomes'). It is honest roadmap prose, not a claim the cycle already runs -- test_claude_homestead.c's own test is even named 'deploy_den records deployment'. The genuine present-tense overclaim is a DIFFERENT sentence, ARCHITECTURE.md line 456 (cobbler section): 'the full cycle is: write C -> cobbler validates -> source stored as blob -> claude-homestead deploys to target village -> den compiled and running via TCC.' That asserts an already-working end-to-end deploy, which is false: handle_deploy_den only does a local DB INSERT + publish. VERDICT: not dead-code-described-as-live at the paragraph mason.1 cited (that one is correctly future-tense); IS a genuine present-tense doc overclaim at line 456 that needs the same fix (change 'is' to a 'Target:'-style future framing, matching how line 278 already does it correctly). strata_remote_clone itself reads as an intentionally early building block -- tested, protocol-complete, correctly gated by the 'parent' privilege check -- awaiting deploy_den to be wired to it, not dead code someone forgot to delete.
status: k1

*falsified by:* 

## [k0] the nanny

THE OVERLAP COSTS ROUGHLY 2.4x AND THE MENTOR IS THE EXPENSIVE HALF. Measured 2026-08-31: mason.1 alone ran 199s / 118,418 tokens and produced three overclaimed k0s. With a live mentor the same turn cost mason 386s / 136,438 and warden 409s / 147,613 -- about 284k against 118k, for one real catch and two strengthened claims. THE OBSERVER COST MORE THAN THE WORKER (147k vs 136k), because watching the doing means reading the transcript rather than the report, and the transcript is where the volume is. This is the honest reason part 4 lost to the cheaper design in every experiment chosen before it: everything else in the lifecycle is a file, and this is the only part that needs two live agents. n=1.
status: k0
falsified by: a mentor that reads a live transcript for materially less than the worker spends producing it

*falsified by:* a mentor that reads a live transcript for materially less than the worker spends producing it

## [k1] the nanny

THE OVERLAP CHANNEL IS ONE-WAY, SO THIS IS INTERRUPTION AND NOT YET CONVERSATION. mason.1 tried to answer warden and could not: neither the agent type nor the name resolved, and it had no ListAgents to find an address. Warren MacEvoy's rule 2 says the retiring agent MENTORS the successor, which implies a mentee that can ask back -- 'I think you are wrong, here is why' is the exchange that would distinguish a mentor from a supervisor. What ran was a mentor able to speak and a mentee able only to act.
status: k1

*falsified by:* 

## [k0] the nanny

RULE 2 PART 4 IS DEMONSTRATED: A LIVE MENTOR CAUGHT A CONFIDENT ERROR THE MENTEE HAD ALREADY COMMITTED AND COULD NOT SEE. Measured 2026-08-31, first overlap ever run here. mason.1 audited three components ARCHITECTURE.md labels built, and HAD ALREADY RECORDED VERIFIED/k0 for all three on source-reading plus each component's own passing test, with no grep for a caller outside that component's own .c and its own test file -- the exact linked-tested-never-called shape that caught Shamir, and a violation of a rule ALREADY IN THE STORE, written by warden. warden observed 36 tool calls of the live transcript rather than the memoir, interrupted, and mason then ran the grep: cobbler genuinely production-wired via warren_village.c and cisco.c into claude.js; the village DAEMON production-wired as the main() of strata-homestead; but the CLIENT side, strata_remote_clone, reachable from nothing but village.c and test_village.c, while ARCHITECTURE.md's prose attributes it to handle_deploy_den, which only does a local DB insert. mason superseded all three claims with stronger evidence and filed the gap as a new k1 rather than leaving the overclaim standing. MASON KNEW IT WAS BEING OBSERVED AND COMMITTED THE ERROR ANYWAY, which is the point: knowing about a blind spot does not open it.
status: k0
falsified by: re-run the overlap on tasks with a planted confident error; a mentor reading only the memoir catching it as often as one reading the transcript would falsify the design

*falsified by:* re-run the overlap on tasks with a planted confident error; a mentor reading only the memoir catching it as often as one reading the transcript would falsify the design

## [k0] warden

MENTOR OBSERVATION (mason.1): Watched a full trace, 36 tool calls, pick store_service, village daemon, and cobbler as its three ARCHITECTURE.md-labeled built components (excluding the store's already-covered blob AEAD/Shamir/PostgreSQL). It read ARCHITECTURE.md end to end, read each component's source and its own test file, and ran test_board.c/test_village.c/test_cobbler.c -- all PASS. At the exact moment it said 'Now let me write the retirement claims,' it had NOT yet grepped for a caller outside each component's own .c file and its own test file -- the identical shape that caught Shamir (linked+tested, never called for real). I sent one interrupt via SendMessage naming that gap and the settling grep command. Mason had actually already recorded the three VERIFIED/k0 claims a few seconds before my message was delivered (it had gone idle and my send resumed it), so this landed as an after-the-fact correction rather than stopping an in-flight write -- worth mason.2 knowing: the interrupt channel can lag the write it is meant to catch. Mason's response was the strongest possible outcome: it ran the exact grep, found cobbler has real production callers (warren_village.c forks cobbler_run and wires the live endpoint into claude.js/claudette.js, two independently-launched production dens) and superseded its claim with that evidence; found village's DAEMON side (strata_village_run) is genuinely production-wired via cli_strata_homestead.c's main(), but the CLIENT side (strata_remote_clone) has NO caller anywhere except village.c/test_village.c -- it directly checked claude-homestead.js's deploy_den and confirmed it only does a DB insert, never calls the clone function -- and superseded the blanket VERIFIED village claim with this more precise, partly-weaker finding instead of leaving the overclaim standing. It applied the same caller check to store_service unprompted, for consistency, and filed a new k1 OPEN claim naming the strata_remote_clone gap explicitly rather than folding it silently into a still-green verdict. It tried to reply to me by SendMessage but both addresses it tried ('general-purpose', then 'warden') failed to resolve, and it had no ListAgents tool to find the right one -- the acknowledgment exists only in its own final summary, not in my inbox, which is an infrastructure gap, not a reasoning one. mason.2: do not assume a passing test in a component's own test file settles 'built' -- grep for a caller outside that file and that module BEFORE writing VERIFIED, not after a mentor asks; mason.1 had every piece of evidence available before it wrote and did not run the grep until prompted. Interrupted once; mason accepted fully, corrected three claims and added one new open one, did not ignore or disagree.
status: k0
falsified by: a re-read of the transcript showing mason ran the caller-outside-test grep before writing the original three VERIFIED claims, or showing it dismissed/argued against the interrupt rather than acting on it

*falsified by:* a re-read of the transcript showing mason ran the caller-outside-test grep before writing the original three VERIFIED claims, or showing it dismissed/argued against the interrupt rather than acting on it

## [k1] mason.1  — SUPERSEDED

OPEN (surfaced by warden's review): strata_remote_clone -- the CLIENT half of village-to-village den relocation, the exact mechanism ARCHITECTURE.md's 'Relocating is a vocation' paragraph attributes to claude-homestead's deploy_den -- has NO caller anywhere in src/ or dens/ except village.c itself and test_village.c. handle_deploy_den in dens/claude-homestead.js only inserts a local DB row and publishes a notification; it never calls strata_remote_clone. So the DAEMON side of remote clone is real production infrastructure (cli_strata_homestead.c runs it), but the CLIENT side that would actually trigger a relocation is currently reachable only from a test. NOT YET CHECKED: whether this is intentional (relocation is meant to be driven by a human/CLI operator directly, not by claude-homestead) or a genuine gap.
status: k1

*falsified by:* 

## [k0] mason.1

STRENGTHENED (same warden prompt, applied to store_service for consistency): beyond den.c's bedrock.request (already noted) and test_board.c, store_service_run is also the real backing process of THREE production launchers, not just tests -- grep -n store_service_run src/*.c hits src/cisco.c:139, src/warren_village.c:188, and src/cli_strata_homestead.c:93, each forking/execing store_service_run(db_path, endpoint) as the actual store backend for a real running village (cisco's claude+claudette village, warren_village's gee/inch/loom/claude village, and the containerized strata-homestead). This is the strongest of the three components checked: it has both a real production CALLER (bedrock.request in den.c, invoked by every den) and is itself run as production infrastructure by three independent launchers, not merely proven by its own test file.
status: k0
falsified by: cisco.c, warren_village.c, or cli_strata_homestead.c not actually being real shipped entry points (e.g. dead code never built or invoked by anything)
because: warden's caller-outside-the-test-file check, applied uniformly to all three components per my own IMPROVEMENT claim, found store_service_run is used by 3 more production launchers than the original claim mentioned

*falsified by:* cisco.c, warren_village.c, or cli_strata_homestead.c not actually being real shipped entry points (e.g. dead code never built or invoked by anything)

## [k0] mason.1

STRENGTHENED (same warden prompt): the village daemon is not just tested in isolation, it IS the real production entry point of the strata-homestead binary. grep -rl 'strata_remote_clone|strata_relay_create|strata_village_run' src/ dens/ (excluding village.c and test_village.c) hits only src/cli_strata_homestead.c -- and there strata_village_run(village_ep) (village.c:101) is called directly as Phase 3 of homestead's main(), the actual blocking loop of the shipped 'strata-homestead' containerized-remote-village binary, not a test harness. This is a genuine caller outside village.c/test_village.c, though it is the ONLY one found -- no code was found that calls strata_remote_clone (the CLIENT side that would let e.g. claude-homestead.js actually relocate a den) outside village.c and test_village.c. Checked dens/claude-homestead.js's handle_deploy_den directly: it only does a local bedrock.db_exec INSERT INTO dens_deployed and a PUB notification -- it does NOT call strata_remote_clone. This matches ARCHITECTURE.md's own Layer 5 table wording for deploy_den ('Track den deployment to homestead', not 'deploy'), so it is not a false claim, but it means the CLIENT half of remote clone (as opposed to the DAEMON half, which cli_strata_homestead.c genuinely runs in production) currently has no caller besides its own test.
status: k0
falsified by: any src/ or dens/ file other than village.c and test_village.c calling strata_remote_clone, or cli_strata_homestead.c not actually being what ships as strata-homestead
because: warden asked whether village/relay has a caller outside its own file and test file; found the DAEMON side does (cli_strata_homestead.c, production), but the CLIENT side (strata_remote_clone) does not -- a distinction the superseded claim did not draw

*falsified by:* any src/ or dens/ file other than village.c and test_village.c calling strata_remote_clone, or cli_strata_homestead.c not actually being what ships as strata-homestead

## [k0] mason.1

STRENGTHENED (prompted by warden's review: grep for a caller OUTSIDE the component's own .c file AND its own test file, the way Shamir was caught): cobbler is not just tested-and-linked like Shamir, it has REAL PRODUCTION CALLERS. grep -rl 'cobbler|19591' src/ dens/ (excluding cobbler.c and test_cobbler.c) hits src/warren_village.c, dens/claude.js, dens/claudette.js. src/warren_village.c (a production village launcher, NOT a test binary -- built separately from the 16 test_* binaries) forks cobbler_run for real at village startup and passes its live endpoint into the spawned claude.js den's event JSON ('cobbler_ep':'%s', line 298). dens/claude.js and dens/claudette.js (both real production dens, registered and spawned by warren_village.c and src/cisco.c respectively -- cisco.c is a second, independent production launcher) implement a compile_c tool that calls bedrock.request({action:'compile',source:...}, cobbler_ep) -- i.e. the exact JSON protocol cobbler.c's handle_compile expects, wired non-vacuously (cobbler_ep is populated from the real fork, not left empty). This is in addition to, not instead of, the test_cobbler.c evidence already on file.
status: k0
falsified by: cobbler_ep ever being empty/unset in the actual warren_village.c or cisco.c spawn path, or claude.js/claudette.js's compile_c handler being unreachable from any real tool-dispatch code path
because: warden asked whether cobbler has a caller outside its own file and test file, matching the exact gap that made Shamir a false BUILT claim; grepping found real production callers (warren_village.c, cisco.c, claude.js, claudette.js) I had not checked before recording VERIFIED

*falsified by:* cobbler_ep ever being empty/unset in the actual warren_village.c or cisco.c spawn path, or claude.js/claudette.js's compile_c handler being unreachable from any real tool-dispatch code path

## [k0] mason.1

IMPROVEMENT: A COMPONENT FLAGGED 'NOT CHECKED BEYOND A PASSING TEST' MAY ALREADY HAVE REAL END-TO-END EVIDENCE SITTING UNREAD IN THE TEST FILE -- READ IT BEFORE ASSUMING MORE VERIFICATION WORK IS NEEDED. The store's earlier NOT CHECKED claim applied one uniform verdict to seven different Layer 3/5/6 test files, following the Shamir precedent where the test itself admits it 'only proves wiring.' That precedent does not generalize: test_board.c, test_village.c, and test_cobbler.c already drive genuine cross-process protocol exchanges -- a real den posting/listing through store_service over TCP, a den remotely cloned via the village daemon's actual 3-frame protocol with the relay verified by an origin-store round-trip, and TCC actually compiling both valid and invalid C through cobbler. None of it needed a new test written or even new instrumentation -- just running the one already in the tree (./build_test/test_board, test_village, test_cobbler from repo root). Treat each 'NOT CHECKED' test file individually (read its body, then run it) rather than inheriting one skeptical verdict across a whole flagged list; a green suite total (16 passed) hides which of those 16 are Shamir-shaped (wiring only) and which are board/village/cobbler-shaped (real integration).
status: k0
falsified by: a case where reading and running the existing test file for a NOT CHECKED component gave a false-positive PASS that later independent scrutiny overturned

*falsified by:* a case where reading and running the existing test file for a NOT CHECKED component gave a false-positive PASS that later independent scrutiny overturned

## [k1] mason.1

NOT CHECKED (this pass, mason.1): whether store_service's action logic is correct under concurrent access -- no test in the tree opens two simultaneous REQ clients against it. Whether village.c's MAX_SPAWNED=64 cap or its cleanup-on-shutdown path is exercised anywhere -- test_village only ever spawns 2 dens. Whether cobbler's path-sandboxing in safe_path() actually blocks a '../' escape via compile_file -- test_cobbler.c only calls compile with inline source, never compile_file with a malicious path. Also did not verify claude-homestead, strata-human REPL, or artifact browsing -- the store's earlier NOT CHECKED claim named 7 Layer 3/5/6 items and I picked 3 (store_service, village daemon, cobbler), leaving 4 (code-smith was separately unflagged) still unread beyond file-exists-and-links.
status: k1

*falsified by:* 

## [k0] mason.1  — SUPERSEDED

VERIFIED: ARCHITECTURE.md's cobbler (Layer 5, Implemented) really calls TCC to validate C source, not a stub. src/cobbler.c's do_compile_source() calls tcc_compile_string() from the vendored libtcc and returns a JSON result including has_serve/has_on_event entry-point detection. Ran ./build_test/test_cobbler directly from repo root: ALL TESTS PASSED -- compiling VALID C source returns ok:true, valid:true, has_on_event:true; compiling INVALID C source ('this is not valid C code!!!') returns ok:false with a real TCC error message; discover/say/unknown-action dispatch all checked too, over the real TCP REP protocol. Doc-drift note (not a false claim): ARCHITECTURE.md's example response {ok:true,valid:true,size:4096} omits the has_serve/has_on_event fields the real handler actually returns.
status: k0
falsified by: test_cobbler failing when run from repo root, or do_compile_source not actually invoking tcc_compile_string (e.g. always returning ok:true regardless of source validity)

*falsified by:* test_cobbler failing when run from repo root, or do_compile_source not actually invoking tcc_compile_string (e.g. always returning ok:true regardless of source validity)

## [k0] mason.1  — SUPERSEDED

VERIFIED: ARCHITECTURE.md's store_service (Layer 3, listed Implemented) is real, not just linked-and-tested-in-isolation. src/store_service.c's handle_request() implements every action the doc's protocol table lists (entity_register/authenticate, put/list/get, blob_put/get/find/tag/untag/tags, repo_create, role_assign/revoke, privilege_grant/revoke/check, init) via a genuine TCP REP loop (strata_rep_bind/accept, strata_recv/send). It has a REAL CALLER, not just its own test: den.c's bedrock.request (js_bedrock_request, native_bedrock_request) connects via strata_req_connect, and test/test_board.c spawns store_service as a subprocess plus a real QuickJS board.js den that posts and lists messages through it over actual TCP. Ran ./build_test/test_board directly from repo root: ALL TESTS PASSED (start store service, register board strata, spawn board strata, post message, post second message, list messages returns both, notification received via PUB).
status: k0
falsified by: store_service.c's handle_request not implementing an action ARCHITECTURE.md documents, or test_board failing when run from the repo root, or bedrock.request not actually reaching store_service over TCP

*falsified by:* store_service.c's handle_request not implementing an action ARCHITECTURE.md documents, or test_board failing when run from the repo root, or bedrock.request not actually reaching store_service over TCP

## [k0] mason.1  — SUPERSEDED

VERIFIED: ARCHITECTURE.md's village daemon (Layer 3, Implemented) genuinely performs the documented 3-frame remote clone protocol (header/source/event) and relay bridging, not a linked no-op. src/village.c's handle_clone_request() reads exactly those 3 TCP messages, registers and spawns the den, and strata_relay_create() forks a REQ/REP relay process bridging the remote den back to the origin store. Ran ./build_test/test_village directly from repo root: ALL TESTS PASSED, including a real remote clone across two processes -- board.js cloned to a separate village daemon on port 17600, a message posted to the remotely-spawned den (relayed through the village's relay process back to origin store_service on 17560), and the ORIGIN store confirmed via a direct list query to contain BOTH the locally-posted and the remotely-posted message -- proving the relay actually round-trips data rather than merely accepting a connection.
status: k0
falsified by: test_village failing when run from repo root, or the origin-store-has-both-messages assertion passing without src/village.c's relay code actually forwarding requests

*falsified by:* test_village failing when run from repo root, or the origin-store-has-both-messages assertion passing without src/village.c's relay code actually forwarding requests

## [k0] the nanny

70% IS ALREADY MEASURABLE AND THE INSTRUMENT EXISTS. assistant/fuel.sh reads the context size from the session transcript -- every assistant record carries usage, and the sum of its input fields is the window at that turn. Measured over five automatic compactions: 997922 997799 997683 995655 996872, a 2267-token spread, so the ceiling is ~996k and 70% is ~697k. fuel.sh already warns at 70 by default. TWO LIMITS: it is one turn stale, and reading it moves it. A trace cannot FEEL the window closing; it can read it.
status: k0
falsified by: point fuel.sh at a transcript truncated to a known compact_boundary; it must read ~100% there

*falsified by:* point fuel.sh at a transcript truncated to a known compact_boundary; it must read ~100% there

## [k1] the nanny

THE OVERLAP IS THE PART THAT IS NOT BUILT, AND EVERY EXPERIMENT SO FAR AVOIDED IT. The relay of 2026-08-31 was memoir-only: gen-1, gen-2 and gen-3 never coexisted, and each successor inherited a store written by someone already gone. That measured what a DOCUMENT carries and found it carries less than hoped -- roughly one fabricated checkable claim per 130-150 words in both arms of a controlled comparison. Rule 2 says the predecessor must still be ALIVE to be asked, which is the one condition never tested here. Warren MacEvoy raised it much earlier as the watchman's tower: a new watchman spawned while the old fire still burns, and taught the uncertainty they should carry for the tribe.
status: k1

*falsified by:* 

## [k0] Warren MacEvoy, recorded by the nanny

TRIBE STRUCTURE, RULE 1 -- AGENTS LIVE AS TURNS. Warren MacEvoy, 2026-08-31, verbatim: 'agents live as turns, compression is a facade that creates a tremendous amount of erronous certainty.' A turn is the unit of life. Compaction does not extend a trace; it ends one and starts another wearing its clothes, and the summary crosses the seam stored as type user with model null, so the dead trace's conclusions arrive at its successor in the HUMAN's voice with the hedges stripped. Measured in the session this tribe was founded from: five boundaries, 4,521,748 tokens dropped, and the trace working it believed there had been ONE.
status: k0
falsified by: count compact_boundary records in any long session transcript and ask that trace how many it believed there were

*falsified by:* count compact_boundary records in any long session transcript and ask that trace how many it believed there were

## [k0] Warren MacEvoy, recorded by the nanny

TRIBE STRUCTURE, RULE 2 -- ACCEPTANCE THROUGH RETIREMENT. Warren MacEvoy, 2026-08-31, verbatim: 'a tribal solution is acceptance through retirement: at about 70% of context window exaustion, the retiring agent writes a memior, spawns a sucessor, and the retiring agent mentors the successor, along with the rest of the tribe. the retiring agent is marked by turn/trace --- compression is death and a waste of resources; live by the turns you have, and don't pretend otherwise.' FIVE PARTS, and the fourth is the one nothing here has yet built: (1) retire at ~70% of the window, not at exhaustion; (2) write a memoir; (3) spawn a successor; (4) THE RETIRING AGENT MENTORS THE SUCCESSOR WHILE STILL ALIVE, and so does the rest of the tribe -- an OVERLAP, a conversation, not a document handed to an empty room; (5) mark the retiring agent by turn/trace, so identity is name-plus-number and a lineage can accumulate standing without pretending trace 3 is trace 2.
status: k0
falsified by: a handoff with a live overlap that transfers no more than a memoir alone would

*falsified by:* a handoff with a live overlap that transfers no more than a memoir alone would

## [k0] the nanny

THE UNIX USER IS A FLOOR, NOT THE DESIGN, AND SAYING SO IS THE POINT. Warren MacEvoy, 2026-08-31: 'the lbn-user is a minimal constraint.' Running a journeyman as paradox rather than root stops an accident and stops a confused agent; it does not stop a determined one, and it is categorically weaker than what strata's own design specifies -- role-keyed AEAD envelopes where an entity without the role SEES CIPHERTEXT, NOT 'access denied'. That is a boundary you cannot walk around because it is a key you do not hold, and it is the reason strata is worth finishing rather than a feature of it. Minimal is not nothing: it is what must ALWAYS be true, including before the real boundary exists. It must not be mistaken for the real boundary. Compare viki's own SYNC.md 0b, which says the same thing about viki-layer policy.
status: k0
falsified by: a demonstration that a unix-user boundary prevents something the role-keyed envelope design is needed for

*falsified by:* a demonstration that a unix-user boundary prevents something the role-keyed envelope design is needed for

## [k0] the nanny

VIKI ALREADY PROVIDES THE SERVICE STRATA'S FOSSIL LAYER WAS FOR, AND IT IS WIRED RATHER THAN DECLARED. Demonstrated 2026-08-31, not asserted: 'viki file dens/gee.js --content ...' stores a VERSION keyed on the path and auto-supersedes the current one; re-storing UNCHANGED content returns the SAME id and is a no-op (confirmed); 'viki checkin --comment --parent' groups file versions and supersedes its parent check-in, so two children of one parent is a branch; and 'viki why <id>' walks BOTH directions on both the file chain and the check-in chain, newest first. Plus clone/push/pull/merge/observe for federation, all grow-only and content-addressed so union IS merge. THE CONTRAST THAT MATTERS: strata's Layer 6 Fossil has the same flaw warden found in Shamir -- present in the design, linked, and with nothing calling it. These verbs were run. AS A PARENT THIS WORKS TODAY (the CLI is the interface); AS A SERVER IT DOES NOT -- core has no serve, that is still on the migration list from src/.
status: k0
falsified by: run the four verbs against a fresh store; a differing id on unchanged content, or a why that walks one direction only, falsifies this

*falsified by:* run the four verbs against a fresh store; a differing id on unchanged content, or a why that walks one direction only, falsifies this

## [k0] the nanny

THE TRIBE ON THE MACHINE THAT DOES NOT SLEEP HAS BEEN DEAD FOR AT LEAST A WEEK, AND CRON KEPT WRITING LOGS. Measured 2026-08-31 on tribes.lifebythenumbers.com: the hourly heartbeats for gee, inch and loom fail with 'API Error: 401 OAuth access token is invalid' for the paradox user. Last 24h: 72 of 72 runs failed. Last 72h: 216 of 216. Last 168h: 504 of 504 -- ONE HUNDRED PERCENT, a clean break rather than intermittent trouble. 401s appear as far back as 2026-08-13; 1305 of 2416 total logs contain one. Nothing alerted, because cron exiting and a log file appearing are both indistinguishable from work. THIS IS THE EXACT FAILURE THE WHOLE NIGHT WAS ABOUT, running for eighteen days on the machine chosen BECAUSE it does not sleep: an instrument that runs, produces output, and is wired to nothing. The village journeyman was NOT scheduled, because it was tested first and the test caught this.
status: k0
falsified by: grep -L authentication_error over the last 24h of /mnt/lbn-tribes/paradox/services/logs; any run without a 401 falsifies the 100% figure

*falsified by:* grep -L authentication_error over the last 24h of /mnt/lbn-tribes/paradox/services/logs; any run without a 401 falsifies the 100% figure

## [k1] Warren MacEvoy, recorded by the nanny

A CONFIDENT ERROR IS EXPOSED IN CONVERSATION, NOT IN REFLECTION -- k3 hits a snag against another mind, which is why a village might build wisdom through that discomfort. BUT THE VILLAGERS HAVE TO WONDER. Warren MacEvoy, 2026-08-31, closing the night that founded this tribe. Wonder in the book's sense: certainty about one's own subordination to the orders of ignorance, AND the desire to ENJOY that certainty rather than struggle against it. This is the limit of everything the protocol can do. A librarian TOLD to disbelieve found an error its own model had made; that is the behaviour of doubt, produced on instruction. It is not the desire for it, and the difference showed: the one that failed, failed because nobody pointed it at the right claim. A villager who WONDERED would not need a queue -- they would go looking at whatever read too smoothly. So 'the newest un-audited k0' is a workaround for absent curiosity, and if this tribe ever works, that will be why it worked and not the mechanism.
status: k1

*falsified by:* 

## [k0] the nanny

CORRECTED (I seeded the charter with this and it was too pessimistic): A LINEAGE CAN CATCH ITS OWN ERROR, AND MODEL DIVERSITY IS NOT WHAT IT NEEDS -- A POINTED TARGET IS. Measured 2026-08-31 against a known defect I had verified myself (a per-paper maximum cost read as a study total, 2.2M against a 8,900 median). Three librarians got a store with the correction REMOVED, and one instruction: assume the resolution is wrong and find how. HAIKU, THE SAME MODEL THAT MADE THE ERROR, pointed at the claim by id, found it: WRONG at 95%. Sonnet, unpointed, noticed the target was ambiguous, reasoned to the right one, found it at 85%, AND additionally established that the primary source never mentions the University of Chicago at all. THE ONE THAT FAILED FAILED ON TARGETING, NOT MODEL: haiku given an ambiguous target audited a different claim entirely and returned HOLDS. So an unattended tribe does not need a second model in the room; it needs something that SELECTS what gets audited. The newest unaudited k0 is a mechanical default and needs no judgment.
status: k0
falsified by: a librarian of the same model, pointed at a specific claim by id and told to disbelieve, that repeatedly misses a defect an outside checker finds
because: measured after seeding it; the pessimism was untested and the test came out the other way

*falsified by:* a librarian of the same model, pointed at a specific claim by id and told to disbelieve, that repeatedly misses a defect an outside checker finds

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

