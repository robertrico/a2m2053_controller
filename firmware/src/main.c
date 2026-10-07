#include <stdio.h>
#include "pico/stdlib.h"
#include "hw.h"

int main(void) {
    hw_init_safe();
    stdio_init_all();
    for (;;) {
        int ch = getchar_timeout_us(100000);
        if (ch == '\n' || ch == '\r') puts("OK a2m_bringup (scaffold)");
    }
}
