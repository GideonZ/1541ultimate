#!/bin/bash
# Ultimate 64 Temp Auto Cleanup E2E Validator

# --- Configuration ---
U64_HOST="${U64_HOST:-u64}"
U64_PASS="${U64_PASS:-}"
REST_HEADER="X-Password: $U64_PASS"
FTP_CRED="-u user:${U64_PASS:-password}"
FTP_URL="ftp://$U64_HOST/Temp"

MANAGED_SIZE=524288 # 512 KiB
MANAGED_LIMIT=10
MOUNT_SOURCE_8="/Temp/mount-drive-8.d64"
MOUNT_SOURCE_9="/Temp/mount-drive-9.d64"
MOUNT_LOCAL_8="/tmp/mount-drive-8.d64"
MOUNT_LOCAL_9="/tmp/mount-drive-9.d64"
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

# --- Utility Functions ---

log_stage() { echo -e "\n${C_BLUE}STAGE: $1${C_NC}\n$(printf '%.s-' {1..60})"; }

extract_json_field() {
    local json=$1
    local key=$2
    sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p" <<< "$json"
}

verify_managed_temp_path() {
    local mounted_path=$1
    local context=$2

    case "$mounted_path" in
        /Temp/cache/temp*)
            echo -e " ${C_GREEN}[VERIFIED]${C_NC}"
            ;;
        *)
            echo -e "\n${C_RED}VERIFICATION FAILED: $context${C_NC}"
            echo "  Unexpected mounted backing path: $mounted_path"
            exit 1
            ;;
    esac
}

list_cache_files() {
    curl -s --ftp-pasv $FTP_CRED "$FTP_URL/cache/" | awk '/^-/ {print $9}'
}

count_persistent_mounts_in_cache() {
    local names
    names=$(list_cache_files)
    local count=0

    if [[ -n "$MOUNTED_BASE_8" ]] && grep -Fxq "$MOUNTED_BASE_8" <<< "$names"; then
        count=$((count + 1))
    fi
    if [[ -n "$MOUNTED_BASE_9" ]] && grep -Fxq "$MOUNTED_BASE_9" <<< "$names"; then
        count=$((count + 1))
    fi
    echo "$count"
}

verify_remote_file_exists() {
    local remote_path=$1
    local context=$2

    local code=$(curl -s --ftp-pasv $FTP_CRED -o /dev/null -w "%{http_code}" "$remote_path")
    if [[ "$code" != "226" && "$code" != "200" ]]; then
        echo -e "\n${C_RED}VERIFICATION FAILED: $context${C_NC}"
        echo "  Missing path: $remote_path"
        exit 1
    fi
    echo -e " ${C_GREEN}[VERIFIED]${C_NC}"
}

verify_remote_file_missing() {
    local remote_path=$1
    local context=$2

    local code=$(curl -s --ftp-pasv $FTP_CRED -o /dev/null -w "%{http_code}" "$remote_path")
    if [[ "$code" == "226" || "$code" == "200" ]]; then
        echo -e "\n${C_RED}VERIFICATION FAILED: $context${C_NC}"
        echo "  Unexpected path: $remote_path"
        exit 1
    fi
    echo -e " ${C_GREEN}[VERIFIED]${C_NC}"
}

# Verifies file exists and matches size via FTP
verify_file_size() {
    local remote_path=$1
    local expected_size=$2
    local context=$3

    local remote_size=$(curl -s --ftp-pasv $FTP_CRED "$remote_path" -I | grep -i "Content-Length" | awk '{print $2}' | tr -d '\r')

    if [[ -z "$remote_size" || "$remote_size" -eq 0 ]]; then
        local dir_path=$(dirname "$remote_path")
        local base_name=$(basename "$remote_path")
        remote_size=$(curl -s --ftp-pasv $FTP_CRED "$dir_path/" | grep "$base_name" | awk '{print $5}')
    fi

    if [[ "$remote_size" != "$expected_size" ]]; then
        echo -e "\n${C_RED}VERIFICATION FAILED: $context${C_NC}"
        echo "  Path:     $remote_path"
        echo "  Expected: $expected_size bytes"
        echo "  Actual:   ${remote_size:-0} bytes"
        exit 1
    fi
    echo -e " ${C_GREEN}[VERIFIED]${C_NC}"
}

