#!/bin/bash
# start.sh -- a trace that survives your laptop.
#
# It runs on a machine that does not sleep, inside tmux, so a closed lid, a
# dropped ssh and a dead battery are all the same thing to it: nothing. You
# detach; it keeps working.
#
#   sh /mnt/lbn-tribes/village/start.sh          # create or attach
#   sh /mnt/lbn-tribes/village/start.sh --status # is it alive, and how full
#   tmux detach                                  # or ctrl-b d
#
# WHAT IT DOES NOT SURVIVE: a reboot of THIS machine, and its own context
# window. The first is what `--boot` installs a systemd unit for. The second is
# not a failure to engineer around -- it is the point. The trace retires at
# ~70%, writes its memoir into the store, and the next one queries rather than
# inherits. Compaction is the failure case, not the plan.
set -u
DIR=/mnt/lbn-tribes/village
SESSION="${VILLAGE_SESSION:-village}"
RUN_USER=paradox

as_paradox() { if [ "$(whoami)" = "$RUN_USER" ]; then bash -lc "$1"; else sudo -u "$RUN_USER" bash -lc "$1"; fi; }

case "${1:-}" in
--status)
    if as_paradox "tmux has-session -t '$SESSION' 2>/dev/null"; then
        echo "session '$SESSION': ALIVE"
        as_paradox "tmux list-panes -t '$SESSION' -F '  pane #{pane_id} #{pane_current_command} (#{pane_width}x#{pane_height})'"
    else
        echo "session '$SESSION': not running.  start it:  sh $DIR/start.sh"
    fi
    echo
    as_paradox "HOME=/home/$RUN_USER CLAUDE_TRANSCRIPT_DIR=/home/$RUN_USER/.claude/projects sh $DIR/fuel.sh"
    echo
    if [ -r "$DIR/LEASE" ]; then echo "lease until: $(cat "$DIR/LEASE")"; else echo "lease: NONE (scheduled journeymen will not run; this interactive session is unaffected)"; fi
    exit 0 ;;
--boot)
    # survive a reboot of the machine itself. The session comes back EMPTY --
    # a new trace, which is correct: the store is the continuity, not the tmux.
    cat > /etc/systemd/system/village-tmux.service <<UNIT
[Unit]
Description=village tmux session
After=network-online.target
[Service]
Type=forking
User=$RUN_USER
ExecStart=/usr/bin/tmux new-session -d -s $SESSION -c $DIR
ExecStop=/usr/bin/tmux kill-session -t $SESSION
RemainAfterExit=yes
[Install]
WantedBy=multi-user.target
UNIT
    systemctl daemon-reload && systemctl enable --now village-tmux.service
    echo "installed: the tmux session is recreated at boot (empty -- start claude in it)"
    exit 0 ;;
esac

if as_paradox "tmux has-session -t '$SESSION' 2>/dev/null"; then
    echo "attaching to existing session '$SESSION' (ctrl-b d to detach)"
else
    echo "creating session '$SESSION' in $DIR"
    as_paradox "tmux new-session -d -s '$SESSION' -c '$DIR'"
    # Do NOT auto-run claude with a prompt here. A trace should begin by
    # reading CLAUDE.md and querying the store, not by executing an errand
    # someone left in a shell script months earlier.
    as_paradox "tmux send-keys -t '$SESSION' 'sh $DIR/fuel.sh; echo; echo \"CLAUDE.md is the brief. Store: village.diary. Start with: claude\"' C-m"
fi
if [ "$(whoami)" = "$RUN_USER" ]; then exec tmux attach -t "$SESSION"; else exec sudo -u "$RUN_USER" tmux attach -t "$SESSION"; fi
