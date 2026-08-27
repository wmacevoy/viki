#!/bin/sh
# cal-probe.sh -- the ICS shredder (CALENDAR_DESIGN_V2.md SS 2, SS 5; QUEUE SS 52).
#
# WHAT THIS IS PROVING. Not "it parsed" -- an ICS parser that returns SOMETHING
# for every input is the easiest thing here to write and the least useful. The
# assertions below target the four places a naive parser is silently WRONG
# rather than loudly broken, plus the tier boundary the design turns on.
#
#   C-series  line folding. RFC 5545 folds at 75 octets with CRLF+space, and
#             the whitespace is PART OF THE FOLD. A parser that strips the
#             newline but keeps the space corrupts every long SUMMARY, and
#             produces a plausible string, so nothing downstream notices.
#   P-series  the name/params/value split. The colon ending the params is not
#             the first colon in the line: a quoted param may contain one, and
#             every mailto: value does. Getting it wrong truncates the value
#             into a suffix of itself -- again plausible, again silent.
#   T-series  the four time forms (T3), and that NO OFFSET is ever stored (T2).
#   R-series  RFC 5546 resolution as a READ-TIME projection: the superseded
#             assertion must still be in the table. This is the grow-only
#             property that makes the tier mergeable (SYNC.md SS 2).
#   N-series  what the shredder REFUSES. A fetch that returned an HTML error
#             page must not read as an empty calendar.
#
# NO `set -e`: several controls are `grep -c ... = 0`, and grep exits 1 on zero
# matches, which would abort the suite exactly when the code is correct. That
# has bitten this repo three times.

ROOT=$(cd "$(dirname "$0")/.." && pwd)
V="${VIKI_BIN:-$ROOT/build/dist/viki}"
DIR="$1"
[ -n "$DIR" ] || { echo "usage: sh build/cal-probe.sh <empty-dir>"; exit 2; }
[ -x "$V" ] || { echo "no viki at $V"; exit 2; }
command -v sqlite3 >/dev/null 2>&1 || { echo "sqlite3 required"; exit 2; }
mkdir -p "$DIR" || exit 2
DIR=$(cd "$DIR" && pwd)
cd "$DIR" || exit 2
export VIKI_MODEL_DIR="$DIR/no-model"

