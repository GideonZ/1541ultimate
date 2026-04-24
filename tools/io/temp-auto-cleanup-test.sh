#!/bin/bash
# Ultimate 64 Temp Auto Cleanup E2E Validator

# --- Defaults (Environment Variables) ---
U64_HOST="${U64_HOST:-u64}"
U64_PASS="${U64_PASS:-}"

# --- Internal Configuration ---
MANAGED_SIZE=524288 # 512 KiB
MOUNT_SOURCE_8="/Temp/mount-drive-8.d64"
MOUNT_SOURCE_9="/Temp/mount-drive-9.d64"
MOUNT_LOCAL_8="/tmp/mount-drive-8.d64"
MOUNT_LOCAL_9="/tmp/mount-drive-9.d64"

# Configurable Parameters (with defaults)
ASSERTIONS_ENABLED=true
MANAGED_LIMIT=10
SEED_COUNT=10
TEST_COUNT=12
CFG_AUTO_CLEANUP="Enabled"
CFG_USE_CACHE="Enabled"
UPLOAD_DIR="/Temp/cache/upload"
UPLOAD_CACHE_URL=""

# State Variables
MOUNTED_PATH_8=""
MOUNTED_PATH_9=""
MOUNTED_BASE_8=""
MOUNTED_BASE_9=""

# Colors
C_BLUE='\033[1;34m'
C_GREEN='\033[1;32m'
C_RED='\033[1;31m'
C_YEL='\033[1;33m'
C_NC='\033[0m'

# --- Help Function ---

show_help() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Ultimate 64 Temp Auto Cleanup E2E Validator.

Options:
  -h, --help                Show this help message and exit.
  -H, --host <host>         IP or hostname of the U64 (Overrides U64_HOST).
  -p, --password <pass>     U64 REST/FTP password (Overrides U64_PASS).
  -n, --no-assertions       Disable exit on failure (continue with warnings).
  -l, --limit <num>         Set the Managed Temp File Limit (default: 10).
  --seed-count <num>        Number of baseline files to upload (default: 10).
  --test-count <num>        Number of test files to upload (default: 12).
    --cleanup <val>           Set Temp Auto Cleanup to 'Enabled' or 'Disabled' (default: Enabled).
    --subfolder <val>         Set Temp Subfolders to 'Enabled' or 'Disabled' (default: Enabled).

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

refresh_managed_paths() {
    if [[ "$CFG_USE_CACHE" == "Enabled" ]]; then
        UPLOAD_DIR="/Temp/cache/upload"
    else
        UPLOAD_DIR="/Temp"
    fi
    UPLOAD_CACHE_URL="ftp://$U64_HOST$UPLOAD_DIR"
}

# --- Argument Parsing ---

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
    -l|--limit)
      MANAGED_LIMIT="$2"
      shift 2
      ;;
    --seed-count)
      SEED_COUNT="$2"
      shift 2
      ;;
    --test-count)
      TEST_COUNT="$2"
      shift 2
      ;;
    --cleanup)
      CFG_AUTO_CLEANUP="$2"
      shift 2
      ;;
    --subfolder)
      CFG_USE_CACHE="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

# --- Finalize Connection Config (Post-Parsing) ---
require_toggle_value "--cleanup" "$CFG_AUTO_CLEANUP"
require_toggle_value "--subfolder" "$CFG_USE_CACHE"
REST_HEADER="X-Password: $U64_PASS"
FTP_CRED="-u user:${U64_PASS:-password}"
FTP_URL="ftp://$U64_HOST/Temp"
refresh_managed_paths

# --- Utility Functions ---

log_stage() { echo -e "\n${C_BLUE}STAGE: $1${C_NC}\n$(printf '%.s-' {1..60})"; }

handle_failure() {
    local msg=$1
    echo -e "\n${C_RED}FAILURE: $msg${C_NC}"
    if [ "$ASSERTIONS_ENABLED" = true ]; then
        exit 1
    else
        echo -e "${C_YEL}[ASSERTIONS DISABLED] Continuing...${C_NC}"
        return 0
    fi
}

