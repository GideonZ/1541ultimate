#!/bin/bash
# Validate the boot-cart display-name trimming used by PRG runner REST endpoints.

# --- Defaults (Environment Variables) ---
U64_HOST="${U64_HOST:-u64}"
U64_PASS="${U64_PASS:-}"

# --- Configurable Parameters (with defaults) ---
ASSERTIONS_ENABLED=true
CFG_AUTO_CLEANUP="Enabled"
CFG_USE_CACHE="Enabled"
REMOTE_PUT_FILE="/Temp/rest-prg-path-trim-target-example.prg"
MULTIPART_FILENAME="rest-prg-path-trim-upload-example.prg"

# --- Internal State ---
INITIAL_AUTO_CLEANUP=""
INITIAL_USE_CACHE=""
CONFIG_RESTORED=false
UPLOAD_ROOT="/Temp/cache/upload"
LOCAL_PRG="/tmp/prg-load-path-trim-test.prg"

# --- Colors ---
C_BLUE='\033[1;34m'
C_GREEN='\033[1;32m'
C_RED='\033[1;31m'
C_YEL='\033[1;33m'
C_NC='\033[0m'

show_help() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Validate the PRG runner REST endpoints that populate the boot-cart load name.

Covered endpoints:
  - PUT  /v1/runners:load_prg?file=<path>
  - PUT  /v1/runners:run_prg?file=<path>
  - POST /v1/runners:load_prg
  - POST /v1/runners:run_prg

The script reads the boot-cart name bytes at \$0174/\$0175 via
GET /v1/machine:readmem and verifies that the firmware trims overlong
path-based names to the last 13 characters with a leading '...'.

Options:
-h, --help                Show this help message and exit.
-H, --host <host>         U64 host name or IP address (default: U64_HOST or u64).
-p, --password <pass>     U64 REST and FTP password (default: U64_PASS).
-n, --no-assertions       Keep running after a failed check and print warnings.
--cleanup <val>           Temp Auto Cleanup setting: Enabled or Disabled.
--subfolder <val>         Temp Subfolders setting: Enabled or Disabled.
--remote-file <path>      On-device PRG path used for PUT tests.
--upload-name <name>      Multipart filename used for POST tests.

Examples:
$(basename "$0") -H u64
$(basename "$0") -H 192.168.0.64 -p secret --subfolder Disabled

EOF
}

require_toggle_value() {
    local flag=$1
    local value=$2
    case "$value" in
        Enabled|Disabled)
            return 0
            ;;
        *)
            echo "Invalid value for $flag: $value"
            echo "Expected: Enabled or Disabled"
            exit 1
            ;;
    esac
}

refresh_upload_root() {
    if [[ "$CFG_USE_CACHE" == "Enabled" ]]; then
        UPLOAD_ROOT="/Temp/cache/upload"
    else
        UPLOAD_ROOT="/Temp"
    fi
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -H|--host)
            U64_HOST="$2"
            shift 2
            ;;
        -p|--password)
            U64_PASS="$2"
            shift 2
            ;;
        -n|--no-assertions)
            ASSERTIONS_ENABLED=false
            shift
            ;;
        --cleanup)
            CFG_AUTO_CLEANUP="$2"
            shift 2
            ;;
        --subfolder)
            CFG_USE_CACHE="$2"
            shift 2
            ;;
        --remote-file)
            REMOTE_PUT_FILE="$2"
            shift 2
            ;;
        --upload-name)
            MULTIPART_FILENAME="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

require_toggle_value "--cleanup" "$CFG_AUTO_CLEANUP"
require_toggle_value "--subfolder" "$CFG_USE_CACHE"
REST_HEADER="X-Password: $U64_PASS"
FTP_CRED="-u user:${U64_PASS:-password}"
refresh_upload_root

log_stage() {
    echo -e "\n${C_BLUE}STAGE: $1${C_NC}\n$(printf '%.s-' {1..60})"
}

handle_failure() {
    local msg=$1
    echo -e "\n${C_RED}FAILURE: $msg${C_NC}"
    if [[ "$ASSERTIONS_ENABLED" == true ]]; then
        exit 1
    fi
    echo -e "${C_YEL}[ASSERTIONS DISABLED] Continuing...${C_NC}"
}

get_config_current() {
    local key=$1
    curl -s -H "$REST_HEADER" "http://$U64_HOST/v1/configs/User%20Interface%20Settings/$key" | \
        sed -n 's/.*"current"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
}

apply_config_setting() {
    local key=$1
    local value=$2
    local mode=${3:-strict}
    local code

    echo -ne "  Setting $key to $value... "
    code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" \
        "http://$U64_HOST/v1/configs/User%20Interface%20Settings/$key?value=$value")

    if [[ "$code" == "200" ]]; then
        echo -e "${C_GREEN}OK${C_NC}"
        return 0
    fi

    if [[ "$mode" == "strict" && "$ASSERTIONS_ENABLED" == true ]]; then
        echo -e "${C_RED}FAIL ($code)${C_NC}"
        exit 1
    fi

    echo -e "${C_YEL}WARNING ($code)${C_NC}"
    return 1
}

