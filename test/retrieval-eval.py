#!/usr/bin/env python3
"""
test/retrieval-eval.py -- viki's retrieval evaluation harness.

WHAT IT MEASURES
----------------
Two numbers that must not be mixed up, reported separately and both
headline:

  1. RANKING QUALITY over queries whose answer is in something `viki index`
     actually covers -- recall@1, recall@5, recall@k and MRR@k, broken down
     by query class, on the dev set and the HELD-OUT test set separately.

  2. COVERAGE. A Fossil repository holds far more than the checkout, and
     the omissions were exactly the episodic ones. The query set contains
     questions whose answer lives ONLY in non-checkout artifacts, and this
     harness reports how many of them are unanswerable at all -- and, since
     the coverage round, how well the reachable ones rank. A memory that
     ranks perfectly over a corpus missing the commit log is still a bad
     memory.

     `viki index` now reads: checkout files, wiki pages (`wiki:`), tickets
     (`ticket:`), forum posts (`forum:`), CHECK-IN COMMENTS (`ckin:`), tech
     notes (`note:`), ticket CHANGE artifacts (`tchg:`), attachment content
     (`attach:`) and unversioned blobs (`uv:`). Still deliberately absent,
     with reasons and numbers in NOT_INDEXED_WHY below: historical file
     versions, and tags as a namespace of their own.

     The two numbers must be read together. Adding correct content to the
     corpus MEASURABLY COSTS precision@1 on queries that already worked,
     because RRF displacement does not care that the new chunks are
     relevant. Report 1 is where that shows up; report 2 is where the gain
     does. Quoting either alone misreports the round.

WHY PYTHON AND NOT SH
---------------------
Everything else executable in this tree is shell, deliberately, and this
file breaks that pattern for three reasons that are not style:

  * It reads `.viki/cache.db` DIRECTLY. That db is plain, unencrypted
    SQLite by design (D-10), and Python's stdlib `sqlite3` opens it with
    FTS5 available -- so the harness can replay `viki ask`'s exact
    OR-of-terms MATCH query and report where the gold chunk sits in the
    COMPLETE BM25 ranking, not just whether it made the top k. That single
    fact separates "the keyword leg never matched this chunk at all" from
    "it matched at rank 137 and the pool cut it off", which is the most
    useful thing in the failure taxonomy. In shell that needs the `sqlite3`
    binary, which FINDINGS.md records as absent on the CI image.
  * MRR and per-class breakdowns are dictionaries of floats. In shell they
    are awk, and a scoring bug in awk is invisible.
  * The query file is TSV with legitimately empty fields, and this repo has
    already lost data once to a splitter that collapsed them (FINDINGS.md,
    `strtok_r`). `str.split('\t')` preserves them.

`test/retrieval-eval.sh` is a one-line wrapper, so the shell-shaped entry
point still exists.

USAGE
    bash test/retrieval-eval.sh                 # everything, dev + held-out
    python3 test/retrieval-eval.py --help

    VIKI_BIN=/path/to/other/viki bash test/retrieval-eval.sh
        Score a different build against the same corpus and query set. This
        is how a later agent shows a change actually helped.

    bash test/retrieval-eval.sh --split dev     # tune against this only
    bash test/retrieval-eval.sh --failures      # full failure taxonomy
    bash test/retrieval-eval.sh --json out.json # machine-readable

The corpus is built once (test/retrieval-corpus.sh) and reused; pass
--rebuild, or VIKI_EVAL_REBUILD=1, to force it.

EXIT STATUS
    0  the run completed and produced numbers
    1  the run could not produce numbers (no corpus, no binary, unusable
       query set). Deliberately NOT "some queries failed" -- failing
       queries are the measurement, not an error.
"""

import argparse
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

DEFAULT_QUERIES = os.path.join(HERE, "retrieval-queries.tsv")
CORPUS_SH = os.path.join(HERE, "retrieval-corpus.sh")

# `viki ask` hit line:  [1] rrf=0.0328  <64 hex>#<ix>  <source path>
# The source path goes LAST because it can be empty, can be
# "(source path unknown)", and can contain spaces -- so nothing may read it
# by position. Snippet lines carry raw newlines from the indexed text and
# can themselves start with '[', which is why this anchors on `rrf=` and not
# on '^\['.
HIT_RE = re.compile(r"^\[(\d+)\] rrf=([0-9.]+)\s+([0-9a-f]{64})#(\d+)\s*(.*)$")

