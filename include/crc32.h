#ifndef OMCI_HOOK_CRC32_H
#define OMCI_HOOK_CRC32_H

#include <stdint.h>
#include <stddef.h>

uint32_t crc32itu(const uint8_t *data, size_t len);

#endif
