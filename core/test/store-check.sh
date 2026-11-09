#!/bin/sh
# core/test/store-check.sh -- build the assertion store with ASan and UBSan,
# run its tests, and print the requirement gate. Exits nonzero on any red.
# Uses the system SQLite: encryption is the host's (it opens the sqlite3*).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/core/build-store"
mkdir -p "$OUT"
cc -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror \
   -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
   -DVK_TEST_FAULTS -D_POSIX_C_SOURCE=200809L \
   -I"$ROOT/core/include" -I"$ROOT/core/src" \
   "$ROOT/core/src/viki_store.c" "$ROOT/core/src/viki_search.c" \
   "$ROOT/core/src/sha256.c" "$ROOT/core/test/store_test.c" \
   -lsqlite3 -o "$OUT/store_test"
rc=0
"$OUT/store_test" | tee "$OUT/results.txt" || rc=$?
[ "$rc" -eq 0 ] || { echo "store_test exited $rc"; }
# The same library through its C interface from Python, against an
# independent model of framing, resolution and merge.
cc -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Werror -shared -fPIC \
   -I"$ROOT/core/include" -I"$ROOT/core/src" \
   "$ROOT/core/src/viki_store.c" "$ROOT/core/src/viki_search.c" \
   "$ROOT/core/src/sha256.c" -lsqlite3 -o "$OUT/libviki_store.so"
VIKI_STORE_LIB="$OUT/libviki_store.so" python3 "$ROOT/core/test/test_store.py" \
   | tee -a "$OUT/results.txt" || rc=$?
# The first host, tools/vk.py, through the same library: REQ3/SCM. Needs git.
VIKI_STORE_LIB="$OUT/libviki_store.so" python3 "$ROOT/tools/test_vk.py" \
   | tee -a "$OUT/results.txt" || rc=$?
python3 "$ROOT/core/test/gate.py" "$ROOT/REQUIREMENTS_v3.md" "$OUT/results.txt" || rc=$?
exit "$rc"
