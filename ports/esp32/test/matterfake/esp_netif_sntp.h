/* matterfake esp_netif_sntp.h — records the sync callback so the test can
 * fire it (mfk_sntp_cb). */
#ifndef MATTERFAKE_ESP_NETIF_SNTP_H
#define MATTERFAKE_ESP_NETIF_SNTP_H

#include <sys/time.h>

#include "esp_err.h"

typedef struct {
	const char *server;
	void (*sync_cb)(struct timeval *tv);
} esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(srv)                                     \
	{                                                                      \
		.server = (srv), .sync_cb = 0                                  \
	}

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *cfg);
const char *esp_err_to_name(esp_err_t err);

#endif /* MATTERFAKE_ESP_NETIF_SNTP_H */
