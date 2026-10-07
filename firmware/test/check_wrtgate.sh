#!/bin/sh
# M0-M3: /WRTGATE must stay high. Only allowed reference: the idle table in hw.c.
cd "$(dirname "$0")/../src" || exit 1
hits=$(grep -n "PIN_MECH_WRTGATE" ./*.c | grep -v "^./hw.c:.*{PIN_MECH_WRTGATE, 1}")
if [ -n "$hits" ]; then
    echo "FAIL: /WRTGATE referenced outside the hw.c idle table:"
    echo "$hits"
    exit 1
fi
echo "check_wrtgate: ok"