RRF_K = 60.0                 # VIKI_RRF_K in src/viki_ask.c
CANDIDATE_POOL = 40          # VIKI_CANDIDATE_POOL in src/viki_ask.h

# Why an artifact type may still be unreachable, per `viki_index.c`. This
# table is a claim about the CODE, so it goes stale silently -- it used to
# say viki reads "checkout files, wiki pages, tickets and forum posts" and
# nothing else, which stopped being true when the coverage round landed.
# Keep it honest against src/viki_index.c's extractor list.
NOT_INDEXED_WHY = {
    "file-history": "historical file versions (`fver:`) are DEFERRED on "
                    "purpose: measured, they add +1 answerable query for "
                    "+122 chunks and push already-answerable answers out of "
                    "the top ten (recall@10 0.605 -> 0.553)",
    "tag": "there is no `tag:` namespace and deliberately so -- the branch "
           "name rides in the `ckin:` header instead; the whole repository's "
           "tagxref free text measured 120 bytes across 18 valued rows",
}


# --------------------------------------------------------------- query set --

class Query(object):
    __slots__ = ("qid", "split", "cls", "expect", "artifact", "query",
                 "anchor", "trap", "gold", "goldtrap", "defect")

    def __init__(self, fields):
        (self.qid, self.split, self.cls, self.expect, self.artifact,
         self.query, self.anchor, self.trap) = fields
        self.gold = []          # [(content_hash, chunk_ix)]
        self.goldtrap = []      # stale/decoy chunks
        self.defect = None      # query-set problem, not a viki problem


def load_queries(path):
    """Parse the TSV. Empty trailing fields are normal and are preserved --
    a splitter that collapses them silently shifts every later column, which
    is the exact bug FINDINGS.md records against strtok_r."""
    out = []
    seen = set()
    with open(path) as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.split("\t")
            if fields[0] == "id":
                continue
            if len(fields) != 8:
                die("%s:%d: expected 8 tab-separated fields, got %d"
                    % (path, lineno, len(fields)))
            q = Query(fields)
            if q.qid in seen:
                die("%s:%d: duplicate query id %s" % (path, lineno, q.qid))
            seen.add(q.qid)
            out.append(q)
    if not out:
        die("no queries in %s" % path)
    return out


# ------------------------------------------------------- ground-truth resolve --

def resolve_ground_truth(db, queries):
    """anchor -> {(content_hash, chunk_ix)} against the live cache db.

    Ground truth is stored as a literal substring rather than as a
    content_hash#chunk_ix so it survives the two things that legitimately
    move: editing a document (new content_hash) and re-chunking (new
    chunk_ix, which is an epoch bump -- see VIKI_DESIGN.md). It also gives
    the coverage measurement for free: an anchor that resolves to NOTHING
    means the artifact holding that answer is not in the index at all."""
    cur = db.cursor()

    def find(text):
        if not text:
            return []
        rows = cur.execute(
            "SELECT DISTINCT content_hash, chunk_ix FROM viki_chunk "
            "WHERE instr(chunk_text, ?) > 0", (text,)).fetchall()
        return [(h, int(i)) for h, i in rows]

    for q in queries:
        q.gold = find(q.anchor)
        q.goldtrap = find(q.trap)
        if q.expect == "indexed" and not q.gold:
            q.defect = ("anchor resolves to no chunk, but the query is "
                        "marked expect=indexed. Either the corpus moved or "
                        "the anchor wraps across a line -- chunk_text keeps "
                        "the source's hard wrapping.")
        # An expect=unindexed anchor that DOES resolve is not a defect. It is
        # the coverage gap CLOSING, which is the one outcome report 2 exists
        # to detect -- the query set's own header says these queries "start
        # scoring with no edit to this file" once somebody indexes check-in
        # comments. This used to be flagged as a defect and EXCLUDED, which
        # made `Coverage score` structurally incapable of ever leaving 0.000:
        # every query that stopped being a gap was dropped from the run
        # before report 2 counted it. Measured, at the moment check-in
        # comments landed: 8 of 16 excluded, coverage still printed 0.000.
        # Reported instead as "COVERAGE CLOSED" in report 2, with ranks.
        elif q.trap and not q.goldtrap:
            q.defect = "trap resolves to no chunk"
        elif q.trap and set(q.goldtrap) & set(q.gold):
            q.defect = ("trap and gold share a chunk, so the trap can never "
                        "outrank the gold and proves nothing")


# --------------------------------------------------------------- BM25 replay --

