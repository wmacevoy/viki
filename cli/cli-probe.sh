#!/bin/sh
# cli-probe.sh -- the CLI face, and `viki run`.
#
# The CLI is a BINDING: every verb is a thin call into viki-core, and the CLI
# is the HOST -- it resolves the path and opens the connection, which core
# never learns about. These assertions are about the face, not the library.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="$ROOT/core/build/viki"
D="${1:?usage: cli-probe.sh <empty-dir>}"
mkdir -p "$D" || exit 2
D=$(cd "$D" && pwd)
export VIKI_STORE="$D/t.db"
export PATH="$ROOT/core/build:$PATH"
nPass=0; nFail=0
ok(){   nPass=$((nPass+1)); printf '  ok    %s\n' "$1"; }
bad(){  nFail=$((nFail+1)); printf '  FAIL  %s\n        %s\n' "$1" "${2:-}"; }
chk(){  if [ "$1" = "$2" ]; then ok "$3"; else bad "$3" "expected [$2] got [$1]"; fi; }
[ -x "$V" ] || { echo "no viki at $V -- run core/build.sh"; exit 2; }

echo "== D: direct, with no context =="
ID=$("$V" note "the gate latch sticks below freezing")
chk "${#ID}" "64" "D1 note returns a content-addressed id"
ID2=$("$V" note "the gate latch sticks below freezing")
chk "$ID2" "$ID" "D2 the same text is the same assertion (content addressing)"
chk "$("$V" count assert)" "1" "D3 CONTROL: ...and stored once, not twice"
"$V" reindex >/dev/null
"$V" ask "gate latch" >/dev/null 2>&1 && ok "D4 ask finds it" || bad "D4 ask found nothing"
"$V" ask "zzzznotpresent" >/dev/null 2>&1 \
  && bad "D5 CONTROL: a query matching nothing should exit non-zero" \
  || ok "D5 CONTROL: a query matching nothing exits non-zero"

echo "== R: viki run =="
cat > "$D/child.sh" <<'CHILD'
[ -n "${VIKI_CONTEXT:-}" ] || { echo "NO CONTEXT" >&2; exit 9; }
viki note "written by the child through the parent's context" > /dev/null
viki note "the very last write, in flight at exit" > /dev/null
exit 7
CHILD
chmod +x "$D/child.sh"
"$V" run "$D/child.sh"; rc=$?
chk "$rc" "7" "R1 the CHILD's exit status is the wrapper's"
chk "$("$V" count assert)" "3" "R2 the child's writes landed in the PARENT's store"
# R3 is the drain: the last write is issued immediately before exit, so a
# server that stopped accepting the moment SIGCHLD arrived would lose it.
"$V" list 2>/dev/null | grep -q 'very last write' \
  && ok "R3 the write in flight at child exit is NOT lost" \
  || bad "R3 the final write was lost to the exit race"

echo "== S: what the context is, and is not =="
# The parent holds a decrypted store. A loopback TCP port would be reachable
# by every process of every user on the machine; a 0700 directory is not.
"$V" run sh -c 'ls -ld "$(dirname "$VIKI_CONTEXT")" > '"$D"'/perm.txt; echo "$VIKI_CONTEXT" > '"$D"'/sock.txt' >/dev/null 2>&1
PERM=$(cut -c1-10 < "$D/perm.txt" 2>/dev/null)
chk "$PERM" "drwx------" "S1 the socket lives in a 0700 directory"
SOCK=$(cat "$D/sock.txt" 2>/dev/null)
[ -n "$SOCK" ] && [ ! -e "$SOCK" ] \
  && ok "S2 the socket is unlinked when the command finishes" \
  || bad "S2 a socket was left behind" "$SOCK"
case "$SOCK" in
  *:*[0-9]) bad "S3 CONTROL: the context is a host:port, not a path" "$SOCK" ;;
  /*)       ok  "S3 CONTROL: the context is a filesystem path, never a port" ;;
  *)        bad "S3 CONTROL: unrecognised context form" "$SOCK" ;;
esac

echo "== F: degradation =="
# A stale context -- a killed parent, a copied environment -- must degrade to
# opening the store directly. Failing would make an exported variable a
# permanent trap.
OUT=$(VIKI_CONTEXT="$D/nonexistent.sock" "$V" count assert 2>&1)
chk "$OUT" "$("$V" count assert)" "F1 a STALE \$VIKI_CONTEXT degrades to direct, not failure"

printf '\n%d passed, %d failed\n' "$nPass" "$nFail"
[ "$nFail" -eq 0 ] || exit 1
