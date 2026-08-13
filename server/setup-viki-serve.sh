#!/usr/bin/env bash
#
# setup-viki-serve.sh -- put `viki serve` behind the existing Caddy+TLS hub
# (setup-hub.sh must have already run: Caddy installed, /etc/caddy/Caddyfile
# has a `$DOMAIN { ... }` site block for the fossil hub). Idempotent: safe
# to re-run.
#
# viki serve itself is never internet-facing: it keeps binding to
# 127.0.0.1 (see viki_serve.h) and gets no code changes here. This script
# adds:
#   - a dedicated no-login system user + systemd unit running
#     `viki serve --host 127.0.0.1 --port $VIKI_PORT`
#   - a Basic-Auth-protected `/viki/*` path inserted into the EXISTING
#     Caddyfile's `$DOMAIN { ... }` block (marker-delimited so re-running
#     replaces, rather than duplicates, the block) -- Caddy terminates TLS
#     and checks the password before anything reaches viki.
#
# This mirrors server/SERVER_SETUP.md's D-8 decision for the fossil hub
# itself (Caddy over nginx/certbot: automatic Let's Encrypt renewal, the
# backend never faces the internet directly) rather than inventing a
# second TLS story -- see FINDINGS.md for why viki_serve.c doesn't do its
# own TLS/auth.
#
# Usage:
#   sudo DOMAIN=fossil.example.com \
#        VIKI_REPO_CHECKOUT=/srv/viki/checkout \
#        VIKI_MODEL_DIR=/srv/viki/model \
#        VIKI_BIN=/usr/local/bin/viki \
#        [VIKI_USER=viki] [VIKI_PORT=8090] \
#        ./setup-viki-serve.sh
#
set -euo pipefail

DOMAIN="${DOMAIN:?set DOMAIN=fossil.example.com (must match the domain setup-hub.sh already configured)}"
VIKI_REPO_CHECKOUT="${VIKI_REPO_CHECKOUT:?set VIKI_REPO_CHECKOUT=/srv/viki/checkout (an OPEN fossil-see checkout -- viki index/ask/serve all run relative to cwd)}"
VIKI_MODEL_DIR="${VIKI_MODEL_DIR:?set VIKI_MODEL_DIR=/srv/viki/model (copy your local build/dist/model here; unset/missing degrades to BM25-only, not an error, but silently -- set it explicitly)}"
VIKI_BIN="${VIKI_BIN:-/usr/local/bin/viki}"
VIKI_USER="${VIKI_USER:-viki}"
VIKI_PORT="${VIKI_PORT:-8090}"
CADDYFILE=/etc/caddy/Caddyfile
CRED_FILE=/etc/caddy/viki.htpasswd

command -v caddy >/dev/null || { echo "error: caddy not found -- run setup-hub.sh first" >&2; exit 1; }
[ -x "$VIKI_BIN" ] || { echo "error: $VIKI_BIN not found/executable -- build viki (build/build.sh) and copy the binary, PLUS every libonnxruntime.* file next to it (see FINDINGS.md's dyld install-name finding), here first" >&2; exit 1; }
[ -e "$VIKI_REPO_CHECKOUT/.fslckout" ] || [ -e "$VIKI_REPO_CHECKOUT/_FOSSIL_" ] || { echo "error: $VIKI_REPO_CHECKOUT doesn't look like an open fossil checkout (no .fslckout/_FOSSIL_ file)" >&2; exit 1; }
[ -f "$CADDYFILE" ] && grep -qF "$DOMAIN {" "$CADDYFILE" || { echo "error: no '$DOMAIN {' block in $CADDYFILE -- run setup-hub.sh for this domain first" >&2; exit 1; }

# --- dedicated system user, no login, read-only to the checkout's content ---
id -u sysviki >/dev/null 2>&1 || useradd --system --home "$VIKI_REPO_CHECKOUT" --shell /usr/sbin/nologin sysviki
chown -R sysviki:sysviki "$VIKI_REPO_CHECKOUT"

