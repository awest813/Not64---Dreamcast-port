#!/usr/bin/env bash
# Build a local-only game test disc; the user's ROM stays under ignored build/.
set -euo pipefail
if [[ $# != 1 || ! -f "$1" ]]; then
    echo "Usage: $0 /absolute/path/to/game.z64" >&2
    exit 1
fi
rom_path=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")/../.."
mkdir -p build/dc/game-disc
cp "$rom_path" build/dc/game-disc/game.z64
sdk_image=${NOT64_DC_SDK_IMAGE:-sha256:7ce19827478d70e75f5180b2c238b947b6d9f598c514ed56d5ad364bfd0fb4ab}
docker run --rm --entrypoint /bin/bash -v "$PWD:/workspace" -w /workspace "$sdk_image" -c '
set -e
source /opt/toolchains/dc/kos/environ.sh >/dev/null
make -f Makefile.dc VIDEO=pvr GFX=soft GAME_DISC=1 -j4
/opt/toolchains/dc/mkdcdisc/build/mkdcdisc \
    -e not64-dc-pvr-soft-game.elf -D build/dc/game-disc -N \
    -n "Not64 Game Test" -o build/dc/not64-game.cdi --allow-overwrite
'
echo "Created $PWD/build/dc/not64-game.cdi"
