#include <hal/nrf_nvmc.h>

#include <string.h>

/* Defined at the bottom, next to the partial-erase model it clears. */
void fake_nvmc_partial_reset(void);

fake_nvmc_t fake_nvmc;
uint8_t fake_nvmc_flash[FAKE_NVMC_FLASH_SIZE];

void fake_nvmc_reset(void)
{
	memset(&fake_nvmc, 0, sizeof(fake_nvmc));
	fake_nvmc.mode = NRF_NVMC_MODE_READONLY;
	fake_nvmc.ready = true;
	memset(fake_nvmc_flash, 0xff, sizeof(fake_nvmc_flash));
	fake_nvmc_partial_reset();
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

/* The part's own page erase time, which partial erase has to add up to. */
#define FAKE_NVMC_PAGE_ERASE_TOTAL_US 89700u

static unsigned s_slices[FAKE_NVMC_FLASH_SIZE / FAKE_NVMC_PAGE_SIZE];

void fake_nvmc_partial_reset(void)
{
	memset(s_slices, 0, sizeof(s_slices));
}

void nrf_nvmc_partial_erase_duration_set(NRF_NVMC_Type *p_reg, uint32_t duration_ms)
{
	/* The controller requires at least two milliseconds. */
	if (duration_ms < 2u) {
		p_reg->violations++;
		return;
	}
	p_reg->partial_duration_ms = duration_ms;
}

uint32_t nrf_nvmc_partial_erase_duration_get(const NRF_NVMC_Type *p_reg)
{
	return p_reg->partial_duration_ms;
}

static unsigned slices_required(void)
{
	uint32_t slice_us = fake_nvmc.partial_duration_ms * 1000u;

	if (slice_us == 0u) {
		return 0;
	}
	return (unsigned)((FAKE_NVMC_PAGE_ERASE_TOTAL_US + slice_us - 1u) / slice_us);
}

unsigned fake_nvmc_slices_remaining(uint32_t address)
{
	unsigned page = address / FAKE_NVMC_PAGE_SIZE;
	unsigned need = slices_required();

	if (page >= (FAKE_NVMC_FLASH_SIZE / FAKE_NVMC_PAGE_SIZE) || s_slices[page] >= need) {
		return 0;
	}
	return need - s_slices[page];
}

void nrf_nvmc_page_partial_erase_start(NRF_NVMC_Type *p_reg, uint32_t address)
{
	unsigned page = address / FAKE_NVMC_PAGE_SIZE;

	/*
	 * Which mode a partial erase must be started from is a property of the
	 * part, not of this model. nRF52833 has the partial-erase registers but
	 * no partial-erase WEN mode, so it slices from the ordinary erase mode;
	 * the nRF52840 and nRF91 have a mode of their own and refuse anything
	 * else. Following the HAL's capability symbol keeps the model as strict
	 * as the hardware it is standing in for, rather than accepting both.
	 */
#if NRF_NVMC_HAS_PARTIAL_ERASE_MODE
	if (p_reg->mode != NRF_NVMC_MODE_PARTIAL_ERASE) {
#else
	if (p_reg->mode != NRF_NVMC_MODE_ERASE) {
#endif
		p_reg->violations++;
		return;
	}
	if ((address % FAKE_NVMC_PAGE_SIZE) != 0u ||
	    address + FAKE_NVMC_PAGE_SIZE > FAKE_NVMC_FLASH_SIZE) {
		p_reg->violations++;
		return;
	}
	if (p_reg->partial_duration_ms == 0u) {
		/* Slicing without a slice length erases nothing on the part. */
		p_reg->violations++;
		return;
	}

	p_reg->partial_slices++;
	s_slices[page]++;
	if (s_slices[page] >= slices_required()) {
		memset(&fake_nvmc_flash[address], 0xff, FAKE_NVMC_PAGE_SIZE);
		s_slices[page] = 0;
		p_reg->page_erases++;
	}
}
