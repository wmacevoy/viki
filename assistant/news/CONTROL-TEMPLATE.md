# The control briefing template

**This is the real Claude Code compaction template, section for section, taken
from an actual summary received in session `ff137b62` on 2026-08-31.** It is
reproduced faithfully on purpose.

    1. Primary Request and Intent
    2. Key Technical Concepts
    3. Files and Code Sections
    4. Errors and Fixes
    5. Problem Solving
    6. All User Messages
    7. Pending Tasks
    8. Current Work
    9. Optional Next Step

## Do not strawman the control

An earlier draft of this experiment described the summary as having "no slot
for what was found false." **That is not accurate, and a control built on it
would prove nothing.** Section 4 is *Errors and Fixes*, and it is a real slot.

The honest version of the hypothesis is narrower:

> Section 4 collects **errors that were FIXED** — a bug, then its patch. It is
> shaped for *things done*, not *things believed*. A CLAIM the trace came to
> hold and later found false has no natural home in it: it was never a defect
> in a file, and there is no fix to pair it with. So it either lands in
> section 2 as a *concept* stripped of its status, or it does not land at all.

That is the thing being tested, and it is a much weaker effect than "there is
no slot." If the control carries falsity anyway, the hypothesis is wrong and
the finding is that the harness summary is better than this project assumed —
which is worth knowing and is why the control is run rather than argued about.

## What the control agent is given

The template above, filled in by the same nanny that writes the treatment
briefing, from the same source material, under instructions to fill it in
FAITHFULLY and well. Same author, same input, same effort — **only the shape
differs.** Any other difference would confound the result.
