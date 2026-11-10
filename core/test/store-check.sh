#!/bin/sh
# core/test/store-check.sh -- build the assertion store with ASan and UBSan,
# run its tests, and print the requirement gate. Exits nonzero on any red.
#
# SQLite is the baseline the store ships on: SQLCipher-LibreSSL, vendored at
# vendor/sqlcipher-libressl (Warren, 2026-11-09: "vendor sqlcipher-libressl
# this is the real baseline anyway"). It is built here once, out of tree, so
# the submodule stays clean. LibreSSL is not vendored: LIBRESSL_PREFIX (default
# $HOME/libressl) must hold the version pinned in that submodule's libressl.env,
# installed as its README says. There is no fallback to the system SQLite.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/core/build-store"
SC="$ROOT/vendor/sqlcipher-libressl"
LIBRESSL_PREFIX=${LIBRESSL_PREFIX:-$HOME/libressl}
mkdir -p "$OUT"

[ -f "$SC/configure" ] || { echo "vendor/sqlcipher-libressl is empty: git submodule update --init vendor/sqlcipher-libressl"; exit 2; }
[ -f "$LIBRESSL_PREFIX/lib/libcrypto.a" ] || { echo "no LibreSSL at $LIBRESSL_PREFIX: see vendor/sqlcipher-libressl/README.md, 'Install LibreSSL'"; exit 2; }

# One line: configure copies CFLAGS into a Makefile, where a newline breaks it.
SQLDEFS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL -DSQLITE_TEMP_STORE=2 -DSQLITE_THREADSAFE=1"
SQLDEFS="$SQLDEFS -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown"
SQLDEFS="$SQLDEFS -DSQLITE_ENABLE_FTS5"
# Rebuilt when the submodule moves; the stamp is the commit it was built from.
want=$(git -C "$SC" rev-parse HEAD)
if [ "$(cat "$OUT/sqlcipher/stamp" 2>/dev/null)" != "$want" ]; then
  rm -rf "$OUT/sqlcipher"; mkdir -p "$OUT/sqlcipher"
  ( cd "$OUT/sqlcipher" &&
    "$SC/configure" --with-tempstore=yes CFLAGS="$SQLDEFS -I$LIBRESSL_PREFIX/include" \
        LDFLAGS="$LIBRESSL_PREFIX/lib/libcrypto.a" >configure.log 2>&1 &&
    make sqlite3.c >make.log 2>&1 &&
    cc -c -O1 -g -fPIC -w $SQLDEFS -I"$LIBRESSL_PREFIX/include" sqlite3.c -o sqlite3.o ) ||
    { echo "building SQLCipher failed: see $OUT/sqlcipher/*.log"; exit 2; }
  echo "$want" > "$OUT/sqlcipher/stamp"
fi
SQLITE="-I$OUT/sqlcipher $OUT/sqlcipher/sqlite3.o $LIBRESSL_PREFIX/lib/libcrypto.a -lpthread -lm"
echo "SQLite: $(sed -n 's/^#define SQLITE_VERSION *"\(.*\)"/\1/p' "$OUT/sqlcipher/sqlite3.h"), SQLCipher-LibreSSL $(git -C "$SC" describe --tags)"

cc -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror \
   -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
   -DVK_TEST_FAULTS -D_POSIX_C_SOURCE=200809L \
   -I"$ROOT/core/include" -I"$ROOT/core/src" \
   "$ROOT/core/src/viki_store.c" "$ROOT/core/src/viki_search.c" "$ROOT/core/src/viki_delta.c" "$ROOT/core/src/viki_vector.c" \
   "$ROOT/core/src/sha256.c" "$ROOT/core/test/store_test.c" \
   $SQLITE -o "$OUT/store_test"
rc=0
"$OUT/store_test" | tee "$OUT/results.txt" || rc=$?
[ "$rc" -eq 0 ] || { echo "store_test exited $rc"; }
# The same library through its C interface from Python, against an
# independent model of framing, resolution and merge. SQLite is linked into
# the library, so Python reaches it through the same handle.
cc -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror -shared -fPIC \
   -I"$ROOT/core/include" -I"$ROOT/core/src" \
   "$ROOT/core/src/viki_store.c" "$ROOT/core/src/viki_search.c" "$ROOT/core/src/viki_delta.c" "$ROOT/core/src/viki_vector.c" \
   "$ROOT/core/src/sha256.c" $SQLITE -o "$OUT/libviki_store.so"
VIKI_STORE_LIB="$OUT/libviki_store.so" python3 "$ROOT/core/test/test_store.py" \
   | tee -a "$OUT/results.txt" || rc=$?
# The first host, tools/vk.py, through the same library: REQ3/SCM. Needs git.
VIKI_STORE_LIB="$OUT/libviki_store.so" python3 "$ROOT/tools/test_vk.py" \
   | tee -a "$OUT/results.txt" || rc=$?
python3 "$ROOT/core/test/gate.py" "$ROOT/REQUIREMENTS_v3.md" "$OUT/results.txt" || rc=$?
exit "$rc"
