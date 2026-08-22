#!/bin/sh
# vikiverse-up.sh -- stand up a working vikiverse to play in.
#
# A TRIBE is one encrypted Fossil repo plus the accounts that can reach it.
# Peers are clones of it. This script builds a tribe and two peers with
# DIFFERENT caching postures, which is the whole point of the exercise:
#
#   laptop   caching = optional   has the model locally, does the embedding,
#                                 publishes the cache and the model as uv blobs
#   phone    caching = required   never builds a model, never runs onnx at
#                                 index time; it PULLS the cache and the model
#                                 and gets hybrid retrieval from the hub alone
#
# That asymmetry is D-11 and D-12 doing their job: an embedding is a
# deterministic function of (content_hash, model_id, chunk_params), so whoever
# sees the content first computes it and everyone else shares it.
#
# Everything runs over real HTTP against a real `fossil server`, not a file://
# stand-in -- FINDINGS.md records that file:// is not a faithful substitute
# when encryption and capabilities are in play, and QUEUE 35 records that the
# uv publish path needs the 'y' capability, which only shows up over HTTP.
#
# Usage:
#   sh build/vikiverse-up.sh <dir> [tribe-name] [--lan]
#
# DO NOT PIPE THIS SCRIPT. `vikiverse-up.sh dir | tail` will appear to hang
# forever even though every step completed and the README was written: the
# fossil server and `viki serve` it leaves running have children holding the
# pipe's write end, so the reader never sees EOF. Redirect to a file instead
# (`> up.log 2>&1`) and read that. Measured -- a fully successful run hung a
# ten-minute timeout this way, twice.
#     --lan   bind `viki serve` to 0.0.0.0 so a phone on the same network can
#             reach it. THERE IS NO AUTH. See the warning it prints.
set -e

DIR="$1"; [ -n "$DIR" ] || { echo "usage: $0 <dir> [tribe-name] [--lan]"; exit 2; }
TRIBE="${2:-camp}"; case "$TRIBE" in --*) TRIBE=camp;; esac
LAN=0; for a in "$@"; do [ "$a" = "--lan" ] && LAN=1; done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="${VIKI_BIN:-$ROOT/build/dist/viki}"
FOSSIL="${VIKI_FOSSIL_BIN:-$ROOT/vendor/fossil-see/build/dist/fossil-see}"
MODEL="${VIKI_MODEL_DIR:-$ROOT/build/dist/model}"
[ -x "$V" ]      || { echo "no viki at $V (run build/build.sh)"; exit 2; }
[ -x "$FOSSIL" ] || { echo "no fossil-see at $FOSSIL (run vendor/fossil-see/build/build.sh)"; exit 2; }
[ -d "$MODEL" ]  || { echo "no model at $MODEL -- the laptop peer needs one to embed with"; exit 2; }

FPORT="${VIKI_TRIBE_PORT:-8770}"
SPORT="${VIKI_SERVE_PORT:-8080}"
mkdir -p "$DIR"; DIR=$(cd "$DIR" && pwd)
RUN="$DIR/run"; mkdir -p "$RUN"
HUB="$DIR/tribe/$TRIBE.efossil"; mkdir -p "$DIR/tribe"

# Every fossil and viki invocation below needs these. FOSSIL_HOME keeps the
# per-user config inside the scratch tree instead of the developer's real
# ~/.config/fossil.db -- build/m1-e2e-probe.sh's original sin.
export FOSSIL_HOME="$DIR"
export USER="${USER:-viki}"
KEYHEX=$(od -An -tx1 -N32 /dev/urandom | tr -d ' \n')
printf 'x%s\n' "'$KEYHEX'" > "$DIR/tribe/KEY"; chmod 600 "$DIR/tribe/KEY"
export FOSSIL_SEE_KEY="x'$KEYHEX'"
export VIKI_MODEL_DIR="$MODEL"
# viki resolves fossil itself (viki_fossil_binary(): $VIKI_FOSSIL_BIN, else
# fossil-see on PATH, else fossil). The scratch fossil-see is on neither PATH,
# so without this every `viki cache push/pull` dies with exec: No such file.
export VIKI_FOSSIL_BIN="$FOSSIL"

say(){ printf '\n== %s\n' "$1"; }