def build_or_query(zquery):
    """Byte-for-byte replica of build_or_query() in src/viki_ask.c.

    Terms are split on any byte <= ' ' and each is emitted as a double-quoted
    FTS5 string literal with embedded quotes doubled, joined by OR. FTS5's
    default MATCH is implicit-AND, which excludes a row outright when any
    term is missing -- wrong for natural language, see FINDINGS.md."""
    terms = []
    cur = []
    for ch in zquery:
        if ch <= " ":
            if cur:
                terms.append("".join(cur))
                cur = []
        else:
            cur.append(ch)
    if cur:
        terms.append("".join(cur))
    if not terms:
        return None
    return " OR ".join('"%s"' % t.replace('"', '""') for t in terms)


def bm25_full_ranking(db, zquery):
    """The COMPLETE BM25 ranking for this query -- no LIMIT.

    `viki ask` only ever sees the first poolSize*4 rows of this, so the tail
    is invisible to it. Having the whole thing is what lets a failure be
    labelled `KEYWORD_NO_MATCH` (the gold chunk shares no stem with the
    query at all -- a vocabulary problem) rather than `KEYWORD_TOO_DEEP`
    (it matched, at rank 212, and the pool cut it off -- a ranking problem).
    Those two want completely different fixes."""
    fts = build_or_query(zquery)
    if fts is None:
        return {}, 0
    try:
        rows = db.execute(
            "SELECT content_hash, chunk_ix FROM chunk_fts "
            "WHERE chunk_fts MATCH ? ORDER BY bm25(chunk_fts)", (fts,)).fetchall()
    except sqlite3.OperationalError:
        return {}, 0
    rank = {}
    n = 0
    for h, i in rows:
        key = (h, int(i))
        if key not in rank:            # rows, not chunks: dedupe per epoch
            n += 1
            rank[key] = n
    return rank, n


# ------------------------------------------------------------------ running --

def run_ask(viki_bin, cwd, query, k, env):
    t0 = time.time()
    proc = subprocess.run([viki_bin, "ask", query, "--k", str(k)],
                          cwd=cwd, env=env, capture_output=True, text=True)
    hits = []
    for line in proc.stdout.splitlines():
        m = HIT_RE.match(line)
        if m:
            hits.append({"rank": int(m.group(1)),
                         "rrf": float(m.group(2)),
                         "hash": m.group(3),
                         "chunk_ix": int(m.group(4)),
                         "source": m.group(5).strip()})
    return hits, time.time() - t0


def rank_of(hits, targets):
    """1-based rank of the first hit in `targets`, or None."""
    tset = set(targets)
    for h in hits:
        if (h["hash"], h["chunk_ix"]) in tset:
            return h["rank"]
    return None


# ---------------------------------------------------------------- diagnosis --

