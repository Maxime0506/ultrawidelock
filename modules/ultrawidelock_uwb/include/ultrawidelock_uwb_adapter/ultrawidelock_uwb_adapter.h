/* SPDX-License-Identifier: ISC */

/** @file ultrawidelock_uwb_adapter.h — reader-device public interface. */

#pragma once

#include <cherry/cherry.h>
#include <cherry/cherry_ccc.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of preferred hopping configurations the reader may list. */
#define ULTRAWIDELOCK_UWB_ADAPTER_PREFERRED_HOP_CONFIG_MAX 3

/** Opaque adapter context (session-independent reader state). */
struct ultrawidelock_uwb_adapter;

/** Status codes returned by the credential UWB adapter. */
enum ultrawidelock_uwb_err {
	ULTRAWIDELOCK_UWB_ERR_NONE,
	ULTRAWIDELOCK_UWB_ERR_INVALID_PARAMETER,
	ULTRAWIDELOCK_UWB_ERR_UWBS_TIMEOUT,
	ULTRAWIDELOCK_UWB_ERR_INTERNAL,
	ULTRAWIDELOCK_UWB_ERR_SESSION_INIT,
	ULTRAWIDELOCK_UWB_ERR_SESSION_ACTIVE,
	ULTRAWIDELOCK_UWB_ERR_SESSION_CONFIG,
	ULTRAWIDELOCK_UWB_ERR_MESSAGE_UNSUPPORTED,
	ULTRAWIDELOCK_UWB_ERR_MESSAGE_STATE,
	ULTRAWIDELOCK_UWB_ERR_INVALID_STATE,
	ULTRAWIDELOCK_UWB_ERR_MSG_MALFORMED,
};

/** Reader-preferred hopping configuration. */
enum ultrawidelock_hopping_config {
	ULTRAWIDELOCK_HOPPING_CONFIG_DISABLED = CHERRY_CCC_HOPPING_MODE_DISABLE,
	ULTRAWIDELOCK_HOPPING_CONFIG_CONTINUOUS_DEFAULT = CHERRY_CCC_HOPPING_MODE_CONTINUOUS_DEFAULT,
	ULTRAWIDELOCK_HOPPING_CONFIG_ADAPTIVE_DEFAULT = CHERRY_CCC_HOPPING_MODE_ADAPTATIVE_DEFAULT,
};

/**
 * @brief Ordered hopping preferences (at least one default sequence required).
 */
struct ultrawidelock_uwb_preferred_hopping_configs {
	enum ultrawidelock_hopping_config configs[ULTRAWIDELOCK_UWB_ADAPTER_PREFERRED_HOP_CONFIG_MAX];
	size_t count;
};

/**
 * @brief Reader-side selection preferences (borrowed for the adapter's lifetime).
 */
struct ultrawidelock_uwb_adapter_reader_config {
	/** Lower bound on the selected RAN multiplier (T_Block = N x 96 ms). */
	uint8_t min_ran_multiplier;
	/**
	 * @brief Ordered preferred hopping configurations.
	 */
	struct ultrawidelock_uwb_preferred_hopping_configs preferred_hopping_configs;
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
struct ultrawidelock_uwb_adapter *
ultrawidelock_uwb_adapter_create_reader(struct cherry *cherry_ctx,
				struct cherry_core_event_device_capabilities *caps,
				struct ultrawidelock_uwb_adapter_reader_config *config);

/**
 * @brief Set the diagnostics configuration applied to new sessions.
 * @param ultrawidelock_ctx Adapter context to update.
 * @param config Diagnostic configuration to apply for CCC reporting.
 */
void ultrawidelock_uwb_adapter_set_diagnostics(struct ultrawidelock_uwb_adapter *ultrawidelock_ctx,
				       struct cherry_common_diag_cfg config);

/**
 * @brief Release an adapter context.
 * @param ultrawidelock_ctx Adapter context to release.
 */
void ultrawidelock_uwb_adapter_destroy(struct ultrawidelock_uwb_adapter *ultrawidelock_ctx);

#ifdef __cplusplus
}
#endif
