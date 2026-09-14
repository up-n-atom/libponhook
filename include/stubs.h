#ifndef OMCI_HOOK_STUBS_H
#define OMCI_HOOK_STUBS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

enum pon_adapter_errno {
	PON_ADAPTER_EAGAIN			= 1,
	PON_ADAPTER_SUCCESS			= 0,
	PON_ADAPTER_ERROR			= -1,
	PON_ADAPTER_ERR_NOT_FOUND		= -2,
	PON_ADAPTER_ERR_NOT_AVAIL		= -3,
	PON_ADAPTER_ERR_NO_MEMORY		= -4,
	PON_ADAPTER_ERR_NOT_SUPPORTED		= -5,
	PON_ADAPTER_ERR_NO_DATA		= -6,
	PON_ADAPTER_ERR_CONFIG_MISMATCH	= -7,
	PON_ADAPTER_ERR_RESOURCE_EXISTS	= -8,
	PON_ADAPTER_ERR_RESOURCE_NOT_AVAIL	= -9,
	PON_ADAPTER_ERR_RESOURCE_NOT_FOUND	= -10,
	PON_ADAPTER_ERR_MATCH_NOT_FOUND	= -11,
	PON_ADAPTER_ERR_INVALID_VAL		= -12,
	PON_ADAPTER_ERR_DRV			= -13,
	PON_ADAPTER_ERR_OMCI_MSG_FIFO_FULL	= -14,
	PON_ADAPTER_ERR_OMCI_ME_INVALID	= -15,
	PON_ADAPTER_ERR_OMCI_ME_NOT_FOUND	= -16,
	PON_ADAPTER_ERR_OMCI_ME_EXISTS		= -17,
	PON_ADAPTER_ERR_OMCI_ACTION		= -18,
	PON_ADAPTER_ERR_OMCI_ME_NOT_SUPPORTED	= -19,
	PON_ADAPTER_ERR_OMCI_ME_ATTR_INVALID	= -20,
	PON_ADAPTER_ERR_OMCI_ME_ACTION_INVALID	= -21,
	PON_ADAPTER_ERR_LOCKING		= -22,
	PON_ADAPTER_ERR_PTR_INVALID		= -23,
	PON_ADAPTER_ERR_OUT_OF_BOUNDS		= -24,
	PON_ADAPTER_ERR_IF_NOT_FOUND		= -25,
	PON_ADAPTER_ERR_SIZE			= -26,
	PON_ADAPTER_ERR_CRC			= -27,
	PON_ADAPTER_ERR_MEM_ACCESS		= -28,
};

struct pa_omci_ik {
	uint8_t key[16];
};

struct pa_config;

struct pa_eh_ops {
	void (*alarm)(void *caller, uint16_t class_id, uint16_t instance_id,
		      unsigned int alarm, bool active);
	void (*optic_alarm)(void *caller, int alarm, bool active);
	void (*omci_msg)(void *caller, const uint8_t *msg, const uint16_t len);
	void (*ploam_state)(void *caller, int prev_state, int curr_state);
	void (*interval_end)(void *caller, uint8_t interval_end_time);
	void (*link_state)(void *caller, uint16_t instance_id, bool state,
			    uint8_t config_ind);
	void (*net_state)(void *caller, const char *iface_name,
			   const bool iface_up);
	void (*link_init)(void *caller, uint16_t instance_id,
			   bool is_initialized);
	void (*loop_detect)(void *caller, uint16_t instance_id);
	void (*auth_result_rdy)(void *caller, const uint8_t *onu_auth_result,
				 const uint16_t len);
	void (*auth_status_chg)(void *caller, uint32_t status);
	void (*omci_ik_update)(void *caller, const struct pa_omci_ik *omci_ik);
	void (*mib_reset)(void *caller);
	void (*epon_link)(void *caller, uint32_t link_index, uint32_t llid,
			   uint32_t status, uint32_t status_prev);
	void (*epon_state)(void *caller, uint32_t prev_counter,
			    uint32_t state_act, uint32_t state_prev);
};

struct pa_system_ops {
	enum pon_adapter_errno (*init)(char const *const *init_data,
					const struct pa_config *config,
					const struct pa_eh_ops *event_handler,
					void *caller_ctx, void **ll_handle);
	enum pon_adapter_errno (*start)(void *ll_handle);
	enum pon_adapter_errno (*reboot)(void *ll_handle, time_t timeout_ms);
	enum pon_adapter_errno (*shutdown)(void *ll_handle);
};

struct pa_msg_ops {
	enum pon_adapter_errno (*msg_rx_cb_register)(
		void *ll_handle,
		enum pon_adapter_errno (*receive_callback)(void *hl_handle,
							    const uint8_t *msg,
							    const uint16_t len,
							    const uint32_t *crc),
		void *hl_handle);
	enum pon_adapter_errno (*msg_send)(void *ll_handle,
					    const uint8_t *msg,
					    const uint16_t len,
					    const uint32_t *crc);
};

struct pa_omci_mib_ops {
	enum pon_adapter_errno (*cleanup)(void *ll_handle);
};

enum pa_dbg_lvl {
	PA_DBG_MSG = 0,
	PA_DBG_PRN = 1,
	PA_DBG_WRN = 2,
	PA_DBG_ERR = 3,
	PA_DBG_OFF = 4,
};

struct pa_ll_dbg_lvl_ops {
	void (*set)(const uint8_t level);
	uint8_t (*get)(void);
};

struct pa_ops {
	const struct pa_system_ops *system_ops;
	const void *sys_cap_ops;
	const void *sys_sts_ops;
	const void *integrity_ops;
	const struct pa_msg_ops *msg_ops;
	const void *omci_mib_ops;
	const void *omci_me_ops;
	const void *omci_mcc_ops;
	const void *omci_meter_ops;
	const void *epon_ops;
	const void *vlan_flow_ops;
	const struct pa_ll_dbg_lvl_ops *dbg_lvl_ops;
};

struct pa_system_status_ops_stub {
	int (*get_pon_op_mode)(void *ll_handle);
};

enum pa_pon_op_mode {
	PA_PON_MODE_UNKNOWN = 0,
	PA_PON_MODE_G984,
	PA_PON_MODE_G987,
	PA_PON_MODE_G989,
	PA_PON_MODE_G9807,
	PA_PON_MODE_IEEE_1GEPON,
	PA_PON_MODE_IEEE_10_1GEPON,
	PA_PON_MODE_IEEE_10GEPON,
};

#endif
