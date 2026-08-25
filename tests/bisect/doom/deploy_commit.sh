#!/usr/bin/env bash
# deploy_commit.sh <commit> [worktree]
#
# Put a U64 on one commit's FPGA bitstream and Nios application over JTAG.
# Nothing is written to flash: a power cycle always restores the flashed
# firmware, which is the recovery for anything that goes wrong here.
#
# The FPGA is programmed first on purpose. nios2-download writes into DDR that
# the bitstream brings up, so downloading against the previous commit's
# bitstream fails to verify and leaves the processor paused; and the build that
# follows gives the memory controller time to settle.
#
# HDMI stays dark for the whole session. The firmware initialises the HDMI
# transmitter only at application start and it does not come back on an older
# core. The network VIC stream still works and is what this harness measures.
set -uo pipefail

COMMIT="${1:?usage: deploy_commit.sh <commit> [worktree]}"
WT="${2:-${DOOM_BISECT_WORKTREE:-$HOME/.cache/doom-bisect/wt}}"
HOST="${DOOM_HOST:-u64}"
JOBS="${JOBS:-8}"
IMAGE="${BUILD_IMAGE:-1541u-build:latest}"
# Mount only the toolchain, not the whole home directory.
TOOLS_ROOT="${BUILD_TOOLS_ROOT:-${INTEL_FPGA_ROOT:-$HOME/altera_lite/18.1}}"
INTEL_FPGA_ROOT="${INTEL_FPGA_ROOT:-$HOME/altera_lite/18.1}"
CABLE="${JTAG_CABLE:-USB-Blaster [1-6]}"
ELF="$WT/target/u64/nios2/ultimate/result/ultimate.elf"
LOGDIR="${DOOM_BISECT_LOGDIR:-$HOME/.cache/doom-bisect}"
mkdir -p "$LOGDIR"

export QUARTUS_ROOTDIR="$INTEL_FPGA_ROOT/quartus"
export PATH="$QUARTUS_ROOTDIR/bin:$INTEL_FPGA_ROOT/nios2eds/bin:$PATH"

die() { echo "DEPLOY_FAIL $*" >&2; exit 2; }
[ -d "$WT/.git" ] || [ -f "$WT/.git" ] || die "no worktree at $WT (see README)"
# This script hard-resets and cleans $WT. Refuse to do that to the checkout the
# script itself lives in, which is where a mistyped path or a stale
# DOOM_BISECT_WORKTREE would otherwise point.
_here_root=$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel 2>/dev/null || echo "")
_wt_root=$(git -C "$WT" rev-parse --show-toplevel 2>/dev/null || echo "")
[ -n "$_wt_root" ] || die "$WT is not a git worktree"
[ "$_wt_root" != "$_here_root" ] \
    || die "$WT is the checkout this script lives in; use a dedicated worktree"
command -v quartus_pgm >/dev/null || die "quartus_pgm not on PATH"
docker image inspect "$IMAGE" >/dev/null 2>&1 || IMAGE=my_docker_image
docker image inspect "$IMAGE" >/dev/null 2>&1 || die "no build image ($IMAGE)"

if [ "${NO_CHECKOUT:-0}" != "1" ]; then
    git -C "$WT" checkout --detach --force "$COMMIT" >/dev/null 2>&1 \
        || die "cannot check out $COMMIT"
else
    # NO_CHECKOUT skips moving HEAD, not the reset and clean below. Without this
    # guard a caller with it set would deploy whatever the worktree happened to
    # be on, while every message still named $COMMIT.
    [ "$(git -C "$WT" rev-parse "$COMMIT" 2>/dev/null)" \
        = "$(git -C "$WT" rev-parse HEAD 2>/dev/null)" ] \
        || die "NO_CHECKOUT=1 but $WT is not on $COMMIT"
fi
# The build rewrites tracked files (tools/64tass/64tass), which would abort the
# next checkout, and stale objects from another commit must not be reused.
git -C "$WT" reset --hard >/dev/null 2>&1 || die "git reset --hard"
git -C "$WT" clean -xdf >/dev/null 2>&1 || die "git clean"
git -C "$WT" submodule update --init --recursive >/dev/null 2>&1 \
    || die "git submodule update"

cd "$WT" || die "cannot enter $WT"
# A fresh checkout gives every file the same timestamp, and the Nios BSP
# makefiles refuse to build when settings.bsp is not older than the makefile.
for d in software/nios_solo_bsp software/nios_appl_bsp; do
    touch "$d/Makefile" "$d/public.mk" "$d/mem_init.mk" 2>/dev/null
