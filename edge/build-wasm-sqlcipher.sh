#!/bin/sh
# build-wasm-sqlcipher.sh -- viki edge, ENCRYPTED AT REST.
#
# Builds viki's read-only retrieval core against SQLCipher (not plain SQLite)
# with LibreSSL compiled to wasm, plus the OPFS VFS. The result reads a
# SQLCipher cache.db directly, decrypting pages as SQLite asks for them, so the
# corpus is never written to a phone's storage in the clear.
#
# WHY THIS AND NOT "ENCRYPT THE BLOB WITH WebCrypto": those are alternatives,
# not layers, and picking wrongly wastes the work. A page-level codec is only
# worth having when pages genuinely come off storage -- i.e. with the VFS,
# i.e. when the cache is too large to sit in memory. Under the byte-store
# design the whole db is in RAM before SQLite touches it and the codec
# encrypts nothing that was not just decrypted. See QUEUE 46.
#
# THE RECIPE IS NOT MINE. It is lifted from sqlcipher-libressl's own CI --
# .github/workflows/build-test.yml, the `wasm` job -- because building
# LibreSSL for wasm by the obvious route does not work: autoconf rejects the
# platform outright ("unsupported platform: emscripten"). It must be emcmake
# CMake, only the `crypto` target, and three feature defines emscripten cannot
# infer. That knowledge lived in CI config, which is exactly the kind of place
# a dependency gets lost (QUEUE 47).
#
# Usage: sh edge/build-wasm-sqlcipher.sh
#   Requires docker. Everything runs in emscripten/emsdk:3.1.74 -- the version
#   that CI pins; do not silently move to :latest.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=emscripten/emsdk:3.1.74

# The fossil-see sibling checkout.  BOTH names are tried: the directory was
# renamed fossil-sqlcipher-libressl -> fossil-see on 2026-08-29, and an older
# clone still has the long name.  Only the local path changed -- the GitHub
# repo is still wmacevoy/fossil-sqlcipher-libressl -- so neither is wrong.
FS="${VIKI_FOSSILSEE_DIR:-}"
if [ -z "$FS" ]; then
  for c in "$ROOT/../fossil-see" "$ROOT/../fossil-sqlcipher-libressl"; do
    [ -d "$c" ] && { FS="$c"; break; }
  done
  : "${FS:=$ROOT/../fossil-see}"
fi
SQLC="${VIKI_SQLCIPHER_DIR:-$FS/vendor/sqlcipher-libressl}"
LRSRC="${VIKI_LIBRESSL_SRC:-$FS/vendor/libressl-cache/libressl-4.2.1}"
VW="$ROOT/edge/vendor-wasm"
docker info >/dev/null 2>&1 || { echo "docker daemon is not running"; exit 2; }
[ -f "$SQLC/sqlite3.c" ] || { echo "no SQLCipher amalgamation at $SQLC"; exit 2; }
mkdir -p "$VW" "$ROOT/edge/dist"

# ---- 1. LibreSSL -> wasm (slow; cached by the artifact check) -------------
if [ ! -f "$VW/libcrypto-wasm.a" ]; then
  echo "==> Building LibreSSL for wasm (slow, and it is meant to be)"
  WORK=$(mktemp -d); cp -R "$LRSRC" "$WORK/src"
  cat > "$WORK/b.sh" <<'INNER'
set -e
command -v cmake >/dev/null 2>&1 || { apt-get update -qq && apt-get install -y -qq cmake >/dev/null; }
cd /work/src && mkdir -p build-wasm && cd build-wasm
emcmake cmake .. -DCMAKE_INSTALL_PREFIX=/work/out \
  -DLIBRESSL_APPS=OFF -DLIBRESSL_TESTS=OFF -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_C_FLAGS="-DHAVE_TIMEGM -DHAVE_GETENTROPY -D__STDC_NO_ATOMICS__" >/work/cmake.log 2>&1
emmake make -j4 crypto >/work/make.log 2>&1
mkdir -p /work/out/lib && cp crypto/libcrypto.a /work/out/lib/
INNER
  docker run --rm -v "$WORK":/work -w /work "$IMG" sh /work/b.sh
  cp "$WORK/out/lib/libcrypto.a" "$VW/libcrypto-wasm.a"
  rm -rf "$WORK"
else
  echo "==> LibreSSL wasm already built ($VW/libcrypto-wasm.a)"
fi

# ---- 2. stage SQLCipher's amalgamation + openssl headers ------------------
cp "$SQLC/sqlite3.c" "$SQLC/sqlite3.h" "$SQLC/sqlite3ext.h" "$VW/"
[ -d "$VW/openssl" ] || cp -R "$FS/vendor/libressl-build-out/include/openssl" "$VW/"

# ---- 3. viki edge ---------------------------------------------------------
echo "==> Compiling viki edge against SQLCipher"
docker run --rm -v "$ROOT":/src -w /src "$IMG" emcc -O2 -msimd128 \
  -I src -I edge/vendor-wasm -I vendor/sqlite-ndvss \
  -DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL \
  -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown \
  -DSQLITE_ENABLE_FTS5 -DSQLITE_CORE \
  -DSQLITE_THREADSAFE=1 -DSQLITE_TEMP_STORE=2 \
  -DSQLITE_OMIT_LOAD_EXTENSION \
  edge/edge_wasm.c edge/edge_embed_js.c edge/vendor_opfs_vfs.c \
  src/viki_ask.c src/viki_db.c src/viki_grep.c src/viki_note.c src/tokenizer.c \
  edge/vendor-wasm/sqlite3.c vendor/sqlite-ndvss/sqlite-ndvss.c \
  edge/vendor-wasm/libcrypto-wasm.a \
  -o edge/dist/viki-edge-sqlcipher.js \
  -sMODULARIZE=1 -sEXPORT_NAME=VikiEdge \
  -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sINITIAL_MEMORY=33554432 \
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","UTF8ToString","HEAP32","HEAPF32","HEAPU8"]' \
  -sEXPORTED_FUNCTIONS='["_edge_open","_edge_open_keyed","_edge_ask","_edge_chunk_count","_edge_free","_edge_vocab_load","_edge_tokenize","_edge_set_query_from_hidden","_edge_clear_query_vector","_edge_key","_edge_exec","_sqlite3_opfs_init","_malloc","_free"]' \
  -sSTACK_SIZE=1048576
ls -la "$ROOT/edge/dist/viki-edge-sqlcipher.wasm"
