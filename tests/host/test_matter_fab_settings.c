/*
 * Host test for firmware/src/matter_fab_settings.c — the FIRST host
 * coverage any file under firmware has had.
 *
 * That gap is why this exists. Every defect this port has hit on hardware lived
 * in the Zephyr glue, never in the woz_matter protocol modules (which are 14/14
 * covered), and the glue was never compiled by anything but a developer's local
 * `make cdk-*`. This compiles the REAL source unmodified against a fake settings
 * backend, so what passes is the code that ships.
 *
 * What it can and cannot prove: the fake is an in-RAM key/value store, so
 * nothing here says anything about NVS durability, wear or garbage collection.
 * What it does prove is the branch logic around failure — and one failure in
 * particular, the TORN WRITE, which is otherwise only reachable by cutting power
 * to a real board between two of seven settings_save_one() calls.
 */
#include <stdio.h>
#include <string.h>

#include "matter_fab_settings.h"
#include "settingsfake.h"
#include "test.h"

/* The keys matter_fab_settings.c writes, in the order it writes them. */
#define K_VER   "mfab/ver"
#define K_FAB0  "mfab/f0"
#define K_FAB1  "mfab/f1"
#define K_TD    "mfab/td"
#define K_XP    "mfab/xp"
#define K_ICLEN "mfab/il"
#define K_ICAC  "mfab/ic"

static void fill_identity(struct matter_device_info *info)
{
	memset(info, 0, sizeof(*info));

	info->fabrics[0].index = 1u;
	info->fabrics[0].have_root = true;
	info->fabrics[0].fabric_id = 0x1122334455667788ULL;
	info->fabrics[0].node_id = 0x99AABBCCDDEEFF00ULL;

	info->fabrics[1].index = 2u;
	info->fabrics[1].have_root = true;
	info->fabrics[1].fabric_id = 0xAAAABBBBCCCCDDDDULL;
	info->fabrics[1].node_id = 0x0011223344556677ULL;

	info->thread_dataset_len = 32u;
	for (size_t i = 0; i < info->thread_dataset_len; i++) {
		info->thread_dataset[i] = (uint8_t)(0x40u + i);
	}
	info->have_thread_xpanid = true;
	memcpy(info->thread_xpanid, "\xde\xad\x00\xbe\xef\x00\xca\xfe", 8);
}

void test_matter_fab_settings(void)
{
	struct matter_device_info stored;
	struct matter_device_info loaded;
	int rc;

	t_group("matter_fab_settings: round trip");
	settingsfake_reset();
	fill_identity(&stored);

	rc = matter_fab_store(&stored);
	T_EQ("store succeeds", rc, 0);
	T_OK("version key written", settingsfake_has(K_VER));
	T_OK("fabric 0 written", settingsfake_has(K_FAB0));
	T_OK("fabric 1 written", settingsfake_has(K_FAB1));
	T_OK("dataset written", settingsfake_has(K_TD));
	T_OK("xpanid written", settingsfake_has(K_XP));

	memset(&loaded, 0, sizeof(loaded));
	rc = matter_fab_load(&loaded);
	T_EQ("load reports a restored record", rc, 0);
	T_EQ("fabric 0 index survives", loaded.fabrics[0].index, 1);
	T_EQ("fabric 1 index survives", loaded.fabrics[1].index, 2);
	T_EQ("dataset length survives", loaded.thread_dataset_len, 32);
	T_OK("dataset bytes survive",
	     memcmp(loaded.thread_dataset, stored.thread_dataset, 32) == 0);
	T_OK("xpanid survives", loaded.have_thread_xpanid &&
				       memcmp(loaded.thread_xpanid, stored.thread_xpanid, 8) == 0);
	T_OK("a restored record means commissioning finished", loaded.commissioning_complete);

	t_group("matter_fab_settings: nothing stored");
	settingsfake_reset();
	memset(&loaded, 0, sizeof(loaded));
	rc = matter_fab_load(&loaded);
	T_EQ("load reports an empty store", rc, 1);
	T_OK("and claims no commissioning", !loaded.commissioning_complete);

	/*
	 * THE ONE THAT MATTERS.
	 *
	 * matter_fab_store() writes seven keys in sequence with no commit
	 * record. A reset between any two of them leaves KEY_VER present and
	 * matching -- it is written FIRST -- so the version guard in fab_set()
	 * passes and the load restores a fabric table that was never finished.
	 * matter_fab_load() then sets commissioning_complete = true on it,
	 * because "a stored record MEANS commissioning finished" is only true
	 * of a record that was fully written.
	 *
	 * The node then advertises operationally with half an identity and can
	 * never complete CASE -- which the file's own comment already calls
	 * worse than having nothing ("half an identity is worse than none").
	 *
	 * Coming up commissionable is the correct outcome: the pairing did not
	 * survive, and saying so costs a re-pair, while pretending otherwise
	 * costs a device that is bricked from the controller's point of view.
	 */
	t_group("matter_fab_settings: torn write");
	settingsfake_reset();
	fill_identity(&stored);
	settingsfake_fail_saves_after(2); /* version + fabric 0 land, then power dies */

	rc = matter_fab_store(&stored);
	T_OK("a torn store reports failure", rc != 0);
	T_EQ("and stopped after the writes that succeeded", settingsfake_save_count(), 2);

	settingsfake_fail_saves_after(-1);
	memset(&loaded, 0, sizeof(loaded));
	rc = matter_fab_load(&loaded);
	T_OK("a torn record does NOT restore as commissioned", !loaded.commissioning_complete);
	T_OK("and no fabric is presented", loaded.fabrics[0].index == 0u &&
						  loaded.fabrics[1].index == 0u);

	t_group("matter_fab_settings: erase");
	settingsfake_reset();
	fill_identity(&stored);
	T_EQ("store for erase", matter_fab_store(&stored), 0);
	T_OK("keys exist before erase", settingsfake_key_count() > 0);
	T_EQ("erase succeeds", matter_fab_erase(), 0);
	T_EQ("every key is gone", settingsfake_key_count(), 0);

	memset(&loaded, 0, sizeof(loaded));
	T_EQ("load after erase reports an empty store", matter_fab_load(&loaded), 1);
}

int main(void)
{
	test_matter_fab_settings();

	if (t_fail > 0) {
		printf("  cdk-fab-settings: FAIL (%d of %d)\n", t_fail, t_fail + t_pass);
		return 1;
	}
	printf("  cdk-fab-settings: PASS (%d checks — fake settings store, no NVS durability)\n",
	       t_pass);
	return 0;
}
