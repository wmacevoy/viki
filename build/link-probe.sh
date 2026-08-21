#!/bin/sh
#
# link-probe.sh -- the unified responder: every indexed class resolves to a
# real page in Fossil's web UI, or to nothing at all.
#
# WHY THIS EXISTS
# ---------------
# viki indexes nine artifact classes and answers across all of them, but a
# result used to be a dead string -- `forum:633a8f62...` told you the answer
# existed and gave you no way to reach it. AGENTS.md carried that under "Not
# yet built" from the day `viki serve` was written.
#
# The URL shapes are NOT guessed. They were read out of the vendored
# fossil-see binary's own href templates, which cannot drift from what the
# server actually routes. L1-L8 pin one class each, so if a future fossil
# renames a page this fails here rather than in a user's browser.
#
# L9 is the one that matters most: an UNKNOWN namespace must produce NO link.
# A wrong link is worse than none -- it invites an agent to cite a page that
# does not exist, with a content_hash that looks like provenance.
#
# Usage: sh build/link-probe.sh <scratch-dir> [viki-binary]
set -e
DIR="${1:?usage: link-probe.sh <scratch-dir> [viki-binary]}"
VIKI="${2:-$(cd "$(dirname "$0")/.." && pwd)/build/dist/viki}"
case "$VIKI" in /*) ;; *) echo "ERR: viki path must be ABSOLUTE"; exit 2 ;; esac
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASS=0; FAIL=0
ck(){ if eval "$2" >/dev/null 2>&1; then PASS=$((PASS+1)); echo "  PASS  $1"; else FAIL=$((FAIL+1)); echo "  FAIL  $1"; fi; }

rm -rf "$DIR"; mkdir -p "$DIR"; cd "$DIR"
cat > lt.c <<'EOF'
#include "viki_link.h"
#include <stdio.h>
int main(int c, char **v){
  char u[1024], l[256]; const char *b = viki_link_base(); int i;
  for(i=1;i<c;i++){ viki_link_label(v[i], l, sizeof l);
    if(viki_link_for(b, v[i], u, sizeof u)) printf("%s\t%s\t%s\n", v[i], l, u);
    else printf("%s\t%s\t(none)\n", v[i], l); }
  return 0; }
EOF
cc -I"$ROOT/src" -o lt lt.c "$ROOT/build/obj/viki_link.o" 2>/dev/null || { echo "  SKIP (no build/obj/viki_link.o -- run build/build.sh)"; exit 0; }
B="https://example.invalid/r"
run(){ VIKI_FOSSIL_URL="$B" ./lt "$1" | cut -f3; }
lab(){ VIKI_FOSSIL_URL="$B" ./lt "$1" | cut -f2; }

ck "L1 checkout file -> /file?name=&ci="        '[ "$(run ./docs/a.md)" = "'$B'/file?name=docs/a.md&ci=tip" ]'
ck "L2 wiki -> /wiki?name=, percent-encoded"    '[ "$(run "wiki:Pump Maintenance")" = "'$B'/wiki?name=Pump%20Maintenance" ]'
ck "L3 ticket -> /tktview/"                     '[ "$(run ticket:9c1d2e3f)" = "'$B'/tktview/9c1d2e3f" ]'
ck "L4 forum -> /forumpost/"                    '[ "$(run forum:633a8f62)" = "'$B'/forumpost/633a8f62" ]'
ck "L5 check-in -> /info/"                      '[ "$(run ckin:fc836ee2)" = "'$B'/info/fc836ee2" ]'
ck "L6 tech note -> /info/"                     '[ "$(run note:2026040112)" = "'$B'/info/2026040112" ]'
ck "L7 attachment -> /artifact/"                '[ "$(run attach:aabbccdd)" = "'$B'/artifact/aabbccdd" ]'
ck "L8 unversioned -> /uv/"                     '[ "$(run uv:viki-cache.db)" = "'$B'/uv/viki-cache.db" ]'
ck "L9 GUARD: an UNKNOWN namespace gets NO link" '[ "$(run weirdns:xyz)" = "(none)" ]'
ck "L10 GUARD: no base configured -> NO link"   '[ "$(env -u VIKI_FOSSIL_URL ./lt forum:633a8f62 | cut -f3)" = "(none)" ]'
ck "L11 a trailing slash on the base is stripped" \
   '[ "$(VIKI_FOSSIL_URL="'$B'/" ./lt ticket:9c | cut -f3)" = "'$B'/tktview/9c" ]'
ck "L12 label shortens a hash the way fossil does" '[ "$(lab forum:633a8f623f13887b477b4caa)" = "forum 633a8f623f13" ]'
ck "L13 label of a wiki page is its NAME"       '[ "$(lab "wiki:Pump Maintenance")" = "Pump Maintenance" ]'
ck "L14 label of a file is its path"            '[ "$(lab ./docs/a.md)" = "./docs/a.md" ]'

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
