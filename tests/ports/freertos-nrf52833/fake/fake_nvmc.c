#include <hal/nrf_nvmc.h>

#include <string.h>

fake_nvmc_t fake_nvmc;
uint8_t fake_nvmc_flash[FAKE_NVMC_FLASH_SIZE];

void fake_nvmc_reset(void)
{
	memset(&fake_nvmc, 0, sizeof(fake_nvmc));
	fake_nvmc.mode = NRF_NVMC_MODE_READONLY;
	fake_nvmc.ready = true;
	memset(fake_nvmc_flash, 0xff, sizeof(fake_nvmc_flash));
}

void nrf_nvmc_mode_set(NRF_NVMC_Type *p_reg, nrf_nvmc_mode_t mode)
{
	p_reg->mode = mode;
	p_reg->mode_changes++;
}

bool nrf_nvmc_ready_check(const NRF_NVMC_Type *p_reg)
{
	return p_reg->ready;
}

void nrf_nvmc_word_write(uint32_t address, uint32_t value)
{
	uint32_t existing;

	if (fake_nvmc.mode != NRF_NVMC_MODE_WRITE) {
		/* The controller ignores a write outside write mode. */
		fake_nvmc.violations++;
		return;
	}
	if ((address % 4u) != 0u || address + 4u > FAKE_NVMC_FLASH_SIZE) {
		fake_nvmc.violations++;
		return;
	}

	memcpy(&existing, &fake_nvmc_flash[address], sizeof(existing));
	if ((value & ~existing) != 0u) {
		/* A write that would set a bit needs an erase first. */
		fake_nvmc.violations++;
		return;
	}

	existing &= value;
	memcpy(&fake_nvmc_flash[address], &existing, sizeof(existing));
	fake_nvmc.word_writes++;
}

void nrf_nvmc_page_erase_start(NRF_NVMC_Type *p_reg, uint32_t address)
{
	if (p_reg->mode != NRF_NVMC_MODE_ERASE) {
		p_reg->violations++;
		return;
	}
	if ((address % FAKE_NVMC_PAGE_SIZE) != 0u ||
	    address + FAKE_NVMC_PAGE_SIZE > FAKE_NVMC_FLASH_SIZE) {
		p_reg->violations++;
		return;
	}

	memset(&fake_nvmc_flash[address], 0xff, FAKE_NVMC_PAGE_SIZE);
	p_reg->page_erases++;
}
