#!/usr/bin/env bash
set -euo pipefail

# Build the viki CLI (Milestone 1 skeleton; see ../KICKOFF.md).
#
# Reuses two things fossil-see already produces, rather than pulling in
# separate copies:
#   - The SQLCipher/SQLite amalgamation (sqlite3.c/sqlite3.h) at
#     vendor/fossil-see/vendor/sqlcipher-libressl -- compiled here a SECOND
#     time, with different flags (FTS5 on, HAS_CODEC off), for viki's own
#     local cache db. The source file doesn't care what flags produced it;
#     activation is chosen at this compile step.
#   - LibreSSL's libcrypto (vendor/fossil-see/vendor/libressl-build-out) --
#     used only for EVP SHA-256 content hashing (src/sha256.c). No new
#     crypto dependency introduced.
#
# vendor/sqlite-ndvss is compiled with -DSQLITE_CORE for static linking
# (same mechanism SQLite's own FTS5/RTREE use), registered via
# sqlite3_auto_extension() in src/viki_db.c -- not loaded as a runtime .so.
#
# Prerequisite: vendor/fossil-see must already be built
# (vendor/fossil-see/build/build.sh) -- this script does NOT build it for
# you, since that's a multi-minute LibreSSL-from-source step best run once
# and reused.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/dist}"

FOSSIL_SEE_DIR="$REPO_ROOT/vendor/fossil-see"
SQLCIPHER_DIR="$FOSSIL_SEE_DIR/vendor/sqlcipher-libressl"
LIBRESSL_PREFIX="$FOSSIL_SEE_DIR/vendor/libressl-build-out"
NDVSS_DIR="$REPO_ROOT/vendor/sqlite-ndvss"
SRC_DIR="$REPO_ROOT/src"

if [ -z "${JOBS:-}" ]; then
    if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
    else JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"; fi
fi

echo "==> Verifying inputs"
[ -f "$SQLCIPHER_DIR/sqlite3.c" ] || {
    echo "ERR: $SQLCIPHER_DIR/sqlite3.c missing."
    echo "     Run vendor/fossil-see/build/build.sh first (produces the amalgamation as a side effect)."
    exit 1
}
[ -f "$LIBRESSL_PREFIX/lib/libcrypto.a" ] || {
    echo "ERR: $LIBRESSL_PREFIX/lib/libcrypto.a missing."
    echo "     Run vendor/fossil-see/build/build.sh first."
    exit 1
}
[ -f "$NDVSS_DIR/sqlite-ndvss.c" ] || { echo "ERR: $NDVSS_DIR/sqlite-ndvss.c missing (submodule not initialized?)"; exit 1; }

mkdir -p "$OUTPUT_DIR" "$SCRIPT_DIR/obj"
OBJ_DIR="$SCRIPT_DIR/obj"

echo "==> Compiling SQLite amalgamation (FTS5 on, no codec -- this is viki's own unencrypted local cache db)"
cc -O2 -Wall -Wno-unused-parameter \
   -DSQLITE_ENABLE_FTS5 \
   -DSQLITE_THREADSAFE=1 \
   -I"$SQLCIPHER_DIR" \
   -c "$SQLCIPHER_DIR/sqlite3.c" \
   -o "$OBJ_DIR/sqlite3.o"

echo "==> Compiling sqlite-ndvss (static link via -DSQLITE_CORE)"
cc -O3 -ffast-math -Wall -Wno-unused-parameter -Wno-unused-function \
   -DSQLITE_CORE \
   -I"$SQLCIPHER_DIR" \
   -c "$NDVSS_DIR/sqlite-ndvss.c" \
   -o "$OBJ_DIR/sqlite-ndvss.o"

echo "==> Compiling viki sources"
for f in viki sha256 viki_db viki_index viki_ask viki_cache; do
    cc -O2 -Wall -Wno-unused-parameter \
       -I"$SQLCIPHER_DIR" \
       -I"$LIBRESSL_PREFIX/include" \
       -I"$SRC_DIR" \
       -c "$SRC_DIR/$f.c" \
       -o "$OBJ_DIR/$f.o"
done

echo "==> Linking viki"
cc -O2 -o "$OUTPUT_DIR/viki" \
    "$OBJ_DIR/viki.o" "$OBJ_DIR/sha256.o" "$OBJ_DIR/viki_db.o" \
    "$OBJ_DIR/viki_index.o" "$OBJ_DIR/viki_ask.o" "$OBJ_DIR/viki_cache.o" \
    "$OBJ_DIR/sqlite3.o" "$OBJ_DIR/sqlite-ndvss.o" \
    "$LIBRESSL_PREFIX/lib/libcrypto.a" \
    -lm -lpthread

ls -lh "$OUTPUT_DIR/viki"

echo "==> Smoke test"
"$OUTPUT_DIR/viki" version
"$OUTPUT_DIR/viki" ndvss-selftest

echo "==> Build complete"
