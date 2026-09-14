#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "stubs.h"
#include "lua.h"
#include "hook.h"
#include "util.h"

static const struct pa_ops *ponnet_ops;
static void *ponnet_ll_handle;
static void *ponnet_handle;

static const struct pa_ll_dbg_lvl_ops *hook_dbg_ops;

static enum pon_adapter_errno (*omcid_rx_cb)(void *hl, const uint8_t *msg,
					    const uint16_t len,
					    const uint32_t *crc);
static void *omcid_hl_handle;

int hook_send(const uint8_t *msg, uint16_t len, uint32_t crc)
{
	if (!ponnet_ops || !ponnet_ops->msg_ops || !ponnet_ops->msg_ops->msg_send) {
		return -1;
	}
	return ponnet_ops->msg_ops->msg_send(ponnet_ll_handle, msg, len, &crc) ==
	       PON_ADAPTER_SUCCESS ? 0 : -1;
}

static void hook_vlog(uint8_t lvl, const char *fmt, va_list ap)
{
#ifndef DEBUG
	if (hook_dbg_ops && hook_dbg_ops->get && hook_dbg_ops->get() > lvl) {
		return;
	}
#else
	(void)lvl;
#endif
	fprintf(stderr, "[libponhook] ");
	vfprintf(stderr, fmt, ap);
}

void hook_log_prn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	hook_vlog(PA_DBG_PRN, fmt, ap);
	va_end(ap);
}

void hook_log_wrn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	hook_vlog(PA_DBG_WRN, fmt, ap);
	va_end(ap);
}

void hook_log_err(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	hook_vlog(PA_DBG_ERR, fmt, ap);
	va_end(ap);
}

static struct pa_eh_ops hook_eh_ops;
static void (*orig_ik_update)(void *caller, const struct pa_omci_ik *ik);

static void hook_ik_update(void *caller, const struct pa_omci_ik *ik)
{
	if (ik) {
		LOGW("OMCI-IK: secure mode live\n");
	}
	if (orig_ik_update) {
		orig_ik_update(caller, ik);
	}
}

static enum pon_adapter_errno hook_sys_init(char const *const *init_data,
					const struct pa_config *config,
					const struct pa_eh_ops *eh,
					void *caller_ctx, void **ll)
{
	if (!ponnet_ops || !ponnet_ops->system_ops || !ponnet_ops->system_ops->init) {
		return PON_ADAPTER_ERR_NOT_FOUND;
	}

	if (eh) {
		orig_ik_update = eh->omci_ik_update;
		hook_eh_ops = *eh;
		hook_eh_ops.omci_ik_update = hook_ik_update;
		eh = &hook_eh_ops;
	}
	return ponnet_ops->system_ops->init(init_data, config, eh, caller_ctx, ll);
}

static enum pon_adapter_errno hook_sys_reboot(void *ll, time_t timeout_ms)
{
	const struct pa_system_ops *ponnet_sys =
		ponnet_ops ? ponnet_ops->system_ops : NULL;

	lua_call_on_reboot();
	return ponnet_sys && ponnet_sys->reboot ?
		ponnet_sys->reboot(ll, timeout_ms) : PON_ADAPTER_SUCCESS;
}

static enum pon_adapter_errno hook_sys_shutdown(void *ll)
{
	const struct pa_system_ops *ponnet_sys =
		ponnet_ops ? ponnet_ops->system_ops : NULL;
	enum pon_adapter_errno rc = ponnet_sys && ponnet_sys->shutdown ?
		ponnet_sys->shutdown(ll) : PON_ADAPTER_SUCCESS;

	lua_detach();
	return rc;
}

static struct pa_system_ops hook_system_ops;

static enum pon_adapter_errno hook_rx(void *hl __attribute__((unused)),
				      const uint8_t *msg,
				      const uint16_t len,
				      const uint32_t *crc)
{
	uint8_t new_msg[OMCI_FRAME_MAX];
	uint16_t new_len = 0;
	uint32_t new_crc = 0;
	enum msg_result res;

	res = lua_call_on_rx(msg, len, new_msg, &new_len, &new_crc);
	switch (res) {
	case MSG_DROP:
		return PON_ADAPTER_SUCCESS;
	case MSG_EDIT:
		return omcid_rx_cb ?
			omcid_rx_cb(omcid_hl_handle, new_msg, new_len, &new_crc) :
			PON_ADAPTER_SUCCESS;
	case MSG_PASS:
	default:
		return omcid_rx_cb ? omcid_rx_cb(omcid_hl_handle, msg, len, crc) : PON_ADAPTER_SUCCESS;
	}
}