capture_initial_config() {
    log_stage "1. Capture Current Configuration"
    INITIAL_AUTO_CLEANUP=$(get_config_current "Temp%20Auto%20Cleanup")
    INITIAL_USE_CACHE=$(get_config_current "Temp%20Subfolders")
    require_toggle_value "captured Temp Auto Cleanup" "$INITIAL_AUTO_CLEANUP"
    require_toggle_value "captured Temp Subfolders" "$INITIAL_USE_CACHE"
    echo -e "  Temp Auto Cleanup: ${C_GREEN}${INITIAL_AUTO_CLEANUP}${C_NC}"
    echo -e "  Temp Subfolders:   ${C_GREEN}${INITIAL_USE_CACHE}${C_NC}"
}

restore_initial_config() {
    [[ "$CONFIG_RESTORED" == true ]] && return
    CONFIG_RESTORED=true
    if [[ -z "$INITIAL_AUTO_CLEANUP" || -z "$INITIAL_USE_CACHE" ]]; then
        return
    fi
    echo -e "\n${C_BLUE}RESTORE: User Interface Settings${C_NC}\n$(printf '%.s-' {1..60})"
    apply_config_setting "Temp%20Auto%20Cleanup" "$INITIAL_AUTO_CLEANUP" restore
    apply_config_setting "Temp%20Subfolders" "$INITIAL_USE_CACHE" restore
}

ftp_list_names() {
    local directory=$1
    curl -s --ftp-pasv --list-only $FTP_CRED "ftp://$U64_HOST$directory/" 2>/dev/null || true
}

ftp_delete_name() {
    local directory=$1
    local name=$2
    curl -s --ftp-pasv $FTP_CRED "ftp://$U64_HOST$directory/" -X "DELE $name" > /dev/null 2>&1 || true
}

cleanup_matching_names() {
    local directory=$1
    local prefix=$2
    local stem="$prefix"
    local ext=""
    local names

    if [[ "$prefix" == *.* ]]; then
        stem="${prefix%.*}"
        ext=".${prefix##*.}"
    fi

    names=$(ftp_list_names "$directory")
    while IFS= read -r name; do
        [[ -z "$name" ]] && continue
        if [[ "$name" == "$prefix" ]]; then
            ftp_delete_name "$directory" "$name"
        elif [[ -n "$ext" && "$name" == "$stem"_*"$ext" ]]; then
            ftp_delete_name "$directory" "$name"
        elif [[ -z "$ext" && "$name" == "$stem"_* ]]; then
            ftp_delete_name "$directory" "$name"
        fi
    done <<< "$names"
}

cleanup_remote_artifacts() {
    cleanup_matching_names "/Temp" "$(basename "$REMOTE_PUT_FILE")"
    cleanup_matching_names "$UPLOAD_ROOT" "$MULTIPART_FILENAME"
}

cleanup_and_restore() {
    local exit_code=$?
    cleanup_remote_artifacts
    rm -f "$LOCAL_PRG"
    restore_initial_config
    return $exit_code
}

create_test_prg() {
    printf '%b' '\x01\x08\x07\x08\x0a\x00\x80\x00\x00\x00' > "$LOCAL_PRG"
}

upload_put_fixture() {
    echo -ne "  Uploading PUT fixture $(basename "$REMOTE_PUT_FILE")... "
    cleanup_matching_names "/Temp" "$(basename "$REMOTE_PUT_FILE")"
    local code
    code=$(curl -s --ftp-pasv $FTP_CRED -T "$LOCAL_PRG" -o /dev/null -w "%{http_code}" "ftp://$U64_HOST$REMOTE_PUT_FILE")
    if [[ "$code" == "226" || "$code" == "200" ]]; then
        echo -e "${C_GREEN}OK${C_NC}"
        return
    fi
    handle_failure "Could not upload PUT fixture (FTP $code)"
}

machine_reset() {
    local code
    code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" "http://$U64_HOST/v1/machine:reset")
    if [[ "$code" != "200" ]]; then
        handle_failure "Machine reset failed (HTTP $code)"
    fi
    sleep 1
}

read_bootcrt_name() {
    local dump_file
    dump_file=$(mktemp)
    local code
    code=$(curl -s -H "$REST_HEADER" -o "$dump_file" -w "%{http_code}" \
        "http://$U64_HOST/v1/machine:readmem?address=0174&length=17")
    if [[ "$code" != "200" ]]; then
        rm -f "$dump_file"
        handle_failure "Reading boot-cart name bytes failed (HTTP $code)"
        printf ''
        return
    fi

    local namelen
    namelen=$(od -An -tu1 -N1 "$dump_file" | tr -d ' ')
    local name=""
    if [[ -n "$namelen" && "$namelen" -gt 0 ]]; then
        name=$(dd if="$dump_file" bs=1 skip=1 count="$namelen" 2>/dev/null)
    fi
    rm -f "$dump_file"
    printf '%s' "$name"
}

