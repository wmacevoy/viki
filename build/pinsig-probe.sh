#!/bin/sh
# pinsig-probe.sh -- the epoch pin's SIGNATURE, where `viki cache pull` uses it.
#
# What this is proving, and why it is not the same as the keywrap probe's
# S-series: that one proves ed25519 sign/verify works. This one proves viki
# ACTS on the result -- refuses a bad pin, and, more importantly, does not
# report a check it never ran.
#
# THE ASSERTION THAT MATTERS IS N3. A signed pin with no verifier installed
# must NOT print like a verified one. A security check that silently does not
# run is worse than no check, because it is believed. Everything else here is
# ordinary; N3 is the reason the file exists.
#
# NO `set -e`: several CONTROLs are `grep -c ... = 0`, and grep exits 1 on zero
# matches, which would abort the suite exactly when the code is correct. That
# has bitten this repo twice (promise-probe, keywrap-probe).
#
# NON-VACUITY, measured 2026-08-25: a binary built from the commit BEFORE the
# signature check scores 7 passed, 10 failed here. Of the 7, five are CONTROLs
# and the N0 gate, which SHOULD pass against any working binary. The other two
# are worth naming: N11 and N13 pass on the old binary because
# `--require-signature` is unknown there and falls through to being taken as a
# DB PATH, so the pull dies early and looks like it stopped at the gate. That is
# precisely the bug N14 exists to catch, and it is why N14 was added --
# a flag that silently becomes a filename disables the check the user asked for.

ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="${VIKI_BIN:-$ROOT/build/dist/viki}"
I="${VIKI_IDENTITY_BIN:-$ROOT/build/dist/viki-identity}"
DIR="$1"
[ -n "$DIR" ] || { echo "usage: sh build/pinsig-probe.sh <empty-dir>"; exit 2; }
[ -x "$V" ] || { echo "no viki at $V"; exit 2; }
[ -x "$I" ] || { echo "build first: sh edge/tools/build-tools.sh"; exit 2; }

# A REAL REPO IS REQUIRED, and finding that out is the reason this block
# exists. The first draft of this probe ran `viki cache pull` in a bare
# directory, where it dies at `fossil uv sync` long before the signature code
# is reached -- and scored 7 PASS, every one of them an `exit != 0` assertion
# passing because the command failed for an unrelated reason. Vacuous green is
# this project's signature failure mode; refuse to run rather than reproduce it.
FOSSIL="${VIKI_FOSSIL_BIN:-}"
if [ -z "$FOSSIL" ]; then
    if command -v fossil-see >/dev/null 2>&1; then FOSSIL=fossil-see
    elif command -v fossil >/dev/null 2>&1; then FOSSIL=fossil
    else echo "no fossil; set VIKI_FOSSIL_BIN" >&2; exit 2; fi
fi
export VIKI_FOSSIL_BIN="$FOSSIL"

mkdir -p "$DIR" || exit 2
DIR=$(cd "$DIR" && pwd)
export FOSSIL_HOME="$DIR/home"; mkdir -p "$FOSSIL_HOME"
export USER=viki
export VIKI_FOSSIL_USER=viki
LOG="$DIR/log"; mkdir -p "$LOG"

# THE BASELINE MUST HAVE A WORKING VERIFIER, or "cannot check" is the only
# state the probe can ever observe and N3 passes for the wrong reason -- which
# it did on the first run here. The tests that want NO verifier override this
# per-command; the default has to be the working one.
export VIKI_IDENTITY_BIN="$I"

PASS=0; FAIL=0
ok(){ PASS=$((PASS+1)); echo "  PASS  $1"; }
no(){ FAIL=$((FAIL+1)); echo "  FAIL  $1"; }

# Hub + a real clone, so `uv sync` succeeds and the pull actually reaches the
# model leg where the signature is checked.
"$FOSSIL" init "$DIR/hub.fossil" -A viki >"$LOG/init.out" 2>&1 || { echo "init failed"; exit 2; }
( cd "$DIR" && "$FOSSIL" clone "file://$DIR/hub.fossil" spoke.fossil ) >"$LOG/clone.out" 2>&1
mkdir -p "$DIR/spoke"
( cd "$DIR/spoke" && "$FOSSIL" open "$DIR/spoke.fossil" ) >"$LOG/open.out" 2>&1
cd "$DIR/spoke" || exit 2