static enum pon_adapter_errno hook_msg_rx_cb_register(void *ll,
	enum pon_adapter_errno (*cb)(void *, const uint8_t *,
				     const uint16_t, const uint32_t *),
	void *hl_handle)
{
	omcid_rx_cb = cb;
	omcid_hl_handle = hl_handle;
	if (!ponnet_ops || !ponnet_ops->msg_ops || !ponnet_ops->msg_ops->msg_rx_cb_register) {
		return PON_ADAPTER_ERR_NOT_FOUND;
	}
	return ponnet_ops->msg_ops->msg_rx_cb_register(ll, hook_rx, NULL);
}

static enum pon_adapter_errno hook_msg_send(void *ll, const uint8_t *msg,
					const uint16_t len,
					const uint32_t *crc)
{
	uint8_t new_msg[OMCI_FRAME_MAX];
	uint16_t new_len = 0;
	uint32_t new_crc = 0;
	enum msg_result res;

	if (!ponnet_ops || !ponnet_ops->msg_ops || !ponnet_ops->msg_ops->msg_send) {
		return PON_ADAPTER_ERR_NOT_FOUND;
	}

	res = lua_call_on_tx(msg, len, new_msg, &new_len, &new_crc);
	switch (res) {
	case MSG_DROP:
		return PON_ADAPTER_SUCCESS;
	case MSG_EDIT:
		return ponnet_ops->msg_ops->msg_send(ll, new_msg, new_len, &new_crc);
	case MSG_PASS:
	default:
		return ponnet_ops->msg_ops->msg_send(ll, msg, len, crc);
	}
}

static const struct pa_msg_ops hook_msg_ops = {
	.msg_rx_cb_register = hook_msg_rx_cb_register,
	.msg_send           = hook_msg_send,
};

static enum pon_adapter_errno hook_mib_cleanup(void *ll)
{
	const struct pa_omci_mib_ops *ponnet_mib;

	lua_call_on_reset();

	if (ponnet_ops && ponnet_ops->omci_mib_ops) {
		ponnet_mib = (const struct pa_omci_mib_ops *)ponnet_ops->omci_mib_ops;
		if (ponnet_mib->cleanup) {
			return ponnet_mib->cleanup(ll);
		}
	}
	return PON_ADAPTER_SUCCESS;
}

static const struct pa_omci_mib_ops hook_mib_ops = {
	.cleanup = hook_mib_cleanup,
};

static struct pa_ops hook_pa_ops;

enum pon_adapter_errno libponhook_ll_register_ops(void *hl_handle,
						  const struct pa_ops **pa_ops,
						  void **ll_handle);

enum pon_adapter_errno libponhook_ll_register_ops(void *hl_handle,
						  const struct pa_ops **pa_ops,
						  void **ll_handle)
{
	enum pon_adapter_errno (*ponnet_register)(void *, const struct pa_ops **,
						  void **);

	ponnet_handle = dlopen("libponnet.so", RTLD_NOW | RTLD_GLOBAL);
	if (!ponnet_handle) {
		LOGE("dlopen libponnet.so failed: %s\n", dlerror());
		return PON_ADAPTER_ERR_NOT_FOUND;
	}

	DLSYM(ponnet_handle, ponnet_register, "libponnet_ll_register_ops");
	if (!ponnet_register) {
		LOGE("dlsym libponnet_ll_register_ops failed: %s\n", dlerror());
		dlclose(ponnet_handle);
		return PON_ADAPTER_ERR_NOT_FOUND;
	}
	if (ponnet_register(hl_handle, &ponnet_ops, &ponnet_ll_handle) != PON_ADAPTER_SUCCESS) {
		LOGE("libponnet register failed\n");
		return PON_ADAPTER_ERROR;
	}
	*ll_handle = ponnet_ll_handle;

	hook_dbg_ops = ponnet_ops->dbg_lvl_ops;

	/* copy all of libponnet's ops groups - override msg + system */
	hook_pa_ops = *ponnet_ops;
	hook_pa_ops.msg_ops = &hook_msg_ops;

	hook_system_ops = ponnet_ops->system_ops ?
		*ponnet_ops->system_ops : (struct pa_system_ops){ 0 };
	hook_system_ops.init     = hook_sys_init;
	hook_system_ops.reboot   = hook_sys_reboot;
	hook_system_ops.shutdown = hook_sys_shutdown;
	hook_pa_ops.system_ops = &hook_system_ops;
	hook_pa_ops.omci_mib_ops = &hook_mib_ops;

	lua_attach();

	*pa_ops = &hook_pa_ops;

	LOG("registered\n");
	return PON_ADAPTER_SUCCESS;
}
