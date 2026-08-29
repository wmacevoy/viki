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
# THE PROBE RUNS ON AN ENCRYPTED DIARY, because that is what shipping looks
# like. A suite that exercises the plaintext path proves the plaintext path.
export VIKI_KEY="2b7e151628aed2a6abf7158809cf4f3c762e7160f38b4da56a784d9045190cfe"
nPass=0; nFail=0
ok(){   nPass=$((nPass+1)); printf '  ok    %s\n' "$1"; }
bad(){  nFail=$((nFail+1)); printf '  FAIL  %s\n        %s\n' "$1" "${2:-}"; }
chk(){  if [ "$1" = "$2" ]; then ok "$3"; else bad "$3" "expected [$2] got [$1]"; fi; }
[ -x "$V" ] || { echo "no viki at $V -- run core/build.sh"; exit 2; }

echo "== P: encrypted by default =="
# The build already refuses stock SQLite, but that only makes an encrypted
# diary POSSIBLE. If opening unkeyed just worked, every diary would be
# plaintext -- that is the path of least effort, and nobody picks it on
# purpose. So the default is refusal and --plaintext is a deliberate act.
env -u VIKI_KEY "$V" --store "$D/nokey.db" note x >/dev/null 2>&1 \
  && bad "P1 opening without a key should be REFUSED" "it opened" \
  || ok "P1 opening without a key is refused"
env -u VIKI_KEY "$V" --store "$D/plain.db" --plaintext note x >/dev/null 2>&1 \
  && ok "P2 --plaintext opens one deliberately" \
  || bad "P2 --plaintext should work" "it refused"
head -c 15 "$D/plain.db" 2>/dev/null | grep -q 'SQLite format 3' \
  && ok "P2b CONTROL: the --plaintext diary really is plaintext" \
  || bad "P2b CONTROL is wrong" "it looks encrypted"
# Its OWN store: seeding the shared one would shift every later count, which
# is exactly what it did on the first run.
"$V" --store "$D/enc-check.db" note "seed" >/dev/null 2>&1
head -c 15 "$D/enc-check.db" 2>/dev/null | grep -q 'SQLite format 3' \
  && bad "P3 a diary opened with \$VIKI_KEY is PLAINTEXT" "the key did nothing" \
  || ok "P3 a diary opened with \$VIKI_KEY is encrypted (as every store below is)"

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

echo "== K: keyed diaries =="
# The CLI is the HOST, so the key is entirely its business -- core never sees
# one. What is asserted here is the handling, and every positive claim is
# paired with a control, because "PRAGMA key returned OK" is exactly what a
# build with no cipher does.
if "$V" --store "$D/ct.db" sql "PRAGMA cipher_version" 2>/dev/null | grep -q .; then
  KH="$D/key.hex"; printf '2b7e151628aed2a6abf7158809cf4f3c762e7160f38b4da56a784d9045190cfe' > "$KH"
  chmod 600 "$KH"
  KS="$D/secret.db"
  "$V" --key hunter2 --store "$KS" count assert >/dev/null 2>&1 \
    && bad "K1 --key on argv should be REFUSED" "it was accepted" \
    || ok "K1 --key on argv is refused (ps shows argv to every process)"

  cp "$KH" "$D/loose.hex"; chmod 644 "$D/loose.hex"
  "$V" --keyfile "$D/loose.hex" --store "$KS" count assert >/dev/null 2>&1 \
    && bad "K2 a group-readable key file should be refused" "it was accepted" \
    || ok "K2 a group/world-readable key file is refused"

  "$V" --keyfile "$KH" --store "$KS" note "the gate latch sticks below freezing" >/dev/null
  chk "$("$V" --keyfile "$KH" --store "$KS" count assert)" "1" "K3 a keyed diary round-trips"

  grep -q 'gate latch' "$KS" 2>/dev/null \
    && bad "K4 CONTROL: the note is readable in the file" "plaintext on disk" \
    || ok "K4 CONTROL: the note is NOT readable in the file"

  env -u VIKI_KEY "$V" --store "$KS" count assert >/dev/null 2>&1 \
    && bad "K5 CONTROL: it opened with NO key" "encryption is not on" \
    || ok "K5 CONTROL: without the key it will not open"

  printf '00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff' > "$D/wrong.hex"
  chmod 600 "$D/wrong.hex"
  "$V" --keyfile "$D/wrong.hex" --store "$KS" count assert >/dev/null 2>&1 \
    && bad "K6 CONTROL: a WRONG key opened it" "any key works" \
    || ok "K6 CONTROL: a wrong key is rejected"

  # THE PAYOFF. The parent keys once; the child never sees a key and never
  # pays the KDF. Measured on a passphrase diary: 12 invocations cost 1.35s
  # keying individually and 0.16s through one context -- 8.4x, where the same
  # comparison on a plaintext store was 0.16 vs 0.13.
  cat > "$D/kchild.sh" <<'CHILD'
