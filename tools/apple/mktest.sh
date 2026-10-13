#!/bin/sh
# M7 sentinel test disk: docs/private/ref/m7_test800k.po (git-ignored: it holds Apple's ProDOS).
# Write it to a floppy with ADTPro through the working UniDisk, then boot it through our board.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../..
ac="java -jar $HOME/Development/AppleCommander-ac-13.0.jar"
src=${PRODOS_SRC:-$HOME/Development/project_byte_hamr/software/SMARTPORT/images/ProDOS_2_4_1.po}
work=${TMPDIR:-/tmp}/m7test.$$
out=$root/docs/private/ref/m7_test800k.po
python3 "$here/mktest.py" "$work"
rm -f "$out"
$ac create --prodos -s 800k -d "$out" -n M7TEST -f "$src"
$ac import -d "$out" --basic -n STARTUP "$work/STARTUP.bas"
$ac import -d "$out" --basic -n SPCOMPARE "$here/SPCOMPARE.bas"
for f in DATA1 DATA2 DATA3; do $ac import -d "$out" --raw -t BIN -a 0x4000 -n $f "$work/$f"; done
$ac import -d "$out" --raw -t BIN -a 0x4000 -n FILLER "$work/FILLER"
$ac import -d "$out" --raw -t BIN -a 0x4000 -n DATA4 "$work/DATA4"
$ac list -d "$out"
rm -rf "$work"
