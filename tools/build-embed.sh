#!/bin/sh
# build-embed.sh -- the host's embedder (REQ3/Q-6 vector): src/'s embed.c and
# tokenizer.c, built in place as core/build-store/libviki_embed.so for vk.py.
# core runs no model; this is the host's side of the vector method.
#
# Needs ONNX Runtime 1.29.0 (build/versions.env pins it, by hash): its include/
# and lib/ under VIKI_ORT_DIR. Fails rather than guessing where it is.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/core/build-store"
ORT=${VIKI_ORT_DIR:-}
[ -n "$ORT" ] || { echo "build-embed: set VIKI_ORT_DIR to an ONNX Runtime 1.29.0 directory (include/, lib/)"; exit 2; }
[ -f "$ORT/include/onnxruntime_c_api.h" ] || { echo "build-embed: no onnxruntime_c_api.h under $ORT/include"; exit 2; }
mkdir -p "$OUT"
cc -std=c99 -O2 -fPIC -shared -Wall -D_POSIX_C_SOURCE=200809L \
   -I"$ORT/include" -I"$ROOT/src" \
   "$ROOT/src/embed.c" "$ROOT/src/tokenizer.c" \
   -L"$ORT/lib" -lonnxruntime -Wl,-rpath,"$ORT/lib" -lm \
   -o "$OUT/libviki_embed.so"
echo "built $OUT/libviki_embed.so"
