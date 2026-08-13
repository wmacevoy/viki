#!/usr/bin/env bash
set -euo pipefail

# Build the viki CLI (Milestone 1; see ../KICKOFF.md).
#
# Self-contained: no other project needs to be built first. Everything
# non-vendored is either pinned+SHA256-verified downloaded source/binary
# (this script's fetch_verify(), same pattern for all three of the
# SQLite amalgamation, ONNX Runtime, and the embedding model+vocab) or
# vendored source compiled directly (src/sha256.c, vendor/sqlite-ndvss).
# This used to reuse fossil-see's SQLCipher amalgamation + LibreSSL
# libcrypto build as a side effect of building fossil-see first (a
# multi-minute LibreSSL-from-source step) -- decoupled so `viki` can be
# built and released independently. See FINDINGS.md. vendor/fossil-see
# remains a submodule only because SOME fossil-compatible binary is
# needed at RUNTIME (wiki/ticket/forum extraction, `fossil uv`
# push/pull) -- resolved dynamically via $VIKI_FOSSIL_BIN/$PATH
# (viki_cache.c), stock `fossil` works fine for that.
#
# vendor/sqlite-ndvss is compiled with -DSQLITE_CORE for static linking
# (same mechanism SQLite's own FTS5/RTREE use), registered via
# sqlite3_auto_extension() in src/viki_db.c -- not loaded as a runtime .so.
#
# ONNX Runtime and the embedding model+vocab are downloaded as pinned,
# SHA256-verified prebuilt release artifacts (build/versions.env), since
# upstream ships no static library/source form worth building here. ONNX
# Runtime is a DYNAMIC library: copied next to the viki binary in
# $OUTPUT_DIR with an rpath pointing at it (or, on Windows, just copied
# next to the .exe -- the loader searches there automatically, no rpath
# concept exists), so it's found without any environment setup.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/dist}"
CACHE_DIR="${CACHE_DIR:-$REPO_ROOT/vendor/download-cache}"

# shellcheck source=./versions.env
. "$SCRIPT_DIR/versions.env"

NDVSS_DIR="$REPO_ROOT/vendor/sqlite-ndvss"
SRC_DIR="$REPO_ROOT/src"

if [ -z "${JOBS:-}" ]; then
    if command -v nproc >/dev/null 2>&1; then JOBS="$(nproc)"
    else JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 2)"; fi
fi

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
# MSYS2's `uname -s` reports something like "MINGW64_NT-10.0-20348", not
# "Windows" -- normalize any MSYS2/Cygwin flavor to a single ORT_OS value
# so the rest of this script only has one Windows case to branch on.
case "$ORT_OS" in
    MINGW*|MSYS*|CYGWIN*) ORT_OS="Windows" ;;
esac
ORT_ARCHIVE_EXT="tgz"
if [ "$ORT_OS" = "Darwin" ] && [ "$ORT_ARCH" = "arm64" ]; then
    ORT_URL="$ONNXRUNTIME_OSX_ARM64_URL"; ORT_SHA="$ONNXRUNTIME_OSX_ARM64_SHA256"
    ORT_LIBNAME="libonnxruntime.dylib"
elif [ "$ORT_OS" = "Linux" ] && [ "$ORT_ARCH" = "x86_64" ]; then
    ORT_URL="$ONNXRUNTIME_LINUX_X64_URL"; ORT_SHA="$ONNXRUNTIME_LINUX_X64_SHA256"
    ORT_LIBNAME="libonnxruntime.so"
elif [ "$ORT_OS" = "Linux" ] && [ "$ORT_ARCH" = "aarch64" ]; then
    ORT_URL="$ONNXRUNTIME_LINUX_ARM64_URL"; ORT_SHA="$ONNXRUNTIME_LINUX_ARM64_SHA256"
    ORT_LIBNAME="libonnxruntime.so"
elif [ "$ORT_OS" = "Windows" ] && [ "$ORT_ARCH" = "x86_64" ]; then
    # Windows ships as .zip, not .tgz -- unlike the Unix platforms above.
    ORT_URL="$ONNXRUNTIME_WIN_X64_URL"; ORT_SHA="$ONNXRUNTIME_WIN_X64_SHA256"
    ORT_LIBNAME="onnxruntime.dll"
    ORT_ARCHIVE_EXT="zip"
else
    echo "ERR: no pinned ONNX Runtime release for $ORT_OS/$ORT_ARCH in versions.env"
    exit 1
fi
ORT_TARBALL="onnxruntime-$ORT_OS-$ORT_ARCH-$ONNXRUNTIME_VERSION.$ORT_ARCHIVE_EXT"
fetch_verify "$ORT_URL" "$ORT_SHA" "$ORT_TARBALL"

