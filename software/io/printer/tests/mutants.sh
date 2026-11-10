#!/bin/sh
# Each mutant is a small edit of a copy of printer_spool.h; the spool test has to fail on every one,
# with a failed check (a run that merely hangs or crashes does not count as caught).
set -u
cd "$(dirname "$0")"
CXX=${CXX:-g++}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
run() { # name perl-expression
    perl -0pe "$2" ../printer_spool.h > "$tmp/printer_spool.h"
    if cmp -s ../printer_spool.h "$tmp/printer_spool.h"; then echo "$1: MUTATION DID NOT APPLY"; return 1; fi
    if ! $CXX -std=c++11 -g -DPRINTER_SPOOL_HOST_TEST -Ihost_rtos -I"$tmp" spool_test.cpp -lpthread -o "$tmp/t" 2>"$tmp/err"; then
        echo "$1: does not compile"; cat "$tmp/err"; return 1; fi
    timeout 60 "$tmp/t" > "$tmp/out" 2>&1
    rc=$?
    if grep -q '^FAIL ' "$tmp/out"; then echo "$1: caught ($(grep -c '^FAIL ' "$tmp/out") failed checks, first: $(grep -m1 '^FAIL ' "$tmp/out" | cut -d: -f2-))"
    elif [ $rc -eq 0 ]; then echo "$1: SURVIVED"; return 1
    else echo "$1: died without a failed check (exit $rc)"; return 1; fi
}
bad=0
run marker-early        's/else if \(ctl_count && \(int32_t\)\(ctl\[ctl_first\]\.pos - t\) <= 0\)/else if (ctl_count)/' || bad=1
run marker-late         's/\(ctl\[ctl_first\]\.pos - t\) <= 0/(ctl[ctl_first].pos - t) < 0/' || bad=1
# the head read after the end of the critical section that looked at the markers (the race of the review)
GAP='s/else if \(t != h\) \{/else if (gap_then_head(t)) {/; s/(    void wake\(void\))/    bool gap_then_head(uint32_t t) { PRINTER_SPOOL_EXIT(); PRINTER_SPOOL_ENTER(); return t != head; }\n\n$1/'
run head-read-outside   "$GAP" || bad=1
run head-read-outside-equality "$GAP"'; s/\(ctl\[ctl_first\]\.pos - t\) <= 0/(ctl[ctl_first].pos - t) == 0/' || bad=1
run wrap-mod            's/ev = ring\[t & mask\]/ev = ring[t % mask]/; s/ring\[head & mask\] = ev/ring[head % mask] = ev/' || bad=1
run wrap-mask-short     's/move_to\(r, n - 1\)/move_to(r, n - 2)/' || bad=1
run full-off-by-one-low 's/\(head - tail\) <= mask\)/(head - tail) < mask)/' || bad=1
run full-off-by-one-high 's/\(head - tail\) <= mask\)/(head - tail) <= mask + 1)/' || bad=1
run notify-missed-push  's/(                wake\(\);\n)(                return true;)/$2/' || bad=1
run notify-missed-flush 's/(c->flushes\+\+;\n        PRINTER_SPOOL_EXIT\(\);\n)        wake\(\);/$1/' || bad=1
run notify-missed-abort 's/(abort_kinds \|= \(kinds \| 0x80\);\n        PRINTER_SPOOL_EXIT\(\);\n)        wake\(\);/$1/' || bad=1
run notify-missed-all   's/xTaskNotifyGive\(t\);/(void) t;/' || bad=1
run abort-keeps-backlog 's/\n                tail = abort_to;/\n                ;/' || bad=1
run move-loses-events   's/move_to\(r, n - 1\);\n        big = r;/ring = r; mask = n - 1;\n        big = r;/' || bad=1
run alloc-race-leak     's/if \(big\) \{                                      \/\* another/if (0) {  \/* another/' || bad=1
run settings-not-ordered 's/&& !\(for_settings && n->flushes\)//' || bad=1
run post-waits-when-full 's/if \(ctl_count == PRINTER_SPOOL_CTRL_DEPTH\)\n                return n;/if (ctl_count == PRINTER_SPOOL_CTRL_DEPTH) { for (;;) vTaskDelay(1); }/' || bad=1
exit $bad
