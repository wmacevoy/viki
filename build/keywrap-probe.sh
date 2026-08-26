#!/bin/sh
# keywrap-probe.sh -- key custody: age-compatible wrapping + identity.db.
#
# The property that matters is INTEROPERABILITY, not that our own round-trip
# closes. A custodial format nobody else can read is a way to lose data, so
# the point of matching age v1 byte-for-byte is that a stock `age` can recover
# a tribe key if these tools are ever lost. I1-I3 are that claim, and they are
# the assertions to care about; they SKIP rather than pass when age is absent.
#
# Non-vacuity: an early version of viki-key-wrap passed its own round-trip
# while emitting secret keys real `age` rejected as "invalid checksum" -- the
# bech32 checksum must be computed over the LOWERCASE hrp even when the key is
# rendered upper case. Only the interop leg caught it.
#
# Usage: sh build/keywrap-probe.sh <empty-dir>
set -e
DIR="$1"; [ -n "$DIR" ] || { echo "usage: $0 <empty-dir>"; exit 2; }
mkdir -p "$DIR"; DIR=$(cd "$DIR" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W="${VIKI_KEYWRAP_BIN:-$ROOT/build/dist/viki-key-wrap}"
I="${VIKI_IDENTITY_BIN:-$ROOT/build/dist/viki-identity}"
[ -x "$W" ] && [ -x "$I" ] || { echo "build first: sh edge/tools/build-tools.sh"; exit 2; }
PASS=0; FAIL=0; SKIP=0
ok(){ PASS=$((PASS+1)); echo "  PASS  $1"; }
no(){ FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
sk(){ SKIP=$((SKIP+1)); echo "  SKIP  $1"; }
cd "$DIR"

KEY="x'$(printf 'c%.0s' $(seq 64))'"
printf '%s' "$KEY" > tribe.key

echo "== identity.db =="
"$I" init --db id.db >/dev/null
PUB=$(printf 'pass one\n' | "$I" add warren --db id.db 2>/dev/null)
PUB2=$(printf 'pass two\n' | "$I" add agent  --db id.db 2>/dev/null)
case "$PUB" in age1*) ok "W1 add mints an age recipient" ;; *) no "W1 (got '$PUB')" ;; esac

# The container is SQLCipher under a KNOWN key: the secrecy is per-identity,
# but the per-page HMAC still gives tamper detection. Assert it is not a plain
# sqlite file, or someone will "simplify" it into one.
if command -v sqlite3 >/dev/null 2>&1; then
  if sqlite3 id.db "SELECT count(*) FROM identity;" >/dev/null 2>&1
  then no "W2 identity.db is readable as PLAIN sqlite (the container lost its codec)"
  else ok "W2 identity.db is a SQLCipher container, not plain sqlite"; fi
else sk "W2 (no sqlite3 on PATH)"; fi

echo "== wrapping =="
"$W" wrap -r "$PUB" -r "$PUB2" -i tribe.key -o tribe.age
printf 'pass one\n' | "$I" unwrap warren -i tribe.age -o got1.key --db id.db >/dev/null 2>&1
if cmp -s tribe.key got1.key; then ok "W3 the tribe key round-trips through identity.db"; else no "W3"; fi
printf 'pass two\n' | "$I" unwrap agent -i tribe.age -o got2.key --db id.db >/dev/null 2>&1
if cmp -s tribe.key got2.key; then ok "W4 a second recipient opens the SAME file independently"; else no "W4"; fi

rm -f bad.key
printf 'not the passphrase\n' | "$I" unwrap warren -i tribe.age -o bad.key --db id.db >/dev/null 2>&1 || true
if [ ! -s bad.key ]; then ok "W5 CONTROL: a wrong passphrase recovers nothing"; else no "W5 wrong passphrase produced output"; fi

