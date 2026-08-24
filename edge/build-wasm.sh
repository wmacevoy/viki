#!/bin/sh
# build-wasm.sh -- compile viki's READ-ONLY retrieval core to WebAssembly.
#
# Runs emscripten INSIDE A CONTAINER so nothing lands on the host: the emsdk
# install is ~1-2GB and this project's whole build story is "no prerequisites
# beyond a C compiler". `docker` and a running daemon are the only host needs.
#
# WHAT IS AND IS NOT IN THIS BUILD. Only the files with no fork/exec/socket/
# dlopen in them: viki_ask.c, viki_db.c, viki_grep.c, viki_note.c, plus SQLite
# and sqlite-ndvss. Deliberately EXCLUDED: viki_index.c and viki_cache.c (they
# spawn fossil), viki_serve.c (POSIX sockets), viki_fossilsee.c (dlopen), and
# embed.c (ONNX Runtime, which has no static wasm build here). The three ONNX
# symbols viki_ask.c references are supplied as aborting stubs by
# edge_noembed.c -- that three-symbol coupling is the entire distance between
# viki's read path and a phone.
#
# ndvss's wasm SIMD kernel is selected by `__wasm_simd128__`, which -msimd128
# defines (see vendor/sqlite-ndvss/sqlite-ndvss.c's dispatch). That kernel
# existing is why this project kept ndvss over sqlite-vec; this script is that
# decision being cashed in.
#
# Usage: sh edge/build-wasm.sh   ->  edge/dist/viki-edge.{js,wasm}
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/build/versions.env"
SQ="vendor/download-cache/sqlite-amalgamation-$SQLITE_AMAL_VERSION"
[ -d "$ROOT/$SQ" ] || { echo "no SQLite amalgamation -- run build/build.sh first"; exit 2; }
docker info >/dev/null 2>&1 || { echo "docker daemon is not running"; exit 2; }
mkdir -p "$ROOT/edge/dist"

docker run --rm -v "$ROOT":/src -w /src emscripten/emsdk:latest \
  emcc -O2 -msimd128 \
    -I src -I "$SQ" -I vendor/sqlite-ndvss \
    -DSQLITE_ENABLE_FTS5 -DSQLITE_CORE -DSQLITE_THREADSAFE=0 \
    -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 \
    edge/edge_wasm.c edge/edge_noembed.c \
    src/viki_ask.c src/viki_db.c src/viki_grep.c src/viki_note.c \
    "$SQ/sqlite3.c" vendor/sqlite-ndvss/sqlite-ndvss.c \
    -o edge/dist/viki-edge.js \
    -sMODULARIZE=1 -sEXPORT_NAME=VikiEdge \
    -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","UTF8ToString"]' \
    -sEXPORTED_FUNCTIONS='["_edge_open","_edge_ask","_edge_chunk_count","_edge_free","_malloc","_free"]' \
    -sSTACK_SIZE=1048576

ls -la "$ROOT/edge/dist/"
