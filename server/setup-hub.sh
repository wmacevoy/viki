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
# but this preserves server-side users/config/tickets state too -- AND the
# unversioned blobs (the embedding cache and the model), which plain
# `fossil clone` does NOT carry. For those, this box is the only copy, so
# an off-box leg is not optional in practice.
set -eu

REPOS=/srv/fossil/repos
BACKUPS=/srv/fossil/backups
STAMP="$BACKUPS/LAST_SUCCESS"
OFFBOX="${FOSSIL_BACKUP_RSYNC_TARGET:-}"   # e.g. u1@u1.your-storagebox.de:fossil-backups/

n=0
# BOTH suffixes. The encrypted hub uses *.efossil, and a glob that matched
# only *.fossil would back up NOTHING, silently, on exactly the deployment
# that most needs it.
for r in "$REPOS"/*.fossil "$REPOS"/*.efossil; do
  [ -e "$r" ] || continue
  base=$(basename "$r"); base=${base%.fossil}; base=${base%.efossil}
  sudo -u fossil fossil backup --overwrite "$BACKUPS/$base-$(date +%a).fossil" -R "$r"
  n=$((n+1))
done
[ "$n" -gt 0 ] || { echo "fossil-backup: NO REPOS MATCHED in $REPOS" >&2; exit 1; }

# OFF-BOX. $BACKUPS is on the same filesystem as $REPOS, so without this the
# "backup" survives nothing that takes the disk -- which is the failure it
# exists for. Configure FOSSIL_BACKUP_RSYNC_TARGET (see /etc/default/fossil-backup).
if [ -n "$OFFBOX" ]; then
  rsync -a --delete "$BACKUPS"/ "$OFFBOX"
else
  echo "fossil-backup: WARNING -- no off-box target configured;" >&2
  echo "  $BACKUPS is on the same disk as $REPOS, so this protects against" >&2
  echo "  accidental deletion and NOT against losing the machine." >&2
fi

# A backup that has been failing for six months must not look like one that
# works. cron.daily mail goes to root, and a stock cloud VPS has no MTA --
# so leave a timestamp something can actually check.
date -u +%Y-%m-%dT%H:%M:%SZ > "$STAMP"
echo "fossil-backup: $n repo(s) at $(cat "$STAMP")"
EOF
# Read by the cron script above; keeps the target out of the script itself.
[ -f /etc/default/fossil-backup ] || cat > /etc/default/fossil-backup <<'EOF'
# Off-box backup target for /etc/cron.daily/fossil-backup.
# Without one, backups live on the same disk as the repos and do not
# survive losing the machine. Example:
# FOSSIL_BACKUP_RSYNC_TARGET=u123456@u123456.your-storagebox.de:fossil-backups/
FOSSIL_BACKUP_RSYNC_TARGET=
EOF
sed -i '1a [ -r /etc/default/fossil-backup ] \&\& . /etc/default/fossil-backup' \
    /etc/cron.daily/fossil-backup
chmod +x /etc/cron.daily/fossil-backup

echo
echo "============================================================"
echo " Hub is up:  https://$DOMAIN/            (repo index)"
echo "             https://$DOMAIN/pm         (the pm repo)"
echo
echo " Next steps (web UI, as $ADMIN_USER):"
echo "  1. Change your password:      /pm/setup_uedit"
echo "  2. Create collaborator + agent accounts (Admin -> Users)."
echo "     Suggested 'claude' capabilities: c i o r w y  (commit/read/write,"
echo "     no admin/setup). Generate a strong password; it is the token."
echo "     'y' IS NOT OPTIONAL: without it 'viki cache push' publishes NOTHING."
echo "     Uploading unversioned content requires 'y'; the server refuses, and"
echo "     fossil's own sync_unversioned() discards the error -- so push used to"
echo "     exit 0 with the hub empty. viki now detects and fails loudly, but the"
echo "     capability still has to be granted. See build/cache-probe.sh (G1)."
echo "  3. Clone:  fossil clone https://$ADMIN_USER@$DOMAIN/pm pm.fossil"
echo "============================================================"
