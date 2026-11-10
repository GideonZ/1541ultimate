#!/bin/sh
# Each mutant is a small edit of a copy of printer_spool.h or iec_printer.cc; the tests have to fail on every one,
# with a failed check. A run that hangs (the alarm of the test: "FAIL hang") or dies without one does NOT count as caught.
set -u
cd "$(dirname "$0")"
CXX=${CXX:-g++}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
ONLY=${1:-.}
want() { echo "$1" | grep -q -E "$ONLY"; }
verdict() { # name  (output in $tmp/out, exit code in $1)
    rc=$2
    if grep -q '^FAIL hang' "$tmp/out"; then echo "$1: HUNG (the alarm fired, that is not a caught mutant)"; return 1
    elif grep -q '^FAIL ' "$tmp/out"; then echo "$1: caught ($(grep -c '^FAIL ' "$tmp/out") failed checks, first: $(grep -m1 '^FAIL ' "$tmp/out" | cut -d: -f2-))"
    elif [ "$rc" -eq 0 ]; then echo "$1: SURVIVED"; return 1
    else echo "$1: died without a failed check (exit $rc)"; return 1; fi
}
spool() { # name perl-expression
    want "$1" || return 0
    mkdir -p "$tmp/s"
    perl -0pe "$2" ../printer_spool.h > "$tmp/s/printer_spool.h"
    if cmp -s ../printer_spool.h "$tmp/s/printer_spool.h"; then echo "$1: MUTATION DID NOT APPLY"; return 1; fi
    if ! $CXX -std=c++11 -g -DPRINTER_SPOOL_HOST_TEST -Ihost_rtos -I"$tmp/s" spool_test.cpp -lpthread -o "$tmp/s/t" 2>"$tmp/err"; then
        echo "$1: does not compile"; cat "$tmp/err"; return 1; fi
    timeout 300 "$tmp/s/t" > "$tmp/out" 2>&1; verdict "$1" $?
}
printer() { # name perl-expression, on iec_printer.cc (built as the Makefile does)
    want "$1" || return 0
    mkdir -p "$tmp/p"
    cp ../iec_printer.h ../printer_spool.h "$tmp/p/"
    sed 's|(int) FS_ROOT "printer"|0|' ../iec_printer.cc | perl -0pe "$2" > "$tmp/p/iec_printer.cc"
    sed 's|(int) FS_ROOT "printer"|0|' ../iec_printer.cc > "$tmp/p/orig.cc"
    if cmp -s "$tmp/p/orig.cc" "$tmp/p/iec_printer.cc"; then echo "$1: MUTATION DID NOT APPLY"; return 1; fi
    if ! $CXX -std=c++14 -fpermissive -w -DPRINTER_SPOOL_HOST_TEST -DIEC_PRINTER_FLUSH_WAIT_MS=300 -Ihost_printer -Ihost_rtos -I"$tmp/p" -I. printer_cmd_test.cpp "$tmp/p/iec_printer.cc" -lpthread -o "$tmp/p/t" 2>"$tmp/err"; then
        echo "$1: does not compile"; cat "$tmp/err"; return 1; fi
    timeout 300 "$tmp/p/t" > "$tmp/out" 2>&1; verdict "$1" $?
}
bad=0

