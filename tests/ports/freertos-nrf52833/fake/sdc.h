/* Subset of the pinned nrfxlib sdc.h. */
#ifndef TEST_SDC_H
#define TEST_SDC_H

#include <stdint.h>

#define SDC_DEFAULT_RESOURCE_CFG_TAG 0

enum sdc_cfg_type {
	SDC_CFG_TYPE_NONE = 0,
	SDC_CFG_TYPE_CENTRAL_COUNT,
	SDC_CFG_TYPE_PERIPHERAL_COUNT,
	SDC_CFG_TYPE_BUFFER_CFG,
	SDC_CFG_TYPE_ADV_COUNT,
};

typedef struct {
	uint8_t count;
} sdc_cfg_role_count_t;

typedef struct {
	uint16_t tx_packet_size;
	uint16_t rx_packet_size;
	uint8_t tx_packet_count;
	uint8_t rx_packet_count;
} sdc_cfg_buffer_cfg_t;

typedef union {
	sdc_cfg_role_count_t central_count;
	sdc_cfg_role_count_t peripheral_count;
	sdc_cfg_buffer_cfg_t buffer_cfg;
	sdc_cfg_role_count_t adv_count;
} sdc_cfg_t;

typedef void (*sdc_fault_handler_t)(const char *file, const uint32_t line);
typedef void (*sdc_callback_t)(void);

int32_t sdc_init(sdc_fault_handler_t fault_handler);
int32_t sdc_cfg_set(uint8_t config_tag, uint8_t config_type, sdc_cfg_t const *p_resource_cfg);
int32_t sdc_enable(sdc_callback_t callback, uint8_t *p_mem);
int32_t sdc_disable(void);
int32_t sdc_support_helper(void (*sdc_support_func)(void));

void sdc_support_adv(void);
void sdc_support_peripheral(void);
void sdc_support_dle_peripheral(void);
void sdc_support_le_2m_phy(void);
void sdc_support_phy_update_peripheral(void);

#endif /* TEST_SDC_H */
