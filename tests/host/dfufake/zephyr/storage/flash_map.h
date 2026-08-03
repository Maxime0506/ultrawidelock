/* dfufake: minimal <zephyr/storage/flash_map.h> over RAM partitions.
 *
 * The signatures are Zephyr's. The semantics are Zephyr's on nRF in the two
 * respects woz_dfu was written around: a write must start on a word boundary
 * and be a whole number of words, an erase must start on a page boundary and
 * be a whole number of pages. Violating either returns -EINVAL here, the same
 * way the nrf flash driver rejects it, so the applier's write combiner and its
 * ROUND_UP on erase are actually load-bearing in these tests. */
#ifndef DFUFAKE_ZEPHYR_STORAGE_FLASH_MAP_H
#define DFUFAKE_ZEPHYR_STORAGE_FLASH_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/** Zephyr's flash-area descriptor, trimmed to the fields woz_dfu reads. */
struct flash_area {
	uint8_t fa_id;
	off_t fa_off;
	size_t fa_size;
};

int flash_area_open(uint8_t id, const struct flash_area **fa);
void flash_area_close(const struct flash_area *fa);
int flash_area_read(const struct flash_area *fa, off_t off, void *dst, size_t len);
int flash_area_write(const struct flash_area *fa, off_t off, const void *src, size_t len);
int flash_area_erase(const struct flash_area *fa, off_t off, size_t len);

#endif /* DFUFAKE_ZEPHYR_STORAGE_FLASH_MAP_H */