# --- the order of markers and events
spool marker-early        's/else if \(ctl_count && \(int32_t\)\(ctl\[ctl_first\]\.pos - t\) <= 0\)/else if (ctl_count)/' || bad=1
spool marker-late         's/\(ctl\[ctl_first\]\.pos - t\) <= 0/(ctl[ctl_first].pos - t) < 0/' || bad=1
# the head read after the end of the critical section that looked at the markers (the race of the review)
GAP='s/else if \(t != h\) \{/else if (gap_then_head(t)) {/; s/(    void wake\(void\))/    bool gap_then_head(uint32_t t) { PRINTER_SPOOL_EXIT(); PRINTER_SPOOL_ENTER(); return t != head; }\n\n$1/'
spool head-read-outside   "$GAP" || bad=1
spool head-read-outside-equality "$GAP"'; s/\(ctl\[ctl_first\]\.pos - t\) <= 0/(ctl[ctl_first].pos - t) == 0/' || bad=1
# --- the ring
spool wrap-mod            's/ev = ring\[t & mask\]/ev = ring[t % mask]/; s/ring\[head & mask\] = ev/ring[head % mask] = ev/' || bad=1
spool wrap-mask-short     's/migrate\(r, n - 1, r\)/migrate(r, n - 2, r)/' || bad=1
spool full-off-by-one-low 's/\(head - tail\) <= mask\)/(head - tail) < mask)/' || bad=1
spool full-off-by-one-high 's/\(head - tail\) <= mask\)/(head - tail) <= mask + 1)/' || bad=1
spool migrate-loses-events 's/to\[\(t \+ i\) & new_mask\] = ring\[\(t \+ i\) & mask\];/;/' || bad=1
spool try-push-ignores-migrating 's/if \(!migrating && \(uint32_t\)\(head - tail\) <= mask\)/if ((uint32_t)(head - tail) <= mask)/' || bad=1
spool next-ignores-migrating 's/if \(migrating\) \{                    \/\*/if (false) {                    \/*/' || bad=1
spool turn-on-ignores-release-in-progress 's/bool have = \(big != NULL && !migrating\);/bool have = (big != NULL);/' || bad=1
spool alloc-race-leak     's/if \(big\) \{\s+\/\* another task was faster \*\//if (0) { \/* another *\//' || bad=1
# --- the wake up
spool notify-missed-push  's/(            wake\(\);\n)(            return true;)/$2/' || bad=1
spool notify-missed-flush 's/(c->flush_gen = gen;\n        PRINTER_SPOOL_EXIT\(\);\n)        wake\(\);/$1/' || bad=1
spool notify-missed-abort 's/(abort_hold_valid = true;\n            \}\n        \}\n        PRINTER_SPOOL_EXIT\(\);\n)        wake\(\);/$1/' || bad=1
spool notify-missed-all   's/xTaskNotifyGive\(t\);/(void) t;/' || bad=1
# --- abort
spool abort-keeps-backlog 's/\n                tail = abort_to;/\n                ;/' || bad=1
spool abort-ignores-pending-flush 's/\(!abort_hold_valid \|\| !ctl_count \|\| \(int32_t\)\(ctl\[ctl_first\]\.pos - abort_hold\) > 0\)/true/' || bad=1
spool abort-release-size-guard-removed 's/big &&\n(\s+)\(uint32_t\)\(head - tail\) <= PRINTER_SPOOL_FALLBACK_EVENTS\) \{\n(\s+)migrating = true;/big) {\n$2migrating = true;/' || bad=1
spool release-size-guard-removed 's/if \(big && !migrating && \(uint32_t\)\(head - tail\) <= PRINTER_SPOOL_FALLBACK_EVENTS\)/if (big \&\& !migrating)/' || bad=1
# --- markers
# (settings-not-ordered, a new marker for settings behind a flush, is covered by flush_first now: same behaviour, so no mutant)
spool merge-moves-earlier 's/\n\s+if \(!\(abort_kinds && abort_hold_valid && n->pos == abort_hold\)\)\n\s+n->pos = head;//' || bad=1
spool settings-coalesced   's/same_settings\(n->settings, s\)/true/; s/if \(!for_settings && n->pos == head\)/if (n->pos == head)/' || bad=1
spool settings-merge-no-flag 's/\n\s+c->type_changed = true;[^\n]*//' || bad=1
spool duplicate-settings-not-dropped 's/same_settings\(n->settings, s\)/false/' || bad=1
spool settings-before-flush-when-full 's/\n\s+if \(c->flushes\)\n\s+c->flush_first = true;[^\n]*//' || bad=1
spool merge-moves-held-flush 's/if \(!\(abort_kinds && abort_hold_valid && n->pos == abort_hold\)\)\n(\s+)n->pos = head;/n->pos = head;/' || bad=1
spool post-waits-when-full 's/if \(ctl_count == PRINTER_SPOOL_CTRL_DEPTH\) \{\n\s+\/\*[^\n]*\*\/\n\s+if \(!\(abort[^\n]*\n\s+n->pos = head;\n\s+return n;\n\s+\}/if (ctl_count == PRINTER_SPOOL_CTRL_DEPTH) { PRINTER_SPOOL_EXIT(); for (;;) vTaskDelay(1); }/' || bad=1
# --- the flush wait
spool flush-not-posted    's/uint32_t gen = post_flush\(\);/uint32_t gen = flush_gen + 1;/' || bad=1
spool flush-wait-unbounded 's/if \(used >= total\)\n\s+return false;//' || bad=1
spool flush-ignores-generation 's/if \(\(int32_t\)\(done_gen - gen\) >= 0\)\n(\s+)return true;/if (true)\n$1return true;/' || bad=1
spool flush-stale-give    's/while \(xSemaphoreTake\(flush_sem, 0\) == pdTRUE\)\n\s+;//; s/xSemaphoreTake\(flush_sem, total - used\);/if (xSemaphoreTake(flush_sem, total - used) == pdTRUE) return true;/' || bad=1
spool flush-done-not-published 's/\n            done_gen = gen;//' || bad=1
# --- the printer's own command paths
printer menu-flush-not-posted   's/\n\s+spool\.flush_and_wait\(IEC_PRINTER_FLUSH_WAIT_MS\);//' || bad=1
printer menu-flush-unbounded    's/spool\.flush_and_wait\(IEC_PRINTER_FLUSH_WAIT_MS\)/spool.flush_and_wait(0x7FFFFFFF)/' || bad=1
printer turn-off-without-reset  's/spool\.abort\(PRINTER_SPOOL_ABORT_RESET \| PRINTER_SPOOL_ABORT_RELEASE\);   \/\* what is not printed yet is lost, the ring is given back \*\//spool.abort(PRINTER_SPOOL_ABORT_RELEASE);/' || bad=1
printer turn-off-without-release 's/spool\.abort\(PRINTER_SPOOL_ABORT_RESET \| PRINTER_SPOOL_ABORT_RELEASE\);   \/\* what/spool.abort(PRINTER_SPOOL_ABORT_RESET);   \/* what/' || bad=1
printer reset-without-reset     's/spool\.abort\(PRINTER_SPOOL_ABORT_RESET\);   \/\* what is not printed yet is lost \*\//spool.abort(0);/' || bad=1
printer abort-leaves-file-open  's/(buffer_pointer = 0;\n\n    if \(output_type != PRINTER_PNG_OUTPUT\)\n)        close_file\(\);/$1        ;/' || bad=1
printer type-changed-not-flushed 's/if \(ctl\.type_changed\)\n\s+p->flush\(\);//' || bad=1
printer task-does-not-publish-flush 's/p->spool\.flush_done\(ctl\.flush_gen\);//g' || bad=1
exit $bad