display_from_path() {
    local path=$1
    local display="$path"
    if [[ ${#path} -gt 16 && ( "$path" == *"/"* || "$path" == *"\\"* ) ]]; then
        display="...${path: -13}"
    fi
    display="${display^^}"
    if [[ "${display,,}" == *.prg ]]; then
        display="${display:0:${#display}-4}"
    fi
    printf '%s' "$display"
}

wait_for_expected_displays() {
    local description=$1
    shift
    local expected=("$@")
    local deadline=$((SECONDS + 8))
    local actual

    while (( SECONDS < deadline )); do
        actual=$(read_bootcrt_name)
        for item in "${expected[@]}"; do
            if [[ "$actual" == "$item" ]]; then
                echo -e "  ${C_GREEN}${description}: $actual${C_NC}"
                return 0
            fi
        done
        sleep 1
    done

    handle_failure "$description\n  Expected one of: ${expected[*]}\n  Actual: ${actual:-<empty>}"
    return 1
}

wait_for_new_upload_paths() {
    local before_names=$1
    local deadline=$((SECONDS + 8))
    local after_names
    local new_names

    while (( SECONDS < deadline )); do
        after_names=$(ftp_list_names "$UPLOAD_ROOT")
        new_names=$(comm -13 \
            <(printf '%s\n' "$before_names" | sed '/^$/d' | sort) \
            <(printf '%s\n' "$after_names" | sed '/^$/d' | sort))
        if [[ -n "$new_names" ]]; then
            while IFS= read -r name; do
                [[ -n "$name" ]] && printf '%s/%s\n' "$UPLOAD_ROOT" "$name"
            done <<< "$new_names"
            return 0
        fi
        sleep 1
    done

    handle_failure "Managed upload file was not created in $UPLOAD_ROOT"
    return 1
}

run_put_case() {
    local endpoint=$1
    local label=$2
    local expected
    local code

    machine_reset
    expected=$(display_from_path "$REMOTE_PUT_FILE")
    echo -ne "  Exercising $endpoint... "
    code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -G -H "$REST_HEADER" \
        --data-urlencode "file=$REMOTE_PUT_FILE" "http://$U64_HOST$endpoint")
    if [[ "$code" != "200" ]]; then
        echo -e "${C_RED}FAIL ($code)${C_NC}"
        handle_failure "$label failed (HTTP $code)"
        return
    fi
    echo -e "${C_GREEN}OK${C_NC}"
    wait_for_expected_displays "$label boot-cart name" "$expected"
}

run_post_case() {
    local endpoint=$1
    local label=$2
    local before_names
    local code
    local new_paths
    local expected=()
    local path

    cleanup_matching_names "$UPLOAD_ROOT" "$MULTIPART_FILENAME"
    machine_reset
    before_names=$(ftp_list_names "$UPLOAD_ROOT")

    echo -ne "  Exercising $endpoint... "
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST -H "$REST_HEADER" \
        -F "file=@$LOCAL_PRG;filename=$MULTIPART_FILENAME" "http://$U64_HOST$endpoint")
    if [[ "$code" != "200" ]]; then
        echo -e "${C_RED}FAIL ($code)${C_NC}"
        handle_failure "$label failed (HTTP $code)"
        return
    fi
    echo -e "${C_GREEN}OK${C_NC}"

    new_paths=$(wait_for_new_upload_paths "$before_names") || return
    while IFS= read -r path; do
        [[ -n "$path" ]] && expected+=("$(display_from_path "$path")")
    done <<< "$new_paths"

    wait_for_expected_displays "$label boot-cart name" "${expected[@]}"
}

run_cases() {
    log_stage "2. Configure Temp Settings"
    apply_config_setting "Temp%20Auto%20Cleanup" "$CFG_AUTO_CLEANUP" strict
    apply_config_setting "Temp%20Subfolders" "$CFG_USE_CACHE" strict
    refresh_upload_root

    log_stage "3. Prepare PRG Fixture"
    create_test_prg
    upload_put_fixture

    log_stage "4. Validate PUT Endpoints"
    run_put_case "/v1/runners:load_prg" "PUT load_prg"
    run_put_case "/v1/runners:run_prg" "PUT run_prg"

    log_stage "5. Validate POST Endpoints"
    run_post_case "/v1/runners:load_prg" "POST load_prg"
    run_post_case "/v1/runners:run_prg" "POST run_prg"
}

clear
echo -e "${C_YEL}Ultimate 64 PRG Load Path Trim Validator${C_NC}"
echo -e "Target Host: ${U64_HOST}"
echo -e "PUT File Path: ${REMOTE_PUT_FILE}"
echo -e "POST Upload Name: ${MULTIPART_FILENAME}"
[[ "$ASSERTIONS_ENABLED" == false ]] && echo -e "${C_RED}!! ASSERTIONS DISABLED !!${C_NC}"

trap cleanup_and_restore EXIT

capture_initial_config
run_cases

echo -e "\n${C_GREEN}SUCCESS: PRG runner REST path trimming validated.${C_NC}"