done
# system_info.cc includes gitinfo.h, which the build expects to have been
# generated for it. Commit time rather than wall-clock time, so the header is
# stable and an unchanged commit does not recompile.
git_tag=$(git -C "$WT" describe --tags 2>/dev/null || echo unknown)
git_branch=$(git -C "$WT" describe --all 2>/dev/null || echo unknown)
git_date=$(git -C "$WT" log -n 1 --format=%ai 2>/dev/null || echo unknown)
git_hash=$(git -C "$WT" rev-parse --short HEAD 2>/dev/null || echo unknown)
while IFS= read -r dir; do
    mkdir -p "$dir/output" "$dir/result"
    cat >"$dir/output/gitinfo.h" <<GITINFO
/* Generated file, do not edit. */
#define APP_VERSION_TAG    "${git_tag}"
#define APP_VERSION_BRANCH "${git_branch}"
#define APP_VERSION_DATE   "${git_date}"
#define APP_VERSION_HASH   "${git_hash}"
#define APP_BUILD_DATE     "${git_date}"
#define APP_BUILD_MACHINE  "$(hostname)"
GITINFO
done < <(find target -type f \( -iname 'makefile' -o -iname 'Makefile' \) -printf '%h\n' | sort -u)

echo "== programming FPGA from external/u64.sof"
# Captured rather than piped into grep -q: grep exits on the first match and
# closes the pipe, quartus_pgm takes SIGPIPE, and under `pipefail` that turns a
# successful programming run into a failure, intermittently.
pgm_output=$(quartus_pgm -c "$CABLE" -m JTAG -o "p;external/u64.sof" 2>&1)
grep -qE "Configuration succeeded" <<<"$pgm_output" \
    || die "quartus_pgm (cable '"'"'$CABLE'"'"'; run jtagconfig to list cables)"

echo "== building the Nios application"
rm -f "$ELF"
# Under rootless Docker this user is already root inside the container and the
# mount belongs to it; passing --user then maps to an id that cannot write.
docker_user=(--user "$(id -u):$(id -g)")
docker_security=$(docker info --format '{{json .SecurityOptions}}' 2>/dev/null || true)
if grep -qi rootless <<<"$docker_security"; then
    docker_user=()
fi
docker run --rm --entrypoint /bin/bash \
    -v "$WT:/__w" "${docker_user[@]}" -e HOME=/tmp/1541u-home \
    -v "$TOOLS_ROOT:/mnt/build-tools:ro" "$IMAGE" -lc '
set -euo pipefail
mkdir -p "$HOME"
for base in /mnt/build-tools /mnt/build-tools/altera_lite/18.1 \
            /mnt/build-tools/intelFPGA_lite/18.1; do
    if [ -d "$base/quartus" ]; then
        export QUARTUS_ROOTDIR="$base/quartus"
        export QSYS_ROOTDIR="$QUARTUS_ROOTDIR/sopc_builder/bin"
        export PATH="$QUARTUS_ROOTDIR/bin:$base/nios2eds/bin:$base/nios2eds/sdk2/bin:$base/nios2eds/bin/gnu/H-x86_64-pc-linux-gnu/bin:$QSYS_ROOTDIR:$PATH"
        break
    fi
done
[ -n "${QUARTUS_ROOTDIR:-}" ] || { echo "no Quartus under /mnt/build-tools" >&2; exit 1; }
cd /__w
make -j '"$JOBS"' -C tools
make -j '"$JOBS"' -C software/nios_solo_bsp
make -j '"$JOBS"' -C software/nios_appl_bsp
make -j '"$JOBS"' -C target/libs/nios2/lwip
make -j '"$JOBS"' -C target/u64/nios2/ultimate result/ultimate.elf
' > "$LOGDIR/build.log" 2>&1
[ -s "$ELF" ] || { tail -25 "$LOGDIR/build.log" >&2; die "build"; }

echo "== downloading the application"
downloaded=0
for attempt in 1 2 3; do
    if nios2-download -g "$ELF" >"$LOGDIR/jtag.log" 2>&1; then
        downloaded=1; break
    fi
    echo "   attempt $attempt failed, retrying"
    sleep 8
done
[ "$downloaded" = "1" ] || { tail -5 "$LOGDIR/jtag.log" >&2; die "nios2-download"; }

info=""
for _ in $(seq 1 30); do
    info=$(curl -s -m 4 "http://$HOST/v1/info" || true)
    [ -n "$info" ] && break
    sleep 2
done
[ -n "$info" ] || die "device did not answer REST"
curl -s -m 8 -X PUT "http://$HOST/v1/machine:reset" >/dev/null
sleep 6

# The bitstream blob, because core_version and fpga_version are hand-bumped
# labels that several distinct builds share.
echo "DEPLOYED $(git -C "$WT" rev-parse --short HEAD)" \
     "sof=$(git -C "$WT" log --format=%h -1 -- external/u64.sof)" \
     "blob=$(git -C "$WT" rev-parse --short HEAD:external/u64.sof)" \
     "$(echo "$info" | tr -d '\n ')"
