#!/bin/sh
# reader-probe.sh -- the Chrome reader is OBSERVE ONLY, and loopback only.
#
# These are not style checks. VIKIVERSE_V1 2.2c defines four rungs of authority
# and this extension claims the lowest one, `observe`. A claim like that is
# worth exactly as much as the check that enforces it, so:
#
#   R1-R5  no code path can act as Warren -- no click, submit, synthetic event,
#          innerHTML, or value assignment anywhere in the extension
#   R6     the only network destination is 127.0.0.1. Everything this reads is
#          private correspondence; a configurable endpoint is one mistyped
#          field away from posting it to a stranger, and no convenience is
#          worth that, so there is deliberately no setting for it
#   R7-R8  every extractor reports a STATUS, so "nothing there" and "I cannot
#          see any more" are different answers. A scraper that silently returns
#          zero when its selectors break launders breakage into false calm --
#          the exact coverage lie 2.5 forbids
#
# Comments are stripped before matching, or the file's own explanation of what
# it does not do would fail the check that it does not do it.
#
# Usage: sh build/reader-probe.sh
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D="$ROOT/edge/chrome"
PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); echo "  PASS  $1"; }
no(){ FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

[ -d "$D" ] || { echo "no extension at $D"; exit 2; }

# strip // and /* */ comments so prose about .click() is not mistaken for a call
code(){ cat "$D"/background.js "$D"/popup.js "$D"/content/*.js 2>/dev/null \
        | sed 's://.*::' | sed 's:/\*.*\*/::' | grep -v '^[[:space:]]*\*' ; }

echo "== observe only: no path can act as Warren =="
# parens escaped: these are ERE, where a bare ( opens a group
for pat in '\.click\(' '\.submit\(' 'dispatchEvent' 'execCommand'; do
  n=$(code | grep -cE "$pat" || true)
  if [ "$n" = 0 ]; then ok "R: no $pat"; else no "R: found $pat ($n)"; fi
done
n=$(code | grep -cE 'innerHTML[[:space:]]*=|document\.write' || true)
if [ "$n" = 0 ]; then ok "R5 no innerHTML / document.write"; else no "R5 found DOM writes ($n)"; fi

echo "== loopback only =="
hosts=$(code | grep -ohE 'https?://[a-zA-Z0-9.:/*-]+' | grep -v '127\.0\.0\.1' | sort -u)
if [ -z "$hosts" ]; then ok "R6 the only network destination is 127.0.0.1"
else no "R6 non-loopback destination in code: $hosts"; fi

echo "== a broken scraper must not look like a quiet day =="
for f in facebook discord; do
  n=$(grep -c "'blind'" "$D/content/$f.js" 2>/dev/null || echo 0)
  if [ "${n:-0}" -ge 1 ]; then ok "R7 $f reports 'blind' when its anchor is gone"
  else no "R7 $f has no blind path -- selector breakage would read as zero items"; fi
done

echo "== manifest =="
if command -v python3 >/dev/null 2>&1; then
  if python3 -c "
import json,sys
m=json.load(open('$D/manifest.json'))
assert m['manifest_version']==3
assert all(h.startswith('http://127.0.0.1') or h.startswith('https://') for h in m['host_permissions'])
banned={'tabs','webRequest','scripting','cookies','history','downloads'}
assert not (banned & set(m['permissions'])), m['permissions']
" 2>/dev/null
  then ok "R9 MV3, and no tabs/scripting/cookies/history permission"
  else no "R9 manifest is invalid or requests a permission a reader does not need"; fi
else
  echo "  SKIP  R9 (no python3)"
fi

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = 0 ]
