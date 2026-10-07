#include <string.h>
#include "test.h"
#include "crc32.h"

int main(void) {
    const char *s = "123456789";
    CHECK(crc32_update(0, (const uint8_t *)s, 9) == 0xCBF43926u);
    CHECK(crc32_update(0, (const uint8_t *)s, 0) == 0);
    uint32_t c = crc32_update(0, (const uint8_t *)s, 4);
    c = crc32_update(c, (const uint8_t *)s + 4, 5);
    CHECK(c == 0xCBF43926u);
    TEST_END();
}
