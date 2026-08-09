/** @file aliro_uwb_adapter.h — reader-device public interface. */

#pragma once

#include <cherry/cherry.h>
#include <cherry/cherry_ccc.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of preferred hopping configurations the reader may list. */
#define ALIRO_UWB_ADAPTER_PREFERRED_HOP_CONFIG_MAX 3

/** Opaque adapter context (session-independent reader state). */
struct aliro_uwb_adapter;

/** Status codes returned by the Aliro UWB adapter. */
enum aliro_uwb_err {
	ALIRO_UWB_ERR_NONE,
	ALIRO_UWB_ERR_INVALID_PARAMETER,
	ALIRO_UWB_ERR_UWBS_TIMEOUT,
	ALIRO_UWB_ERR_INTERNAL,
	ALIRO_UWB_ERR_SESSION_INIT,
	ALIRO_UWB_ERR_SESSION_ACTIVE,
	ALIRO_UWB_ERR_SESSION_CONFIG,
	ALIRO_UWB_ERR_MESSAGE_UNSUPPORTED,
	ALIRO_UWB_ERR_MESSAGE_STATE,
	ALIRO_UWB_ERR_INVALID_STATE,
	ALIRO_UWB_ERR_MSG_MALFORMED,
};

/** Reader-preferred hopping configuration. */
enum aliro_hopping_config {
	ALIRO_HOPPING_CONFIG_DISABLED = CHERRY_CCC_HOPPING_MODE_DISABLE,
	ALIRO_HOPPING_CONFIG_CONTINUOUS_DEFAULT = CHERRY_CCC_HOPPING_MODE_CONTINUOUS_DEFAULT,
	ALIRO_HOPPING_CONFIG_ADAPTIVE_DEFAULT = CHERRY_CCC_HOPPING_MODE_ADAPTATIVE_DEFAULT,
};

/**
 * @brief Ordered hopping preferences (at least one default sequence required).
 */
struct aliro_uwb_preferred_hopping_configs {
	enum aliro_hopping_config configs[ALIRO_UWB_ADAPTER_PREFERRED_HOP_CONFIG_MAX];
	size_t count;
};

/**
 * @brief Reader-side selection preferences (borrowed for the adapter's lifetime).
 */
struct aliro_uwb_adapter_reader_config {
	/** Lower bound on the selected RAN multiplier (T_Block = N x 96 ms). */
	uint8_t min_ran_multiplier;
	/**
	 * @brief Ordered preferred hopping configurations.
	 */
	struct aliro_uwb_preferred_hopping_configs preferred_hopping_configs;
	/** MAC mode: b0-b5 round offset, b6-b7 number of ranging rounds. */
	uint8_t mac_mode;
	/** {Tx, Rx} antenna sets for the first ranging round. */
	uint8_t r1_antennas[2];
	/** {Tx, Rx} antenna sets for the second ranging round (MAC mode 1). */
	uint8_t r2_antennas[2];
};

/**
 * @brief Create a reader-mode adapter.
 * @param cherry_ctx Cherry library context to bind the new reader adapter to.
 * @param caps Device capabilities to advertise during CCC discovery.
 * @param config Reader adapter configuration, borrowed for the adapter's lifetime.
 * @return New adapter, or NULL on bad parameters or allocation failure.
 */
struct aliro_uwb_adapter *
aliro_uwb_adapter_create_reader(struct cherry *cherry_ctx,
				struct cherry_core_event_device_capabilities *caps,
				struct aliro_uwb_adapter_reader_config *config);

/**
 * @brief Set the diagnostics configuration applied to new sessions.
 * @param aliro_ctx Adapter context to update.
 * @param config Diagnostic configuration to apply for CCC reporting.
 */
void aliro_uwb_adapter_set_diagnostics(struct aliro_uwb_adapter *aliro_ctx,
				       struct cherry_common_diag_cfg config);

/**
 * @brief Release an adapter context.
 * @param aliro_ctx Adapter context to release.
 */
void aliro_uwb_adapter_destroy(struct aliro_uwb_adapter *aliro_ctx);

#ifdef __cplusplus
}
#endif
