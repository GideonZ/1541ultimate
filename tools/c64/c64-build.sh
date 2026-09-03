#!/bin/bash
#
# C64 Assembly Build Tool
# Unified script for building C64 assembly programs
# Supports building any .asm file and optionally running in VICE emulator

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Default values
ASM_FILE=""
RUN_EMULATOR=false
INSTALL_DEPS=false
VERBOSE=false
OUTPUT_DIR=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1" >&2
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" >&2
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

usage() {
    cat << EOF
C64 Assembly Build Tool

Usage: $0 <assembly-file> [options]

DESCRIPTION:
    Builds C64 assembly programs using 64tass assembler.
    Optionally runs the resulting program in VICE emulator.

ARGUMENTS:
    assembly-file       Path to .asm file to build (required)

OPTIONS:
    --build-only        Build only, don't run emulator (default behavior)
    --run               Build and run in VICE emulator  
    --output DIR        Output directory for .prg file (default: same as .asm)
    --install-deps      Install required dependencies (64tass, VICE)
    --verbose           Enable verbose output
    --help              Show this help message

EXAMPLES:
    ./c64-build.sh micromys-wheel.asm                    # Build micromys-wheel.prg
    ./c64-build.sh micromys-wheel.asm --run              # Build and run in VICE
    ./c64-build.sh micromys-wheel.asm --output /tmp      # Build to specific directory
    ./c64-build.sh --install-deps                        # Install the full toolchain:
      - 64tass: C64 cross-assembler (sudo apt install 64tass)
      - VICE: C64 emulator (sudo apt install vice)

NOTES:
    - Assembly file can be specified with relative or absolute path
    - Output .prg file will have same basename as input .asm file
    - VICE emulator is auto-detected (tries x64sc, x64, xvice in order)
    - Use --install-deps to automatically install required tools
EOF
}

find_64tass() {
    local local_64tass="$SCRIPT_DIR/../64tass/64tass"

    if [[ -x "$local_64tass" ]]; then
        printf '%s\n' "$local_64tass"
        return 0
    fi

    command -v 64tass
}

check_dependencies() {
    local missing_deps=()

    if ! find_64tass >/dev/null 2>&1; then
        missing_deps+=("64tass")
    fi

    if [[ "$RUN_EMULATOR" == "true" ]] && ! find_vice_command >/dev/null; then
        missing_deps+=("vice")
    fi

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_error "Missing dependencies: ${missing_deps[*]}"
        log_info "Install with: sudo apt install ${missing_deps[*]}"
        log_info "Or run: $0 --install-deps"
        return 1
    fi

    return 0
}

install_dependencies() {
    log_info "Installing C64 development dependencies..."

    if ! command -v apt-get >/dev/null 2>&1; then
        log_error "APT package manager not found. Please install dependencies manually."
        return 1
    fi

    local packages=("64tass")
    
    if [[ "$RUN_EMULATOR" == "true" ]]; then
        packages+=("vice")
    fi

    log_info "Installing packages: ${packages[*]}"
    sudo apt-get update
    sudo apt-get install -y "${packages[@]}"

    log_success "Dependencies installed successfully"
}

find_vice_command() {
    for cmd in x64sc x64 xvice; do
        if command -v "$cmd" >/dev/null 2>&1; then
            echo "$cmd"
            return 0
        fi
    done
    return 1
}

