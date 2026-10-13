#!/bin/sh
# Builds the M6/M7 flash image docs/private/ref/m6_boot800k.po (git-ignored: it holds Apple's
# ProDOS). ProDOS 2.4.1 boot blocks + PRODOS + BASIC.SYSTEM from the PicoPort image, plus
# SPCOMPARE (M7 comparison program). Then rebuild the firmware (CMake links the image).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../..
ac="java -jar $HOME/Development/AppleCommander-ac-13.0.jar"
src=${PRODOS_SRC:-$HOME/Development/project_byte_hamr/software/SMARTPORT/images/ProDOS_2_4_1.po}
out=$root/docs/private/ref/m6_boot800k.po
rm -f "$out"
$ac create --prodos -s 800k -d "$out" -n M6BOOT -f "$src"
$ac import -d "$out" --basic -n SPCOMPARE "$here/SPCOMPARE.bas"
$ac list -d "$out"
