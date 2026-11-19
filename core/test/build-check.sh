#!/bin/sh
# build-check.sh -- what a build of core contains, checked from its objects:
#   C-1    the layer calls no clock
#   V-4a   core imports nothing that could call a judge: no socket, process or loader
#   ENV-3  a build without vectors links no vector code and no embedding runtime
# Run by store-check.sh, which sets ROOT, OUT and SQLITE; prints PASS/FAIL lines
# for the gate and exits nonzero on any FAIL.
set -u
CORE="viki_store viki_search viki_delta viki_vector viki_regexp sha256"
CC="cc -std=c99 -O1 -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L -I$ROOT/core/include -I$ROOT/core/src"
SQLINC=$(for w in $SQLITE; do case "$w" in -I*) printf '%s ' "$w";; esac; done)   # for -c, the includes alone
mkdir -p "$OUT/obj"
fail=0
say(){ echo "$1"; case "$1" in FAIL*) fail=1;; esac; }

# C-1. Core's own objects, without SQLite, import no clock function, and its SQL
# asks SQLite for no time ('now' and CURRENT_* are SQLite's clock).
clocks='^(time|clock|clock_gettime|gettimeofday|ftime|timespec_get|times|localtime|localtime_r|gmtime|gmtime_r|mktime|timegm|difftime|strftime)$'
bad=""
for f in $CORE; do
  $CC $SQLINC -c "$ROOT/core/src/$f.c" -o "$OUT/obj/$f.o" || { bad="$bad $f(build)"; continue; }
  for s in $(nm -u "$OUT/obj/$f.o" | awk '{print $NF}' | grep -E "$clocks"); do bad="$bad $f:$s"; done
done
sql=$(cd "$ROOT/core/src" && grep -lE "'now'|CURRENT_(TIME|DATE|TIMESTAMP)" $(for f in $CORE; do echo "$f.c"; done))
[ -n "$sql" ] && bad="$bad sql:$sql"
if [ -z "$bad" ]; then say "PASS C-1 core imports no clock function and asks SQLite for no time"
else say "FAIL C-1 core reads a clock:$bad"; fi

# V-4a. Judging is a connector's, behind its own API: core has no way to reach
# one. Its objects import no socket, process or loader function.
calls='^(socket|connect|bind|listen|accept|getaddrinfo|gethostbyname|send|sendto|recv|recvfrom|fork|vfork|execl|execlp|execle|execv|execve|execvp|execvpe|posix_spawn|posix_spawnp|system|popen|dlopen|dlsym)$'
bad=""
for f in $CORE; do
  [ -f "$OUT/obj/$f.o" ] || { bad="$bad $f(build)"; continue; }
  for s in $(nm -u "$OUT/obj/$f.o" | awk '{print $NF}' | grep -E "$calls"); do bad="$bad $f:$s"; done
done
if [ -z "$bad" ]; then say "PASS V-4a core imports no network, process or loader function, so it cannot call a judge"
else say "FAIL V-4a core can reach outside itself:$bad"; fi

# ENV-3. Without viki_vector.c and with VK_OMIT_VECTOR, core links into a static
# program that stores and searches; nothing in it is vector code or a model runtime.
nov="$OUT/novector"
if $CC -DVK_OMIT_VECTOR "$ROOT/core/src/viki_store.c" "$ROOT/core/src/viki_search.c" "$ROOT/core/src/viki_delta.c" \
     "$ROOT/core/src/viki_regexp.c" "$ROOT/core/src/sha256.c" "$ROOT/core/test/novector.c" $SQLITE -o "$nov"; then
  "$nov" || fail=1
  vec=$(nm "$nov" | awk '{print $NF}' | grep -E '^(vkVector|vk_cosine_func|vk_vector_put|vk_vector_drop)$|[Oo]nnx|^Ort' | tr '\n' ' ')
  if [ -z "$vec" ]; then say "PASS ENV-3 the build without vectors holds no vector code and no embedding runtime"
  else say "FAIL ENV-3 the build without vectors holds: $vec"; fi
  # Nor does the whole core: it embeds nothing, so no build of it carries a model runtime.
  ort=$(for f in $CORE; do nm "$OUT/obj/$f.o"; done | awk '{print $NF}' | grep -E '[Oo]nnx|^Ort' | tr '\n' ' ')
  if [ -z "$ort" ]; then say "PASS ENV-3 no core object references an embedding runtime"
  else say "FAIL ENV-3 core references an embedding runtime: $ort"; fi
else
  say "FAIL ENV-3 core does not link without viki_vector.c"
fi
exit "$fail"
