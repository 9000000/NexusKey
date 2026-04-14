#!/bin/bash
set -e

# Convert PNG images to multi-resolution ICO files for NexusKey Classic UI.
# Uses ImageMagick with Lanczos downscaling and alpha preservation.
#
# Usage:
#   bash tools/png_to_ico.sh <input.png> [output.ico]
#   bash tools/png_to_ico.sh <directory>          # convert all PNGs in dir
#   bash tools/png_to_ico.sh --tabs               # regenerate tab icons from Fluent Emoji
#
# Requires: ImageMagick (convert) or curl (for --tabs download)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TABS_DIR="$PROJECT_DIR/src/app/resources/tabs"

# Icon sizes to embed (matches DPI range 100%-200%)
SIZES="32,24,20,16"

# ── Helpers ──────────────────────────────────────────────────

die() { echo "ERROR: $*" >&2; exit 1; }

check_imagemagick() {
    if command -v magick &>/dev/null; then
        CONVERT="magick"
    elif command -v convert &>/dev/null; then
        CONVERT="convert"
    else
        die "ImageMagick not found. Install with: sudo apt install imagemagick"
    fi
}

convert_one() {
    local input="$1"
    local output="$2"

    [ -f "$input" ] || die "File not found: $input"

    $CONVERT "$input" \
        -alpha on -background none -filter Lanczos \
        -strip -define icon:auto-resize="$SIZES" \
        "$output"

    local size
    size=$(stat -c%s "$output" 2>/dev/null || stat -f%z "$output" 2>/dev/null)
    echo "  $(basename "$output"): ${size} bytes"
}

# ── Modes ────────────────────────────────────────────────────

convert_single() {
    local input="$1"
    local output="${2:-${input%.png}.ico}"
    echo "Converting: $(basename "$input") → $(basename "$output")"
    convert_one "$input" "$output"
}

convert_directory() {
    local dir="$1"
    local count=0
    echo "Converting all PNGs in: $dir"
    for png in "$dir"/*.png; do
        [ -f "$png" ] || continue
        local ico="${png%.png}.ico"
        convert_one "$png" "$ico"
        ((count++))
    done
    echo "Done: $count file(s) converted."
}

regenerate_tabs() {
    local FLUENT_BASE="https://raw.githubusercontent.com/microsoft/fluentui-emoji/main/assets"
    local tmpdir
    tmpdir=$(mktemp -d)
    trap "rm -rf $tmpdir" EXIT

    echo "Downloading Fluent Emoji 3D PNGs..."

    local -A ICONS=(
        [tab_input]="Memo/3D/memo_3d.png"
        [tab_macros]="Rocket/3D/rocket_3d.png"
        [tab_system]="Gear/3D/gear_3d.png"
        [btn_pick]="Bullseye/3D/bullseye_3d.png"
    )

    for name in "${!ICONS[@]}"; do
        local url="$FLUENT_BASE/${ICONS[$name]}"
        local png="$tmpdir/${name}.png"
        echo "  ↓ ${ICONS[$name]}"
        curl -sfL "$url" -o "$png" || die "Download failed: $url"
        file "$png" | grep -q "PNG image" || die "Not a valid PNG: $png"
    done

    echo ""
    echo "Converting to ICO (sizes: $SIZES)..."
    mkdir -p "$TABS_DIR"
    for name in "${!ICONS[@]}"; do
        convert_one "$tmpdir/${name}.png" "$TABS_DIR/${name}.ico"
    done

    echo ""
    echo "Done! Icons written to: src/app/resources/tabs/"
}

# ── Main ─────────────────────────────────────────────────────

if [ $# -eq 0 ]; then
    echo "Usage:"
    echo "  $0 <input.png> [output.ico]   Convert single PNG"
    echo "  $0 <directory>                 Convert all PNGs in directory"
    echo "  $0 --tabs                      Regenerate tab icons from Fluent Emoji"
    exit 0
fi

check_imagemagick

case "$1" in
    --tabs)
        regenerate_tabs
        ;;
    *)
        if [ -d "$1" ]; then
            convert_directory "$1"
        else
            convert_single "$1" "$2"
        fi
        ;;
esac
