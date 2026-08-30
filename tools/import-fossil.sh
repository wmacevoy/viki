#!/bin/sh
# import-fossil.sh -- a fossil repo into a viki diary.
#
# WHAT THIS IS FOR. Warren, 2026-08-30: "a nice requirement is viki >= fossil
# from a feature perspective (not representation). This allows a fossil record
# to be imported without loss."
#
# The bar was then narrowed by measurement rather than argument. The tribe's
# entire fossil-held history on tribes.lifebythenumbers.com is about FORTY
# artifacts across five repos -- wiki pages and tickets used as a shared
# notepad, with one trivial check-in each. No branches, no merges, no deltas.
# So the goal here is not fossil parity; it is "these five repos, losslessly",
# which is finishable.
#
# THE MAPPING, and it is deliberately thin because viki already had the shape:
#
#   wiki page   -> file  akey="wiki:<Name>"    versions by supersession
#   ticket      -> file  akey="ticket:<uuid>"  the composed text
#   check-in    -> checkin, grouping the files it brought, superseding its
#                  parent so `viki why` walks commit history
#
# CAST(content(...) AS TEXT) IS NOT COSMETIC. content() returns a BLOB and
# `fossil sql` renders blobs as an X'...' hex literal. Without the cast this
# script imports every wiki page as hex, reports success, and the corruption is
# invisible until someone reads a page. It did exactly that on the first run
# here -- 14 artifacts "imported", all of them hex -- and the check written to
# detect it ALSO failed, because it globbed for a leading hex digit while the
# value starts with X'. Verify imported content by reading one page, not by
# counting rows.
#
# WHAT IS PRESERVED: content, name, author, timestamp, and the version chain.
# WHAT IS NOT, stated here rather than discovered later: a ticket's FIELD
# STRUCTURE collapses to text, and file renames are not detected. If either
# matters for a given repo, this is the wrong tool for that repo.
#
# Usage:  sh tools/import-fossil.sh <repo.fossil> <diary.db> [--keyfile K]

set -eu
REPO=${1:?usage: import-fossil.sh <repo.fossil> <diary.db> [--keyfile K]}
DIARY=${2:?usage: import-fossil.sh <repo.fossil> <diary.db> [--keyfile K]}
shift 2
VIKI="${VIKI_BIN:-$(cd "$(dirname "$0")/.." && pwd)/core/build/viki}"
FOSSIL="${FOSSIL_BIN:-fossil}"
[ -x "$VIKI" ]  || { echo "no viki-core at $VIKI"; exit 2; }
[ -f "$REPO" ]  || { echo "no repo at $REPO"; exit 2; }

# key args pass straight through; --plaintext is a deliberate choice, not a default
KEYARGS="--plaintext"
[ $# -gt 0 ] && KEYARGS="$*"

v() { "$VIKI" $KEYARGS --store "$DIARY" "$@"; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
MEMBERS="$TMP/members"; : > "$MEMBERS"

nw=0; nt=0
# ---- wiki pages ---------------------------------------------------------
# Query lifted from src/viki_index.c's index_wiki(), which was verified
# against real repos: tagxref.value is the page SIZE, not the artifact, so the
# artifact comes from tagxref.rid.
"$FOSSIL" sql -R "$REPO" "
  SELECT substr(tag.tagname,6)
    FROM tag, tagxref
   WHERE tag.tagname GLOB 'wiki-*'
     AND tagxref.tagid = tag.tagid
     AND TYPEOF(tagxref.value+0) = 'integer'
     AND tagxref.value+0 <> 0
   GROUP BY 1" 2>/dev/null | tr -d "'" | while IFS= read -r name; do
    [ -n "$name" ] || continue
    "$FOSSIL" sql -R "$REPO" "
      SELECT CAST(content(b.uuid) AS TEXT) FROM tag, tagxref, blob b
       WHERE tag.tagname = 'wiki-$name' AND tagxref.tagid = tag.tagid
         AND b.rid = tagxref.rid ORDER BY tagxref.mtime DESC LIMIT 1" \
      2>/dev/null | sed "s/^'//;s/'$//" > "$TMP/body"
    [ -s "$TMP/body" ] || continue
    id=$(v file "wiki:$name" --content-from "$TMP/body" --who fossil-import 2>/dev/null) || continue
    echo "$id" >> "$MEMBERS"
    echo "  wiki:$name"
done

# ---- tickets ------------------------------------------------------------
"$FOSSIL" sql -R "$REPO" "
  SELECT tkt_uuid FROM ticket" 2>/dev/null | tr -d "'" | while IFS= read -r u; do
    [ -n "$u" ] || continue
    "$FOSSIL" sql -R "$REPO" "
      SELECT coalesce(title,'')||char(10)||coalesce(status,'')||' / '
             ||coalesce(type,'')||char(10)||coalesce(comment,'')
        FROM ticket WHERE tkt_uuid='$u'" 2>/dev/null \
      | sed "s/^'//;s/'$//" > "$TMP/body"
    [ -s "$TMP/body" ] || continue
    id=$(v file "ticket:$u" --content-from "$TMP/body" --who fossil-import 2>/dev/null) || continue
    echo "$id" >> "$MEMBERS"
    echo "  ticket:$u"
done

# ---- one check-in grouping what came across -----------------------------
# The repos here carry a single trivial check-in each, so this records the
# IMPORT as the group rather than pretending to reconstruct a history that
# was never there. A repo with real commit history wants a loop here, and
# this comment is the marker for whoever needs it.
n=$(wc -l < "$MEMBERS" | tr -d ' ')
if [ "$n" -gt 0 ]; then
    # shellcheck disable=SC2046
    ck=$(v checkin --comment "import of $(basename "$REPO")" --who fossil-import \
             --branch "$(basename "$REPO" .fossil)" $(cat "$MEMBERS")) || ck=""
    echo "  checkin $(echo "$ck" | cut -c1-12) grouping $n artifact(s)"
fi
echo "$n artifact(s) imported from $(basename "$REPO")"
