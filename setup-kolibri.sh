#!/usr/bin/env bash
# Download and unpack the KolibriOS runtime files that kex needs.
#
# Layout produced (default KEX_DIR=~/.kex):
#   ~/.kex/char.mt
#   ~/.kex/charUni.mt
#   ~/.kex/root/RD/1/            (contents of the floppy image)
#   ~/.kex/root/CD0/1/           (contents of kolibri.iso, if KEX_WITH_ISO=1)
#
# Environment overrides:
#   KEX_DIR         target root (default $HOME/.kex)
#   KEX_WITH_ISO    if set to 1, also fetch/extract latest-iso.7z into CD0/1
#   KEX_FORCE       if set to 1, re-download and re-extract even if present

set -euo pipefail

KEX_DIR="${KEX_DIR:-$HOME/.kex}"
KEX_WITH_ISO="${KEX_WITH_ISO:-0}"
KEX_FORCE="${KEX_FORCE:-0}"
RD_DIR="$KEX_DIR/root/RD/1"
CD_DIR="$KEX_DIR/root/CD0/1"

FLOPPY_URL="http://builds.kolibrios.org/en_US/latest-img.7z"
ISO_URL="http://builds.kolibrios.org/en_US/latest-iso.7z"
CHAR_URL="https://git.kolibrios.org/KolibriOS/kolibrios/raw/branch/main/kernel/trunk/gui/char.mt"
CHARUNI_URL="https://git.kolibrios.org/KolibriOS/kolibrios/raw/branch/main/kernel/trunk/gui/charUni.mt"

for cmd in 7z wget; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "error: '$cmd' is required. Install it (e.g. apt install p7zip-full wget) and retry." >&2
        exit 1
    fi
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

mkdir -p "$RD_DIR" "$CD_DIR"

fetch() {
    local url="$1" dest="$2"
    if [ -s "$dest" ]; then
        echo "== keep $(basename "$dest") (already present)"
    else
        echo "== fetch $(basename "$dest")"
        wget --no-config -q --show-progress -O "$dest" "$url"
    fi
}

fetch "$CHAR_URL"    "$KEX_DIR/char.mt"
fetch "$CHARUNI_URL" "$KEX_DIR/charUni.mt"

unpack_archive() {
    # unpack_archive <url> <archive-name> <target-dir> <inner-glob>
    local url="$1" name="$2" target="$3" inner_glob="$4"

    if [ "$KEX_FORCE" != "1" ] && [ -f "$target/DEFAULT.SKN" ]; then
        echo "== keep $target (DEFAULT.SKN already present; set KEX_FORCE=1 to redo)"
        return 0
    fi

    echo "== fetch $name"
    wget --no-config -q --show-progress -O "$tmp/$name" "$url"

    echo "== extract $name"
    7z x -y -o"$tmp" "$tmp/$name" >/dev/null

    local inner
    inner="$(find "$tmp" -maxdepth 2 -type f -iname "$inner_glob" | head -n1)"
    if [ -z "$inner" ]; then
        echo "error: no $inner_glob found inside $name" >&2
        exit 1
    fi

    echo "== unpack $(basename "$inner") into $target"
    # 7z reads FAT12/ISO9660 images directly, so no sudo/mount is needed.
    7z x -y -o"$target" "$inner" >/dev/null
}

unpack_archive "$FLOPPY_URL" "latest-img.7z" "$RD_DIR" "*.img"

if [ ! -f "$RD_DIR/DEFAULT.SKN" ]; then
    echo "error: DEFAULT.SKN missing from $RD_DIR after floppy extraction" >&2
    exit 1
fi

if [ "$KEX_WITH_ISO" = "1" ]; then
    unpack_archive "$ISO_URL" "latest-iso.7z" "$CD_DIR" "*.iso"
fi

echo
echo "Done."
echo "  KEX_DIR = $KEX_DIR"
echo "  Contents of $RD_DIR:"
ls "$RD_DIR" | sed 's/^/    /'
if [ "$KEX_WITH_ISO" = "1" ]; then
    echo "  Contents of $CD_DIR:"
    ls "$CD_DIR" | sed 's/^/    /'
fi
echo
echo "Try: $(dirname "$(readlink -f "$0")")/kex $RD_DIR/GAMES/XONIX"