# The cache leg must succeed so the pull gets as far as pull_model. Publish an
# empty-but-valid cache from the spoke first.
"$V" index . >"$LOG/index.out" 2>&1
"$V" cache push >"$LOG/push.out" 2>&1
printf 'pass one\n' | "$I" add signer --db id.db >/dev/null 2>&1
SPUB=$("$I" signpub signer --db id.db 2>/dev/null)
printf 'pass two\n' | "$I" add other  --db id.db >/dev/null 2>&1
OPUB=$("$I" signpub other  --db id.db 2>/dev/null)

printf '{"model_id":"epoch-test","model_sha256":"aa","vocab_sha256":"bb"}' > viki-model.json
printf '{"signers":[{"name":"signer","ed25519":"%s"}]}' "$SPUB" > viki-signers.json

run_pull(){ "$V" cache pull "$@" 2>&1; }

# DID THE PULL STOP AT THE SIGNATURE GATE, or merely fail later?
#
# EXIT STATUS CANNOT ANSWER THAT, and assuming it could cost three assertions
# their meaning on the first pass. This probe publishes no model, so `cache
# pull` exits 1 whatever the signature says -- N7/N11/N13 were green against a
# binary with no signature code at all. The observable difference is that a
# refusal returns BEFORE the model blobs are fetched, and the fetch announces
# itself ("uv export viki-model/<file>"). Absence of that line is the refusal;
# presence of it means the gate was passed. Matched on the DIRECTORY, not on
# model.onnx: the fetch order is vocab.txt first (aModelFile[]), so naming one
# specific file read "stopped" on a pull that had plainly continued.
stopped_at_gate(){ [ "$(printf '%s' "$1" | grep -c 'uv export viki-model/')" -eq 0 ]; }

# NON-VACUITY GATE. Every assertion below reads output produced inside
# pull_model. If the pull cannot get there, they are all meaningless, so prove
# it arrives before trusting a single one of them.
echo "== the probe reaches the code it claims to test =="
OUT=$(run_pull)
if printf '%s' "$OUT" | grep -q 'epoch pin from' && ! stopped_at_gate "$OUT"; then
  ok "N0 the pull reaches the model leg AND gets past the gate when nothing refuses it"
else
  no "N0 pull never reached pull_model -- EVERYTHING BELOW IS VACUOUS"
  printf '%s\n' "$OUT" | tail -5
  echo "PASS=$PASS FAIL=$FAIL"; exit 1
fi

echo "== unsigned: reported, and NOT fatal by default =="
OUT=$(run_pull)
if printf '%s' "$OUT" | grep -q 'UNSIGNED'; then ok "N1 an unsigned pin says so"; else no "N1"; fi
if printf '%s' "$OUT" | grep -qi 'no authority'; then
  ok "N2 ...and names what is missing: authority, not integrity"
else no "N2"; fi

echo "== signed but uncheckable: MUST NOT read as verified =="
printf 'pass one\n' | "$I" sign signer -i viki-model.json --db id.db 2>/dev/null > viki-model.json.sig
[ -s viki-model.json.sig ] && ok "N2b the pin got a signature" || no "N2b no signature written"
# Hide the verifier by pointing at a path that does not exist.
OUT=$(VIKI_IDENTITY_BIN=/nonexistent/viki-identity run_pull)
if printf '%s' "$OUT" | grep -q 'CANNOT BE CHECKED'; then
  ok "N3 no verifier => 'CANNOT BE CHECKED', the state that must never be silent"
else no "N3 a signed pin with no verifier did not announce itself"; fi
if [ "$(printf '%s' "$OUT" | grep -c 'SIGNED by')" -eq 0 ]; then
  ok "N4 CONTROL: ...and it does NOT claim a signer"
else no "N4 it reported a signer while nothing verified it"; fi

