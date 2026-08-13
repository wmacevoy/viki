#!/usr/bin/env bash
set -euo pipefail

# Build the viki CLI (Milestone 1; see ../KICKOFF.md).
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
# ONNX Runtime is downloaded as a pinned, SHA256-verified prebuilt release
# tarball (build/versions.env) -- same pattern as LibreSSL's tarball cache
# in fossil-see, since upstream ships no static library to build from
# source against here. It's a DYNAMIC library (no static build offered
# upstream): copied next to the viki binary in $OUTPUT_DIR with an
# rpath pointing at it, so it's found without any environment setup.
#
# The embedding model + vocab are downloaded the same pinned+verified way.
# See build/versions.env for why (and its caveats).
#
# Prerequisite: vendor/fossil-see must already be built
# (vendor/fossil-see/build/build.sh) -- this script does NOT build it for
# you, since that's a multi-minute LibreSSL-from-source step best run once
# and reused.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/dist}"
CACHE_DIR="${CACHE_DIR:-$REPO_ROOT/vendor/download-cache}"

# shellcheck source=./versions.env
. "$SCRIPT_DIR/versions.env"

FOSSIL_SEE_DIR="$REPO_ROOT/vendor/fossil-see"
SQLCIPHER_DIR="$FOSSIL_SEE_DIR/vendor/sqlcipher-libressl"
LIBRESSL_PREFIX="$FOSSIL_SEE_DIR/vendor/libressl-build-out"
NDVSS_DIR="$REPO_ROOT/vendor/sqlite-ndvss"
SRC_DIR="$REPO_ROOT/src"

if [ -z "${JOBS:-}" ]; then
    if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
    else JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"; fi
fi

echo "==> Verifying fossil-see inputs"
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

mkdir -p "$OUTPUT_DIR" "$SCRIPT_DIR/obj" "$CACHE_DIR"
OBJ_DIR="$SCRIPT_DIR/obj"

# -- helper: download to CACHE_DIR (skip if already present), verify sha256 --
fetch_verify() {
    local url="$1" sha256="$2" outfile="$3"
    if [ ! -f "$CACHE_DIR/$outfile" ]; then
        echo "  downloading $outfile"
        curl -fsSL "$url" -o "$CACHE_DIR/$outfile.tmp"
        mv "$CACHE_DIR/$outfile.tmp" "$CACHE_DIR/$outfile"
    fi
    local actual
    actual="$(shasum -a 256 "$CACHE_DIR/$outfile" | awk '{print $1}')"
    if [ "$actual" != "$sha256" ]; then
        echo "ERR: $outfile SHA256 mismatch: expected $sha256, got $actual"
        rm -f "$CACHE_DIR/$outfile"
        exit 1
    fi
}

# -- Step 1: ONNX Runtime (platform-detected, pinned, verified) --
echo "==> Fetching ONNX Runtime $ONNXRUNTIME_VERSION"
ORT_OS="$(uname -s)"
ORT_ARCH="$(uname -m)"
if [ "$ORT_OS" = "Darwin" ] && [ "$ORT_ARCH" = "arm64" ]; then
    ORT_URL="$ONNXRUNTIME_OSX_ARM64_URL"; ORT_SHA="$ONNXRUNTIME_OSX_ARM64_SHA256"
    ORT_LIBNAME="libonnxruntime.dylib"
elif [ "$ORT_OS" = "Linux" ] && [ "$ORT_ARCH" = "x86_64" ]; then
    ORT_URL="$ONNXRUNTIME_LINUX_X64_URL"; ORT_SHA="$ONNXRUNTIME_LINUX_X64_SHA256"
    ORT_LIBNAME="libonnxruntime.so"
elif [ "$ORT_OS" = "Linux" ] && [ "$ORT_ARCH" = "aarch64" ]; then
    ORT_URL="$ONNXRUNTIME_LINUX_ARM64_URL"; ORT_SHA="$ONNXRUNTIME_LINUX_ARM64_SHA256"
    ORT_LIBNAME="libonnxruntime.so"
else
    echo "ERR: no pinned ONNX Runtime release for $ORT_OS/$ORT_ARCH in versions.env"
    exit 1
fi
ORT_TARBALL="onnxruntime-$ORT_OS-$ORT_ARCH-$ONNXRUNTIME_VERSION.tgz"
fetch_verify "$ORT_URL" "$ORT_SHA" "$ORT_TARBALL"

ORT_EXTRACT_DIR="$CACHE_DIR/onnxruntime-$ORT_OS-$ORT_ARCH-$ONNXRUNTIME_VERSION"
if [ ! -d "$ORT_EXTRACT_DIR" ]; then
    # --strip-components=1 behaves inconsistently between GNU tar (Linux)
    # and bsdtar (macOS) in some environments -- observed empirically:
    # bsdtar here left the top-level "onnxruntime-<plat>-<ver>/" directory
    # un-stripped despite the flag. Extract into a scratch dir and move the
    # single top-level entry's contents up instead, which works identically
    # on both.
    rm -rf "$ORT_EXTRACT_DIR.tmp"
    mkdir -p "$ORT_EXTRACT_DIR.tmp"
    tar xzf "$CACHE_DIR/$ORT_TARBALL" -C "$ORT_EXTRACT_DIR.tmp"
    ORT_INNER="$(find "$ORT_EXTRACT_DIR.tmp" -mindepth 1 -maxdepth 1 -type d | head -1)"
    [ -n "$ORT_INNER" ] || { echo "ERR: unexpected tarball layout in $ORT_TARBALL"; exit 1; }
    mv "$ORT_INNER" "$ORT_EXTRACT_DIR"
    rm -rf "$ORT_EXTRACT_DIR.tmp"