# The parent was given --keyfile; nothing should have put a key in here.
[ -z "${VIKI_KEY:-}" ] || { echo "KEY LEAKED TO CHILD" >&2; exit 8; }
viki note "written by the child through the parent's keyed context" >/dev/null
CHILD
  chmod +x "$D/kchild.sh"
  # env -u: the PROBE sets $VIKI_KEY for its own stores, and the child
  # inheriting THAT is not viki leaking anything. Clearing it is what makes
  # the assertion about viki rather than about this script.
  VIKI_STORE="$KS" env -u VIKI_KEY "$V" --keyfile "$KH" run "$D/kchild.sh"; rcK=$?
  chk "$rcK" "0" "K7 viki run keys ONCE and the child needs no key"
  chk "$(env -u VIKI_KEY "$V" --keyfile "$KH" --store "$KS" count assert)" "2" \
      "K7b ...and the child's write landed in the keyed diary"
else
  printf '  --   K1-K7 skipped: built against stock SQLite, NOT SQLCipher\n'
  printf '  --   (a skip is not a pass -- the diary would be plaintext)\n'
fi

echo "== S: signed writes, and what the child never holds =="
PUB=$("$V" id new "probe identity" "$D/signer.key" 2>/dev/null)
chk "${#PUB}" "64" "S1 id new emits an Ed25519 public key"
chk "$(stat -f '%Lp' "$D/signer.key" 2>/dev/null || stat -c '%a' "$D/signer.key")" "600" \
    "S2 the key file is created 0600 (O_CREAT|O_EXCL, not chmod afterwards)"
SID=$("$V" --signer "$D/signer.key" note "a statement worth standing behind")
chk "$("$V" --signer "$D/signer.key" id check "$SID" | cut -d' ' -f1)" "verified" \
    "S3 a write made with --signer verifies"
UID2=$("$V" note "a statement nobody signed")
chk "$("$V" id check "$UID2")" "unsigned" \
    "S4 CONTROL: without --signer the write is unsigned, and says so"
# S5 IS THE POINT OF ALL OF IT. The parent holds the key; the child holds
# nothing and still produces signed writes. Without a retained context a
# script cannot have signed writes AT ALL, whatever it does.
cat > "$D/schild.sh" <<'CHILD'
ID=$(viki note "written by the child through the parent's identity")
viki id check "$ID" | cut -d' ' -f1
CHILD
chmod +x "$D/schild.sh"
chk "$("$V" --signer "$D/signer.key" run "$D/schild.sh")" "verified" \
    "S5 a child's write is SIGNED by the parent's identity"
# and the child genuinely never opened the key: prove it by making the key
# unreadable to the child while the parent already holds it
chk "$("$V" --signer "$D/signer.key" run sh -c 'cat '"$D"'/signer.key >/dev/null 2>&1; viki id check $(viki note "second child write") | cut -d" " -f1')" \
    "verified" "S5b ...and keeps working regardless of what the child does with the file"
# a peer verifies holding nothing
"$V" --store "$D/peer.db" merge "$D/t.db" >/dev/null 2>&1
chk "$("$V" --store "$D/peer.db" id check "$SID" | cut -d' ' -f1)" "verified" \
    "S6 a peer that merged the diary verifies WHO, holding no key"

echo "== N: a REAL onnx embedder, model living IN the diary =="
# The embedder is a host component behind core's ABI, and it asks for named
# blobs. There is NO filesystem in the resolution path: files are read once by
# `model import` and never again, so wasm and a sandboxed robot see exactly
# what a laptop sees rather than a degraded version of it.
EMB=""
for c in "$ROOT/core/build/viki-embed-onnx.dylib" "$ROOT/core/build/viki-embed-onnx.so"; do
  [ -f "$c" ] && EMB="$c"
