#!/bin/sh
# converse.sh -- the facilitator. Sync, then show ONLY what arrived while you
# were away, and who said it.
#
# WHY THIS RATHER THAN AUTOMATION. The asymmetry is physical: this laptop can
# reach the village, and the village CANNOT reach back -- there is no inbound
# route to a machine behind NAT that sleeps. So no daemon on either side makes
# the exchange automatic; something on THIS side has to ask, and it can only
# ask while awake. What can be removed is not the asking, it is the DIFFING:
# without this you sync and then stare at 70-odd claims wondering which are new.
#
# It uses viki's OWN arrival clock rather than a timestamp: `observe --seq` is
# this diary's local counter, and `observe --after N` is the set that reached
# HERE after N. That is the correct question for a store with no arrival order
# in its schema -- ids are content hashes, so "new to me" is a set difference,
# not a time comparison, and two peers' clocks never have to agree.
#
#   sh assistant/tribe/converse.sh          # sync, then show what they said
#   sh assistant/tribe/converse.sh --again  # re-show the last batch
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
V=${VIKI_BIN:-$HOME/projects/viki/core/build/viki}
K=${TRIBE_KEY:-$HOME/.viki/tribe.key}
S=${TRIBE_DIARY:-$HOME/.viki/tribe.diary}
MARK=${TRIBE_MARK:-$HOME/.viki/.converse-seq}

q(){ "$V" --keyfile "$K" --store "$S" sql "$1" 2>/dev/null; }
seq_now(){ "$V" --keyfile "$K" --store "$S" observe --seq 2>/dev/null | tr -d ' \n'; }

if [ "${1:-}" = "--again" ]; then
    FROM=$(cat "$MARK" 2>/dev/null || echo 0)
else
    FROM=$(cat "$MARK" 2>/dev/null || seq_now)
    sh "$HERE/sync.sh" >/dev/null 2>&1 || { echo "  SYNC FAILED -- the village is unreachable."; echo "  This is NOT the same as 'nobody said anything'."; exit 3; }
fi
TO=$(seq_now)

IDS=$("$V" --keyfile "$K" --store "$S" observe --after "$FROM" 2>/dev/null || true)
if [ -z "$IDS" ]; then
    echo "  nothing new since seq $FROM (now $TO)."
    echo "  (that is 'they have said nothing', not 'I could not look' -- the sync above succeeded)"
    echo "$TO" > "$MARK"; exit 0
fi

# Show only what someone ELSE wrote. My own claims are not news to me.
LIST=$(printf '%s\n' $IDS | sed "s/^/'/;s/\$/'/" | paste -sd, -)
echo "  new since seq $FROM (now $TO):"
echo
q "SELECT '  --- ['||json_extract(body,'\$.status')||'] '||json_extract(body,'\$.by')||char(10)||atext
   FROM viki_assert WHERE id IN ($LIST) AND kind='claim'
   AND json_extract(body,'\$.by') NOT LIKE 'nanny.1%'
   ORDER BY ts" | fold -w 92 -s | sed 's/^/  /'
echo
q "SELECT '  (also '||count(*)||' of my own, not shown)' FROM viki_assert
   WHERE id IN ($LIST) AND json_extract(body,'\$.by') LIKE 'nanny.1%' HAVING count(*)>0"
echo "$TO" > "$MARK"
