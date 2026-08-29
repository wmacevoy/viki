#!/bin/sh
# build-embedder.sh -- the ONNX embedder as a loadable .so.
#
# SEPARATE FROM core/build.sh ON PURPOSE. core links no ONNX and must keep
# building on a machine that has none; this is a host component and its
# absence is a working configuration (degraded retrieval, reported).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/core/build"
ORT="${VIKI_ORT_DIR:-}"
if [ -z "$ORT" ]; then
  for c in "$ROOT"/vendor/download-cache/onnxruntime-*; do
    [ -f "$c/include/onnxruntime_c_api.h" ] && { ORT="$c"; break; }
  done
fi
[ -n "$ORT" ] || { echo "no ONNX Runtime; run build/build.sh once to fetch it"; exit 2; }
case "$(uname)" in Darwin) EXT=dylib; SOFLAG="-dynamiclib";; *) EXT=so; SOFLAG="-shared";; esac
mkdir -p "$OUT"
echo "==> viki-embed-onnx ($ORT)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# embed.c compiles with its entry points RENAMED, so its viki_embedder_open
# does not collide with the ABI symbol of the same name. Done here rather than
# by editing src/, so the fossil-linked binary keeps building unchanged.
# BOTH names are renamed: embed.c's viki_embedder_open takes a directory and
# would collide with the ABI symbol of the same name, which is exactly the
# assumption this adapter exists to remove.
cc -std=c11 -w -O2 -fPIC -I"$ROOT/src" -I"$ORT/include" \
   -Dviki_embedder_open=viki_embedder_open_dir \
   -Dviki_embedder_close=viki_embedder_close_impl \
   -c "$ROOT/src/embed.c" -o "$TMP/embed.o"
cc -std=c11 -w -O2 -fPIC -I"$ROOT/src" -c "$ROOT/src/tokenizer.c" -o "$TMP/tok.o"
cc -std=c11 -Wall -O2 -fPIC $SOFLAG \
   -I"$ROOT/src" -I"$ORT/include" \
   -o "$OUT/viki-embed-onnx.$EXT" \
   "$ROOT/cli/viki_embed_onnx.c" "$TMP/embed.o" "$TMP/tok.o" \
   -L"$ORT/lib" -lonnxruntime -Wl,-rpath,"$ORT/lib" -lm
echo "==> built: $OUT/viki-embed-onnx.$EXT"