# Both long-lived children below are started with `nohup ... < /dev/null` and
# their output redirected to files. That is not decoration: a background child
# that inherits this script's stdout keeps the write end of a pipe open, so
# `sh build/vikiverse-up.sh dir | tail` never terminates even though the script
# itself finished. Measured the hard way -- a run that completed every step,
# wrote its README, and still hung a 10-minute timeout.

say "tribe '$TRIBE': encrypted hub"
"$FOSSIL" new -A warren "$HUB" > "$RUN/new.out" 2>&1
# 'y' is the capability that authorises UNVERSIONED writes. Without it
# `fossil uv sync` is a silent no-op over HTTP and `viki cache push` used to
# exit 0 having published nothing -- QUEUE 35, and cache-probe.sh's G1.
"$FOSSIL" user capabilities warren saodxy -R "$HUB" >> "$RUN/new.out" 2>&1 || true
"$FOSSIL" user new peer peer@example.com peerpw -R "$HUB" >> "$RUN/new.out" 2>&1
"$FOSSIL" user capabilities peer ciorwy -R "$HUB" >> "$RUN/new.out" 2>&1
head -c 16 "$HUB" | od -An -tx1 | tr -d ' \n' > "$RUN/hub.magic"
if head -c 15 "$HUB" | grep -q 'SQLite format 3'; then
  echo "REFUSING: the hub is PLAINTEXT. The key was not applied."; exit 1
fi
echo "   hub is ciphertext (not 'SQLite format 3')  ok"

say "serving the tribe over HTTP on :$FPORT"
nohup "$FOSSIL" server "$HUB" --port "$FPORT" --localhost > "$RUN/fossil-server.log" 2>&1 < /dev/null &
echo $! > "$RUN/fossil.pid"
i=0; while [ $i -lt 40 ]; do
  if curl -sf "http://localhost:$FPORT/" >/dev/null 2>&1; then break; fi
  i=$((i+1)); sleep 0.25
done
curl -sf "http://localhost:$FPORT/" >/dev/null 2>&1 || { echo "server did not come up; see $RUN/fossil-server.log"; exit 1; }
echo "   http://localhost:$FPORT/  up"

say "peer 'laptop' (caching = optional, HAS the model)"
( cd "$DIR" && "$FOSSIL" clone --save-http-password \
    "http://peer:peerpw@localhost:$FPORT/" "$DIR/laptop.efossil" > "$RUN/clone-laptop.out" 2>&1 )
mkdir -p "$DIR/laptop" && ( cd "$DIR/laptop" && "$FOSSIL" open "$DIR/laptop.efossil" >> "$RUN/clone-laptop.out" 2>&1 )

mkdir -p "$DIR/laptop/notes"
cat > "$DIR/laptop/notes/water.md" <<'EOF'
# Stock water

The trough by the north gate freezes below about fifteen degrees, so the
tank heater goes in before the first hard freeze. The float valve sticks
when the pressure drops; tapping the brass body frees it.
EOF
cat > "$DIR/laptop/notes/fence.md" <<'EOF'
# Fence line

The east fence has a broken insulator two posts north of the creek
crossing. Until it is replaced the whole east run reads low on the tester.
EOF
cat > "$DIR/laptop/notes/tractor.md" <<'EOF'
# Tractor

The hydraulic filter is a NAPA 1051 and takes eleven quarts. The PTO
shear bolt is grade 2 on purpose -- do not substitute a grade 8.
EOF
( cd "$DIR/laptop" && "$FOSSIL" add notes >> "$RUN/laptop.out" 2>&1 \
  && "$FOSSIL" commit -m "seed notes" --no-warnings >> "$RUN/laptop.out" 2>&1 )

( cd "$DIR/laptop" && "$V" index . > "$RUN/laptop-index.out" 2>&1 )
echo "   indexed: $(grep -oE '[0-9]+ file' "$RUN/laptop-index.out" | head -1)"
( cd "$DIR/laptop" && "$V" cache push > "$RUN/laptop-push.out" 2>&1 ) \
  || { echo "cache push FAILED -- see $RUN/laptop-push.out"; tail -5 "$RUN/laptop-push.out"; exit 1; }
echo "   cache + model published as uv blobs"
"$FOSSIL" unversioned list -R "$HUB" 2>/dev/null | sed 's/^/     /' || true

say "peer 'phone' (caching = required, NO local model)"
( cd "$DIR" && "$FOSSIL" clone --save-http-password \
    "http://peer:peerpw@localhost:$FPORT/" "$DIR/phone.efossil" > "$RUN/clone-phone.out" 2>&1 )
