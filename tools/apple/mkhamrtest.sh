#!/bin/sh
# Chain test floppy: assembles hamrtest.s to tools/apple/HAMRTEST.SYSTEM and builds
# docs/private/ref/hamrtest140k.po (git-ignored: it holds Apple's ProDOS), a bootable
# 5.25 image with PRODOS and HAMRTEST.SYSTEM as the only system file, so it runs at boot.
# Write the image to a 5.25 floppy with ADTPro. The floppy must stay write-enabled:
# the test keeps a scratch file on it for a few seconds.
# Needs ACME (https://sourceforge.net/projects/acme-crossass/): ACME=/path/to/acme in
# .env.local or the environment, or acme on PATH.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../..
. "$here/env.sh"
out=$root/docs/private/ref/hamrtest140k.po
"${ACME:-acme}" -f plain -o "$here/HAMRTEST.SYSTEM" "$here/hamrtest.s"
rm -f "$out"
$ac create --prodos -s 140k -d "$out" -n HAMRTEST -f "$src"
$ac rm -f -d "$out" BASIC.SYSTEM
$ac import -d "$out" --raw -t SYS -a 0x2000 -n HAMRTEST.SYSTEM "$here/HAMRTEST.SYSTEM"
$ac list -d "$out"