def diagnose(q, hyb, deg, bm25rank, bm25total, pool_size, src_of):
    """Why did this query come out the way it did.

    Returns (primary, [detail, ...]). `primary` is one label, chosen so the
    counts across a run add up to something a later agent can act on."""
    details = []
    gold = q.gold
    hyb_rank = rank_of(hyb, gold)
    deg_rank = rank_of(deg, gold)

    if not gold:
        return ("NOT_INDEXED",
                ["the answer lives in a %s; nothing in the index contains it"
                 % q.artifact,
                 NOT_INDEXED_WHY.get(q.artifact,
                                     "no chunk holds this text at any rank")])

    # --- what each leg did, independent of whether the query "passed" ---
    best_bm25 = None
    for g in gold:
        r = bm25rank.get(g)
        if r is not None and (best_bm25 is None or r < best_bm25):
            best_bm25 = r
    if best_bm25 is None:
        leg_kw = "KEYWORD_NO_MATCH"
        details.append("keyword leg: gold chunk matches NONE of the query "
                       "terms (after porter stemming); %d chunk(s) matched "
                       "something" % bm25total)
    elif best_bm25 > pool_size:
        leg_kw = "KEYWORD_TOO_DEEP"
        details.append("keyword leg: gold chunk is BM25 rank %d of %d "
                       "matches -- past the %d-chunk candidate pool"
                       % (best_bm25, bm25total, pool_size))
    else:
        leg_kw = "KEYWORD_OK"
        details.append("keyword leg: gold chunk is BM25 rank %d of %d matches"
                       % (best_bm25, bm25total))

    if hyb_rank is not None and deg_rank is None:
        details.append("the VECTOR leg is what surfaced it: absent from the "
                       "BM25-only run, present at rank %d in hybrid" % hyb_rank)
    elif hyb_rank is None and deg_rank is not None:
        details.append("REGRESSION FROM FUSION: BM25-only ranked it %d, "
                       "hybrid dropped it out of the top %d entirely"
                       % (deg_rank, len(hyb) if hyb else 0))
    elif hyb_rank is not None and deg_rank is not None:
        details.append("both runs found it (BM25-only rank %d, hybrid rank %d)"
                       % (deg_rank, hyb_rank))

    # --- right document, wrong chunk: the chunk-boundary failure ---
    gold_hashes = set(h for h, _ in gold)
    gold_ixs = set(gold)
    neighbour = None
    for h in hyb:
        key = (h["hash"], h["chunk_ix"])
        if h["hash"] in gold_hashes and key not in gold_ixs:
            neighbour = h
            break
    if neighbour is not None:
        adj = any(neighbour["hash"] == gh and abs(neighbour["chunk_ix"] - gi) == 1
                  for gh, gi in gold)
        details.append("%s chunk of the SAME document is at rank %d (%s#%d)"
                       % ("ADJACENT" if adj else "another", neighbour["rank"],
                          src_of.get(neighbour["hash"], "?"), neighbour["chunk_ix"]))

    # --- a superseded answer beating the current one ---
    trap_rank = rank_of(hyb, q.goldtrap) if q.goldtrap else None
    stale_wins = (trap_rank is not None and
                  (hyb_rank is None or trap_rank < hyb_rank))
    if stale_wins:
        details.append("STALE: a chunk carrying the superseded answer is at "
                       "rank %d, above the current one (%s)"
                       % (trap_rank, "not retrieved" if hyb_rank is None
                          else "rank %d" % hyb_rank))

    if hyb_rank == 1:
        primary = "HIT@1"
    elif hyb_rank is not None and hyb_rank <= 5:
        primary = "HIT@5"
    elif hyb_rank is not None:
        primary = "HIT@K"
    elif stale_wins:
        primary = "STALE_OUTRANKS"
    elif neighbour is not None:
        primary = "NEIGHBOUR_CHUNK"
    elif leg_kw == "KEYWORD_NO_MATCH":
        primary = "BOTH_LEGS_MISSED" if deg_rank is None else "KEYWORD_NO_MATCH"
    elif leg_kw == "KEYWORD_TOO_DEEP":
        primary = "KEYWORD_TOO_DEEP"
    else:
        primary = "FUSED_OUT"

    if hyb_rank is not None and hyb_rank > 1 and stale_wins:
        primary = "STALE_OUTRANKS"

    if hyb:
        details.append("rank 1 was %s#%d" %
                       (src_of.get(hyb[0]["hash"], "?"), hyb[0]["chunk_ix"]))
    else:
        details.append("no results at all")
    return primary, details


# ------------------------------------------------------------------ metrics --

def score(results, k):
    n = len(results)
    if n == 0:
        return {"n": 0, "r@1": 0.0, "r@5": 0.0, "r@k": 0.0, "mrr": 0.0}
    r1 = sum(1 for r in results if r["rank"] == 1)
    r5 = sum(1 for r in results if r["rank"] is not None and r["rank"] <= 5)
    rk = sum(1 for r in results if r["rank"] is not None)
    mrr = sum(1.0 / r["rank"] for r in results if r["rank"] is not None)
    return {"n": n, "r@1": r1 / n, "r@5": r5 / n, "r@k": rk / n, "mrr": mrr / n}


def fmt_row(label, s, width=34):
    return ("  %-*s  n=%-3d  recall@1=%.3f  recall@5=%.3f  recall@k=%.3f  MRR=%.3f"
            % (width, label, s["n"], s["r@1"], s["r@5"], s["r@k"], s["mrr"]))


def die(msg):
    sys.stderr.write("retrieval-eval: FATAL: %s\n" % msg)
    sys.exit(1)


# --------------------------------------------------------------------- main --