"$W" keygen > stranger.txt
SSEC=$(grep '^AGE-SECRET-KEY' stranger.txt)
rm -f none.key
"$W" unwrap -k "$SSEC" -i tribe.age -o none.key >/dev/null 2>&1 || true
if [ ! -s none.key ]; then ok "W6 CONTROL: an unrelated identity recovers nothing"; else no "W6"; fi

echo "== the tribe registry =="
# The registry is what makes identity.db the vikiverse's key custody rather
# than just a keyring: it records which tribes exist, where to pull each from,
# and which identity opens it -- with the tribe key age-wrapped to that
# identity, never stored in the clear.
TK="x'$(printf 'e%.0s' $(seq 64))'"
printf '%s' "$TK" > tribe1.key
"$I" tribe add camp -r warren --key-file tribe1.key --url http://localhost:8770/ \
     --cache camp.db --caching required --db id.db >/dev/null
if "$I" tribe list --db id.db | grep -q '^camp .*required .*via warren'
then ok "R1 tribe list shows the tribe, its tier and its identity"
else no "R1"; fi

# The point of wrapping: grepping the file must not find the key.
if strings id.db 2>/dev/null | grep -q "eeeeeeeeeeee"
then no "R2 the tribe key is stored IN THE CLEAR in identity.db"
else ok "R2 identity.db holds no plaintext tribe key"; fi

GOT=$(printf 'pass one\n' | "$I" tribe key camp --db id.db 2>/dev/null || true)
if [ "$GOT" = "$TK" ]; then ok "R3 the tribe key is recovered with the owning passphrase"
else no "R3 (got '$GOT')"; fi

OUT=$(printf 'wrong\n' | "$I" tribe key camp --db id.db 2>&1 || true)
case "$OUT" in *"$TK"*) no "R4 a wrong passphrase still yielded the key" ;;
                    *) ok "R4 CONTROL: a wrong passphrase yields nothing" ;; esac

# A tribe registered to ANOTHER identity needs THAT identity's passphrase --
# holding one tribe grants nothing about another on the same device.
"$I" tribe add other -r agent --key-file tribe1.key --db id.db >/dev/null
OUT=$(printf 'pass one\n' | "$I" tribe key other --db id.db 2>&1 || true)
case "$OUT" in *"$TK"*) no "R5 another identity's tribe opened with the wrong passphrase" ;;
                    *) ok "R5 CONTROL: a tribe owned by another identity needs ITS passphrase" ;; esac

echo "== signing: authority, where the Merkle chain gives only integrity =="
# Warren: "infrastructure versions are signed? i think that closes the loop."
# A versioned artifact is Merkle-linked, so INTEGRITY is checkable -- nobody can
# alter it after the fact undetected. But any tribe member can COMMIT one, so
# integrity is not authority. The signature is the missing half.
printf 'pass one\n' | "$I" add signer --db id.db >/dev/null 2>&1
SPUB=$("$I" signpub signer --db id.db 2>/dev/null || true)
case "$SPUB" in ?*) ok "S1 an identity mints an ed25519 signing key" ;;
                 *) no "S1 no signing key minted" ;; esac

printf '{"model_id":"epoch-1","model_sha256":"deadbeef"}' > pin.json
SIG=$(printf 'pass one\n' | "$I" sign signer -i pin.json --db id.db 2>/dev/null || true)
case "$SIG" in ?*) ok "S2 it signs an epoch pin" ;; *) no "S2 signing produced nothing" ;; esac

# VERIFICATION MUST NEED NO KEYS AND NO PASSPHRASE -- a peer checking a pin
# holds nothing, and if verifying required a secret it would be useless.
if "$I" verify -p "$SPUB" -i pin.json -s "$SIG" >/dev/null 2>&1
then ok "S3 verification needs NO identity.db and NO passphrase"
else no "S3 verification failed on a good signature"; fi

