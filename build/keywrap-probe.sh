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
PUB=$(printf 'pass one\n' | "$I" add warren --db id.db)
PUB2=$(printf 'pass two\n' | "$I" add agent  --db id.db)
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