ORT_EXTRACT_DIR="$CACHE_DIR/onnxruntime-$ORT_OS-$ORT_ARCH-$ONNXRUNTIME_VERSION"
if [ ! -d "$ORT_EXTRACT_DIR" ]; then
    # --strip-components=1 behaves inconsistently between GNU tar (Linux)
    # and bsdtar (macOS) in some environments -- observed empirically:
    # bsdtar here left the top-level "onnxruntime-<plat>-<ver>/" directory
    # un-stripped despite the flag. Extract into a scratch dir and move the
    # single top-level entry's contents up instead, which works identically
    # on both (and, for the zip case below, across platforms too).
    rm -rf "$ORT_EXTRACT_DIR.tmp"
    mkdir -p "$ORT_EXTRACT_DIR.tmp"
    if [ "$ORT_ARCHIVE_EXT" = "zip" ]; then
        unzip -q "$CACHE_DIR/$ORT_TARBALL" -d "$ORT_EXTRACT_DIR.tmp"
    else
        tar xzf "$CACHE_DIR/$ORT_TARBALL" -C "$ORT_EXTRACT_DIR.tmp"
    fi
    ORT_INNER="$(find "$ORT_EXTRACT_DIR.tmp" -mindepth 1 -maxdepth 1 -type d | head -1)"
    [ -n "$ORT_INNER" ] || { echo "ERR: unexpected archive layout in $ORT_TARBALL"; exit 1; }
    mv "$ORT_INNER" "$ORT_EXTRACT_DIR"
    rm -rf "$ORT_EXTRACT_DIR.tmp"
fi
ORT_INCLUDE="$ORT_EXTRACT_DIR/include"
ORT_LIB_SRC="$(find "$ORT_EXTRACT_DIR/lib" -maxdepth 1 -name 'libonnxruntime.*dylib' -o -name 'libonnxruntime.so.*' | head -1)"
[ -n "$ORT_LIB_SRC" ] || ORT_LIB_SRC="$ORT_EXTRACT_DIR/lib/$ORT_LIBNAME"
[ -f "$ORT_INCLUDE/onnxruntime_c_api.h" ] || { echo "ERR: onnxruntime_c_api.h not found under $ORT_INCLUDE"; exit 1; }
[ -f "$ORT_LIB_SRC" ] || { echo "ERR: $ORT_LIB_SRC missing"; exit 1; }
# Windows links against the MSVC-format import library (.lib), which sits
# alongside the runtime .dll in the same release archive -- there's no
# separate download for it.
ORT_IMPLIB=""
if [ "$ORT_OS" = "Windows" ]; then
    ORT_IMPLIB="$ORT_EXTRACT_DIR/lib/onnxruntime.lib"
    [ -f "$ORT_IMPLIB" ] || { echo "ERR: $ORT_IMPLIB missing"; exit 1; }
fi

# -- Step 1b: official SQLite amalgamation, pinned + verified (sqlite.org
# publishes SHA3-256; we independently compute+pin our own SHA256 here,
# same fetch_verify() every other download in this script uses) --
echo "==> Fetching SQLite amalgamation $SQLITE_AMAL_VERSION"
SQLITE_ARCHIVE="sqlite-amalgamation-$SQLITE_AMAL_VERSION.zip"
fetch_verify "$SQLITE_AMAL_URL" "$SQLITE_AMAL_SHA256" "$SQLITE_ARCHIVE"
SQLITE_DIR="$CACHE_DIR/sqlite-amalgamation-$SQLITE_AMAL_VERSION"
if [ ! -d "$SQLITE_DIR" ]; then
    rm -rf "$SQLITE_DIR.tmp"
    mkdir -p "$SQLITE_DIR.tmp"
    unzip -q "$CACHE_DIR/$SQLITE_ARCHIVE" -d "$SQLITE_DIR.tmp"
    SQLITE_INNER="$(find "$SQLITE_DIR.tmp" -mindepth 1 -maxdepth 1 -type d | head -1)"
    [ -n "$SQLITE_INNER" ] || { echo "ERR: unexpected archive layout in $SQLITE_ARCHIVE"; exit 1; }
    mv "$SQLITE_INNER" "$SQLITE_DIR"
    rm -rf "$SQLITE_DIR.tmp"