mkdir -p "$DIR/phone" && ( cd "$DIR/phone" && "$FOSSIL" open "$DIR/phone.efossil" >> "$RUN/clone-phone.out" 2>&1 )
# The phone gets NO model directory of its own. It pulls one.
( cd "$DIR/phone" && VIKI_MODEL_DIR="$DIR/phone/model" "$V" cache pull > "$RUN/phone-pull.out" 2>&1 ) \
  || { echo "cache pull FAILED -- see $RUN/phone-pull.out"; tail -5 "$RUN/phone-pull.out"; exit 1; }
echo "   pulled cache$( [ -f "$DIR/phone/model/model.onnx" ] && echo ' + model' )"

say "proof: the phone answers a SEMANTIC query it never embedded"
Q="what keeps the livestock water from icing over"
( cd "$DIR/phone" && VIKI_MODEL_DIR="$DIR/phone/model" "$V" ask "$Q" --k 2 \
    > "$RUN/phone-ask.out" 2>&1 ) || true
sed -n '1,4p' "$RUN/phone-ask.out" | sed 's/^/   /'
if grep -q 'water.md' "$RUN/phone-ask.out"; then
  echo "   -> the phone found it. Hybrid retrieval from the hub alone."
else
  echo "   -> NOT FOUND. See $RUN/phone-ask.out"
fi

say "viki serve on the laptop peer"
if [ "$LAN" = 1 ]; then
  HOSTARG="--host 0.0.0.0"
  IP=$(ipconfig getifaddr en0 2>/dev/null || hostname -I 2>/dev/null | awk '{print $1}')
  URL="http://${IP:-<this-machine>}:$SPORT/"
  echo "   !! BOUND TO 0.0.0.0 WITH NO AUTHENTICATION."
  echo "   !! Anyone on this network can read the whole corpus. CLAUDE.md's"
  echo "   !! position is that real exposure goes behind the Caddy instance"
  echo "   !! server/setup-viki-serve.sh configures, not a raw LAN bind."
else
  HOSTARG=""; URL="http://127.0.0.1:$SPORT/"
fi
( cd "$DIR/laptop" && nohup $V serve $HOSTARG --port "$SPORT" > "$RUN/viki-serve.log" 2>&1 < /dev/null & echo $! > "$RUN/serve.pid" )
sleep 1
echo "   $URL       (capture UI at ${URL}capture)"

cat > "$DIR/vikiverse-down.sh" <<DOWN
#!/bin/sh
for p in "$RUN/serve.pid" "$RUN/fossil.pid"; do
  [ -f "\$p" ] && kill "\$(cat \$p)" 2>/dev/null && rm -f "\$p"
done
echo "vikiverse '$TRIBE' stopped."
DOWN
chmod +x "$DIR/vikiverse-down.sh"

cat > "$DIR/README.txt" <<README
VIKIVERSE '$TRIBE'

  tribe   $HUB          (encrypted; key in tribe/KEY, mode 0600)
  hub     http://localhost:$FPORT/
  laptop  $DIR/laptop   caching=optional, has the model, does the embedding
  phone   $DIR/phone    caching=required, no model of its own, pulled everything
  serve   $URL

Set these in any shell you play from:

  export FOSSIL_HOME="$DIR"
  export FOSSIL_SEE_KEY="\$(cat $DIR/tribe/KEY)"
  export VIKI_FOSSIL_BIN="$FOSSIL"

Then, as the laptop:
  cd $DIR/laptop
  export VIKI_MODEL_DIR="$MODEL"
  \$EDITOR notes/whatever.md && $FOSSIL commit -m "..." --no-warnings
  $V index . && $V cache push

And as the phone (note it points at its PULLED model, not the build tree):
  cd $DIR/phone
  export VIKI_MODEL_DIR="$DIR/phone/model"
  $FOSSIL update && $V cache pull
  $V ask "what keeps the livestock water from icing over"

Add another peer to the same tribe:
  $FOSSIL clone --save-http-password http://peer:peerpw@localhost:$FPORT/ p3.efossil

Stop everything:
  $DIR/vikiverse-down.sh
README
say "ready"
sed -n '1,8p' "$DIR/README.txt" | sed 's/^/   /'
echo
echo "   full instructions: $DIR/README.txt"
echo "   stop:              $DIR/vikiverse-down.sh"
