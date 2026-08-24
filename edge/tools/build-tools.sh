#!/bin/sh
# build-tools.sh -- the native key-custody tools.
#
# All three link ONLY the LibreSSL and SQLCipher that fossil-see already
# vendors (QUEUE 48): X25519, ChaCha20-Poly1305, HMAC-SHA256, PBKDF2 and
# RAND_bytes all come from there, and HKDF is implemented from HMAC in
# viki-key-wrap.c. Nothing new is downloaded and nothing is installed.
#
# Usage: sh edge/tools/build-tools.sh [outdir]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="${1:-$ROOT/build/dist}"
V="${VIKI_SQLCIPHER_VENDOR:-$ROOT/../fossil-sqlcipher-libressl/vendor}"
SQLC="$V/sqlcipher-libressl"
LR="$V/libressl-build-out"
[ -f "$SQLC/sqlite3.c" ] || { echo "no SQLCipher amalgamation at $SQLC"; exit 2; }
[ -f "$LR/lib/libcrypto.a" ] || { echo "no LibreSSL at $LR"; exit 2; }
mkdir -p "$OUT"

CODEC="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL \
 -DSQLITE_EXTRA_INIT=sqlcipher_extra_init -DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown \
 -DSQLITE_ENABLE_FTS5 -DSQLITE_THREADSAFE=1 -DSQLITE_TEMP_STORE=2"

echo "==> viki-key-wrap (age v1, no sqlite needed)"
cc -O2 -I "$LR/include" "$ROOT/edge/tools/viki-key-wrap.c" \
   "$LR/lib/libcrypto.a" -o "$OUT/viki-key-wrap"

echo "==> viki-identity"
# libtls for the puller's HTTPS. It must precede libcrypto on the link line:
# libtls depends on libssl/libcrypto, not the other way round.
cc -O2 $CODEC -I "$ROOT/edge/tools" -I "$SQLC" -I "$LR/include" \
   "$ROOT/edge/tools/viki-identity.c" "$ROOT/edge/tools/viki-http.c" "$SQLC/sqlite3.c" \
   "$LR/lib/libtls.a" "$LR/lib/libssl.a" "$LR/lib/libcrypto.a" \
   -lpthread -ldl -lm -o "$OUT/viki-identity"

echo "==> viki-cache-encrypt"
cc -O2 $CODEC -I "$SQLC" -I "$LR/include" \
   "$ROOT/edge/tools/viki-cache-encrypt.c" "$SQLC/sqlite3.c" \
   "$LR/lib/libcrypto.a" -lpthread -ldl -lm -o "$OUT/viki-cache-encrypt"

ls -la "$OUT/viki-key-wrap" "$OUT/viki-identity" "$OUT/viki-cache-encrypt"
