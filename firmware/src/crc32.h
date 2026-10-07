#pragma once
#include <stddef.h>
#include <stdint.h>
// zlib-compatible CRC-32. Start with crc = 0; chain calls for streaming.
uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n);
