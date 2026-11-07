#!/bin/bash
# run.sh OUT RUN ARM SEED [FINDINGS]: one librarian session on one stripped pile.
#
#   OUT      evidence directory, outside this repository (the key, transcript
#            and answer land in OUT/RUN; the key never enters the session)
#   RUN      a neutral run id, e.g. r7
#   ARM      nop (bare answers) or p (each answer carries a probability)
#   SEED     the pile's seed: small for dev; a holdout seed is the owner's
#   FINDINGS default: this repository's FINDINGS.md
#
# The session runs headless under vendor/dm1-self-improvement/sandbox: it can
# write only its own project directory in /tmp, which holds the pile and
# nothing else, with an empty home -- so neither this repository, its
# FINDINGS.md, nor the key is in reach. That is the fix for 2026-11-07, when
# two runs started inside a worktree of this repository (PLAN_v1 Departures).
# The network is NOT limited, and the credentials file is bound read-only;
# the sandbox's own header recommends a separate low-limit API key.
#
# This script does not score. Scoring is a look, and a separate, deliberate
# step: stripped.py score, which appends to docs/ledger.jsonl.
set -euo pipefail
[ $# -ge 4 ] || { sed -n '2,9p' "$0" >&2; exit 2; }
OUT=$1; RUN=$2; ARM=$3; SEED=$4
HERE=$(cd "$(dirname "$0")" && pwd); K=$(dirname "$HERE"); R=$(cd "$K/../.." && pwd)
FINDINGS=${5:-$R/FINDINGS.md}
MODEL=claude-sonnet-5-5
case "$ARM" in nop|p) ;; *) echo "ARM is nop or p" >&2; exit 2;; esac
E=$(mkdir -p "$OUT/$RUN" && cd "$OUT/$RUN" && pwd)
case "$E/" in "$R"/*) echo "OUT must be outside the repository: $E" >&2; exit 2;; esac
[ -e "$E/answer.json" ] && { echo "$E already has an answer; use a new RUN" >&2; exit 2; }
P=/tmp/viki-lib-$RUN
rm -rf "$P"; mkdir -p "$P"

python3 "$K/stripped.py" build --out "$E/build" --seed "$SEED" --findings "$FINDINGS" > "$E/build.txt"
cp "$E/build/corpus/"*.md "$P/"
(cd "$P" && sha256sum f*.md) > "$E/pile.sha256"

TASK=$(ARM=$ARM python3 - "$HERE/TASK.txt" <<'EOF'
import os, sys
t = open(sys.argv[1]).read()
if os.environ["ARM"] == "p":
    conf = ("\nCONFIDENCE: every relation must carry `p`, your probability (0 to 1) that the relation is "
            "exactly right, kind and direction included. You will be scored on calibration: across your "
            "answers, claims you give p=0.7 should be right about 70% of the time. Don't round everything to 0.9.\n")
    ex = '{"relations": [{"kind": "corrects", "a": "f1a2b3c4", "b": "f5d6e7f8", "p": 0.8}, ...]}'
else:
    conf = ""
    ex = '{"relations": [{"kind": "corrects", "a": "f1a2b3c4", "b": "f5d6e7f8"}, ...]}'
print(t.replace("{{CONFIDENCE}}", conf).replace("{{EXAMPLE}}", ex), end="")
EOF
)
printf '%s\n' "$TASK" > "$E/task.txt"

CL=$(readlink -f "$HOME/.local/bin/claude")
printf 'run %s arm %s seed %s model %s started %s\n' "$RUN" "$ARM" "$SEED" "$MODEL" "$(date -u +%FT%TZ)" > "$E/run.txt"
set +e
SANDBOX_CLAUDE_DIR=$HOME/.local/share/claude SANDBOX_CREDENTIALS=$HOME/.claude/.credentials.json \
CLAUDE_CODE_DISABLE_AUTO_MEMORY=1 timeout 2400 "$R/vendor/dm1-self-improvement/sandbox/bwrap-run.sh" "$P" \
  "$CL" -p "$TASK" --model "$MODEL" --setting-sources project --strict-mcp-config --no-session-persistence \
  --disable-slash-commands --permission-mode dontAsk --allowedTools "Read,Write,Edit,Glob,Grep,Bash" \
  --disallowedTools "Agent,WebFetch,WebSearch,Skill" --output-format stream-json --verbose \
  < /dev/null > "$E/transcript.jsonl" 2> "$E/stderr.txt"
rc=$?
set -e
printf 'exit %s finished %s\n' "$rc" "$(date -u +%FT%TZ)" >> "$E/run.txt"

# Finished means a non-error result in the transcript, not an exit status.
python3 - "$E/transcript.jsonl" <<'EOF' || { echo "run $RUN: no finished result (see $E)" >&2; exit 1; }
import json, sys
for line in open(sys.argv[1], encoding="utf-8"):
    try: o = json.loads(line)
    except ValueError: continue
    if o.get("type") == "result" and o.get("is_error") is False: sys.exit(0)
sys.exit(1)
EOF
(cd "$P" && sha256sum f*.md) | diff -q - "$E/pile.sha256" > /dev/null || echo "run $RUN: the pile changed during the session" >&2
[ -f "$P/answer.json" ] || { echo "run $RUN: finished, and wrote no answer.json" >&2; exit 1; }
cp "$P/answer.json" "$E/answer.json"
echo "run $RUN done: $E/answer.json (not scored)"