get_stats() {
    local r_ls=$(curl -s --ftp-pasv $FTP_CRED "$FTP_URL/")
    local c_ls=$(curl -s --ftp-pasv $FTP_CRED "$FTP_URL/cache/")
    local r_sz=$(echo "$r_ls" | awk '/^-/ {s+=$5} END {print s+0}')
    local c_cnt=$(echo "$c_ls" | awk '/^-/ {c++} END {print c+0}')
    local c_sz=$(echo "$c_ls" | awk '/^-/ {s+=$5} END {print s+0}')
    local persistent_cnt=$(count_persistent_mounts_in_cache)
    local managed_cnt=$((c_cnt - persistent_cnt))
    printf "%d|%d|%d|%d|%d" "$r_sz" "$c_cnt" "$c_sz" "$persistent_cnt" "$managed_cnt"
}

upload_rest_managed_temp() {
    local label=$1
    local before_cache=$(list_cache_files | sort)

    head -c $MANAGED_SIZE /dev/urandom > "/tmp/${label}.bin"
    echo -ne "  [REST] Uploading ${label}.bin ($MANAGED_SIZE bytes)..."
    curl -s -o /dev/null -X POST -H "$REST_HEADER" --data-binary "@/tmp/${label}.bin" "http://$U64_HOST/v1/runners:run_prg"

    sleep 1.5
    local after_cache=$(list_cache_files | sort)
    local latest=$(comm -13 <(printf '%s\n' "$before_cache") <(printf '%s\n' "$after_cache") | head -n 1)
    if [[ -z "$latest" ]]; then
        echo -e " ${C_RED}[FAIL: Not found in cache]${C_NC}"
        exit 1
    fi

    verify_file_size "$FTP_URL/cache/$latest" "$MANAGED_SIZE" "Managed file ${label}"
}

upload_until_removed() {
    local mounted_path=$1
    local label_prefix=$2
    local missing_context=$3
    local still_mounted_path=$4
    local still_mounted_context=$5

    for attempt in $(seq 1 $((MANAGED_LIMIT + 2))); do
        upload_rest_managed_temp "${label_prefix}_${attempt}"

        if [[ -n "$still_mounted_path" ]]; then
            verify_remote_file_exists "ftp://$U64_HOST${still_mounted_path}" "$still_mounted_context"
        fi

        local code=$(curl -s --ftp-pasv $FTP_CRED -o /dev/null -w "%{http_code}" "ftp://$U64_HOST${mounted_path}")
        if [[ "$code" != "226" && "$code" != "200" ]]; then
            echo -e "    ${C_GREEN}Removed after ${attempt} post-unmount upload(s).${C_NC}"
            return 0
        fi
    done

    echo -e "\n${C_RED}VERIFICATION FAILED: $missing_context${C_NC}"
    echo "  Path remained present after repeated uploads: ftp://$U64_HOST${mounted_path}"
    exit 1
}

# --- Functional Stages ---

run_config() {
    log_stage "1. Configuration Setup"
    for key in "Auto%20Cleanup" "Use%20Cache%20Subfolder"; do
        echo -ne "  Setting $key to Enabled... "
        local code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" \
            "http://$U64_HOST/v1/configs/User%20Interface%20Settings/$key?value=Enabled")
        [[ "$code" == "200" ]] && echo -e "${C_GREEN}OK${C_NC}" || (echo -e "${C_RED}FAIL ($code)${C_NC}"; exit 1)
    done
}

run_purge() {
    log_stage "2. Purging /Temp"
    for drive in a b; do
        curl -s -o /dev/null -X PUT -H "$REST_HEADER" "http://$U64_HOST/v1/drives/$drive:remove"
    done
    local files=$(curl -s --ftp-pasv $FTP_CRED "$FTP_URL/" | awk '/^-/ {print $9}')
    for f in $files; do
        if [[ "$f" != "cache" && -n "$f" ]]; then
            echo -ne "  Deleting $f... "
            curl -s --ftp-pasv $FTP_CRED "$FTP_URL/" -X "DELE $f" > /dev/null && echo -e "OK"
        fi
    done
    local cache_files=$(curl -s --ftp-pasv $FTP_CRED "$FTP_URL/cache/" | awk '/^-/ {print $9}')
    for f in $cache_files; do
        if [[ -n "$f" ]]; then
            echo -ne "  Deleting cache/$f... "
            curl -s --ftp-pasv $FTP_CRED "$FTP_URL/cache/" -X "DELE $f" > /dev/null && echo -e "OK"
        fi
    done
}