PASS=0; FAIL=0; SKIP=0
ok(){ PASS=$((PASS+1)); echo "  PASS  $1"; }
no(){ FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
sk(){ SKIP=$((SKIP+1)); echo "  SKIP  $1"; }
chk(){ if [ "$2" = "$3" ]; then ok "$1"; else no "$1 (got '$2' want '$3')"; fi; }
q(){ sqlite3 "$DIR/.viki/cache.db" "$1" 2>/dev/null; }

# CRLF throughout, because that is what real exporters emit and it is what the
# fold rule is defined in terms of.
{
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//viki cal-probe//EN\r\n'
printf 'BEGIN:VEVENT\r\nUID:evt-fold\r\nDTSTAMP:20260820T100000Z\r\nSEQUENCE:0\r\n'
printf 'DTSTART:20260901T150000Z\r\n'
printf 'SUMMARY:delivery of alfalfa and a description long enough to be fol\r\n ded across two lines\r\n'
printf 'END:VEVENT\r\n'
printf 'BEGIN:VEVENT\r\nUID:evt-quote\r\nDTSTAMP:20260820T100000Z\r\nSEQUENCE:0\r\n'
printf 'DTSTART;TZID=America/Denver:20260828T140000\r\n'
printf 'ATTENDEE;PARTSTAT=ACCEPTED;CN="MacEvoy, Warren: ranch":mailto:w@example.com\r\n'
printf 'SUMMARY:quoted param carries a colon and a comma\r\n'
printf 'END:VEVENT\r\n'
printf 'BEGIN:VEVENT\r\nUID:evt-esc\r\nDTSTAMP:20260820T100000Z\r\nSEQUENCE:0\r\n'
printf 'DTSTART:20260902T150000Z\r\nSUMMARY:moved\\, later\; and again\r\nEND:VEVENT\r\n'
printf 'BEGIN:VEVENT\r\nUID:evt-date\r\nDTSTAMP:20260820T100000Z\r\nDTSTART;VALUE=DATE:20260905\r\nSUMMARY:county fair\r\nEND:VEVENT\r\n'
printf 'BEGIN:VEVENT\r\nUID:evt-float\r\nDTSTAMP:20260820T100000Z\r\nDTSTART:20260906T090000\r\nSUMMARY:ride wherever I am\r\nEND:VEVENT\r\n'
printf 'BEGIN:VEVENT\r\nUID:evt-seq\r\nDTSTAMP:20260820T100000Z\r\nSEQUENCE:0\r\nDTSTART:20260910T140000Z\r\nSUMMARY:original time\r\nEND:VEVENT\r\n'
printf 'BEGIN:VEVENT\r\nUID:evt-seq\r\nDTSTAMP:20260821T090000Z\r\nSEQUENCE:1\r\nDTSTART:20260910T160000Z\r\nSUMMARY:moved two hours later\r\nEND:VEVENT\r\n'
printf 'BEGIN:VTODO\r\nUID:todo-brand\r\nDTSTAMP:20260820T100000Z\r\nDUE;TZID=America/Denver:20260830T170000\r\nSUMMARY:file the brand inspection\r\nEND:VTODO\r\n'
printf 'BEGIN:VTIMEZONE\r\nTZID:America/Denver\r\nEND:VTIMEZONE\r\n'
printf 'END:VCALENDAR\r\n'
} > feed.ics

"$V" calendar shred feed.ics --source probe-feed >shred.out 2>shred.err
chk "S1 SETUP: the shred succeeded" "$?" "0"

echo "== C: RFC 5545 line folding =="
# The fold is CRLF + one space, and BOTH are removed. "fol\r\n ded" is "folded".
chk "C1 a folded SUMMARY rejoins with NO space at the seam" \
    "$(q "SELECT count(*) FROM cal_event WHERE summary LIKE '%be folded across%'")" "1"
# CONTROL, and the reason C1 is not just a LIKE that would pass on the raw
# bytes: the broken form -- newline dropped, fold space kept -- must be absent.
chk "C1b CONTROL: the space-at-the-seam corruption is NOT present" \
    "$(q "SELECT count(*) FROM cal_event WHERE summary LIKE '%fol ded%'")" "0"

echo "== P: the params/value split =="
# A quoted param containing ':' and ',' must not end the params early.
chk "P1 a value survives a quoted param containing a colon" \
    "$(q "SELECT count(*) FROM cal_attendee WHERE attendee='mailto:w@example.com'")" "1"
chk "P2 the param AFTER the quoted one is still read" \
    "$(q "SELECT count(*) FROM cal_attendee WHERE partstat='ACCEPTED'")" "1"
# CONTROL: the classic first-colon bug truncates mailto: to its scheme.
chk "P2b CONTROL: no attendee was truncated at the first colon" \
    "$(q "SELECT count(*) FROM cal_attendee WHERE attendee='mailto' OR attendee=''")" "0"
chk "P3 TEXT escaping is undone (\\, and \;)" \
    "$(q "SELECT count(*) FROM cal_event WHERE summary='moved, later; and again'")" "1"

echo "== T: the four time forms, and no stored offset =="
chk "T1 zoned  keeps the IANA name by reference" \
    "$(q "SELECT dtstart_tzid||'|'||dtstart_form FROM cal_event WHERE uid='evt-quote'")" "America/Denver|zoned"
chk "T2 utc    is recognised by the trailing Z" \
    "$(q "SELECT dtstart_form FROM cal_event WHERE uid='evt-fold'")" "utc"
chk "T3 date   is recognised by VALUE=DATE" \
    "$(q "SELECT dtstart_form FROM cal_event WHERE uid='evt-date'")" "date"
chk "T4 floating is the residue, not the default for everything" \
    "$(q "SELECT dtstart_form FROM cal_event WHERE uid='evt-float'")" "floating"
# T2 of the design: never store an offset. If any column held one, THIS is
# where it would show; a zone name is a fact, an offset is a computed value
# with a shelf life.
chk "T5 CONTROL: no row stores a numeric UTC offset anywhere" \
    "$(q "SELECT count(*) FROM cal_event WHERE dtstart_tzid GLOB '*[-+][0-9][0-9][0-9][0-9]*' OR dtend_tzid GLOB '*[-+][0-9][0-9][0-9][0-9]*'")" "0"
# VTIMEZONE is skipped, not half-stored -- T1 exists to avoid exactly that.
chk "T6 CONTROL: VTIMEZONE produced no row" \
    "$(q "SELECT count(*) FROM cal_event WHERE component NOT IN ('VEVENT','VTODO')")" "0"

echo "== R: RFC 5546 resolution is a READ-TIME projection =="
chk "R1 both assertions for the moved event are RETAINED" \
    "$(q "SELECT count(*) FROM cal_event WHERE uid='evt-seq'")" "2"
chk "R2 the higher SEQUENCE is what the read surface shows" \
    "$("$V" calendar events --all 2>/dev/null | grep -c 'moved two hours later')" "1"
# CONTROL: and the superseded one is NOT shown. Without this, R2 passes on a
# surface that shows both -- which is not resolution, it is duplication.
chk "R2b CONTROL: the superseded assertion is not shown" \
    "$("$V" calendar events --all 2>/dev/null | grep -c 'original time')" "0"
chk "R3 the count of superseded rows is REPORTED, not hidden" \
    "$("$V" calendar events --all 2>&1 | grep -c '1 superseded')" "1"
# Re-shredding the same file must add nothing: identity is
# (uid,recurrence_id,sequence,dtstamp), so the tier is grow-only on a content
# key and union IS merge (SYNC.md SS 2).
"$V" calendar shred feed.ics --source probe-feed >/dev/null 2>&1
chk "R4 re-shredding the same feed adds NO rows (grow-only on a content key)" \
    "$(q "SELECT count(*) FROM cal_event")" "8"

echo "== V: VTODO is a point, not an interval =="
chk "V1 a VTODO with DUE and no DTSTART is stored as a VTODO" \
    "$(q "SELECT component FROM cal_event WHERE uid='todo-brand'")" "VTODO"
chk "V2 ...and its DUE is shown rather than '(no time)'" \
    "$("$V" calendar events --all 2>/dev/null | grep -c '20260830T170000.*\[due\]')" "1"

echo "== N: what it REFUSES =="
printf '<html><body>401 Unauthorized</body></html>\n' > notcal.ics
"$V" calendar shred notcal.ics >/dev/null 2>err.html
chk "N1 a non-iCalendar file is REFUSED (nonzero)" "$?" "1"
chk "N1b ...and says why, rather than reporting an empty calendar" \
    "$(grep -c 'no BEGIN:VCALENDAR' err.html)" "1"
"$V" calendar shred /nonexistent/nope.ics >/dev/null 2>err.missing
chk "N2 a missing file is an error, not silence" "$?" "1"
# A component with no UID has no identity, so RFC 5546 cannot resolve it and a
# synthesised key would collide or duplicate. Refuse the row, keep the rest.
{ printf 'BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTAMP:20260820T100000Z\r\nSUMMARY:no uid at all\r\nEND:VEVENT\r\n'
  printf 'BEGIN:VEVENT\r\nUID:has-uid\r\nDTSTAMP:20260820T100000Z\r\nSUMMARY:this one is fine\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n'; } > nouid.ics
"$V" calendar shred nouid.ics >/dev/null 2>&1
chk "N3 a component with no UID is dropped (no identity, no resolution)" \
    "$(q "SELECT count(*) FROM cal_event WHERE summary='no uid at all'")" "0"
chk "N3b CONTROL: ...and the valid component beside it still landed" \
    "$(q "SELECT count(*) FROM cal_event WHERE uid='has-uid'")" "1"

echo "== J: the judgment that must NOT be here =="
# SCOPES SS 3 / CALENDAR_DESIGN_V2 SS 5: TRANSP, STATUS and PARTSTAT are facts.
# Nothing in src/ may score them into "busy", and no time band may appear.
# ASSERTED AGAINST THE SCHEMA, NOT THE SOURCE TEXT. The first cut grepped
# viki_cal.c for /busy/ and went red on the word VFREEBUSY inside a comment
# explaining that the component is SKIPPED -- i.e. it failed on prose that says
# the right thing. What the rule is actually about is whether a column or a
# value scores availability, so ask the table.
chk "J1 no column in the assertion tier scores availability" \
    "$(q "SELECT count(*) FROM pragma_table_info('cal_event') WHERE lower(name) GLOB '*busy*' OR lower(name) GLOB '*free*' OR lower(name) GLOB '*avail*'")" "0"
# CONTROL: the FACTS those judgments would be computed FROM are present, so J1
# is 'viki declines to decide', not 'viki dropped the inputs'.
chk "J1b CONTROL: ...but TRANSP, STATUS and PARTSTAT are all stored as facts" \
    "$(q "SELECT (SELECT count(*) FROM pragma_table_info('cal_event') WHERE name IN ('transp','status')) + (SELECT count(*) FROM pragma_table_info('cal_attendee') WHERE name='partstat')")" "3"
chk "J2 no named time band leaked into the tool" \
    "$(grep -ciE '\"(morning|afternoon|evening|open enough)\"' "$ROOT/src/viki_cal.c")" "0"

echo
echo "== the READ surface, which 28 green assertions never touched =="
# THE GAP THIS CLOSES. The original 28 assertions proved the PARSER and never
# ran --from, --to or --json. Every one of the defects below was live under a
# fully green suite, and two of them make the feature useless rather than
# merely wrong -- which is what a brief consuming this would have hit first.
{ printf 'BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:read-1\r\nDTSTAMP:20260820T100000Z\r\nSEQUENCE:0\r\n'
  printf 'DTSTART;TZID=America/Denver:20260828T140000\r\nDTEND;TZID=America/Denver:20260828T150000\r\n'
  printf 'SUMMARY:readmarker vet visit for "the mare"\r\n'
  printf 'ATTENDEE;PARTSTAT=ACCEPTED:mailto:warren@example.com\r\n'
  printf 'BEGIN:VALARM\r\nACTION:EMAIL\r\nSUMMARY:alarmmarker robot\r\nDURATION:PT15M\r\n'
  printf 'ATTENDEE:mailto:noreply@robot.example\r\nEND:VALARM\r\n'
  printf 'END:VEVENT\r\nEND:VCALENDAR\r\n'; } > read.ics
"$V" calendar shred read.ics >/dev/null 2>&1

echo "-- nested sub-components --"
# VALARM lives INSIDE a VEVENT and carries its own SUMMARY/DURATION/ATTENDEE.
# Those landed on the meeting: a one-hour appointment read as PT15M and a
# notification robot joined the attendee list.
chk "K1 a VALARM's DURATION does not become the event's" \
    "$(q "SELECT count(*) FROM cal_event WHERE uid='read-1' AND duration IS NOT NULL")" "0"
chk "K2 a VALARM's ATTENDEE does not join the event's attendee list" \
    "$(q "SELECT count(*) FROM cal_attendee WHERE attendee LIKE '%robot%'")" "0"
chk "K3 a VALARM's SUMMARY does not overwrite the event's" \
    "$(q "SELECT count(*) FROM cal_event WHERE uid='read-1' AND summary LIKE 'readmarker%'")" "1"
# CONTROL: the real attendee still landed, so K2 is not "attendees are dropped".
chk "K3b CONTROL: the event's OWN attendee is still stored" \
    "$(q "SELECT count(*) FROM cal_attendee WHERE attendee='mailto:warren@example.com'")" "1"

echo "-- the date filter, in the format the usage string documents --"
# Stored times are RFC 5545 BASIC ("20260828T140000"); a person writes EXTENDED
# ("2026-08-28"). '-' sorts below every digit, so a --to bound excluded the
# entire calendar: --all returned 1 and --from/--to returned 0.
chk "K4 --from/--to in ISO finds the event" \
    "$("$V" calendar events --from 2026-08-01 --to 2026-12-31 2>/dev/null | grep -c readmarker)" "1"
# A date-only --to means the END of that day -- the same trap that made a
# promise due today read OVERDUE.
chk "K5 a date-only --to includes events later that same day" \
    "$("$V" calendar events --from 2026-08-28 --to 2026-08-28 2>/dev/null | grep -c readmarker)" "1"
# CONTROL: the filter must still EXCLUDE. Without this, K4/K5 are satisfied by
# a filter that ignores its arguments -- which is what "0 results" was, in
# reverse.
chk "K6 CONTROL: a range that excludes the event returns nothing" \
    "$("$V" calendar events --from 2027-01-01 --to 2027-12-31 2>/dev/null | grep -c readmarker)" "0"

echo "-- --json is machine-readable, and calendar text is attacker-controlled --"
# A SUMMARY containing a quote fabricated fields in the JSON. An .ics is
# attacker-controlled the moment a feed URL is.
chk "K7 --json parses with a quote in SUMMARY" \
    "$("$V" calendar events --json 2>/dev/null | python3 -c 'import json,sys
try:
    json.load(sys.stdin); print(1)
except Exception: print(0)')" "1"
chk "K8 ...and the summary survives intact rather than being stripped" \
    "$("$V" calendar events --json 2>/dev/null | python3 -c 'import json,sys
d=json.load(sys.stdin)
print(1 if any(chr(34)+"the mare"+chr(34) in (e.get("summary") or "") for e in d) else 0)')" "1"
chk "K9 --json carries the source, so a consumer can answer coverage" \
    "$("$V" calendar events --json 2>/dev/null | python3 -c 'import json,sys
d=json.load(sys.stdin); print(1 if d and d[0].get("source") else 0)')" "1"

echo "-- what it refuses to guess --"
{ printf 'BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:seq-1\r\nDTSTAMP:20260820T100000Z\r\n'
  printf 'SEQUENCE:not-a-number\r\nDTSTART:20260901T150000Z\r\nSUMMARY:seqmarker\r\n'
  printf 'END:VEVENT\r\nEND:VCALENDAR\r\n'; } > seq.ics
"$V" calendar shred seq.ics >seq.out 2>seq.err
chk "K10 a non-numeric SEQUENCE is announced, not silently taken as 0" \
    "$(grep -c 'not a number' seq.err)" "1"
chk "K10b CONTROL: ...and the component is still stored" \
    "$(q "SELECT count(*) FROM cal_event WHERE uid='seq-1'")" "1"


echo "PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
[ "$FAIL" = 0 ]
