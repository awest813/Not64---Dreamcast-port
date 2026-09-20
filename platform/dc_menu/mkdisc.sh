#!/bin/bash
# Build a bootable Dreamcast CD image from the KallistiOS ELF.
#
# Flycast (and every DC emulator) will happily open a raw .elf, but nothing
# renders: the KOS framebuffer examples come up black that way too, so it is
# the loader, not the program. A real disc boots. This is also how the port
# would actually ship, and it puts ROMs on /cd where the browser looks first.
#
# Run it inside the KallistiOS container, from the repo root:
#
#   docker run --rm -v "$PWD":/src -w /src --user "$(id -u):$(id -g)" \
#       einsteinx2/dcdev-kos-toolchain:latest \
#       bash -lc 'source /opt/toolchains/dc/kos/environ.sh && \
#                 platform/dc_menu/mkdisc.sh build/not64.cdi roms/*.z64'
#
# Everything after the output path is copied onto the disc as /cd/roms.
set -eu

OUT="${1:?usage: mkdisc.sh <out.cdi> [roms...]}"
shift || true

U=/opt/toolchains/dc/kos/utils
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$(dirname "$OUT")" "$WORK/cd/roms"

# 1. The ELF becomes a flat binary, then gets the Dreamcast's boot scramble.
sh-elf-objcopy -O binary not64-dc.elf "$WORK/out.bin"
"$U/scramble/scramble" "$WORK/out.bin" "$WORK/cd/1ST_READ.BIN"

# 2. IP.BIN is the 32 KiB bootstrap in the first session.
cat > "$WORK/ip.txt" <<'IPTXT'
Hardware ID   : SEGA SEGAKATANA
Maker ID      : SEGA ENTERPRISES
Device Info   : 0000 CD-ROM1/1
Area Symbols  : JUE
Peripherals   : E000F10
Product No    : T0000
Version       : V1.000
Release Date  : 20250101
Boot Filename : 1ST_READ.BIN
SW Maker Name : NOT64
Game Title    : NOT64 DREAMCAST
IPTXT
# makeip looks for IP.TMPL in the working directory, not next to itself.
cp "$U/makeip/IP.TMPL" "$WORK/"
( cd "$WORK" && "$U/makeip/makeip" ip.txt IP.BIN )

# 3. Whatever ROMs were asked for.
for rom in "$@"; do
    [ -f "$rom" ] && cp "$rom" "$WORK/cd/roms/"
done
echo "disc contents:"
ls -la "$WORK/cd" "$WORK/cd/roms"

# 4. -C 0,11702 is the standard DC data-track offset for a 2-session disc.
mkisofs -C 0,11702 -V NOT64 -G "$WORK/IP.BIN" -joliet -rock -l \
        -o "$WORK/data.iso" "$WORK/cd" > /dev/null 2>&1

"$U/img4dc/cdi4dc/cdi4dc" "$WORK/data.iso" "$OUT" > /dev/null
echo "wrote $OUT"
ls -la "$OUT"
