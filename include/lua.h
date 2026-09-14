#ifndef OMCI_HOOK_LUA_H
#define OMCI_HOOK_LUA_H

#include <stdint.h>
#include <stdbool.h>

void lua_attach(void);

enum msg_result {
	MSG_DROP = -1,
	MSG_PASS =  0,
	MSG_EDIT =  1,
};

enum msg_result lua_call_on_rx(const uint8_t *msg, uint16_t len,
				uint8_t *out_msg, uint16_t *out_len);

enum msg_result lua_call_on_tx(const uint8_t *msg, uint16_t len,
				uint8_t *out_msg, uint16_t *out_len);

void lua_call_on_reset(void);

void lua_call_on_reboot(void);

void lua_detach(void);

#endif
