# Console reporting shared by run-e2e-tests and the shell e2e suites.
#
# The rules are written down in tests/e2e/README.md. This is the shell
# implementation of them; tests/e2e/lib/report.py is the Python one, and the two
# produce the same lines and the same JSONL records, so a run reads as one log
# rather than as one convention per suite.
#
# Source it, do not execute it:
#     source "$(dirname "$0")/../lib/report.sh"

C_BLUE='\033[1;34m'
C_GREEN='\033[1;32m'
C_RED='\033[1;31m'
C_YEL='\033[1;33m'
C_NC='\033[0m'

# Colour is on or off for the whole run, so a captured log looks like what was
# on screen. Deciding per stream would colour the harness and not the suites.
if [[ -n "${NO_COLOR:-}" ]]; then
    C_BLUE='' C_GREEN='' C_RED='' C_YEL='' C_NC=''
fi

# Detail lines line up under the label, not under the "[NN] " index.
REPORT_INDENT='     '
REPORT_RULE_WIDTH=60
REPORT_COUNT=0
REPORT_SUITE="${E2E_SUITE:-$(basename "${0%.*}")}"
REPORT_JSONL="${E2E_JSONL:-}"

_report_now() { printf '%s' "${EPOCHREALTIME/,/.}"; }

REPORT_SUITE_STARTED="$(_report_now)"
_report_check_started=""
_report_scenario=""
_report_scenario_started=""
_report_scenario_checks=0
_report_scenario_verdict="OK"

# Wall time with fewer decimals as the number grows: 0.020s, 1.002s, 23.5s, 264s.
# At a second the milliseconds separate a round trip from a redraw; at a minute
# they are noise, and printing them only makes the column harder to scan.
format_duration() {
    awk -v s="$1" 'BEGIN {
        if (s < 10)       printf "%.3fs", s;
        else if (s < 100) printf "%.1fs", s;
        else              printf "%.0fs", s;
    }'
}

_report_since() { awk -v a="$1" -v b="$(_report_now)" 'BEGIN { printf "%.4f", b - a }'; }

