/* sdkfake esp_attr.h — placement attributes are meaningless on host. */
#ifndef SDKFAKE_ESP_ATTR_H
#define SDKFAKE_ESP_ATTR_H

#define IRAM_ATTR
#define DRAM_ATTR
#define WORD_ALIGNED_ATTR _Alignas(4)

#endif