def main():
    ap = argparse.ArgumentParser(
        description="Measure viki retrieval quality and index coverage.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", default=os.environ.get(
        "VIKI_EVAL_DIR", "/tmp/viki-retrieval-eval"),
        help="corpus work dir (default: $VIKI_EVAL_DIR or /tmp/viki-retrieval-eval)")
    ap.add_argument("--queries", default=DEFAULT_QUERIES)
    ap.add_argument("--viki", default=os.environ.get(
        "VIKI_BIN", os.path.join(REPO_ROOT, "build", "dist", "viki")),
        help="viki binary to score (default: $VIKI_BIN or build/dist/viki)")
    ap.add_argument("-k", type=int, default=10,
                    help="how many hits to ask for (default 10)")
    ap.add_argument("--split", choices=["dev", "test", "all"], default="all")
    ap.add_argument("--rebuild", action="store_true",
                    help="rebuild the corpus even if one is already there")
    ap.add_argument("--no-build", action="store_true",
                    help="never invoke test/retrieval-corpus.sh")
    ap.add_argument("--no-degraded", action="store_true",
                    help="skip the BM25-only control run (halves runtime, "
                         "loses per-leg attribution)")
    ap.add_argument("--failures", action="store_true",
                    help="print the full failure taxonomy, one block per "
                         "wrong query")
    ap.add_argument("--json", dest="json_out", default=None)
    args = ap.parse_args()

    viki = os.path.abspath(args.viki)
    if not (os.path.isfile(viki) and os.access(viki, os.X_OK)):
        die("no executable viki at %s (run build/build.sh, or set VIKI_BIN)" % viki)

    work = os.path.abspath(args.corpus)
    stamp = os.path.join(work, "CORPUS-COMPLETE")
    if not args.no_build and (args.rebuild or not os.path.exists(stamp)):
        env = dict(os.environ)
        if args.rebuild:
            env["VIKI_EVAL_REBUILD"] = "1"
        rc = subprocess.call(["bash", CORPUS_SH, work], env=env)
        if rc != 0:
            die("test/retrieval-corpus.sh failed (exit %d)" % rc)
    if not os.path.exists(stamp):
        die("no corpus at %s -- run: bash test/retrieval-corpus.sh %s" % (work, work))

    co = os.path.join(work, "co")
    dbpath = os.path.join(co, ".viki", "cache.db")
    if not os.path.exists(dbpath):
        die("no cache db at %s" % dbpath)

    db = sqlite3.connect("file:%s?mode=ro" % dbpath, uri=True)
    src_of = {h: p for h, p in db.execute(
        "SELECT content_hash, path FROM viki_source")}
    n_chunks = db.execute("SELECT count(*) FROM viki_chunk").fetchone()[0]

    # A fingerprint of WHAT WAS INDEXED, so two baselines can be compared
    # for corpus identity rather than assumed to share one. This matters
    # more here than in a normal eval: the corpus is built from this repo's
    # OWN docs, so editing AGENTS.md or FINDINGS.md changes the corpus and
    # therefore the numbers. A baseline quoted against a different
    # fingerprint is not a baseline, it is a different experiment.
    # Deliberately excludes every namespace whose KEY or whose composed text
    # carries a timestamp-derived artifact uuid, because those differ on
    # every corpus rebuild and would make the fingerprint useless as an
    # identity check. ticket:/forum: were the original two; ckin:, tchg:,
    # attach: and note: joined them when those classes started being indexed
    # (a check-in uuid is derived from its own manifest, which carries the
    # commit time). uv: is NOT excluded: its key is the blob's name and its
    # composed text carries no timestamp, so it is stable across rebuilds.
    import hashlib
    VOLATILE_NS = ("ticket:", "forum:", "ckin:", "tchg:", "attach:", "note:")
    fp = hashlib.sha256()
    for h, p in sorted(src_of.items(), key=lambda kv: kv[1]):
        if p.startswith(VOLATILE_NS):
            continue
        fp.update(("%s %s\n" % (p, h)).encode())
    corpus_fp = fp.hexdigest()[:16]

    queries = load_queries(args.queries)
    resolve_ground_truth(db, queries)

    defects = [q for q in queries if q.defect]

    # The environment `viki ask` runs under. Fossil variables are set because
    # the corpus is an ENCRYPTED repo and `viki ask` itself touches no
    # fossil, but a stray inherited FOSSIL_HOME would still be wrong to use.
    base_env = dict(os.environ)
    base_env["FOSSIL_HOME"] = os.path.join(work, "fossilhome")
    base_env["FOSSIL_SEE_KEY"] = "viki-retrieval-eval-key-7d3a"
    base_env["FOSSIL_USER"] = "vikieval"
    base_env["VIKI_FOSSIL_USER"] = "vikieval"

    hyb_env = dict(base_env)
    hyb_env["VIKI_MODEL_DIR"] = os.environ.get(
        "VIKI_MODEL_DIR", os.path.join(REPO_ROOT, "build", "dist", "model"))
    have_model = os.path.isfile(os.path.join(hyb_env["VIKI_MODEL_DIR"],
                                             "viki-manifest.json"))

    # FINDINGS.md: unsetting VIKI_MODEL_DIR, or setting it to "", both fall
    # back to the RELATIVE path build/dist/model. Only a path that fails
    # stat() forces the degraded (BM25-only) leg.
    deg_env = dict(base_env)
    deg_env["VIKI_MODEL_DIR"] = os.path.join(work, "no-such-model-directory")

    pool_size = min(args.k * 4, CANDIDATE_POOL)

    selected = [q for q in queries
                if args.split == "all" or q.split == args.split]

    print("=" * 78)
    print("viki retrieval evaluation")
    print("=" * 78)
    print("  viki binary   : %s" % viki)
    print("  build mtime   : %s" % time.strftime(
        "%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(viki))))
    print("  corpus        : %s  (%d chunks, %d sources)"
          % (work, n_chunks, len(src_of)))
    print("  corpus fp     : %s   <- quote this with any baseline; the corpus"
          % corpus_fp)
    print("                  is built from this repo's own docs, so editing")
    print("                  them changes the numbers.")
    print("  query set     : %s  (%d queries, %d selected)"
          % (args.queries, len(queries), len(selected)))
    # Deliberately worded as "offered", not "hybrid". FINDINGS.md: `viki
    # ask`'s own banner proves a model FILE loaded, never that the vector
    # leg contributed -- and the binary under test may ignore this variable
    # entirely (a shim that forces degraded mode is exactly how this
    # harness's own non-vacuity was proven). The fusion-helped/hurt lines
    # below are the behavioural statement; this line is only configuration.
    print("  model offered : %s" % (hyb_env["VIKI_MODEL_DIR"] if have_model
                                    else "NONE FOUND -- every number below "
                                         "is BM25-only and is not comparable "
                                         "to a hybrid run"))
    print("  k             : %d  (candidate pool %d = min(4k, %d))"
          % (args.k, pool_size, CANDIDATE_POOL))
    if defects:
        print()
        print("  ###  %d QUERY-SET DEFECT(S) -- these queries are excluded  ###"
              % len(defects))
        for q in defects:
            print("  %s  %s" % (q.qid, q.defect))
    print()

    live = [q for q in selected if not q.defect]
    results = []
    t_start = time.time()
    for i, q in enumerate(live, 1):
        sys.stderr.write("\r  running %d/%d (%s) " % (i, len(live), q.qid))
        sys.stderr.flush()
        hyb, _ = run_ask(viki, co, q.query, args.k, hyb_env)
        deg = [] if args.no_degraded else run_ask(viki, co, q.query, args.k, deg_env)[0]
        bm25rank, bm25total = bm25_full_ranking(db, q.query)
        primary, details = diagnose(q, hyb, deg, bm25rank, bm25total,
                                    pool_size, src_of)
        # Rank of the best NON-gold chunk that belongs to a gold DOCUMENT.
        # Compared against the gold chunk's own rank this is the direct
        # measure of the chunk-boundary failure: the retriever found the
        # right document and handed back the wrong 40 lines of it.
        gold_hashes = set(h for h, _ in q.gold)
        gold_keys = set(q.gold)
        nb_rank = None
        for h in hyb:
            if h["hash"] in gold_hashes and (h["hash"], h["chunk_ix"]) not in gold_keys:
                nb_rank = h["rank"]
                break
        results.append({
            "qid": q.qid, "split": q.split, "class": q.cls,
            "expect": q.expect, "artifact": q.artifact, "query": q.query,
            "gold": ["%s#%d (%s)" % (h[:12], i2, src_of.get(h, "?"))
                     for h, i2 in q.gold],
            "n_gold": len(q.gold),
            "rank": rank_of(hyb, q.gold),
            "deg_rank": rank_of(deg, q.gold),
            "bm25_rank": min([bm25rank[g] for g in q.gold if g in bm25rank]
                             or [None], key=lambda v: (v is None, v)),
            "bm25_matches": bm25total,
            "neighbour_rank": nb_rank,
            "diagnosis": primary,
            "details": details,
            "top": ["%s#%d rrf=%.4f" % (src_of.get(h["hash"], "?"),
                                        h["chunk_ix"], h["rrf"])
                    for h in hyb[:3]],
        })
    sys.stderr.write("\r" + " " * 40 + "\r")
    elapsed = time.time() - t_start

    indexed = [r for r in results if r["expect"] == "indexed"]
    unindexed = [r for r in results if r["expect"] == "unindexed"]

    # ------------------------------------------------------------ report 1 --
    print("-" * 78)
    print("1. RANKING QUALITY  (queries whose answer IS in something viki indexes)")
    print("-" * 78)
    for sp in ("dev", "test"):
        rows = [r for r in indexed if r["split"] == sp]
        if not rows:
            continue
        tag = "DEV (tunable)" if sp == "dev" else "TEST (HELD OUT)"
        print(fmt_row(tag, score(rows, args.k)))
    print(fmt_row("ALL indexed-answer queries", score(indexed, args.k)))
    print()
    print("  by class:")
    for cls in sorted(set(r["class"] for r in indexed)):
        for sp in ("dev", "test"):
            rows = [r for r in indexed if r["class"] == cls and r["split"] == sp]
            if rows:
                print(fmt_row("%s [%s]" % (cls, sp), score(rows, args.k)))
    print()
    print("  by artifact type:")
    for art in sorted(set(r["artifact"] for r in indexed)):
        rows = [r for r in indexed if r["artifact"] == art]
        print(fmt_row(art, score(rows, args.k)))

    # ----------------------------------------------------------- report 1b --
    # The control nobody had run. `viki ask` is hybrid whenever a model file
    # loads -- and FINDINGS.md already records that the mode banner proves a
    # model LOADED, not that the vector leg helped. Scoring the identical
    # query set with the model forced absent is the only way to say whether
    # fusion is earning its place, per query and in aggregate.
    if not args.no_degraded and have_model:
        deg_scored = [dict(r, rank=r["deg_rank"]) for r in indexed]
        print()
        print("  CONTROL -- same queries, BM25 only (model forced absent):")
        for sp in ("dev", "test"):
            rows = [r for r in deg_scored if r["split"] == sp]
            if rows:
                print(fmt_row("BM25-only [%s]" % sp, score(rows, args.k)))
        print(fmt_row("BM25-only [all]", score(deg_scored, args.k)))
        helped = [r for r in indexed
                  if r["rank"] is not None and r["deg_rank"] is None]
        hurt = [r for r in indexed
                if r["rank"] is None and r["deg_rank"] is not None]
        demoted = [r for r in indexed
                   if r["rank"] is not None and r["deg_rank"] is not None
                   and r["rank"] > r["deg_rank"]]
        print()
        print("  fusion helped (found only WITH the vector leg): %d  %s"
              % (len(helped), " ".join(r["qid"] for r in helped)))
        print("  fusion HURT  (lost by adding the vector leg)  : %d  %s"
              % (len(hurt), " ".join(r["qid"] for r in hurt)))
        print("  fusion demoted (found by both, worse in hybrid): %d  %s"
              % (len(demoted), " ".join(r["qid"] for r in demoted)))

    # ------------------------------------------------------------ report 2 --
    print()
    print("-" * 78)
    print("2. INDEX COVERAGE  (queries whose answer is in an UN-INDEXED artifact)")
    print("-" * 78)
    answerable = [r for r in unindexed if r["n_gold"] > 0]
    print("  %d of %d such queries are ANSWERABLE AT ALL (the rest have no"
          % (len(answerable), len(unindexed)))
    print("  chunk in the index that contains the answer, at any rank).")
    print()
    by_art = {}
    for r in unindexed:
        by_art.setdefault(r["artifact"], []).append(r)
    print("  %-18s %-6s %s" % ("artifact type", "n", "answerable"))
    for art in sorted(by_art):
        rows = by_art[art]
        ok = sum(1 for r in rows if r["n_gold"] > 0)
        print("  %-18s %-6d %d/%d %s" % (art, len(rows), ok, len(rows),
                                         "" if ok else "<- NOT INDEXED"))
    print()
    print("  Coverage score = %.3f  (0.000 means viki reads none of these"
          % (len(answerable) / float(len(unindexed)) if unindexed else 0.0))
    print("  artifact types; 1.000 means it reads all of them.)")

    # ANSWERABLE IS NOT THE SAME AS ANSWERED, and quoting only the first
    # overstates the win. A newly-reachable answer sitting at rank 9 is a
    # coverage success and a ranking failure at the same time; both belong
    # in the report. These queries are scored here and NOWHERE ELSE -- they
    # never enter report 1, so closing a coverage gap cannot flatter, or
    # damage, the ranking-quality numbers a tuning agent is reading.
    if answerable:
        print()
        print("  Where those answers actually RANK (scored here only; these")
        print("  queries are excluded from report 1's ranking quality):")
        print(fmt_row("coverage-closed [all]", score(answerable, args.k)))
        for sp in ("dev", "test"):
            rows = [r for r in answerable if r["split"] == sp]
            if rows:
                print(fmt_row("coverage-closed [%s]" % sp, score(rows, args.k)))
        print()
        for art in sorted(set(r["artifact"] for r in answerable)):
            rows = [r for r in answerable if r["artifact"] == art]
            print("  %-18s %s" % (art, " ".join(
                "%s@%s" % (r["qid"], r["rank"] if r["rank"] else "miss")
                for r in rows)))

    # ------------------------------------------------------------ report 3 --
    print()
    print("-" * 78)
    print("3. FAILURE TAXONOMY")
    print("-" * 78)
    counts = {}
    for r in results:
        counts[r["diagnosis"]] = counts.get(r["diagnosis"], 0) + 1
    for d in sorted(counts, key=lambda x: -counts[x]):
        print("  %-20s %d" % (d, counts[d]))

    # Two aggregate numbers that no single query shows, and that between
    # them explain most of the failures above.
    print()
    sel = sorted(r["bm25_matches"] for r in indexed)
    if sel:
        med = sel[len(sel) // 2]
        print("  keyword-leg selectivity: the OR-of-terms MATCH selects a")
        print("  MEDIAN of %d of the %d chunks in the corpus (min %d, max %d)."
              % (med, n_chunks, sel[0], sel[-1]))
        print("  `porter unicode61` carries no stopword list and every query")
        print("  term is OR'd, so a natural-language question matches nearly")
        print("  everything and BM25 is ranking the whole corpus, not a")
        print("  candidate set. %d of %d queries match >90%% of it."
              % (sum(1 for v in sel if v > 0.9 * n_chunks), len(sel)))
    nb = [r for r in indexed
          if r["neighbour_rank"] is not None
          and (r["rank"] is None or r["neighbour_rank"] < r["rank"])]
    nb1 = [r for r in nb if r["neighbour_rank"] == 1]
    print()
    print("  RIGHT DOCUMENT, WRONG CHUNK: in %d of %d indexed-answer queries"
          % (len(nb), len(indexed)))
    print("  another chunk of the gold DOCUMENT outranks the gold CHUNK")
    print("  (%d of them at rank 1). Fixed 40-line chunks with no overlap"
          % len(nb1))
    print("  split an answer away from the vocabulary that would find it.")
    print("  %s" % " ".join(r["qid"] for r in nb))

    if args.failures:
        print()
        for r in results:
            if r["diagnosis"] in ("HIT@1",):
                continue
            print()
            print("  %s [%s/%s/%s] %s" % (r["qid"], r["split"], r["class"],
                                          r["expect"], r["diagnosis"]))
            print("    query : %s" % r["query"])
            print("    gold  : %s" % (", ".join(r["gold"]) or
                                      "(nothing in the index holds this answer)"))
            print("    rank  : %s" % (r["rank"] if r["rank"] else "NOT RETRIEVED"))
            for d in r["details"]:
                print("    - %s" % d)
            for t in r["top"]:
                print("      top: %s" % t)

    print()
    print("-" * 78)
    print("  %d queries in %.1fs (%.2fs/query, %d `viki ask` invocations)"
          % (len(live), elapsed, elapsed / max(1, len(live)),
             len(live) * (1 if args.no_degraded else 2)))
    print()
    print("  COMPARING TWO BUILDS: keep the SAME --corpus directory and vary")
    print("  VIKI_BIN. Measured: run-to-run on one corpus is bit-identical,")
    print("  and recall@1/recall@5 also survive a full corpus rebuild -- but")
    print("  recall@k and MRR move by ~0.02 across rebuilds, because ticket")
    print("  and forum artifact UUIDs are timestamp-derived and are part of")
    print("  the indexed text, which reshuffles the deep tail. A rebuilt")
    print("  corpus is not a valid A/B baseline for those two figures.")
    print("-" * 78)

    if args.json_out:
        payload = {
            "viki": viki,
            "corpus": work,
            "corpus_fp": corpus_fp,
            "queries": args.queries,
            "k": args.k,
            "have_model": have_model,
            "n_chunks": n_chunks,
            "defects": [{"qid": q.qid, "defect": q.defect} for q in defects],
            "summary": {
                "indexed_dev": score([r for r in indexed if r["split"] == "dev"], args.k),
                "indexed_test": score([r for r in indexed if r["split"] == "test"], args.k),
                "indexed_all": score(indexed, args.k),
                "coverage_answerable": len(answerable),
                "coverage_total": len(unindexed),
            },
            "results": results,
        }
        with open(args.json_out, "w") as f:
            json.dump(payload, f, indent=2)
        print("  wrote %s" % args.json_out)

    return 0


if __name__ == "__main__":
    sys.exit(main())