run_mount_setup() {
    log_stage "3. Mounting Uploaded D64 Images On Drives 8 And 9"

    for spec in \
        "$MOUNT_SOURCE_8|Drive8|$MOUNT_LOCAL_8|a|MOUNTED_PATH_8|MOUNTED_BASE_8" \
        "$MOUNT_SOURCE_9|Drive9|$MOUNT_LOCAL_9|b|MOUNTED_PATH_9|MOUNTED_BASE_9"; do
        IFS='|' read remote_path diskname local_path drive path_var base_var <<< "$spec"

        echo -ne "  Creating $remote_path... "
        local create_code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" \
            "http://$U64_HOST/v1/files${remote_path}:create_d64?diskname=$diskname")
        [[ "$create_code" == "200" ]] && echo -e "${C_GREEN}OK${C_NC}" || (echo -e "${C_RED}FAIL ($create_code)${C_NC}"; exit 1)

        echo -ne "  Downloading $(basename "$remote_path") for upload mount... "
        curl -s --ftp-pasv $FTP_CRED -o "$local_path" "ftp://$U64_HOST${remote_path}"
        verify_file_size "ftp://$U64_HOST${remote_path}" 174848 "Source image $(basename "$remote_path")"

        echo -ne "  Removing staging source $(basename "$remote_path")... "
        curl -s --ftp-pasv $FTP_CRED "$FTP_URL/" -X "DELE $(basename "$remote_path")" > /dev/null
        echo -e "${C_GREEN}OK${C_NC}"
        verify_remote_file_missing "ftp://$U64_HOST${remote_path}" "Staging source $(basename "$remote_path") removed before upload mount"

        echo -ne "  Mounting $(basename "$remote_path") on drive ${drive^^}... "
        local response=$(curl -s -X POST -H "$REST_HEADER" --data-binary "@$local_path" \
            "http://$U64_HOST/v1/drives/$drive:mount?type=d64&mode=readwrite")
        local mounted_path=$(extract_json_field "$response" "file")
        if [[ -z "$mounted_path" ]]; then
            echo -e "${C_RED}FAIL${C_NC}"
            echo "  Response: $response"
            exit 1
        fi
        printf -v "$path_var" '%s' "$mounted_path"
        printf -v "$base_var" '%s' "$(basename "$mounted_path")"
        echo -e "${C_GREEN}OK${C_NC}"
        echo -ne "  Verifying managed backing path for drive ${drive^^}..."
        verify_managed_temp_path "$mounted_path" "Drive ${drive^^} mount should use a managed temp backing file"
        verify_remote_file_exists "ftp://$U64_HOST${mounted_path}" "Mounted upload backing file $(basename "$mounted_path")"
    done
}

run_seed() {
    log_stage "4. Seeding Baseline (10x750KB via FTP)"
    local seed_sz=768000
    for i in {1..10}; do
        head -c $seed_sz /dev/urandom > "/tmp/base_$i.bin"
        echo -ne "  Uploading base_$i.bin..."
        curl -s --ftp-pasv $FTP_CRED -T "/tmp/base_$i.bin" "$FTP_URL/base_$i.bin"
        verify_file_size "$FTP_URL/base_$i.bin" "$seed_sz" "Baseline File $i"
    done
}

