#!/bin/sh
# core/build.sh -- builds viki-core and its probe.
#
# NO DOWNLOADS, NO SUBMODULES, NO FOSSIL. The only inputs are the SQLite
# amalgamation this repo already caches and retain.h from the sibling
# retain-recall checkout. That short list IS the design: core's contract is
# SQLite and nothing else.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/core/build"
SQLITE="${VIKI_SQLITE_DIR:-$ROOT/vendor/download-cache/sqlite-amalgamation-3530400}"
RETAIN="${VIKI_RETAIN_DIR:-}"
if [ -z "$RETAIN" ]; then
  for c in "$ROOT/../retain-recall/ports/c" "$ROOT/vendor/retain-recall/ports/c"; do
    [ -f "$c/retain.h" ] && { RETAIN="$c"; break; }
  done
fi
[ -f "$SQLITE/sqlite3.c" ] || { echo "no sqlite3.c at $SQLITE"; exit 2; }
[ -f "$RETAIN/retain.h" ] || { echo "no retain.h (set VIKI_RETAIN_DIR)"; exit 2; }
mkdir -p "$OUT"
CFLAGS="-std=c11 -Wall -Wextra -Wno-unused-parameter -O2"
INC="-I$ROOT/core/include -I$ROOT/core/src -I$SQLITE -I$RETAIN"
# FTS5 is required: the keyword leg is not optional, and a build without it
# would degrade silently rather than fail here.
SQLFLAGS="-DSQLITE_ENABLE_FTS5 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1"
echo "==> sqlite3.o"
[ -f "$OUT/sqlite3.o" ] || cc $CFLAGS -w $SQLFLAGS -I$SQLITE -c "$SQLITE/sqlite3.c" -o "$OUT/sqlite3.o"
echo "==> viki-core"
cc $CFLAGS $INC -c "$ROOT/core/src/viki_core.c" -o "$OUT/viki_core.o"
cc $CFLAGS $INC -c "$ROOT/core/src/viki_cal.c"  -o "$OUT/viki_cal.o"
cc $CFLAGS $INC -c "$ROOT/core/src/sha256.c"    -o "$OUT/sha256.o"
ar rcs "$OUT/libvikicore.a" "$OUT/viki_core.o" "$OUT/viki_cal.o" "$OUT/sha256.o"
echo "==> probe"
cc $CFLAGS $INC -o "$OUT/core-probe" "$ROOT/core/test/core_probe.c" \
   "$OUT/libvikicore.a" "$OUT/sqlite3.o" -lm -lpthread
echo "==> built: $OUT/libvikicore.a  $OUT/core-probe"
