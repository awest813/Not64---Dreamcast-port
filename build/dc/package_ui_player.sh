set -e
source /opt/toolchains/dc/kos/environ.sh >/dev/null 2>&1
cd /f/GitHub/Not64---Dreamcast-port
work=build/dc/ui-package
disc=build/dc/ui-disc
mkdir -p "$work" "$disc/roms/Diagnostics" "$disc/roms/Empty folder"
cp build/dc/stadium2/game.z64 "$disc/roms/Pokemon Stadium 2.z64"
cp roms/dc_cputest.z64 roms/dc_vitest.z64 "$disc/roms/Diagnostics/"
printf 'Invalid ROM fixture for launch validation\n' > "$disc/roms/Diagnostics/Invalid test.z64"
sh-elf-objcopy -O binary build/dc/not64-ui.elf "$work/not64.bin"
/opt/toolchains/dc/kos/utils/scramble/scramble "$work/not64.bin" "$disc/1ST_READ.BIN"
cat > "$work/ip.txt" <<'IPTXT'
Hardware ID   : SEGA SEGAKATANA
Maker ID      : SEGA ENTERPRISES
Device Info   : 0000 CD-ROM1/1
Area Symbols  : JUE
Peripherals   : E000F10
Product No    : T0000
Version       : V1.000
Release Date  : 20261004
Boot Filename : 1ST_READ.BIN
SW Maker Name : NOT64
Game Title    : NOT64 DC
IPTXT
cp /opt/toolchains/dc/kos/utils/makeip/rsrc/templates/ip.tmpl "$work/"
(cd "$work" && /opt/toolchains/dc/kos/utils/makeip/makeip -f ip.txt IP.BIN)
mkisofs -C 0,11702 -V NOT64_DC -G "$work/IP.BIN" -joliet -rock -l -o "$work/data.iso" "$disc"
cdi4dc "$work/data.iso" build/dc/not64-ui.cdi