fi
[ -f "$SQLITE_DIR/sqlite3.c" ] || { echo "ERR: $SQLITE_DIR/sqlite3.c missing after extraction"; exit 1; }

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
if [ "$ORT_OS" = "Windows" ]; then
    # Windows has no rpath equivalent -- the loader finds a DLL next to the
    # .exe automatically, so copying it there (same as the Unix cases
    # above) is both necessary and sufficient. Grab every .dll in the
    # archive's lib/, not just onnxruntime.dll itself, in case a provider
    # plugin DLL is ever needed later (defensive, same reasoning as above).
    for f in "$ORT_EXTRACT_DIR"/lib/*.dll; do
        [ -e "$f" ] || continue
        cp "$f" "$OUTPUT_DIR/$(basename "$f")"
    done
fi

echo "==> Compiling SQLite amalgamation (FTS5 on, no codec -- this is viki's own unencrypted local cache db)"
cc -O2 -Wall -Wno-unused-parameter \
   -DSQLITE_ENABLE_FTS5 \
   -DSQLITE_THREADSAFE=1 \
   -I"$SQLITE_DIR" \
   -c "$SQLITE_DIR/sqlite3.c" \
   -o "$OBJ_DIR/sqlite3.o"

echo "==> Compiling sqlite-ndvss (static link via -DSQLITE_CORE)"
cc -O3 -ffast-math -Wall -Wno-unused-parameter -Wno-unused-function \
   -DSQLITE_CORE \
   -I"$SQLITE_DIR" \
   -c "$NDVSS_DIR/sqlite-ndvss.c" \
   -o "$OBJ_DIR/sqlite-ndvss.o"

echo "==> Compiling viki sources"
VIKI_EXTRA_DEFS=""
if [ "$ORT_OS" = "Windows" ]; then
    # See src/embed.c's comment: onnxruntime.dll always expects UTF-16
    # model paths on Windows, regardless of whether _WIN32 is defined in
    # THIS translation unit (it isn't, under MSYS2's plain MSYS
    # environment) -- this flag is build.sh's own, independent signal.
    VIKI_EXTRA_DEFS="-DVIKI_WIN_ORT_PATH"
fi
for f in viki sha256 viki_db viki_index viki_ask viki_cache viki_serve tokenizer embed; do
    cc -O2 -g -Wall -Wno-unused-parameter $VIKI_EXTRA_DEFS \
       -I"$SQLITE_DIR" \
       -I"$ORT_INCLUDE" \
       -I"$SRC_DIR" \
       -c "$SRC_DIR/$f.c" \
       -o "$OBJ_DIR/$f.o"
done

echo "==> Linking viki"
VIKI_BIN_NAME="viki"
LINK_RPATH_FLAGS=""
ORT_LINK_ARG="-L$OUTPUT_DIR -lonnxruntime"
EXTRA_LIBS="-lm -lpthread"
if [ "$ORT_OS" = "Darwin" ]; then
    LINK_RPATH_FLAGS="-Wl,-rpath,@executable_path"
elif [ "$ORT_OS" = "Windows" ]; then
    # No rpath concept on Windows -- the DLLs copied next to the .exe in
    # Step 3 are found via the loader's default search order. Link
    # directly against the MSVC-format import library sitting in the ONNX
    # Runtime archive (there's no separate -lonnxruntime name to resolve
    # under mingw/MSYS for a Microsoft-built DLL).
    VIKI_BIN_NAME="viki.exe"
    ORT_LINK_ARG="$ORT_IMPLIB"
    EXTRA_LIBS="-lm -lpthread -lws2_32"
else
    LINK_RPATH_FLAGS="-Wl,-rpath,\$ORIGIN"
fi
cc -O2 -o "$OUTPUT_DIR/$VIKI_BIN_NAME" \
    "$OBJ_DIR/viki.o" "$OBJ_DIR/sha256.o" "$OBJ_DIR/viki_db.o" \
    "$OBJ_DIR/viki_index.o" "$OBJ_DIR/viki_ask.o" "$OBJ_DIR/viki_cache.o" "$OBJ_DIR/viki_serve.o" \
    "$OBJ_DIR/tokenizer.o" "$OBJ_DIR/embed.o" \
    "$OBJ_DIR/sqlite3.o" "$OBJ_DIR/sqlite-ndvss.o" \
    $ORT_LINK_ARG $LINK_RPATH_FLAGS \
    $EXTRA_LIBS

ls -lh "$OUTPUT_DIR/$VIKI_BIN_NAME"

echo "==> Smoke test"
"$OUTPUT_DIR/$VIKI_BIN_NAME" version
"$OUTPUT_DIR/$VIKI_BIN_NAME" ndvss-selftest
"$OUTPUT_DIR/$VIKI_BIN_NAME" embed-selftest "$OUTPUT_DIR/model"

echo "==> Build complete"
