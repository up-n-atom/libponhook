#ifndef OMCI_HOOK_HOOK_H
#define OMCI_HOOK_HOOK_H

#include <stdint.h>

int hook_send(const uint8_t *msg, uint16_t len);

void hook_log_prn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void hook_log_wrn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void hook_log_err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
