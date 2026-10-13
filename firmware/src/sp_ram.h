#pragma once
// Hot SmartPort path (receive, decode, ACK) runs from RAM on the RP2350: after a quiet spell
// the flash cache is cold, and the INIT after a bus reset was ACKed 31 us after /WRREQ rose
// (M6 bench, 2026-10-12). Host test builds leave the functions alone.
#if defined(__arm__)
#include "pico.h"
#define SP_RAM(name) __not_in_flash_func(name)
#else
#define SP_RAM(name) name
#endif