# Append one JSONL object when a path was asked for. Opened per record and in
# append mode: the suites are separate processes writing the same file, and a
# single short line under O_APPEND is not interleaved with another.
_report_json() {
    [[ -n "$REPORT_JSONL" ]] || return 0
    local out='' key value
    while [[ $# -ge 2 ]]; do
        key="$1"; value="$2"; shift 2
        # Numbers unquoted, everything else a JSON string.
        if [[ "$value" =~ ^-?[0-9]+(\.[0-9]+)?$ ]]; then
            out+="\"$key\":$value,"
        else
            value="${value//\\/\\\\}"; value="${value//\"/\\\"}"
            value="${value//$'\n'/\\n}"; value="${value//$'\t'/\\t}"
            out+="\"$key\":\"$value\","
        fi
    done
    printf '{%s"suite":"%s","time":%s}\n' "$out" "$REPORT_SUITE" "$(_report_now)" \
        >> "$REPORT_JSONL" 2>/dev/null || true
}

_report_worse() {
    # Worst wins: a scenario reports the worst verdict any of its checks produced.
    local -A rank=([FAIL]=0 [WARN]=1 [SKIP]=2 [OK]=3)
    [[ ${rank[$1]} -lt ${rank[$_report_scenario_verdict]} ]] && _report_scenario_verdict="$1"
    return 0
}

# Open a numbered check line, leaving the verdict for later.
check_start() {
    REPORT_COUNT=$((REPORT_COUNT + 1))
    REPORT_LAST_LABEL="$1"
    _report_check_started="$(_report_now)"
    printf '[%02d] %s ... ' "$REPORT_COUNT" "$1"
}

# Open an unnumbered line, for a step that is not a check of its own: the
# harness's precondition and teardown gates run around the suites rather than
# inside one, so numbering them would interleave two counters.
step_start() {
    REPORT_LAST_LABEL="$1"
    _report_check_started="$(_report_now)"
    printf '%s ... ' "$1"
}

_report_close() {
    local verdict=$1 extra=${2:-} colour=$3
    local seconds; seconds="$(_report_since "${_report_check_started:-$(_report_now)}")"
    local shown; shown="$(format_duration "$seconds")"
    if [[ -n "$extra" ]]; then
        echo -e "${colour}${verdict}${C_NC} ($extra, $shown)"
    else
        echo -e "${colour}${verdict}${C_NC} ($shown)"
    fi
    if [[ -n "$_report_scenario" ]]; then
        _report_scenario_checks=$((_report_scenario_checks + 1))
        _report_worse "$verdict"
    fi
    _report_json kind check index "$REPORT_COUNT" label "${REPORT_LAST_LABEL:-}" \
        verdict "$verdict" extra "$extra" seconds "$seconds" scenario "$_report_scenario"
}

check_ok()   { _report_close OK   "${1:-}" "$C_GREEN"; }
check_fail() { _report_close FAIL "${1:-}" "$C_RED"; }
check_warn() { _report_close WARN "${1:-}" "$C_YEL"; }
check_skip() { _report_close SKIP "${1:-}" "$C_YEL"; }

# A continuation line under the check that produced it.
detail() {
    echo -e "${REPORT_INDENT}$1"
}

_report_close_scenario() {
    [[ -n "$_report_scenario" ]] || return 0
    # A heading that grouped no checks is a heading, not a scenario, and a
    # verdict on nothing is noise.
    if [[ "$_report_scenario_checks" -eq 0 ]]; then _report_scenario=""; return 0; fi
    local seconds; seconds="$(_report_since "$_report_scenario_started")"
    local colour="$C_GREEN"
    [[ "$_report_scenario_verdict" == FAIL ]] && colour="$C_RED"
    [[ "$_report_scenario_verdict" == WARN || "$_report_scenario_verdict" == SKIP ]] && colour="$C_YEL"
    echo -e "${colour}--- ${_report_scenario_verdict}${C_NC} (${_report_scenario_checks} checks, $(format_duration "$seconds"))"
    _report_json kind scenario title "$_report_scenario" \
        verdict "$_report_scenario_verdict" checks "$_report_scenario_checks" seconds "$seconds"
    _report_scenario=""
}

# Start a scenario: a group of checks with a heading and its own verdict. The
# previous scenario is closed first, so its verdict and elapsed time land
# directly under its checks.
section() {
    _report_close_scenario
    echo -e "\n${C_BLUE}--- $1${C_NC}"
    _report_scenario="$1"
    _report_scenario_started="$(_report_now)"
    _report_scenario_checks=0
    _report_scenario_verdict="OK"
}

# A top-level heading: a rule above and below the title. Heavier than a scenario
# heading on purpose: this is where a reader looks to see a new suite start.
banner() {
    _report_close_scenario
    local rule; rule="$(printf "%.s=" $(seq 1 $REPORT_RULE_WIDTH))"
    echo -e "\n${C_BLUE}${rule}\n$1\n${rule}${C_NC}"
}

# A warning that belongs to no particular check.
warn() {
    echo -e "${REPORT_INDENT}${C_YEL}WARN${C_NC} $1"
    _report_json kind warning message "$1"
}

# NAME [EXTRA] [SECONDS]. Without SECONDS the time since this file was sourced is
# used, which is what a suite wants; the runner times each suite itself and
# reports no check count, because the checks were counted in the child process.
_report_suite_line() {
    local verdict=$1 colour=$2 name=$3 extra=${4:-} seconds=${5:-}
    _report_close_scenario
    if [[ -n "$seconds" ]]; then
        [[ -n "$extra" ]] && extra="$extra, "
    else
        seconds="$(_report_since "$REPORT_SUITE_STARTED")"
        [[ -n "$extra" ]] && extra="$extra, " || extra="$REPORT_COUNT checks, "
    fi
    echo -e "${name}: ${colour}${verdict}${C_NC} (${extra}$(format_duration "$seconds"))"
    _report_json kind suite name "$name" verdict "$verdict" note "${extra%, }" \
        checks "$REPORT_COUNT" seconds "$seconds"
    return 0
}


suite_ok()   { _report_suite_line OK   "$C_GREEN" "$@"; }
suite_fail() { _report_suite_line FAIL "$C_RED"   "$@"; }
suite_skip() { _report_suite_line SKIP "$C_YEL"   "$@"; }
suite_warn() { _report_suite_line WARN "$C_YEL"   "$@"; }

check_count() {
    echo "$REPORT_COUNT"
}
