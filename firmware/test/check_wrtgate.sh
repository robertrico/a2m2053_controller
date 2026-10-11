#!/bin/sh
# /WRTGATE low erases the track under the head. Allowed references: the define in pins.h,
# the idle table and the ADDR_MASK static_assert in hw.c, and flux_tx.c (the only writer, M5).
cd "$(dirname "$0")/../src" || exit 1
hits=$(grep -n "PIN_MECH_WRTGATE" ./*.c ./*.h | grep -v "^./pins.h:" | grep -v "^./flux_tx.c:" | grep -v "^./hw.c:.*{PIN_MECH_WRTGATE, 1}" | grep -v "^./hw.c:.*_Static_assert((ADDR_MASK & (1u << PIN_MECH_WRTGATE))")
if [ -n "$hits" ]; then
    echo "FAIL: /WRTGATE referenced outside pins.h, the hw.c idle table and flux_tx.c:"
    echo "$hits"
    exit 1
fi
if [ ! -f ./flux_tx.pio ] || ! grep -q "set pins, 1" ./flux_tx.pio; then
    echo "FAIL: flux_tx.pio must raise /WRTGATE itself (set pins, 1)"
    exit 1
fi
echo "check_wrtgate: ok"