validate_assembly_file() {
    local asm_file="$1"

    if [[ -z "$asm_file" ]]; then
        log_error "No assembly file specified"
        return 1
    fi

    # Convert to absolute path
    if [[ ! "$asm_file" = /* ]]; then
        asm_file="$PWD/$asm_file"
    fi

    if [[ ! -f "$asm_file" ]]; then
        log_error "Assembly file not found: $asm_file"
        return 1
    fi

    if [[ ! "$asm_file" =~ \.asm$ ]]; then
        log_warning "File doesn't have .asm extension: $asm_file"
    fi

    echo "$asm_file"
}

cleanup_spurious_files() {
    local output_dir="$1"
    
    # Remove any files that might have been created by argument parsing issues
    # These can be created when shell command parsing goes wrong
    local spurious_files=(
        "--quiet"
        "--cbm-prg"  
        "--labels"
        "-o"
    )
    
    for file in "${spurious_files[@]}"; do
        # Check in output directory
        if [[ -f "$output_dir/$file" ]]; then
            log_info "Cleaning up spurious file: $output_dir/$file"
            rm -f "$output_dir/$file"
        fi
        
        # Check in current working directory (where script is run from)
        if [[ -f "./$file" ]]; then
            log_info "Cleaning up spurious file: ./$file" 
            rm -f "./$file"
        fi
    done
}

build_program() {
    local asm_file="$1"
    local output_dir="${2:-$(dirname "$asm_file")}"
    local assembler
    
    local basename=$(basename "$asm_file" .asm)
    local prg_file="$output_dir/$basename.prg"

    log_info "Building C64 assembly program..."
    log_info "Source: $asm_file"
    log_info "Output: $prg_file"

    # Create output directory if it doesn't exist
    mkdir -p "$output_dir"

    # Remove existing PRG file to avoid 64tass confusion
    if [[ -f "$prg_file" ]]; then
        log_info "Removing existing PRG file: $prg_file"
        rm -f "$prg_file"
    fi

    assembler=$(find_64tass)

    # Build with 64tass and emit only the PRG artifact this helper advertises.
    local build_args=("$assembler" "--cbm-prg")
    
    if [[ "$VERBOSE" == "false" ]]; then
        build_args+=("--quiet")
    fi
    
    build_args+=("-o" "$prg_file" "$asm_file")

    if "${build_args[@]}" >&2; then
        log_success "Build completed successfully!"
        log_info "Created: $prg_file"
        
        if [[ -f "$prg_file" ]]; then
            local size=$(stat -c%s "$prg_file")
            log_info "File size: $size bytes"
        fi
        
        # Clean up any spurious files that might have been created during build
        cleanup_spurious_files "$output_dir"
        
        # Output the PRG file path to stdout (for return value)
        echo "$prg_file"
        return 0
    else
        log_error "Build failed"
        # Clean up any spurious files even on failure
        cleanup_spurious_files "$output_dir"
        return 1
    fi
}

run_in_emulator() {
    local prg_file="$1"
    
    local vice_cmd
    if ! vice_cmd=$(find_vice_command); then
        log_error "VICE emulator not found"
        return 1
    fi

    log_info "Running program in VICE emulator..."
    log_info "Using: $vice_cmd"
    log_info "Loading: $prg_file"

    # Launch program in VICE with autostart
    "$vice_cmd" -autostart "$prg_file" -autostartprgmode 1

    log_success "Emulator session completed"
}

main() {
    # Parse arguments
    if [[ $# -eq 0 ]]; then
        usage
        exit 1
    fi

    # Check for help first
    if [[ "$1" == "--help" || "$1" == "-h" ]]; then
        usage
        exit 0
    fi

    # Check for install-deps only mode
    if [[ "$1" == "--install-deps" ]]; then
        RUN_EMULATOR=true
        install_dependencies
        exit 0
    fi

    # First argument should be assembly file (unless it's an option)
    if [[ "$1" != --* ]]; then
        ASM_FILE="$1"
        shift
    else
        log_error "Assembly file must be specified as first argument"
        usage
        exit 1
    fi

    # Parse options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --build-only)
                RUN_EMULATOR=false
                shift
                ;;
            --run)
                RUN_EMULATOR=true
                shift
                ;;
            --output)
                if [[ $# -lt 2 || -z "${2:-}" || "${2:-}" == --* ]]; then
                    log_error "--output requires a directory argument"
                    usage
                    exit 1
                fi
                OUTPUT_DIR="$2"
                shift 2
                ;;
            --install-deps)
                INSTALL_DEPS=true
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done

    # Validate assembly file
    if ! ASM_FILE=$(validate_assembly_file "$ASM_FILE"); then
        exit 1
    fi

    # Set output directory if not specified
    if [[ -z "$OUTPUT_DIR" ]]; then
        OUTPUT_DIR=$(dirname "$ASM_FILE")
    fi

    log_info "C64 Assembly Build Tool"
    log_info "Assembly file: $ASM_FILE"
    log_info "Output directory: $OUTPUT_DIR"
    log_info "Run emulator: $RUN_EMULATOR"

    # Install dependencies if requested
    if [[ "$INSTALL_DEPS" == "true" ]]; then
        install_dependencies
    fi

    # Check dependencies
    if ! check_dependencies; then
        exit 1
    fi

    # Build the program
    local prg_file
    if ! prg_file=$(build_program "$ASM_FILE" "$OUTPUT_DIR"); then
        exit 1
    fi

    # Run in emulator if requested
    if [[ "$RUN_EMULATOR" == "true" ]]; then
        run_in_emulator "$prg_file"
    fi

    # Final cleanup of any remaining spurious files
    cleanup_spurious_files "."
    cleanup_spurious_files "$OUTPUT_DIR"

    log_success "All operations completed successfully!"
    
    if [[ "$RUN_EMULATOR" == "false" ]]; then
        log_info ""
        log_info "To run the program manually:"
        log_info "  x64sc -autostart \"$prg_file\" -autostartprgmode 1"
    fi
}

main "$@"