# --- Basic Auth credential -----------------------------------------------
# Generated once; re-running reuses the existing hash rather than silently
# rotating it out from under anyone who already has the password.
if [ -s "$CRED_FILE" ]; then
    VIKI_HASH="$(cat "$CRED_FILE")"
    echo ">>> Reusing existing viki Basic Auth credential in $CRED_FILE."
    echo ">>> (delete that file and re-run this script to rotate it)"
else
    VIKI_PASSWORD="$(openssl rand -base64 24)"
    VIKI_HASH="$(caddy hash-password --plaintext "$VIKI_PASSWORD")"
    echo "$VIKI_HASH" > "$CRED_FILE"
    chmod 600 "$CRED_FILE"
    echo ">>> Generated viki Basic Auth credentials -- SAVE THESE NOW, shown only once:"
    echo ">>>   username: $VIKI_USER"
    echo ">>>   password: $VIKI_PASSWORD"
fi

# --- systemd service -------------------------------------------------------
cat > /etc/systemd/system/viki-serve.service <<EOF
[Unit]
Description=viki serve (hybrid BM25+vector search over a fossil-see checkout)
After=network.target

[Service]
User=sysviki
Group=sysviki
WorkingDirectory=$VIKI_REPO_CHECKOUT
Environment=VIKI_MODEL_DIR=$VIKI_MODEL_DIR
ExecStart=$VIKI_BIN serve --host 127.0.0.1 --port $VIKI_PORT
Restart=on-failure
# Hardening
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=$VIKI_REPO_CHECKOUT
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now viki-serve

# --- Caddy: insert a Basic-Auth-protected /viki/* path into the EXISTING
# $DOMAIN block, replacing any block we inserted on a previous run -------
# (Note: an earlier version of this used `awk -v block="<multi-line>"` --
# some awk implementations, including macOS's /usr/bin/awk used to write
# and check this script, reject a -v value containing a raw newline
# ("newline in string"). sed's `r file` command sidesteps that entirely.)
BEGIN_MARK="    # --- viki-serve BEGIN (managed by setup-viki-serve.sh; do not hand-edit between markers) ---"
END_MARK="    # --- viki-serve END ---"

re_escape(){ printf '%s' "$1" | sed 's/[][\.*^$\/]/\\&/g'; }

BEGIN_ESC="$(re_escape "$BEGIN_MARK")"
END_ESC="$(re_escape "$END_MARK")"
sed -i.bak "/^${BEGIN_ESC}\$/,/^${END_ESC}\$/d" "$CADDYFILE"
rm -f "$CADDYFILE.bak"

BLOCKFILE="$(mktemp)"
trap 'rm -f "$BLOCKFILE"' EXIT
cat > "$BLOCKFILE" <<EOF
$BEGIN_MARK
    handle_path /viki/* {
        basic_auth {
            $VIKI_USER $VIKI_HASH
        }
        reverse_proxy 127.0.0.1:$VIKI_PORT
    }
$END_MARK
EOF

DOMAIN_ESC="$(re_escape "$DOMAIN")"
sed -i.bak "/^${DOMAIN_ESC} {\$/r $BLOCKFILE" "$CADDYFILE"
rm -f "$CADDYFILE.bak"
rm -f "$BLOCKFILE"
trap - EXIT

systemctl reload caddy

echo
echo "============================================================"
echo " viki serve is up behind TLS + Basic Auth: https://$DOMAIN/viki/"
echo
echo " NOTE: this does not set up periodic re-indexing. The cache at"
echo " $VIKI_REPO_CHECKOUT/.viki/cache.db only reflects whatever"
echo " '$VIKI_BIN index' last saw when it was last run by hand -- wire"
echo " up a cron job (e.g. after each fossil sync) to keep it fresh."
echo "============================================================"
