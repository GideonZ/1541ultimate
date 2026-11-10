#!/bin/sh
# Each mutant is a one line edit of a copy of printer_spool.h; the spool test has to fail on every one.
set -u
cd "$(dirname "$0")"
CXX=${CXX:-g++}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
run() { # name sed-expression
    sed "$2" ../printer_spool.h > "$tmp/printer_spool.h"
    if cmp -s ../printer_spool.h "$tmp/printer_spool.h"; then echo "$1: MUTATION DID NOT APPLY"; return 1; fi
    if ! $CXX -std=c++11 -g -Ihost_rtos -I"$tmp" spool_test.cpp -lpthread -o "$tmp/t" 2>"$tmp/err"; then
        echo "$1: does not compile"; cat "$tmp/err"; return 1; fi
    if timeout 60 "$tmp/t" > "$tmp/out" 2>&1; then echo "$1: SURVIVED"; return 1; fi
    echo "$1: killed ($(tail -1 "$tmp/out"))"
}
bad=0
run marker-early        's/if (ctl_count \&\& ctl\[ctl_first\].pos == t)/if (ctl_count)/' || bad=1
run marker-one-late     's/ctl\[ctl_first\].pos == t)/ctl[ctl_first].pos + 1 == t)/' || bad=1
run wrap-mod            's/ev = ring\[t \& mask\]/ev = ring[t % mask]/; s/ring\[head \& mask\] = ev/ring[head % mask] = ev/' || bad=1
run wrap-mask-short     's/mask = n - 1;/mask = n - 2;/' || bad=1
run full-off-by-one-low 's/(head - tail) > mask)/(head - tail) >= mask)/' || bad=1
run full-off-by-one-high 's/(head - tail) > mask)/(head - tail) > mask + 1)/' || bad=1
run notify-missed-push  's/^        wake();$/        \/\/ wake();/' || bad=1
run notify-missed-ctrl  's/^                wake();$/                \/\/ wake();/' || bad=1
run notify-missed-all   's/xTaskNotifyGive(t);/(void) t;/' || bad=1
exit $bad
