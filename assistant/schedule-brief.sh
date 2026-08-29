#!/bin/sh
# schedule-brief.sh -- make the morning brief ARRIVE instead of waiting to be
# asked. VIKIVERSE_V1 SS 2.4: "the clock, not the query".
#
# WHY THIS IS THE HIGHEST-VALUE THING LEFT AND ALSO THE SMALLEST. brief.sh has
# worked for days and nothing runs it, so the one requirement that says
# "Warren's first interaction of the day is READING, not asking" was unmet by a
# missing scheduler rather than by missing code.
#
# THIS IS L3 AND STAYS L3. It schedules an assistant; it changes nothing in
# src/ and viki does not learn that a clock exists. What time the brief runs is
# a judgment about Warren's day, which is the same reason `viki coverage`
# carries no staleness threshold.
#
# WHERE THE OUTPUT GOES, and it is a deliberate choice rather than a default.
# The brief is written to a FILE and the file is what you read. Mailing it or
# posting it would make this a sender, and SS 2.2c's `observe` rung is the only
# authority level that needs no policy decided first -- the reader extension
# lives under exactly that rule and so does this.
#
# Usage:
#   sh assistant/schedule-brief.sh install [HH:MM]   (default 06:30)
#   sh assistant/schedule-brief.sh status
#   sh assistant/schedule-brief.sh uninstall
#   sh assistant/schedule-brief.sh run               (run it once, now)
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
LABEL="net.vikiverse.brief"
OUTDIR="${VIKI_BRIEF_DIR:-$HOME/.viki/brief}"
ACTION="${1:-status}"
shift 2>/dev/null || true

# The time is an INSTALL argument only. Validating it for every action made
# `schedule-brief.sh run --me warren` fail with "time must be HH:MM, got
# --me" -- the scheduler refusing to do the one thing you would try first.
ATTIME=06:30
if [ "$ACTION" = "install" ] && [ $# -gt 0 ]; then
    ATTIME="$1"; shift
    case "$ATTIME" in
        [0-2][0-9]:[0-5][0-9]) : ;;
        *) echo "schedule-brief: time must be HH:MM, got '$ATTIME'" >&2; exit 2 ;;
    esac
fi
HH=${ATTIME%%:*}; MM=${ATTIME##*:}
HH=$(printf '%s' "$HH" | sed 's/^0//'); MM=$(printf '%s' "$MM" | sed 's/^0//')
: "${HH:=0}" "${MM:=0}"

PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
RUNNER="$ROOT/assistant/brief-run.sh"

# The runner exists so the scheduler entry stays a one-liner and so `run`
# and the scheduled invocation are THE SAME CODE PATH -- a scheduled job that
# differs from what you tested by hand is a job that fails on a morning you
# are not watching.
write_runner(){
    cat > "$RUNNER" <<'RUN'
#!/bin/sh
# brief-run.sh -- one scheduled brief. Written by schedule-brief.sh; edit
# freely, it is never regenerated over your changes without --force.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUTDIR="${VIKI_BRIEF_DIR:-$HOME/.viki/brief}"
mkdir -p "$OUTDIR" || exit 1
STAMP=$(date -u +%Y-%m-%d)
OUT="$OUTDIR/$STAMP.txt"

# WHERE IT RUNS FROM decides what it can see: the ledger and the cache are
# relative to a checkout. VIKI_BRIEF_CWD names it; the repo root is the
# default because that is the tribe most agents are working in.
cd "${VIKI_BRIEF_CWD:-$ROOT}" 2>/dev/null || cd "$ROOT" || exit 1

# A FAILED BRIEF MUST LEAVE EVIDENCE, not an empty file. brief.sh's own header
# says silence is indistinguishable from a broken job; a scheduler makes that
# worse, because nobody is watching when it breaks.
{
    printf 'brief generated %s  (cwd %s)\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(pwd)"
    printf '%s\n' "----------------------------------------------------------------"
    if sh "$ROOT/assistant/brief.sh" "$@" 2>&1; then
        :
    else
        rc=$?
        printf '\n*** THE BRIEF FAILED (exit %d). The lines above are all there is.\n' "$rc"
        printf '*** Do NOT read a short brief as a quiet day.\n'
    fi
} > "$OUT.tmp" 2>&1
mv -f "$OUT.tmp" "$OUT"
ln -sfn "$OUT" "$OUTDIR/latest.txt"
printf '%s\n' "$OUT"
RUN
    chmod +x "$RUNNER"
}

case "$ACTION" in
run)
    write_runner
    "$RUNNER" "$@"       # remaining args go straight to brief.sh
    ;;

install)
    write_runner
    mkdir -p "$OUTDIR" "$HOME/Library/LaunchAgents"
    if [ "$(uname)" != "Darwin" ]; then
        # No launchd. Print the cron line rather than editing a user's crontab
        # behind their back -- installing a recurring job is not a thing to do
        # silently on someone's machine.
        echo "schedule-brief: not macOS. Add this to your crontab (crontab -e):"
        echo
        echo "  $MM $HH * * *  $RUNNER >/dev/null 2>&1"
        echo
        echo "Then: sh assistant/schedule-brief.sh status"
        exit 0
    fi
    cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/bin/sh</string>
    <string>$RUNNER</string>
  </array>
  <key>StartCalendarInterval</key>
  <dict>
    <key>Hour</key><integer>$HH</integer>
    <key>Minute</key><integer>$MM</integer>
  </dict>
  <!-- RunAtLoad is deliberately false: installing the scheduler should not
       also fire it. Use \`schedule-brief.sh run\` to see one now. -->
  <key>RunAtLoad</key><false/>
  <key>StandardOutPath</key><string>$OUTDIR/launchd.out</string>
  <key>StandardErrorPath</key><string>$OUTDIR/launchd.err</string>
</dict>
</plist>
PL
    launchctl unload "$PLIST" >/dev/null 2>&1
    if launchctl load "$PLIST" 2>/dev/null; then
        echo "schedule-brief: installed, $ATTIME daily."
        echo "  runner : $RUNNER"
        echo "  output : $OUTDIR/latest.txt  (dated copies alongside)"
        echo "  see one now:  sh assistant/schedule-brief.sh run"
    else
        echo "schedule-brief: wrote $PLIST but launchctl load failed." >&2
        exit 1
    fi
    ;;

uninstall)
    if [ -f "$PLIST" ]; then
        launchctl unload "$PLIST" >/dev/null 2>&1
        rm -f "$PLIST"
        echo "schedule-brief: removed $PLIST"
    else
        echo "schedule-brief: nothing installed at $PLIST"
    fi
    ;;

status)
    # STATUS REPORTS FACTS AND JUDGES NOTHING -- the same rule viki coverage
    # follows. It says when the brief last ran and whether a job is loaded; it
    # does not decide whether that is too long ago.
    if [ -f "$PLIST" ]; then
        echo "scheduler: installed ($PLIST)"
        launchctl list 2>/dev/null | grep -q "$LABEL" \
            && echo "           loaded" || echo "           NOT loaded -- launchctl load '$PLIST'"
    else
        echo "scheduler: not installed"
    fi
    if [ -e "$OUTDIR/latest.txt" ]; then
        echo "last brief: $(ls -l "$OUTDIR/latest.txt" | sed 's/.*-> //')"
        echo "            $(head -1 "$OUTDIR/latest.txt" 2>/dev/null)"
    else
        echo "last brief: none in $OUTDIR"
    fi
    ;;

*)
    echo "usage: sh assistant/schedule-brief.sh install [HH:MM] | status | uninstall | run" >&2
    exit 2 ;;
esac
