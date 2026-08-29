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
# ENCRYPTION AT REST IS THE BASELINE, not an option: a diary holds what
# someone told it in confidence, and "we will add encryption later" is a
# promise nobody keeps to a file already on disk. So the SQLite core is
# SQLCipher-LibreSSL from fossil-see's vendor tree -- already built there,
# nothing new downloaded (the same source edge/tools/build-tools.sh uses).
#
# Stock SQLite still builds, for a machine with no fossil-see checkout, and
# the probe reports which one it got rather than letting a plaintext diary
# pass for an encrypted one.
FS="${VIKI_FOSSILSEE_DIR:-}"
if [ -z "$FS" ]; then
  for c in "$ROOT/../fossil-see" "$ROOT/vendor/fossil-see"; do
    [ -f "$c/vendor/sqlcipher-libressl/sqlite3.c" ] && { FS="$c"; break; }
  done
fi
SQLITE="${VIKI_SQLITE_DIR:-}"
VIKI_CRYPTO=""
if [ -z "$SQLITE" ] && [ -n "$FS" ] && [ -f "$FS/vendor/sqlcipher-libressl/sqlite3.c" ]; then
    SQLITE="$FS/vendor/sqlcipher-libressl"
    VIKI_CRYPTO="$FS/vendor/libressl-build-out"
fi
[ -n "$SQLITE" ] || SQLITE="$ROOT/vendor/download-cache/sqlite-amalgamation-3530400"
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
LIBS="-lm -lpthread"
if [ -n "$VIKI_CRYPTO" ]; then
    # SQLITE_HAS_CODEC + SQLCIPHER_CRYPTO_OPENSSL is what turns PRAGMA key into
    # real encryption rather than a no-op that returns SQLITE_OK.
    # SQLCipher refuses to compile without these two -- it #errors rather than
    # building something that would silently not encrypt, which is the right
    # way round and worth keeping visible.
    SQLFLAGS="$SQLFLAGS -DSQLITE_HAS_CODEC -DSQLITE_TEMP_STORE=2 -DSQLCIPHER_CRYPTO_OPENSSL"
    SQLFLAGS="$SQLFLAGS -DSQLITE_EXTRA_INIT=sqlcipher_extra_init"
    SQLFLAGS="$SQLFLAGS -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown"
    SQLFLAGS="$SQLFLAGS -I$VIKI_CRYPTO/include"
    LIBS="$VIKI_CRYPTO/lib/libcrypto.a $LIBS"
    echo "==> SQLCipher-LibreSSL (encryption at rest)"
else
    echo "==> stock SQLite (NO encryption at rest)"
fi
echo "==> sqlite3.o"
[ -f "$OUT/sqlite3.o" ] && [ "$OUT/sqlite3.o" -nt "$SQLITE/sqlite3.c" ] \
  || cc $CFLAGS -w $SQLFLAGS -I$SQLITE -c "$SQLITE/sqlite3.c" -o "$OUT/sqlite3.o"
echo "==> viki-core"
cc $CFLAGS $INC -c "$ROOT/core/src/viki_core.c" -o "$OUT/viki_core.o"
cc $CFLAGS $INC -c "$ROOT/core/src/viki_cal.c"  -o "$OUT/viki_cal.o"
cc $CFLAGS $INC -c "$ROOT/core/src/sha256.c"    -o "$OUT/sha256.o"
ar rcs "$OUT/libvikicore.a" "$OUT/viki_core.o" "$OUT/viki_cal.o" "$OUT/sha256.o"
echo "==> cli"
cc $CFLAGS $INC -o "$OUT/viki" "$ROOT/cli/viki_cli.c" "$OUT/libvikicore.a" "$OUT/sqlite3.o" $LIBS
echo "==> probe"
cc $CFLAGS $INC -o "$OUT/core-probe" "$ROOT/core/test/core_probe.c" \
   "$OUT/libvikicore.a" "$OUT/sqlite3.o" $LIBS
echo "==> built: $OUT/libvikicore.a  $OUT/core-probe"