fi
ORT_INCLUDE="$ORT_EXTRACT_DIR/include"
ORT_LIB_SRC="$(find "$ORT_EXTRACT_DIR/lib" -maxdepth 1 -name 'libonnxruntime.*dylib' -o -name 'libonnxruntime.so.*' | head -1)"
[ -n "$ORT_LIB_SRC" ] || ORT_LIB_SRC="$ORT_EXTRACT_DIR/lib/$ORT_LIBNAME"
[ -f "$ORT_INCLUDE/onnxruntime_c_api.h" ] || { echo "ERR: onnxruntime_c_api.h not found under $ORT_INCLUDE"; exit 1; }
[ -f "$ORT_LIB_SRC" ] || { echo "ERR: $ORT_LIB_SRC missing"; exit 1; }

# -- Step 2: pinned embedding model + vocab --
echo "==> Fetching embedding model ($VIKI_MODEL_ID)"
fetch_verify "$VIKI_MODEL_URL" "$VIKI_MODEL_SHA256" "model-$VIKI_MODEL_ID.onnx"
fetch_verify "$VIKI_VOCAB_URL" "$VIKI_VOCAB_SHA256" "vocab-$VIKI_MODEL_ID.txt"
mkdir -p "$OUTPUT_DIR/model"
cp "$CACHE_DIR/model-$VIKI_MODEL_ID.onnx" "$OUTPUT_DIR/model/model.onnx"
cp "$CACHE_DIR/vocab-$VIKI_MODEL_ID.txt" "$OUTPUT_DIR/model/vocab.txt"
cat > "$OUTPUT_DIR/model/viki-manifest.json" <<EOF
{
  "model_id": "$VIKI_MODEL_ID",
  "dim": $VIKI_MODEL_DIM,
  "model_sha256": "$VIKI_MODEL_SHA256",
  "vocab_sha256": "$VIKI_VOCAB_SHA256"
}
EOF

# -- Step 3: copy the ONNX Runtime shared lib(s) next to the binary --
# Copy every libonnxruntime.* name/symlink variant (unversioned, SONAME
# like .1.dylib/.so.1, and the fully-versioned real file), not just the
# one -lonnxruntime links against: the binary's embedded install name
# (macOS LC_ID_DYLIB / Linux SONAME) is whichever name the *source*
# library itself was built with, which the dynamic linker looks up by
# that exact name at runtime regardless of what we copy it as -- observed
# empirically as a dyld "Library not loaded: @rpath/libonnxruntime.1.dylib"
# failure when only one name was copied. See FINDINGS.md.
for f in "$ORT_EXTRACT_DIR"/lib/libonnxruntime*.dylib "$ORT_EXTRACT_DIR"/lib/libonnxruntime.so*; do
    [ -e "$f" ] || continue
    cp -P "$f" "$OUTPUT_DIR/$(basename "$f")"
done

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
for f in viki sha256 viki_db viki_index viki_ask viki_cache viki_serve tokenizer embed; do
    cc -O2 -g -Wall -Wno-unused-parameter \
       -I"$SQLCIPHER_DIR" \
       -I"$LIBRESSL_PREFIX/include" \
       -I"$ORT_INCLUDE" \
       -I"$SRC_DIR" \
       -c "$SRC_DIR/$f.c" \
       -o "$OBJ_DIR/$f.o"
done

echo "==> Linking viki"
LINK_RPATH_FLAGS=""
if [ "$ORT_OS" = "Darwin" ]; then
    LINK_RPATH_FLAGS="-Wl,-rpath,@executable_path"
else
    LINK_RPATH_FLAGS="-Wl,-rpath,\$ORIGIN"
fi
cc -O2 -o "$OUTPUT_DIR/viki" \
    "$OBJ_DIR/viki.o" "$OBJ_DIR/sha256.o" "$OBJ_DIR/viki_db.o" \
    "$OBJ_DIR/viki_index.o" "$OBJ_DIR/viki_ask.o" "$OBJ_DIR/viki_cache.o" "$OBJ_DIR/viki_serve.o" \
    "$OBJ_DIR/tokenizer.o" "$OBJ_DIR/embed.o" \
    "$OBJ_DIR/sqlite3.o" "$OBJ_DIR/sqlite-ndvss.o" \
    "$LIBRESSL_PREFIX/lib/libcrypto.a" \
    -L"$OUTPUT_DIR" -lonnxruntime $LINK_RPATH_FLAGS \
    -lm -lpthread

ls -lh "$OUTPUT_DIR/viki"

echo "==> Smoke test"
"$OUTPUT_DIR/viki" version
"$OUTPUT_DIR/viki" ndvss-selftest
"$OUTPUT_DIR/viki" embed-selftest "$OUTPUT_DIR/model"

echo "==> Build complete"