echo "== signed and checkable =="
OUT=$(run_pull)
if printf '%s' "$OUT" | grep -q "SIGNED by 'signer'"; then
  ok "N5 a good signature verifies and NAMES the signer"
else no "N5 (got: $(printf '%s' "$OUT" | grep -i sign | head -2))"; fi

echo "== rejection =="
cp viki-model.json good.json
printf '{"model_id":"epoch-EVIL","model_sha256":"aa","vocab_sha256":"bb"}' > viki-model.json
OUT=$(run_pull); RC=$?
if printf '%s' "$OUT" | grep -q 'SIGNATURE REJECTED'; then
  ok "N6 an altered pin is REJECTED"
else no "N6 an altered pin was not rejected"; fi
if [ "$RC" -ne 0 ] && stopped_at_gate "$OUT"; then
  ok "N7 ...and it STOPS THERE with no flag -- rejection is evidence, not policy"
else no "N7 rejection did not stop the pull before the model fetch"; fi
cp good.json viki-model.json

# The one that makes the signature mean WHO rather than merely SOMETHING.
printf '{"signers":[{"name":"other","ed25519":"%s"}]}' "$OPUB" > viki-signers.json
OUT=$(run_pull)
if printf '%s' "$OUT" | grep -q 'SIGNATURE REJECTED' && stopped_at_gate "$OUT"; then
  ok "N8 CONTROL: a pin signed by an UNLISTED identity is refused"
else no "N8 a signature by an unlisted identity was accepted"; fi
printf '{"signers":[{"name":"signer","ed25519":"%s"}]}' "$SPUB" > viki-signers.json

echo "== the trust anchor is required, and only from the checkout =="
mv viki-signers.json anchor.bak
OUT=$(run_pull)
if printf '%s' "$OUT" | grep -q 'no trusted key'; then
  ok "N9 signed pin + no signer list => nothing verified, said plainly"
else no "N9"; fi
if [ "$(printf '%s' "$OUT" | grep -c 'SIGNED by')" -eq 0 ]; then
  ok "N10 CONTROL: ...and again no signer is claimed"
else no "N10"; fi
mv anchor.bak viki-signers.json

echo "== --require-signature turns the unknowable states into refusals =="
mv viki-model.json.sig sig.bak
OUT=$(run_pull --require-signature); RC=$?
if [ "$RC" -ne 0 ] && stopped_at_gate "$OUT"; then
  ok "N11 --require-signature stops an UNSIGNED pin at the gate"
else no "N11 unsigned pin was not stopped under --require-signature"; fi
OUT=$(run_pull); RC=$?
# Without the flag an unsigned pin proceeds past the signature gate; it may
# still fail later for want of a repo, so assert on the message, not on $?.
if printf '%s' "$OUT" | grep -q 'UNSIGNED' && ! stopped_at_gate "$OUT"; then
  ok "N12 CONTROL: ...and without the flag it is a notice and the pull CONTINUES"
else no "N12 unsigned pin without the flag did not proceed"; fi
mv sig.bak viki-model.json.sig

OUT=$(VIKI_IDENTITY_BIN=/nonexistent/viki-identity run_pull --require-signature); RC=$?
if [ "$RC" -ne 0 ] && stopped_at_gate "$OUT"; then
  ok "N13 --require-signature stops at the gate when it CANNOT CHECK"
else no "N13 uncheckable was not stopped under --require-signature"; fi
OUT=$(VIKI_IDENTITY_BIN=/nonexistent/viki-identity run_pull)
if ! stopped_at_gate "$OUT"; then
  ok "N13b CONTROL: ...and without the flag an uncheckable pin still proceeds"
else no "N13b uncheckable was refused without being asked to"; fi

echo "== an unknown flag is refused, not taken as a db path =="
OUT=$("$V" cache pull --require-sig 2>&1); RC=$?
if [ "$RC" -ne 0 ] && printf '%s' "$OUT" | grep -q "unknown option"; then
  ok "N14 a mistyped --require-sig is an error, not a silently disabled check"
else no "N14 a typo'd flag was swallowed"; fi

echo
echo "PASS=$PASS FAIL=$FAIL"
[ "$FAIL" -eq 0 ]
