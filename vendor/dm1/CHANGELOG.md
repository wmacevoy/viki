# Changelog

What changed between releases, by principle ID, newest first. A change you cannot spot is a K3
factory ([SE1/U7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u7)),
so changes are listed here by ID, and copies of AGENTS.md and the templates say which release they
came from.

Within DM1, a minor release may add principles, reword them, or extend what they ask; it never moves
an ID ([principle index](README.md#principle-index)). Work toward a release goes under
`## vX.Y — unreleased`; give it a date when you tag it. `tools/check.py` requires copies to link to
the newest dated release. Links to SE1 never name
`main`: they name the SE1 release this guide was last reviewed against, which each release below
states.

**If you keep a copy** of AGENTS.md or the templates, read *For copies* in each release newer than
yours.

## v1.3 — 2026-11-05

[Full diff from v1.2.1](https://github.com/wmacevoy/data-mining/compare/v1.2.1...v1.3) · 101 principles

**For copies:** `AGENTS.md`'s *Observe, do not ask* now says to trust a record of what was done over an account of it.

### Changed principles

- [DM1/U12](guide/02-unknowing.md#u12): reframed as trusting data about observations over data about reflection. Asking what someone would prefer is
  far less useful than observing what they choose, and asking why is a K3 factory for both parties. A stated preference or a self-report
  is labeled as what people say, with the observation that would check it. The owner's word, 2026-11-05.

## v1.2.1 — 2026-11-05

[Full diff from v1.2](https://github.com/wmacevoy/data-mining/compare/v1.2...v1.2.1). No principle, AGENTS.md, or template
changed; copies at v1.2 are current.

### Also changed

- The README's principle index escapes a `|` in a title, which the web guidebook rendered as a table at DM1/U8.
  `tools/check.py` writes the index that way, and a test holds it to it.

## v1.2 — 2026-11-05

[Full diff from v1.1](https://github.com/wmacevoy/data-mining/compare/v1.1...v1.2) · 101 principles

Reviewed against **SE1 v1.7**. No ID moved.

**For copies:** re-copy `AGENTS.md`, which now extends SE1 v1.7, and SE1's `AGENTS.md` and `se.py`, as
[ADOPT.md](ADOPT.md) now says, and add SE1's `QUICKREF.md`, imported from the root `CLAUDE.md`: `se --doctor` fails a
repository that does not, and each agent that loads it starts about 5,000 words heavier. The QUESTION template's *Shape* section is now *Fundamental shape*, with the same
`PIPE-` IDs; a question you already have can keep its heading until its next version.

### New principles

- [DM1/U12](guide/02-unknowing.md#u12): a set cannot contain itself, so complete self-awareness is not possible, and believing you have it is K3. Observe what
  was done, which is worth far more than asking why. The owner's word, 2026-11-05, with one trial behind its example.
  AGENTS.md's *Advising* takes the rule.

### Also changed

- [DM1/D9](guide/05-design.md#d9) no longer restates SE1/D9, and chapter 5's opening no longer says its ideas carry over unchanged. Wording only; the owner's word, 2026-11-04.
- `tools/check.py` and CI read SE1 from `vendor/se1`, and the checker holds that tree to `SE_RELEASE` and the copies here to it. Contributors no longer need a clone.
- Requirements v2 and v3, confirmed by the owner, 2026-11-04 and 2026-11-05: the mission, the fundamental shape, and that this guide iterates to completion and adapts SE1's fundamental shape.
- AGENTS.md and the README say what a synthetic trial has and has not tested.
- The guide now has a [web guidebook](https://wmacevoy.github.io/data-mining/), a rendering of the Markdown, which stays the source. The owner's request, 2026-11-05.
- The DM1 quick reference was tried and not adopted: [why](https://github.com/wmacevoy/data-mining-self-improvement). The owner's decision, 2026-11-05.

### SE1 v1.6 to v1.7, reviewed

SE1 v1.7 changes no principle's meaning. Reviewed 2026-11-05 by Claude Opus 5.5; Warren MacEvoy signed this review with the release (SE1/U9).

- SE1's [QUICKREF.md](https://github.com/wmacevoy/software-engineering/blob/v1.7/QUICKREF.md), one line per principle, about 5,000 words: adopted. `CLAUDE.md` imports
  it from `vendor/se1/`, and [ADOPT.md](ADOPT.md) step 2 copies it to an adopter's root.
- SE1's ADOPT.md vendors its guide at `vendor/se1`, the project's choice: adopted here, the owner's decision of 2026-11-05, and
  wider than SE1 suggests: all of SE1 at the tag, not only what a reader looks things up in. It replaces the clone: `tools/check.py` and
  CI read SE1 from there, and the checker holds it and the copies to `SE_RELEASE`. [ADOPT.md](ADOPT.md) leaves vendoring to each project.
- SE1's headings now carry their citations: adopted, the owner's word, 2026-11-05. Every principle's heading here reads
  `DM1/D5.`, so a grep for a citation reaches its definition. Anchors and links are unchanged, and no principle's meaning moved.
- SE1's Quick lines: not adopted. This guide has no quick reference of its own.

### SE1 v1.5 to v1.6, reviewed

Each SE1 principle that changed between v1.5 and v1.6, and what this guide did about it. Reviewed 2026-11-05 by Claude
Opus 5.5; Warren MacEvoy signed this review with the release (SE1/U9).

- [SE1/A12](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a12), work toward finished, not toward more: declined as a new principle. It applies to data work
  unchanged, so it stays off the map, and [REQ2/SHAPE-3](docs/REQUIREMENTS_v2.md) holds this guide to it.
- [SE1/D9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d9), a fundamental shape iterates toward finished, and unstated, assume it does: adopted into the QUESTION
  template, in the sentence SE1's template uses. [DM1/D9](guide/05-design.md#d9) points at SE1/D9 for this and is unchanged.
- [SE1/D5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d5), time zones and what a day is: confirmed, and [DM1/D5](guide/05-design.md#d5)'s *Reframes* line now
  holds. It no longer rests on a gap. DM1/D5 still says UTC inside and local at the edges, which SE1/D5 now says too. Kept, the owner's word, 2026-11-05: a quick
  read is not quick if it includes "and also read this."
- SE1's AGENTS.md says it is guidelines, not law: confirmed, no change. This guide's agreement extends it, and repeats
  neither that nor A12's two lines.
- Not done: the *Foundation* line of SE1's requirements template, which pins a release for `se --doctor`, is not in
  the QUESTION template or in this guide's requirements. Without it, any newer SE1 release warns.
- Not reviewed: whether the reframings of SE1 principles v1.6 left unchanged still read true, as before.

### SE1 v1.4 to v1.5, reviewed

Each SE1 principle that changed between v1.4 and v1.5 (v1.4.3 and v1.4.4 are patches and change none), and what this
guide did about it. Reviewed 2026-11-04 by Claude Opus 5.5; Warren MacEvoy signed this review with the release (SE1/U9).

- [SE1/D9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d9), the *fundamental shape*, an aesthetic that decides what belongs: adopted into
  [DM1/D9](guide/05-design.md#d9), GLOSSARY, CHECKLISTS, and the QUESTION template. Plain *shape* stays plain English
  here, as it does in SE1.
- [SE1/W14](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w14), moving a lower layer can move what is built on it: declined as a new principle.
  It applies to data work unchanged, so it stays off the map. [DM1/W2](guide/08-work.md#w2) and AGENTS.md's stop
  and ask already name what moving the question invalidates.
- [SE1/A5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a5), spend the speed on understanding and changing later too: confirmed, no change.
  [DM1/A5](guide/10-agents.md#a5) is about checks, which SE1/A5 still asks for.
- SE1/F4 gained only a link to W14, and this guide's DM1/F4 is new and does not reframe it: confirmed, no change.
- SE1's AGENTS.md says *earlier* and *later* in its order of authority: confirmed, no change. This guide's agreement
  says only that the order holds.
- Not done: SE1's AGENTS.md now gives the PowerShell form of `se`; this guide's does not yet give `dm.ps1`'s.
- Not reviewed: whether the reframings of SE1 principles v1.5 left unchanged still read true, as before.

## v1.1 — 2026-11-03

· 100 principles

Warren MacEvoy's review of [DM1/U11](guide/02-unknowing.md#u11) and [DM1/T13](guide/06-testing.md#t13), 2026-11-03.
No ID moved.

**For copies:** `AGENTS.md`'s two rules on verdicts and experiments changed; `templates/LEDGER.md` gained an
*Environment* column.

### Changed principles

- [DM1/U11](guide/02-unknowing.md#u11): a judge may approve a retrain policy in advance, and a retrain inside its
  limits keeps the verdict and still gets its checks ([DM1/O6](guide/07-operating.md#o6)). What SE1/U10 already says
  is cited and not restated. "Nearly every time" is now "often", since nothing supported it.
- [DM1/T13](guide/06-testing.md#t13): a run is anything whose result someone looked at or used. A limit on keeping
  data about people or a promise of confidentiality comes before "keep everything"
  ([DM1/R9](guide/04-requirements.md#r9), [DM1/A10](guide/10-agents.md#a10)). The cheapest row records the environment,
  as [DM1/O1](guide/07-operating.md#o1) asks. "One run at full size, early" is cut, since it contradicted starting
  small. The attribution now separates the owner's model from what the agent drafted.

## v1.0.1 — 2026-11-03

No principle, AGENTS.md, or template changed; copies at v1.0 are current.

### Also changed

- `se.py` is re-copied from SE1 v1.4.4. v1.4.3 needs no `du`, reads the battery on Windows, and advises by
  platform; v1.4.4 leaves a top-level `vendor/` out of its doctor's citations, tests, and verdicts. SE1 links
  stay at v1.4: neither changed a principle or SE1's AGENTS.md. `ADOPT.md` copies `se`, `se.ps1`, and `se.py`
  from v1.4.4.

## v1.0 — 2026-11-03

· 100 principles

The first release: 78 principles that reframe SE1 principles for data work, and 22 new ones that
SE1 has no counterpart for. Refactored from the course document `DATA-SCIENCE.md`, reorganized into
SE1's ten chapters. Bias is treated as SE1 treats security: a permanent requirement
([DM1/R10](guide/04-requirements.md#r10)), an audit before and after deploy
([DM1/O9](guide/07-operating.md#o9)), and items in the checklists, AGENTS.md, and the templates.

Written on 2026-10-27 against SE1 at commit `d256d10`, between SE1 v1.1 and v1.2, with every SE1
link naming `main`. On 2026-10-28 the links were pinned to **SE1 v1.3**: `tools/check.js` builds
every SE1 link from one constant, `SE_RELEASE`, and CI checks SE1 IDs against that tag. A later SE1
bump is that one edit, a rewrite of the links, and a walk through SE1's CHANGELOG for every ID this
guide reframes, recorded here by ID. On 2026-11-03 the links moved to **SE1 v1.4**.

### SE1 v1.3 to v1.4, reviewed

Each SE1 principle that changed between v1.3 and v1.4 (v1.3.1, v1.3.2, v1.4.1, and v1.4.2 are patches
and change none), and what this guide did about it. Reviewed 2026-11-03 by Claude Opus 5.5 with Warren
MacEvoy, who signs the release (SE1/U9); every wording below is a draft until then.

- [SE1/U10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u10), *essential K1*: reframed as a new [DM1/U11](guide/02-unknowing.md#u11). Data work sharpens it,
  because the work product is a number that reads like the answer. The bar becomes a veto, and whether
  the result is worth acting on goes to a named judge. GLOSSARY gains *essential K1* and *judged*.
- [SE1/U2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u2), some K1 will not convert: adopted into the K1 row of [DM1/U1](guide/02-unknowing.md#u1).
- [SE1/R2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r2), where what matters is a judgment, write down who judges it: adopted into
  [DM1/R3](guide/04-requirements.md#r3).
- [SE1/R10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r10), a change in meaning is a new version and a spelling or lint fix is an edit named in the
  commit: adopted into the QUESTION template. The guide had said it only there.
- [SE1/D9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d9), coherence answers to a written mission and a shape: adopted into
  [DM1/D9](guide/05-design.md#d9), where the mission is the decision the work serves. The QUESTION
  template gains a *(judged)* `DECISION-1` and a *Shape* section (named apart from the requirements' IDs: where a project keeps both documents, SE1's doctor reads a shared ID in a question as a bare citation of its requirements), CHECKLISTS a line, GLOSSARY both words.
- [SE1/W11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w11), *(judged)* requirements and verdicts, and a change naming what it must not break: adopted into
  [DM1/W7](guide/08-work.md#w7), AGENTS.md's *Verify*, and CHECKLISTS.
- [SE1/W13](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w13), a finding closes with what it changed: reframed by [DM1/W6](guide/08-work.md#w6), and
  AGENTS.md's *Finishing*.
- SE1's change of *shape* to *contract* or *interface* where it meant what one part promises another
  (SE1/H4, SE1/O7, SE1/W5): confirmed, no change. SE1 keeps *shape* in plain English elsewhere (the team's
  shape, the shape of the work), and so do the places this guide uses it.
- Not done: `dm --doctor` does not yet read *(judged)* in a question or report a verdict, as SE1's
  trace row does in requirements (queue #4).
- Not reviewed: whether the reframings of SE1 principles v1.4 left unchanged still read true, as before.
- [ADOPT.md](ADOPT.md) is new: vendoring both guides, for a project starting today or one with results, and
  what an agent does where adoption is partial. It protects the evidence first, since a question rebuilt
  from results is chosen by them. Its vendoring commands ran on a copy of the course repository; the
  steps after that have not been followed by any project.
- `dm --doctor`, four false passes found while trying that, each seen red first: a holdout line still the
  template's read as a pass with no text; a datasheet with zero columns read as every column saying when it
  is knowable; the plan template's *Holdout* line read as filled in, now marked as a placeholder; and
  `dm.py`, vendored, read as the ledger's writer because its source names the file.
- `tools/check.py` replaces `tools/check.js`, so contributing needs Python 3.9 or later and git, and not Node:
  the owner's decision, 2026-11-03, as SE1 did at v1.4. Before the JavaScript was removed, the two ran on 352
  cases, 52 planted and 300 seeded random edits, 67 of them red, and agreed on 351. The one difference is a
  fix: a CHANGELOG with no release heading crashed the JavaScript, and the Python reports it.
  `tools/test_check.py` is new, a breakage per rule in a copy of this repository. Earlier entries below that
  name `tools/check.js` are kept as written.
- [DM1/R12](guide/04-requirements.md#r12), [DM1/U10](guide/02-unknowing.md#u10), [DM1/F10](guide/03-feedback.md#f10),
  [DM1/T12](guide/06-testing.md#t12), and [DM1/A10](guide/10-agents.md#a10), which came from the first field report against
  v1.3, merged here 2026-11-03: confirmed at v1.4. SE1's [SE1/T3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t3)
  and [SE1/T8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t8) changed only their markers. Two IDs were written
  as U10 at once, neither released: the field report's keeps it, since it came first and rests on a project,
  and this review's is [DM1/U11](guide/02-unknowing.md#u11). The owner's pin of 2026-11-02, v1.3, was replaced on 2026-11-03 by v1.4;
  the owner tagged v1.0 the same day. ADOPT.md vendors once at the root. `REVIEW-v1.md` is removed, now that it is incorporated; it had been redacted for confidentiality before it was pushed ([GAPS.md](GAPS.md)).
- [DM1/T13](guide/06-testing.md#t13) is new, Warren MacEvoy's decision of 2026-11-03: a run is one experiment on one question, tested
  small before large, with evidence kept in proportion to the cost of running it again. *Project* stops being a term here: it hid
  the repository, the question, and the experiment, and "one run is one project", recorded as the owner's on 2026-11-02, is
  withdrawn, since the owner did not remember it. It stays as plain English in the field reports and for the course's projects. `--repo` takes
  a directory that holds a question. `LEDGER` gains *Would count against it* and *Cost to run again*. W3, O10, CHECKLISTS, AGENTS.md, and ADOPT.md follow.
- This repository now follows SE1 as a project, not only by citation (ADOPT.md): SE1's agreement copied
  unedited to `docs/SE1-AGENTS.md`, since the root `AGENTS.md` here is the file this guide publishes;
  `se`, `se.ps1`, and `se.py` copied from v1.4; both agreements imported from a new `CLAUDE.md`; the
  self test as a `SessionStart` hook in `.claude/settings.json`; `CONTRIBUTING.md` and a working set in
  `docs/`, reconstructed and marked *(inferred)*. the checker leaves `HANDOFF-*` notes alone: a
  finished note keeps the links it was written with.

### SE1 v1.2 to v1.3, reviewed

Each SE1 principle that changed between the commit this guide was written against and v1.3, and
what this guide did about it. Reviewed 2026-10-28 by Claude Fable 5.1 with Warren MacEvoy, who
signs the release (SE1/U9).

- [SE1/U7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u7) and
  [SE1/R8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r8), *medicine* for
  *cure*: adopted. The guide said *cure* in [DM1/U2](guide/02-unknowing.md#u2),
  [DM1/U6](guide/02-unknowing.md#u6), and [DM1/D9](guide/05-design.md#d9). A K3 factory is
  lessened, not fixed, and so is train/serve skew.
- [SE1/U7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u7), *torn work*: adopted
  into [DM1/U2](guide/02-unknowing.md#u2), which names its data shapes (a pipeline half-run, a
  model trained on a partial extract, a notebook run out of order) and the medicine (idempotent
  stages, the cold run from raw, and treating what you find on resuming as torn), and in one
  sentence into [DM1/U6](guide/02-unknowing.md#u6): an interrupted retrain's checkpoint answers.
- [SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a11), *knowing is not doing*:
  adopted into [DM1/U7](guide/02-unknowing.md#u7), where *look at the rows* becomes something the
  command does on every run, and [DM1/A2](guide/10-agents.md#a2): a leakage check the agent must
  choose to run will be read and not run, so the assertions go in the pipeline and CI. A11's
  other change, that the import boundary is the working directory, is in AGENTS.md.
- [SE1/U8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u8), *leave whitespace*:
  reframed as [DM1/U9](guide/02-unknowing.md#u9). It pulls against [DM1/F4](guide/03-feedback.md#f4)
  to [DM1/F6](guide/03-feedback.md#f6), where every look spends evidence; the resolution is what
  is looked at. Whitespace on the inputs is free and is where K2 lives; on the outcome it is a
  look, and goes in the ledger. GLOSSARY gains *whitespace*.
- [SE1/H4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h4), the stand-up's fourth
  question: reframed as [DM1/H8](guide/09-people.md#h8). The discomfort in data work has a short
  list of shapes, each an early sign of something this guide names, so the question becomes *what
  about the current number do you not believe?*
- [SE1/U9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u9), *rules change*: not a
  principle here. For labels, [DM1/R7](guide/04-requirements.md#r7) already asks who, when, and how
  often a second person disagrees; for definitions, [DM1/H5](guide/09-people.md#h5) asks who chose
  it. What the templates lacked was the falsifier: DATASHEET and MODEL_CARD now say who wrote them,
  when, and what would show them wrong.
- [SE1/A3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a3), an agent may ask, and a
  private agent memory is a head: confirmed, no change. This guide never said the client does not
  talk to the agent, and [DM1/A3](guide/10-agents.md#a3) already keeps the record of what was
  tried in the ledger, not in the agent's memory.
- [SE1/W7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w7), Brown's experiment as he ran
  it: confirmed, no change. [DM1/W5](guide/08-work.md#w5) cites the gradient and does not restate
  the experiment.
- [SE1/W2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w2) and
  [SE1/T5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t5), wording only: no DM1
  principle quotes the changed words.
- Not reviewed: whether the 73 reframings written against the earlier commit still read true
  against v1.3's unchanged principles. Their IDs resolve, which is all `tools/check.js` can say.
- AGENTS.md, after SE1 v1.2's: says who drafted it and when, and that nothing in it has been
  tried; loads from copies inside the working directory, with a check that they loaded; a new
  section *After a gap, additions* (the ledger, not the summary, counts looks; derived data found
  after a gap is torn; a kernel is a summary). CHECKLISTS' *Handing an agent data work* aligns
  with SE1's *Handing a task to an agent*: loaded not opened, checks in the pipeline, the retro
  questions. The `./se` line waits until `se` knows this guide's documents.
- `tools/check.js`, after SE1 v1.2.1 to v1.3's: SE1 IDs and anchors resolve at the pinned tag,
  read from the local clone through git, and a clone without the tag is a named problem; copies
  cite no DM1 ID their labeled release lacks; a patch release (`v1.0.1`) changes no principle,
  AGENTS.md, or template, and a principle's text ends at a horizontal rule. Seen red on three
  planted breakages. *Changed in* markers wait for a release to mark against.
- README gains *Why to trust this, and why to doubt it*: SE1/U9 applied to the guide on its front
  page. CI checks out SE1 at the pinned tag and this repository with its history, for the checks
  that read tags.
- `dm`, `dm.ps1`, `dm.py`: a command-line companion for a data project, beside SE1's `se.py`.
  `./dm --doctor` runs SE1's doctor and adds eleven rows this guide asks to be mechanical: both
  working agreements loaded, the question's one-way doors, the plan, the holdout's home and reach,
  every column's knowable-at, raw read-only, raw to report, the ledger and its writer, the model
  card and its bias audit, the environment, and citations of the question. Every row seen red on a
  fixture built to fail it (`tools/test_dm.py`); every threshold invented (GAPS 7). CI runs the checker and the tests on Linux, macOS, and Windows, on Python 3.9
  and the newest; `.gitattributes` keeps line endings LF. MODEL_CARD's bias row cites `Q1/BIAS-1`,
  versioned, as the question template asks (SE1/R10).
- The two copies have names: SE1's stays `AGENTS.md` at the root, where SE1's doctor and Claude
  Code's own read look for it, and this guide's is `AGENTS-DM.md` beside it, both imported from
  `CLAUDE.md`. The doctor's *extends* row compares the SE1 release this guide's copy was written
  against with the SE1 copy present, and says when what lies between them has not been reviewed.
  The release check for this guide's own copy waits for the v1.0 tag.

**For copies:** copy this guide's AGENTS.md as `AGENTS-DM.md` beside SE1's `AGENTS.md` at v1.4, inside the
working directory; import both from `CLAUDE.md` at every start, and check that they loaded (SE1/A11).
Add a Mission and a Shape at your next question version.
A copy made before 2026-11-01, such as the course repository's, re-copies AGENTS.md and `dm.py`; its
question gains KIND-1, and `CONTRIBUTING.md` *Main product*. Without them the doctor warns, and does not fail.

### The first field reports, applied

Written 2026-11-01 by Claude Opus 5.5, from `REVIEW-v1.md` and the course's `FINDINGS.md`, both by way of
[GAPS.md](GAPS.md).

- **Five new principles:** [DM1/R12](guide/04-requirements.md#r12), the kind of claim;
  [DM1/U10](guide/02-unknowing.md#u10), "none found"; [DM1/F10](guide/03-feedback.md#f10), what a summary drops;
  [DM1/T12](guide/06-testing.md#t12), a full reading as the baseline; [DM1/A10](guide/10-agents.md#a10), when reading the
  data is the task. They come from one project, so each is a lesson with one data point. The reframings of
  [SE1/T3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t3) and
  [SE1/T8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t8) were read against SE1's text at
  v1.3; the second runs through [DM1/T1](guide/06-testing.md#t1).
- **Extended:** [DM1/P5](guide/01-purpose.md#p5), [DM1/U7](guide/02-unknowing.md#u7), [DM1/F5](guide/03-feedback.md#f5),
  [DM1/F6](guide/03-feedback.md#f6), [DM1/F9](guide/03-feedback.md#f9), [DM1/R2](guide/04-requirements.md#r2),
  [DM1/R3](guide/04-requirements.md#r3), [DM1/R8](guide/04-requirements.md#r8), [DM1/R10](guide/04-requirements.md#r10),
  [DM1/D6](guide/05-design.md#d6), [DM1/D13](guide/05-design.md#d13), [DM1/T1](guide/06-testing.md#t1),
  [DM1/T3](guide/06-testing.md#t3), [DM1/O2](guide/07-operating.md#o2), [DM1/O8](guide/07-operating.md#o8),
  [DM1/W5](guide/08-work.md#w5), [DM1/W7](guide/08-work.md#w7), [DM1/A4](guide/10-agents.md#a4),
  [DM1/A6](guide/10-agents.md#a6), [DM1/A8](guide/10-agents.md#a8), [DM1/A9](guide/10-agents.md#a9). No ID moved.
- **The doctor:** KIND-n and *n/a* with a required reason, which never quiets a failure; `raw to report` and `data working
  agreement` say what they did not do; a project names what counts as raw and which logs are its ledger; a command on a line
  is not a path; one run is one project, with `--repo <project>`, and the agreements are found upward as far as the repository's
  root. `QUESTION`, `PLAN`, `DATASHEET`, `LEDGER`, and `CONTRIBUTING` follow.
- **Still unproven, by chapter:** of the 93, those that fit by argument only or didn't reach the first report, from its verdict
  table. 1 Purpose 2 of 5; 2 Unknowing 5 of 9; 3 Feedback 4 of 9; 4 Requirements 3 of 11; 5 Design 7 of 13; 6 Testing 8 of 11;
  **7 Operating 8 of 11**, and the other 3 met a project with nothing in production; 8 Work 3 of 7; 9 People 6 of 8; 10 Agents 1
  of 9. A tag isn't evidence.

### New

The principles with no SE1 counterpart:

- [DM1/U3](guide/02-unknowing.md#u3) Leakage is the canonical K3: it feels like a good result.
- [DM1/T13](guide/06-testing.md#t13) A run is an experiment. Added 2026-11-03.
- [DM1/U8](guide/02-unknowing.md#u8) People ask Prob(result | me); the data holds Prob(result |
  the rows it counts as like me).
- [DM1/F4](guide/03-feedback.md#f4) The search is part of the result.
- [DM1/F5](guide/03-feedback.md#f5) The holdout is a consumable.
- [DM1/F6](guide/03-feedback.md#f6) Count your looks, and say the number.
- [DM1/F7](guide/03-feedback.md#f7) A point estimate is half a result.
- [DM1/F8](guide/03-feedback.md#f8) A large sample does not rescue a biased one; it disguises it.
- [DM1/R2](guide/04-requirements.md#r2) Fix the unit, the outcome, and the prediction point before
  anything else.
- [DM1/R6](guide/04-requirements.md#r6) Know, per column, when its value became knowable.
- [DM1/R7](guide/04-requirements.md#r7) A label is an opinion with a timestamp.
- [DM1/T7](guide/06-testing.md#t7) Ask for the diff in the numbers, not only the diff in the code.
- [DM1/T9](guide/06-testing.md#t9) Evaluate by slice.
- [DM1/T11](guide/06-testing.md#t11) Unsupervised results have no answer key: test their stability.
- [DM1/O4](guide/07-operating.md#o4) Name the shift.
- [DM1/H6](guide/09-people.md#h6) Present uncertainty in the client's units and decisions.
- [DM1/A4](guide/10-agents.md#a4) Keep the holdout out of the agent's reach.
- [DM1/A8](guide/10-agents.md#a8) Data sent to an agent has left the room.
- [DM1/A9](guide/10-agents.md#a9) A language model in the pipeline is a model.
- [DM1/F10](guide/03-feedback.md#f10) A faithful summary can still lose the finding: check what left, not only what stayed.
- [DM1/R12](guide/04-requirements.md#r12) Name the kind of claim first: predicted, described, explained, or extracted.
- [DM1/A10](guide/10-agents.md#a10) When reading the data is the task, the agent must read it: say what then has to be true.
