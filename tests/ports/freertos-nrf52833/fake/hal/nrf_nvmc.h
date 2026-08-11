/*
 * Register-level model of the nRF52 non-volatile memory controller, matching
 * the pinned hal_nordic hal/nrf_nvmc.h surface the port uses.
 *
 * The model enforces the controller's rules rather than merely storing bytes:
 * a word write outside write mode is refused, a page erase outside erase mode
 * is refused, a write may only clear bits, a word write is word-aligned and a
 * page erase is page-aligned. A driver that gets any of those wrong works
 * against a permissive model and corrupts flash on the board.
 */
#ifndef TEST_HAL_NRF_NVMC_H
#define TEST_HAL_NRF_NVMC_H

#include <stdbool.h>
#include <stdint.h>

#define FAKE_NVMC_FLASH_SIZE (512u * 1024u)
#define FAKE_NVMC_PAGE_SIZE 4096u

typedef enum {
	NRF_NVMC_MODE_READONLY = 0,
	NRF_NVMC_MODE_WRITE = 1,
	NRF_NVMC_MODE_ERASE = 2,
	NRF_NVMC_MODE_PARTIAL_ERASE = 3,
} nrf_nvmc_mode_t;

typedef struct {
	nrf_nvmc_mode_t mode;
	bool ready;
	unsigned word_writes;
	unsigned page_erases;
	unsigned partial_slices;
	uint32_t partial_duration_ms;
	unsigned mode_changes;
	/* Operations the controller would have refused, or that break a rule. */
	unsigned violations;
} fake_nvmc_t;

extern fake_nvmc_t fake_nvmc;
#define NRF_NVMC (&fake_nvmc)

typedef fake_nvmc_t NRF_NVMC_Type;

/* The array the model programs. Reads are mapped straight onto it. */
extern uint8_t fake_nvmc_flash[FAKE_NVMC_FLASH_SIZE];

void nrf_nvmc_mode_set(NRF_NVMC_Type *p_reg, nrf_nvmc_mode_t mode);
bool nrf_nvmc_ready_check(const NRF_NVMC_Type *p_reg);
void nrf_nvmc_word_write(uint32_t address, uint32_t value);
void nrf_nvmc_page_erase_start(NRF_NVMC_Type *p_reg, uint32_t address);
/*
 * Partial erase. A page is only actually erased once enough slices have run to
 * cover the part's 89.7 ms erase time; until then it still reads as it was,
 * which is what makes an interrupted erase visible to a test.
 */
void nrf_nvmc_partial_erase_duration_set(NRF_NVMC_Type *p_reg, uint32_t duration_ms);
uint32_t nrf_nvmc_partial_erase_duration_get(const NRF_NVMC_Type *p_reg);
void nrf_nvmc_page_partial_erase_start(NRF_NVMC_Type *p_reg, uint32_t address);
/* Slices the model still wants before the page at this address comes up erased. */
unsigned fake_nvmc_slices_remaining(uint32_t address);

/* Test control. */
void fake_nvmc_reset(void);

#endif /* TEST_HAL_NRF_NVMC_H */