run_count_limit_test() {
    log_stage "5. Managed Temp File Limit"
    echo "  Target Limit: $MANAGED_LIMIT managed files"

    for i in {1..12}; do
        local before_cache=$(list_cache_files | sort)
        head -c $MANAGED_SIZE /dev/urandom > "/tmp/managed_$i.bin"
        echo -ne "  [REST] Uploading managed_$i.bin ($MANAGED_SIZE bytes)..."

        curl -s -o /dev/null -X POST -H "$REST_HEADER" --data-binary "@/tmp/managed_$i.bin" "http://$U64_HOST/v1/runners:run_prg"

        sleep 1.5
        local after_cache=$(list_cache_files | sort)
        local latest=$(comm -13 <(printf '%s\n' "$before_cache") <(printf '%s\n' "$after_cache") | head -n 1)
        if [[ -n "$latest" ]]; then
            verify_file_size "$FTP_URL/cache/$latest" "$MANAGED_SIZE" "Managed File $i"
        else
            echo -e " ${C_RED}[FAIL: Not found in cache]${C_NC}"
            exit 1
        fi

        verify_remote_file_exists "ftp://$U64_HOST${MOUNTED_PATH_8}" "Drive 8 mounted upload still present after managed upload $i"
        verify_remote_file_exists "ftp://$U64_HOST${MOUNTED_PATH_9}" "Drive 9 mounted upload still present after managed upload $i"

        IFS='|' read r_sz c_cnt c_sz persistent_cnt managed_cnt <<< "$(get_stats)"
        printf "    Stats: Root=%'d | Cache=%'d bytes (%d files total, %d mounted, %d managed)\n" \
            "$r_sz" "$c_sz" "$c_cnt" "$persistent_cnt" "$managed_cnt"

        if [[ "$managed_cnt" -gt "$MANAGED_LIMIT" ]]; then
            echo -e "${C_RED}FAIL: Managed temp count exceeded limit ($managed_cnt > $MANAGED_LIMIT).${C_NC}"
            exit 1
        fi
    done

    echo -e "    ${C_GREEN}PASS: Managed temp cleanup held at $MANAGED_LIMIT files while mounted uploads remained.${C_NC}"
}

run_unmount_cleanup_test() {
    log_stage "6. Unmounted Uploads Rejoin Cleanup"

    echo -ne "  Removing drive A image... "
    local code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" "http://$U64_HOST/v1/drives/a:remove")
    [[ "$code" == "200" ]] && echo -e "${C_GREEN}OK${C_NC}" || (echo -e "${C_RED}FAIL ($code)${C_NC}"; exit 1)

    upload_until_removed "$MOUNTED_PATH_8" "post_unmount_a" \
        "Drive 8 uploaded image removed after unmount and continued temp creation" \
        "$MOUNTED_PATH_9" "Drive 9 uploaded image remains while still mounted"
    verify_remote_file_missing "ftp://$U64_HOST${MOUNTED_PATH_8}" "Drive 8 uploaded image removed after unmount and continued temp creation"

    echo -ne "  Removing drive B image... "
    code=$(curl -s -o /dev/null -w "%{http_code}" -X PUT -H "$REST_HEADER" "http://$U64_HOST/v1/drives/b:remove")
    [[ "$code" == "200" ]] && echo -e "${C_GREEN}OK${C_NC}" || (echo -e "${C_RED}FAIL ($code)${C_NC}"; exit 1)

    upload_until_removed "$MOUNTED_PATH_9" "post_unmount_b" \
        "Drive 9 uploaded image removed after unmount and continued temp creation" "" ""
    verify_remote_file_missing "ftp://$U64_HOST${MOUNTED_PATH_9}" "Drive 9 uploaded image removed after unmount and continued temp creation"
}

run_final_integrity() {
    log_stage "7. Final Integrity Check"
    IFS='|' read r_sz c_cnt c_sz persistent_cnt managed_cnt <<< "$(get_stats)"
    if [[ "$r_sz" -lt 7600000 ]]; then
         echo -e "${C_RED}FAIL: Baseline files were deleted! Root size: $r_sz${C_NC}"
         exit 1
    fi
    if [[ "$managed_cnt" -gt "$MANAGED_LIMIT" ]]; then
         echo -e "${C_RED}FAIL: Managed temp count ended above limit ($managed_cnt > $MANAGED_LIMIT).${C_NC}"
         exit 1
    fi
    if [[ "$persistent_cnt" -ne 0 ]]; then
        echo -e "${C_RED}FAIL: Mounted temp count expected 0 after unmount, found $persistent_cnt.${C_NC}"
        exit 1
    fi
    echo -e "${C_GREEN}SUCCESS: Temp Auto Cleanup verified. Baseline preserved.${C_NC}"
}

# --- Execution ---
clear
echo -e "${C_YEL}Ultimate 64 Temp Auto Cleanup E2E Validator${C_NC}"
run_config
run_purge
run_mount_setup
run_seed
run_count_limit_test
run_unmount_cleanup_test
run_final_integrity