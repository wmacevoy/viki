# Contributing — the stripped-structure question

How to work on this one question. The repository's `CONTRIBUTING.md` governs everything else. What the
test is and how it scores is in `README.md`; the question and plan are in `docs/`.

## Run it

- **Prerequisites:** Python 3.9 or later, `bwrap` (bubblewrap), and the `claude` CLI at `~/.local/bin/claude`.
- **Raw to report:** `test/stripped/librarian/run.sh OUT RUN ARM SEED` — builds the pile, runs one
  sandboxed librarian session, and leaves the answer in `OUT/RUN`, outside the repository. Then
  `python3 test/stripped/stripped.py score OUT/RUN/build/key.json OUT/RUN/answer.json --what … --against …`
  scores it and appends to the ledger. The 8-fragment pilot took 12 s; a full pile is unmeasured.
- **Main product:** `docs/ledger.jsonl` — every score, which is what DECISION-1 reads.
- **Reset derived data:** `rm -rf OUT/RUN /tmp/viki-lib-RUN` — the source is never touched.

## Check it

- **The full check:** `python3 -m unittest discover -s test/stripped -v`
- **Success prints:** `OK`
- **The doctor:** `./dm --doctor --repo test/stripped`

## Data rules for this question

- **Raw data:** none kept here: the source is the repository's living `FINDINGS.md`, versioned by
  git, and every look records its sha256.
- **The ledger:** `docs/ledger.jsonl` — one line per score, appended by the scorer.
- **The holdout:** seeds of 1,000,000 or more, chosen and kept by Warren in `~/.viki-holdout/`,
  outside the working directory. Only he may read it, and he spends it once, at DECISION-1, giving
  the scorer a reason. An agent does not go looking for it.
- **Data about people:** owner-authored notes; sent to Sonnet by the owner's request. Copies live in
  `OUT/` and `/tmp/viki-lib-*`, and are deleted when the trial closes.
- **Clock:** UTC throughout.