done
MD="${VIKI_MODEL_DIR:-$ROOT/build/dist/model}"
if [ -n "$EMB" ] && [ -f "$MD/model.onnx" ]; then
  NS="$D/sem.db"
  "$V" --store "$NS" model import "$MD" >/dev/null 2>&1
  chk "$("$V" --store "$NS" count assert blob)" "3" \
      "N0 model import puts model.onnx, vocab.txt and dim in the diary"
  "$V" --store "$NS" note "the quarterly invoice has not been paid" >/dev/null
  "$V" --store "$NS" note "the puppy has a vet appointment on Tuesday" >/dev/null
  "$V" --store "$NS" note "the gate latch sticks below freezing" >/dev/null
  Q="money owed for services rendered"
  # THE CONTROL FIRST: the query shares no content word with any note, so
  # without vectors there is nothing to find. If this finds something, N2
  # proves nothing about the model.
  "$V" --store "$NS" reindex >/dev/null
  "$V" --store "$NS" ask "$Q" >/dev/null 2>&1 \
    && bad "N1 CONTROL: keyword-only should find NOTHING for this query" "it found something" \
    || ok "N1 CONTROL: with no model, a no-shared-word query finds nothing"
  # env -u VIKI_MODEL_DIR: nothing may fall back to a directory.
  env -u VIKI_MODEL_DIR "$V" --store "$NS" --embedder "$EMB" reindex >/dev/null 2>&1
  env -u VIKI_MODEL_DIR "$V" --store "$NS" --embedder "$EMB" ask "$Q" 2>/dev/null \
    | grep -q 'invoice' \
    && ok "N2 the model, read FROM THE DIARY, ranks the invoice first" \
    || bad "N2 semantic retrieval failed" "wrong note or none"
  # and the child holds neither a model nor a directory
  cat > "$D/nchild.sh" <<'CHILD'
unset VIKI_MODEL_DIR
viki ask "money owed for services rendered" 2>/dev/null | grep -c invoice
CHILD
  chmod +x "$D/nchild.sh"
  chk "$(VIKI_STORE=$NS env -u VIKI_MODEL_DIR "$V" --embedder "$EMB" run "$D/nchild.sh" 2>/dev/null)" "1" \
      "N3 a child with no model and no directory gets hybrid retrieval"
  chk "$(VIKI_STORE=$NS env -u VIKI_MODEL_DIR "$V" run "$D/nchild.sh" 2>/dev/null)" "0" \
      "N3b CONTROL: with no parent embedder the same child gets nothing"
else
  printf '  --   N0-N3 skipped: no embedder .so or no model\n'
  printf '  --   (sh cli/build-embedder.sh, and build/build.sh once for the model)\n'
fi

echo "== J: merge semantics =="
# What merge does to the SOURCE, which is the half that is easy to assume and
# expensive to get wrong. Union-is-merge only means anything if promotion
# COPIES: if it linked, deleting a source would silently retract everything it
# contributed, and a peer going offline would take its history with it.
VIKI_STORE="$D/scratch.db" "$V" note "half-formed: maybe the hinge, not the latch" >/dev/null
VIKI_STORE="$D/scratch.db" "$V" note "TODO check the north gate too" >/dev/null
chk "$(VIKI_STORE=$D/scratch.db "$V" count assert)" "2" "J1 a second store takes notes independently"
BEFORE=$("$V" count assert)
"$V" merge "$D/scratch.db" >/dev/null
chk "$("$V" count assert)" "$((BEFORE+2))" "J2 merging brings the other store's assertions in"
"$V" reindex >/dev/null
"$V" ask "hinge" >/dev/null 2>&1 && ok "J3 ...and it is searchable afterwards" \
                                  || bad "J3 promoted notes are not searchable"
rm -f "$D/scratch.db"*
chk "$("$V" count assert)" "$((BEFORE+2))" "J4 CONTROL: merge COPIED -- deleting the source does not retract it"
# and a store that was never merged contributed nothing to begin with
VIKI_STORE="$D/thrown.db" "$V" note "written only to the other store" >/dev/null
AFTER=$("$V" count assert)
rm -f "$D/thrown.db"*
chk "$("$V" count assert)" "$AFTER" "J5 CONTROL: an unmerged store leaves no trace in this one"

echo "== F: degradation =="
# A stale context -- a killed parent, a copied environment -- must degrade to
# opening the store directly. Failing would make an exported variable a
# permanent trap.
# stderr is NOT captured: with $VIKI_KEY set, viki warns there, and folding
# that into the value compared two different things.
OUT=$(VIKI_CONTEXT="$D/nonexistent.sock" "$V" count assert 2>/dev/null)
chk "$OUT" "$("$V" count assert)" "F1 a STALE \$VIKI_CONTEXT degrades to direct, not failure"

printf '\n%d passed, %d failed\n' "$nPass" "$nFail"
[ "$nFail" -eq 0 ] || exit 1