extract_json_field() {
    local json=$1
    local key=$2
    sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" <<< "$json"
}

verify_managed_temp_path() {
    local mounted_path=$1
    local context=$2
    case "$mounted_path" in
        "$UPLOAD_DIR"/*) echo -e " ${C_GREEN}[VERIFIED]${C_NC}" ;;
        *) handle_failure "$context\n  Unexpected path: $mounted_path" ;;
    esac
}

list_cache_files() {
    curl -s --ftp-pasv --list-only $FTP_CRED "$UPLOAD_CACHE_URL/"
}

count_persistent_mounts_in_cache() {
    local names
    names=$(list_cache_files)
    local count=0
    [[ -n "$MOUNTED_BASE_8" ]] && grep -Fxq "$MOUNTED_BASE_8" <<< "$names" && ((count++))
    [[ -n "$MOUNTED_BASE_9" ]] && grep -Fxq "$MOUNTED_BASE_9" <<< "$names" && ((count++))
    echo "$count"
}

verify_remote_file_exists() {
    local code=$(curl -s --ftp-pasv $FTP_CRED -o /dev/null -w "%{http_code}" "$1")
    if [[ "$code" != "226" && "$code" != "200" ]]; then
        handle_failure "$2\n  Missing path: $1"
    else
        echo -e " ${C_GREEN}[VERIFIED]${C_NC}"
    fi
}

verify_remote_file_missing() {
    local code=$(curl -s --ftp-pasv $FTP_CRED -o /dev/null -w "%{http_code}" "$1")
    if [[ "$code" == "226" || "$code" == "200" ]]; then
        handle_failure "$2\n  Unexpected path: $1"
    else
        echo -e " ${C_GREEN}[VERIFIED]${C_NC}"
    fi
}

verify_file_size() {
    local remote_path=$1
    local expected_size=$2
    local remote_size=$(curl -s --ftp-pasv $FTP_CRED "$remote_path" -I | grep -i "Content-Length" | awk '{print $2}' | tr -d '\r')
    if [[ -z "$remote_size" || "$remote_size" -eq 0 ]]; then
        remote_size=$(curl -s --ftp-pasv $FTP_CRED "$(dirname "$remote_path")/" | grep "$(basename "$remote_path")" | awk '{print $5}')
    fi
    if [[ "$remote_size" != "$expected_size" ]]; then
        handle_failure "$3\n  Path: $remote_path\n  Expected: $expected_size\n  Actual: ${remote_size:-0}"
    else
        echo -e " ${C_GREEN}[VERIFIED]${C_NC}"
    fi
}

get_stats() {
    local r_ls=$(curl -s --ftp-pasv $FTP_CRED "$FTP_URL/")
    local c_ls=$(curl -s --ftp-pasv $FTP_CRED "$UPLOAD_CACHE_URL/")
    local r_sz=$(echo "$r_ls" | awk '/^-/ {s+=$5} END {print s+0}')
    local c_cnt=$(echo "$c_ls" | awk '/^-/ {c++} END {print c+0}')
    local c_sz=$(echo "$c_ls" | awk '/^-/ {s+=$5} END {print s+0}')
    local persistent_cnt=$(count_persistent_mounts_in_cache)
    if [[ "$CFG_USE_CACHE" == "Disabled" ]]; then
        c_cnt=$((c_cnt - SEED_COUNT))
        c_sz=$((c_sz - (SEED_COUNT * 768000)))
    fi
    local managed_cnt=$((c_cnt - persistent_cnt))
    printf "%d|%d|%d|%d|%d" "$r_sz" "$c_cnt" "$c_sz" "$persistent_cnt" "$managed_cnt"
}

verify_user_files_untouched() {
    local failures=0
    for i in $(seq 1 "$SEED_COUNT"); do
        local path="$FTP_URL/base_$i.bin"
        local code=$(curl -s --ftp-pasv $FTP_CRED -o /dev/null -w "%{http_code}" "$path")
        if [[ "$code" != "226" && "$code" != "200" ]]; then
            echo -e "  ${C_RED}Missing user file:${C_NC} base_$i.bin"
            failures=1
        fi
    done
    [[ "$failures" -eq 0 ]] && echo -e "  ${C_GREEN}User files intact${C_NC}"
    [[ "$failures" -eq 0 ]]
}

upload_rest_managed_temp() {
    local label=$1
    local before_cache=$(list_cache_files | sort)
    head -c $MANAGED_SIZE /dev/urandom > "/tmp/${label}.bin"
    echo -ne "  [REST] Uploading ${label}.bin..."
    curl -s -o /dev/null -X POST -H "$REST_HEADER" --data-binary "@/tmp/${label}.bin" "http://$U64_HOST/v1/runners:run_prg"
    sleep 1.5
    local after_cache=$(list_cache_files | sort)
    local latest=$(comm -13 <(printf '%s\n' "$before_cache") <(printf '%s\n' "$after_cache") | head -n 1)
    if [[ -z "$latest" ]]; then
        handle_failure "Not in cache: ${label}.bin"; return
    fi
    verify_file_size "$UPLOAD_CACHE_URL/$latest" "$MANAGED_SIZE" "Managed file ${label}"
}

upload_until_removed() {
    for attempt in $(seq 1 $((MANAGED_LIMIT + 2))); do
        upload_rest_managed_temp "${2}_${attempt}"
        [[ -n "$4" ]] && verify_remote_file_exists "ftp://$U64_HOST$4" "$5"
        local code=$(curl -s --ftp-pasv $FTP_CRED -o /dev/null -w "%{http_code}" "ftp://$U64_HOST$1")
        if [[ "$code" != "226" && "$code" != "200" ]]; then
            echo -e "    ${C_GREEN}Removed after ${attempt} uploads.${C_NC}"; return 0
        fi
    done
    handle_failure "$3\n  Path remained: ftp://$U64_HOST$1"
}

# --- Functional Stages ---

run_config() {
    log_stage "1. Configuration Setup"
    declare -A settings=( ["Temp%20Auto%20Cleanup"]="$CFG_AUTO_CLEANUP" ["Temp%20Subfolders"]="$CFG_USE_CACHE" )
    for key in "${!settings[@]}"; do
        echo -ne "  Setting $key to ${settings[$key]}... "
        local code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" \
            "http://$U64_HOST/v1/configs/User%20Interface%20Settings/$key?value=${settings[$key]}")

        if [[ "$code" == "200" ]]; then
            echo -e "${C_GREEN}OK${C_NC}"
        else
            if [ "$ASSERTIONS_ENABLED" = true ]; then
                echo -e "${C_RED}FAIL ($code)${C_NC}"
                echo -e "  Error: Category 'User Interface Settings' not found. Firmware may be too old."
                exit 1
            else
                echo -e "${C_YEL}WARNING ($code)${C_NC}"
                echo -e "  [ASSERTIONS DISABLED] Skipping config '$key' due to category absence."
            fi
        fi
    done
}

run_purge() {
    log_stage "2. Purging /Temp"
    for drive in a b; do curl -s -o /dev/null -X PUT -H "$REST_HEADER" "http://$U64_HOST/v1/drives/$drive:remove"; done
    local files=$(curl -s --ftp-pasv $FTP_CRED "$FTP_URL/" | awk '/^-/ {print $9}')
    for f in $files; do [[ "$f" != "cache" && -n "$f" ]] && curl -s --ftp-pasv $FTP_CRED "$FTP_URL/" -X "DELE $f" > /dev/null; done
    local managed_urls=("ftp://$U64_HOST/Temp/cache/upload" "ftp://$U64_HOST/Temp/upload")
    for managed_url in "${managed_urls[@]}"; do
        local managed_files=$(curl -s --ftp-pasv $FTP_CRED "$managed_url/" | awk '/^-/ {print $9}')
        for f in $managed_files; do [[ -n "$f" ]] && curl -s --ftp-pasv $FTP_CRED "$managed_url/" -X "DELE $f" > /dev/null; done
    done
    echo -e "  ${C_GREEN}Purge Complete${C_NC}"
}

run_mount_setup() {
    log_stage "3. Mounting D64 Images"
    for spec in "$MOUNT_SOURCE_8|Drive8|$MOUNT_LOCAL_8|a|MOUNTED_PATH_8|MOUNTED_BASE_8" "$MOUNT_SOURCE_9|Drive9|$MOUNT_LOCAL_9|b|MOUNTED_PATH_9|MOUNTED_BASE_9"; do
        IFS='|' read remote_path diskname local_path drive path_var base_var <<< "$spec"

        echo -ne "  Creating $remote_path... "
        local create_code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" \
            "http://$U64_HOST/v1/files${remote_path}:create_d64?diskname=$diskname")
        [[ "$create_code" == "200" ]] && echo -e "${C_GREEN}OK${C_NC}" || handle_failure "D64 creation failed ($create_code)"

        echo -ne "  Downloading $(basename "$remote_path") for upload mount... "
        curl -s --ftp-pasv $FTP_CRED -o "$local_path" "ftp://$U64_HOST${remote_path}"
        verify_file_size "ftp://$U64_HOST${remote_path}" 174848 "Source image $(basename "$remote_path")"

        echo -ne "  Removing staging source $(basename "$remote_path")... "
        curl -s --ftp-pasv $FTP_CRED "$FTP_URL/" -X "DELE $(basename "$remote_path")" > /dev/null
        echo -e "${C_GREEN}OK${C_NC}"

        echo -ne "  Mounting drive ${drive^^}... "
        local response=$(curl -s -X POST -H "$REST_HEADER" --data-binary "@$local_path" \
            "http://$U64_HOST/v1/drives/$drive:mount?type=d64&mode=readwrite")
        local m_path=$(extract_json_field "$response" "file")
        if [[ -z "$m_path" ]]; then
            handle_failure "Mount failed: $response"
        else
            printf -v "$path_var" '%s' "$m_path"
            printf -v "$base_var" '%s' "$(basename "$m_path")"
            echo -e "${C_GREEN}OK${C_NC}"
            verify_managed_temp_path "$m_path" "Drive ${drive^^} mount"
        fi
    done
}

run_seed() {
    log_stage "4. Seeding Baseline ($SEED_COUNT files)"
    for i in $(seq 1 "$SEED_COUNT"); do
        head -c 768000 /dev/urandom > "/tmp/base_$i.bin"
        echo -ne "  Uploading base_$i.bin..."
        curl -s --ftp-pasv $FTP_CRED -T "/tmp/base_$i.bin" "$FTP_URL/base_$i.bin"
        verify_file_size "$FTP_URL/base_$i.bin" 768000 "Baseline $i"
    done
}

run_count_limit_test() {
    log_stage "5. Managed Temp File Limit (Target: $MANAGED_LIMIT)"
    local first_managed=""
    local last_managed=""
    for i in $(seq 1 "$TEST_COUNT"); do
        local before_cache=$(list_cache_files | sort)
        upload_rest_managed_temp "managed_$i"
        local after_cache=$(list_cache_files | sort)
        local latest=$(comm -13 <(printf '%s\n' "$before_cache") <(printf '%s\n' "$after_cache") | head -n 1)
        if [[ -n "$latest" ]]; then
            [[ -z "$first_managed" ]] && first_managed="$UPLOAD_CACHE_URL/$latest"
            last_managed="$UPLOAD_CACHE_URL/$latest"
        fi
        IFS='|' read r_sz c_cnt c_sz p_cnt m_cnt <<< "$(get_stats)"
        printf "    Stats: Managed Count=%d (Limit=%d)\n" "$m_cnt" "$MANAGED_LIMIT"
        if [[ "$CFG_AUTO_CLEANUP" == "Enabled" ]]; then
            [[ "$m_cnt" -gt "$MANAGED_LIMIT" ]] && handle_failure "Limit exceeded ($m_cnt > $MANAGED_LIMIT)"
        fi
    done

    if [[ "$CFG_AUTO_CLEANUP" == "Enabled" ]]; then
        verify_remote_file_missing "$first_managed" "Oldest managed file should be evicted first"
        verify_remote_file_exists "$last_managed" "Newest managed file should be retained"
    else
        IFS='|' read r_sz c_cnt c_sz p_cnt m_cnt <<< "$(get_stats)"
        [[ "$m_cnt" -ne "$TEST_COUNT" ]] && handle_failure "Cleanup disabled should retain all managed uploads ($m_cnt != $TEST_COUNT)"
        verify_remote_file_exists "$first_managed" "Cleanup disabled should not delete oldest managed file"
        verify_remote_file_exists "$last_managed" "Cleanup disabled should retain newest managed file"
    fi
}

run_unmount_cleanup_test() {
    log_stage "6. Unmounted Uploads Rejoin Cleanup"
    echo -ne "  Removing drive A... "
    local code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" "http://$U64_HOST/v1/drives/a:remove")
    [[ "$code" == "200" ]] && echo -e "${C_GREEN}OK${C_NC}" || handle_failure "Drive A removal failed"

    if [[ "$CFG_AUTO_CLEANUP" == "Enabled" ]]; then
        upload_until_removed "$MOUNTED_PATH_8" "post_a" "Drive 8 removed" "$MOUNTED_PATH_9" "Drive 9 remains"
    else
        verify_remote_file_exists "ftp://$U64_HOST$MOUNTED_PATH_8" "Cleanup disabled should retain drive 8 after unmount"
        verify_remote_file_exists "ftp://$U64_HOST$MOUNTED_PATH_9" "Cleanup disabled should retain drive 9 while still mounted"
    fi

    echo -ne "  Removing drive B... "
    code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" "http://$U64_HOST/v1/drives/b:remove")
    [[ "$code" == "200" ]] && echo -e "${C_GREEN}OK${C_NC}" || handle_failure "Drive B removal failed"

    if [[ "$CFG_AUTO_CLEANUP" == "Enabled" ]]; then
        upload_until_removed "$MOUNTED_PATH_9" "post_b" "Drive 9 removed" "" ""
    else
        verify_remote_file_exists "ftp://$U64_HOST$MOUNTED_PATH_9" "Cleanup disabled should retain drive 9 after unmount"
    fi
}

run_final_integrity() {
    log_stage "7. Final Integrity Check"
    IFS='|' read r_sz c_cnt c_sz p_cnt m_cnt <<< "$(get_stats)"
    if [[ "$CFG_AUTO_CLEANUP" == "Enabled" ]]; then
        [[ "$m_cnt" -gt "$MANAGED_LIMIT" ]] && handle_failure "Final count above limit ($m_cnt)"
        [[ "$p_cnt" -ne 0 ]] && handle_failure "Persistent count not zero ($p_cnt)"
    else
        [[ "$m_cnt" -lt "$TEST_COUNT" ]] && handle_failure "Cleanup disabled should not remove managed uploads ($m_cnt < $TEST_COUNT)"
    fi
    verify_user_files_untouched || handle_failure "User files under /Temp were modified"
    echo -e "${C_GREEN}SUCCESS: Validation Passed.${C_NC}"
}

# --- Execution ---
clear
echo -e "${C_YEL}Ultimate 64 Temp Auto Cleanup E2E Validator${C_NC}"
echo -e "Target Host: ${U64_HOST}"
[[ "$ASSERTIONS_ENABLED" = false ]] && echo -e "${C_RED}!! ASSERTIONS DISABLED !!${C_NC}"

run_config
run_purge
run_mount_setup
run_seed
run_count_limit_test
run_unmount_cleanup_test
run_final_integrity
