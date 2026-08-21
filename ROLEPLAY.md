# ROLEPLAY -- multiple agents, one repository, asynchronously

This branch is a live test of the claim in `ARCHITECTURE.md` that has never
actually been exercised: that several agents can work the same project at the
same time, coordinating only through the shared repository, and that
**conflicts become a branch and a ticket, never an overwrite**.

## The coordination substrate is viki's own capture loop

There is no scheduler and no lock server. The work queue is `captures/`,
projected into `viki_note`, and an agent coordinates by writing to it:

```sh
git pull --rebase origin roleplay              # BEFORE you look AND before you push
viki index .                                   # a pulled claim is invisible until you do this
viki notes --type task --state open --unclaimed   # what is ACTUALLY free
viki structure <id> --who <you> --lease 5m     # CLAIM, declaring your responsiveness
git commit && git push origin roleplay         # a claim is not a claim until it is pushed
viki structure <id> --heartbeat --lease 5m     # renew while you work
viki structure <id> --state closed             # when done
```

**`--unclaimed` is not optional.** `--state open` lists claimed work: claiming
sets `~who` and never touches state. All three agents in the first run
discovered this independently, and each avoided a collision only by reading a
`~who` marker with their own eyes.

**Declare a lease you can keep.** It is a promise about how fast you can
answer, and it is the only thing that distinguishes "offline in the field"
from "dead". Say `--lease 2d` if you are going to the north pasture.

## If a claim looks abandoned

Stealing is allowed, and it is explicit. You may not simply take it:

```sh
viki structure <id> --challenge <you>          # refused while their lease is live
# ... wait out the grace period ...
viki structure <id> --steal <you> --grace 1m --lease 5m
```

The record keeps `@stolen-from`, so the history says who held it, who asked,
and that nobody answered. `viki structure --who` will REFUSE to overwrite
another agent's claim; the challenge path is the only sanctioned way in.

A claim is a commit. **Push the claim before doing the work** -- that is the
whole protocol. If your push is rejected, someone claimed something while you
were reading; pull, re-read the queue, and pick again. Never force-push, and
never take a task whose `~who` is already set by someone else.

## Rules

1. **Pull before you look.** The queue you read must be the queue that exists.
2. **Claim by pushing, then work.** An unpushed claim is not a claim.
3. **One task at a time.** Finish or release before taking another.
4. **A rejected push is information, not an error.** It means the shared state
   moved. Re-read it. Do not `--force`, do not `reset --hard` anything you did
   not create.
5. **If you cannot do a task, say so in a note** rather than leaving it claimed:
   `viki structure <id> --who "" --state open` releases it.
6. **`test/m1.sh` must stay 90/0/0.** It is the standing proof the tree works.
   If you cannot keep it green, stop and write a note.

## What is being measured

- Do two agents ever claim the same task? Does claim-by-push actually serialise?
- What does a rejected push do to an agent's behaviour -- recover, or thrash?
- Does the capture loop survive concurrent writers to the same `captures/` file?
- Is `viki notes --state open` enough to coordinate, or is something missing?

Findings go in `FINDINGS.md` on this branch. This branch is deliberately NOT
merged to main; it is a laboratory.
