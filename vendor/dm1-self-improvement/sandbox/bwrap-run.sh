#!/bin/bash
# bwrap-run.sh <project-dir> <command...>: run a command that can write only <project-dir>, with an empty home.
# Keeps other repositories and keys out of reach. It does not limit the network or hide the credentials file, which a
# claude session needs: use a separate API key with a low spending limit. Add its directory with --ro-bind by hand.
set -e
dir=$(readlink -f "$1"); shift
extra=()
[ -n "$SANDBOX_PYTHON" ] && extra+=(--ro-bind "$SANDBOX_PYTHON" "$SANDBOX_PYTHON")
[ -n "$SANDBOX_CLAUDE_DIR" ] && extra+=(--ro-bind "$SANDBOX_CLAUDE_DIR" "$HOME/.local/share/claude")
[ -n "$SANDBOX_CREDENTIALS" ] && extra+=(--dir "$HOME/.claude" --ro-bind "$SANDBOX_CREDENTIALS" "$HOME/.claude/.credentials.json")
exec bwrap --ro-bind /usr /usr --symlink usr/bin /bin --symlink usr/lib /lib --symlink usr/lib64 /lib64 \
  --ro-bind /etc /etc --ro-bind-try /run/resolvconf /run/resolvconf --ro-bind-try /run/systemd/resolve /run/systemd/resolve --proc /proc --dev /dev --tmpfs /tmp --tmpfs /home --dir "$HOME" --bind "$dir" "$dir" \
  "${extra[@]}" --unshare-pid --unshare-ipc --unshare-uts --die-with-parent --chdir "$dir" "$@"
