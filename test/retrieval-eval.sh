#!/bin/sh
#
# test/retrieval-eval.sh -- shell entry point for the retrieval evaluation.
#
# The measurement itself lives in test/retrieval-eval.py; this wrapper exists
# so the shell-shaped convention every other runnable file in this tree
# follows still finds it. See that file's header for WHY it is Python: it
# reads .viki/cache.db directly (deliberately plain SQLite, D-10) to replay
# `viki ask`'s exact FTS5 query and report where the gold chunk sits in the
# COMPLETE BM25 ranking -- which is what separates "the keyword leg never
# matched at all" from "it matched at rank 137 and the pool cut it off".
# Python's stdlib sqlite3 carries FTS5; a shell version would need the
# `sqlite3` binary, which FINDINGS.md records as absent on the CI image.
#
# Usage:
#     bash test/retrieval-eval.sh [options]        # all options pass through
#     bash test/retrieval-eval.sh --help
#     VIKI_BIN=/path/to/viki bash test/retrieval-eval.sh
#
# Corpus: test/retrieval-corpus.sh, built once into $VIKI_EVAL_DIR (default
# /tmp/viki-retrieval-eval) and reused. --rebuild forces it.
#
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)

PY=${VIKI_EVAL_PYTHON:-}
if [ -z "$PY" ]; then
    for c in python3 python; do
        if command -v "$c" >/dev/null 2>&1; then PY="$c"; break; fi
    done
fi
[ -n "$PY" ] || { echo "retrieval-eval.sh: no python3 on PATH" >&2; exit 1; }

exec "$PY" "$HERE/retrieval-eval.py" "$@"