printf '{"model_id":"epoch-1","model_sha256":"deadbeee"}' > pin.json
rc=0; "$I" verify -p "$SPUB" -i pin.json -s "$SIG" >/dev/null 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then ok "S4 CONTROL: one changed byte fails verification"
else no "S4 tampered file still verified"; fi

# A DIFFERENT signer must not verify -- otherwise the signature proves nothing
# about WHO, which is the only thing it was added for.
printf 'pass two\n' | "$I" add other --db id.db >/dev/null 2>&1
OPUB=$("$I" signpub other --db id.db 2>/dev/null || true)
printf '{"model_id":"epoch-1","model_sha256":"deadbeef"}' > pin.json
rc=0; "$I" verify -p "$OPUB" -i pin.json -s "$SIG" >/dev/null 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then ok "S5 CONTROL: another identity's key does not verify it"
else no "S5 a different signer verified -- the signature proves nothing about who"; fi

echo "== identity.db is PRIVATE: refused by code, not by convention =="
# SYNC.md classes blobs derived / grow-only / owned / private. `private` is the
# only class with no safe sync at any frequency, and identity.db is the live
# case: passphrase-wrapped private keys inside a container whose key is public
# by design (QUEUE 49 keeps it for the per-page HMAC). Publishing it hands every
# wrapped key to an offline attacker with no rate limit.
cp id.db identity.db 2>/dev/null || :
# `|| rc=$?` rather than a bare call: this probe runs under `set -e`, and the
# command under test is REQUIRED to fail -- without the guard the suite dies at
# exactly the assertion that is passing. Same trap promise-probe.sh hit.
rc=0
"$ROOT/build/dist/viki" cache push identity.db >/dev/null 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then ok "R6 pushing identity.db is REFUSED (exit nonzero)"
else no "R6 pushing identity.db was ALLOWED"; fi
OUT=$("$ROOT/build/dist/viki" cache push identity.db 2>&1 || true)
case "$OUT" in *"REFUSING to publish"*) ok "R7 ...and says why, naming it a private blob" ;;
                                     *) no "R7 refused without explaining" ;; esac
# The control that keeps R6 from passing for the wrong reason: a NON-private
# path must fail differently (or succeed), not with the same refusal.
OUT2=$("$ROOT/build/dist/viki" cache push /nonexistent/cache.db 2>&1 || true)
case "$OUT2" in *"REFUSING to publish"*) no "R8 CONTROL: a normal path was also refused as private" ;;
                                      *) ok "R8 CONTROL: a normal path is not refused as private" ;; esac

echo "== interop with stock age (the reason for this format) =="
if command -v age >/dev/null 2>&1 && command -v age-keygen >/dev/null 2>&1; then
  "$W" keygen > id.txt
  SEC=$(grep '^AGE-SECRET-KEY' id.txt); P=$(grep '^# public key:' id.txt | awk '{print $4}')
  printf '%s\n' "$SEC" > age-id.txt; chmod 600 age-id.txt
  D=$(age-keygen -y age-id.txt 2>/dev/null || true)
  if [ "$D" = "$P" ]; then ok "I1 age-keygen derives the SAME public key from our secret"
  else no "I1 (age said '$D', we said '$P')"; fi
  "$W" wrap -r "$P" -i tribe.key -o ours.age
  if age -d -i age-id.txt ours.age > viaage.key 2>/dev/null && cmp -s tribe.key viaage.key
  then ok "I2 stock \`age -d\` reads a file WE wrote"; else no "I2"; fi
  age -r "$P" -o theirs.age tribe.key 2>/dev/null
  if "$W" unwrap -k "$SEC" -i theirs.age -o viaus.key >/dev/null 2>&1 && cmp -s tribe.key viaus.key
  then ok "I3 we read a file STOCK AGE wrote"; else no "I3"; fi
else
  sk "I1 (no age on PATH -- brew install age)"
  sk "I2 (no age on PATH)"
  sk "I3 (no age on PATH)"
fi

echo
echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" = 0 ]
