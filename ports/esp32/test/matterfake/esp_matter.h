/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * matterfake esp_matter.h — the slice of the esp-matter data-model API the
 * matter-lock app uses, as recording doubles (matterfake.cc). See matterfake.h.
 */
#ifndef MATTERFAKE_ESP_MATTER_H
#define MATTERFAKE_ESP_MATTER_H

#include "esp_err.h"
#include "matterfake.h"

#define ENDPOINT_FLAG_NONE 0
#define CLUSTER_FLAG_SERVER 0x01
#define ATTRIBUTE_FLAG_WRITABLE 0x02
#define ATTRIBUTE_FLAG_NONVOLATILE 0x04

typedef struct esp_matter_attr_val {
	int type; /* 8 = bitmap8 */
	uint8_t b8;
} esp_matter_attr_val_t;

static inline esp_matter_attr_val_t esp_matter_bitmap8(uint8_t v)
{
	esp_matter_attr_val_t val;
	val.type = 8;
	val.b8 = v;
	return val;
}

namespace esp_matter {

typedef struct node_opaque node_t;
typedef struct endpoint_opaque endpoint_t;
typedef struct cluster_opaque cluster_t;
typedef struct attribute_h attribute_t;

namespace attribute {
enum callback_type_t { PRE_UPDATE, POST_UPDATE };
typedef esp_err_t (*callback_t)(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
				uint32_t attribute_id, esp_matter_attr_val_t *val,
				void *priv_data);
attribute_t *create(cluster_t *cluster, uint32_t attribute_id, uint8_t flags,
		    esp_matter_attr_val_t val);
} // namespace attribute

namespace identification {
enum callback_type_t { START, EFFECT, STOP };
typedef esp_err_t (*callback_t)(callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
				uint8_t effect_variant, void *priv_data);
} // namespace identification

namespace node {
typedef struct config {
} config_t;
node_t *create(config_t *config, attribute::callback_t attribute_cb,
	       identification::callback_t identification_cb, void *priv_data = nullptr);
} // namespace node

namespace cluster {

cluster_t *create(endpoint_t *endpoint, uint32_t cluster_id, uint8_t flags);
cluster_t *get(endpoint_t *endpoint, uint32_t cluster_id);

namespace global {
namespace attribute {
esp_matter::attribute_t *create_feature_map(cluster_t *cluster, uint32_t value);
esp_matter::attribute_t *create_cluster_revision(cluster_t *cluster, uint16_t value);
} // namespace attribute
} // namespace global

namespace door_lock {
namespace feature {
namespace credential_over_the_air_access {
typedef struct config {
} config_t;
esp_err_t add(cluster_t *cluster, config_t *config);
} // namespace credential_over_the_air_access
namespace pin_credential {
typedef struct config {
} config_t;
esp_err_t add(cluster_t *cluster, config_t *config);
} // namespace pin_credential
namespace user {
typedef struct config {
} config_t;
esp_err_t add(cluster_t *cluster, config_t *config);
} // namespace user
namespace aliro_provisioning {
esp_err_t add(cluster_t *cluster);
} // namespace aliro_provisioning
namespace aliro_bleuwb {
esp_err_t add(cluster_t *cluster);
} // namespace aliro_bleuwb
} // namespace feature
namespace attribute {
esp_matter::attribute_t *create_auto_relock_time(cluster_t *cluster, uint32_t value);
} // namespace attribute
} // namespace door_lock

} // namespace cluster

namespace endpoint {
namespace door_lock {
typedef struct config {
	struct {
		void *delegate = nullptr;
		uint8_t lock_state = 0;
	} door_lock;
} config_t;
endpoint_t *create(node_t *node, config_t *config, uint8_t flags, void *priv_data);
} // namespace door_lock
uint16_t get_id(endpoint_t *endpoint);
} // namespace endpoint

typedef void (*event_callback_t)(const ChipDeviceEvent *event, intptr_t arg);
esp_err_t start(event_callback_t callback);
void factory_reset();

} // namespace esp_matter

#endif /* MATTERFAKE_ESP_MATTER_H */
