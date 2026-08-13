#!/usr/bin/env bash
#
# setup-hub.sh — stand up the viki hub on a fresh Debian/Ubuntu VPS
# (tested against Hetzner cloud defaults). Idempotent: safe to re-run.
#
# Usage:  sudo DOMAIN=fossil.example.com ADMIN_USER=warren ./setup-hub.sh
#
set -euo pipefail

DOMAIN="${DOMAIN:?set DOMAIN=fossil.example.com (must already point at this VPS)}"
ADMIN_USER="${ADMIN_USER:-warren}"
FOSSIL_PORT="${FOSSIL_PORT:-8080}"
REPO_DIR=/srv/fossil/repos
BACKUP_DIR=/srv/fossil/backups

# --- packages ---------------------------------------------------------------
apt-get update -qq
apt-get install -y -qq fossil caddy ufw >/dev/null
fossil version

# --- dedicated user + directories -------------------------------------------
id -u fossil >/dev/null 2>&1 || useradd --system --home /srv/fossil --shell /usr/sbin/nologin fossil
mkdir -p "$REPO_DIR" "$BACKUP_DIR"
chown -R fossil:fossil /srv/fossil

# --- first repository (the personal-management hub repo) --------------------
if [ ! -f "$REPO_DIR/pm.fossil" ]; then
  sudo -u fossil fossil init "$REPO_DIR/pm.fossil" -A "$ADMIN_USER"
  echo ">>> NOTE the initial password printed above for user '$ADMIN_USER'."
  # Sensible hub defaults:
  sudo -u fossil fossil settings backoffice-disable 1 -R "$REPO_DIR/pm.fossil" --exact
  # Lock down 'nobody'/'anonymous' until you decide otherwise (web UI still
  # allows login). Capabilities are managed in the web UI: Admin -> Users.
fi

# --- systemd service: fossil serves ALL repos in the directory ---------------
cat > /etc/systemd/system/fossil-hub.service <<EOF
[Unit]
Description=Fossil SCM hub (repolist mode)
After=network.target

[Service]
User=fossil
Group=fossil
ExecStart=/usr/bin/fossil server $REPO_DIR --repolist --localhost --port $FOSSIL_PORT \\
  --baseurl https://$DOMAIN --errorlog /srv/fossil/error.log
Restart=on-failure
# Hardening
NoNewPrivileges=true
ProtectSystem=strict
ReadWritePaths=/srv/fossil
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now fossil-hub

# --- Caddy: automatic HTTPS reverse proxy ------------------------------------
cat > /etc/caddy/Caddyfile <<EOF
$DOMAIN {
    reverse_proxy 127.0.0.1:$FOSSIL_PORT
    encode gzip
}
EOF
systemctl reload caddy || systemctl restart caddy

# --- firewall ----------------------------------------------------------------
ufw allow OpenSSH >/dev/null
ufw allow 80/tcp  >/dev/null    # ACME challenges + redirect
ufw allow 443/tcp >/dev/null
ufw --force enable >/dev/null

# --- nightly consistent backups ---------------------------------------------
cat > /etc/cron.daily/fossil-backup <<'EOF'
#!/bin/sh
# SQLite-consistent snapshot of every hub repo. Clones are also backups,
# but this preserves server-side users/config/tickets state too.
for r in /srv/fossil/repos/*.fossil; do
  b="/srv/fossil/backups/$(basename "$r" .fossil)-$(date +%a).fossil"
  sudo -u fossil fossil backup --overwrite "$b" -R "$r"
done
# Optional off-box copy (uncomment + configure, e.g. Hetzner Storage Box):
# rsync -a /srv/fossil/backups/ u123456@u123456.your-storagebox.de:fossil-backups/
EOF
chmod +x /etc/cron.daily/fossil-backup

echo
echo "============================================================"
echo " Hub is up:  https://$DOMAIN/            (repo index)"
echo "             https://$DOMAIN/pm         (the pm repo)"
echo
echo " Next steps (web UI, as $ADMIN_USER):"
echo "  1. Change your password:      /pm/setup_uedit"
echo "  2. Create collaborator + agent accounts (Admin -> Users)."
echo "     Suggested 'claude' capabilities: c i o r w  (commit/read/write,"
echo "     no admin/setup). Generate a strong password; it is the token."
echo "  3. Clone:  fossil clone https://$ADMIN_USER@$DOMAIN/pm pm.fossil"
echo "============================================================"
