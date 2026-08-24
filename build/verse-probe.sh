#!/bin/sh
# verse-probe.sh -- the VIKIVERSE edge: several separately-keyed tribes, one
# question, attributed answers.
#
# The claim under test is not "multi-database works". It is that the edge can
# answer from a tribe the asker is NOT working in, which is the failure QUEUE 47
# recorded from life: SQLCipher-for-wasm existed one directory over and nothing
# in reach would have said so. V4/V5 replay exactly that question.
#
# V3 is the control that makes the rest mean anything: one tribe's key must not
# open another. Without it "several tribes" is just several files.
#
# Needs: the SQLCipher wasm build (edge/build-wasm-sqlcipher.sh), node, and a
# corpus. It REFUSES rather than passing vacuously when the build is missing.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
[ -f "$ROOT/edge/dist/viki-edge-sqlcipher.js" ] || {
  echo "no SQLCipher edge build -- run: sh edge/build-wasm-sqlcipher.sh"
  echo "REFUSING to run: without the codec build there are no per-tribe keys"
  echo "to test, and every assertion below would pass for the wrong reason."
  exit 2; }
command -v node >/dev/null 2>&1 || { echo "node not found"; exit 2; }
exec node "$ROOT/edge/tests/verse.mjs"
