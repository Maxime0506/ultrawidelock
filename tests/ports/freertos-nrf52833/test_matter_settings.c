/*
 * The Matter settings backend over the port's key-value store.
 *
 * This runs the real path table and the real store over the flash model,
 * because the property under test is that what matter_commission.c persists --
 * the msub subscription records and the two writable Door Lock attributes --
 * comes back after a reboot, and only the real store can be wrong about that.
 * The store's state is static, so every scenario forks; the reboot pair shares
 * one flash mapping on purpose.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fake_flash.h"

#include <ultrawidelock_freertos_kv.h>
#include <ultrawidelock_freertos_platform.h>

#include "matter_settings_freertos.h"

static unsigned g_checks;
static unsigned g_failures;

#define CHECK(label, condition)                                                                    \
	do {                                                                                       \
		g_checks++;                                                                        \
		if (!(condition)) {                                                                \
			g_failures++;                                                              \
			printf("  FAIL %s\n", (label));                                            \
		} else {                                                                           \
			printf("  ok   %s\n", (label));                                            \
		}                                                                                  \
	} while (0)

void ultrawidelock_freertos_log(enum ultrawidelock_freertos_log_level level, const char *tag,
				const char *fmt, ...)
{
	(void)level;
	(void)tag;
	(void)fmt;
}

/* One loaded record: whether the callback ran, and what it delivered. */
struct loaded {
	bool called;
	size_t len;
	uint8_t bytes[64];
};

static int load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param)
{
	struct loaded *out = param;

	(void)key;

	out->called = true;
	out->len = len;
	if (len <= sizeof(out->bytes)) {
		(void)read_cb(cb_arg, out->bytes, len);
	}
	return 0;
}

static bool loads_back(const char *path, const void *expect, size_t expect_len)
{
	struct loaded got;

	memset(&got, 0, sizeof(got));
	if (settings_load_subtree_direct(path, load_cb, &got) != 0) {
		return false;
	}
	return got.called && got.len == expect_len && memcmp(got.bytes, expect, expect_len) == 0;
}

/* ---- scenarios ---------------------------------------------------------- */

/* A path the table does not know is refused and named, never stored. */
static void scenario_unknown_path(void)
{
	struct loaded got;
	uint32_t v = 7u;

	(void)ultrawidelock_freertos_kv_init();

	CHECK("saving an unknown path is refused",
	      settings_save_one("nope/x", &v, sizeof(v)) == -22);
	memset(&got, 0, sizeof(got));
	CHECK("loading an unknown path is refused",
	      settings_load_subtree_direct("nope/x", load_cb, &got) == -22 && !got.called);
	CHECK("deleting an unknown path is refused", settings_delete("nope/x") == -22);
}

/* The two Door Lock attributes: fixed-width, so size is part of the contract. */
static void scenario_dl_attrs(void)
{
	uint32_t relock = 120u;
	uint8_t approach = 0x05u;
	uint16_t wrong = 0xbeefu;

	(void)ultrawidelock_freertos_kv_init();

	CHECK("AutoRelockTime saves as a u32",
	      settings_save_one("mdl/art", &relock, sizeof(relock)) == 0);
	CHECK("and loads back", loads_back("mdl/art", &relock, sizeof(relock)));

	CHECK("a save of the wrong width is refused",
	      settings_save_one("mdl/art", &wrong, sizeof(wrong)) == -22);
	CHECK("and the stored value is untouched", loads_back("mdl/art", &relock, sizeof(relock)));

	relock = 0u;
	CHECK("zero -- no automatic relock -- is a storable value",
	      settings_save_one("mdl/art", &relock, sizeof(relock)) == 0 &&
		      loads_back("mdl/art", &relock, sizeof(relock)));

	CHECK("the approach direction saves as one byte",
	      settings_save_one("mdl/apd", &approach, sizeof(approach)) == 0);
	CHECK("and loads back", loads_back("mdl/apd", &approach, sizeof(approach)));

	/*
	 * A record of the wrong size, planted under the table's back. The
	 * backend must not hand it to the caller: the boot default stands.
	 */
	{
		struct loaded got;

		(void)ultrawidelock_freertos_kv_set(ULTRAWIDELOCK_KV_KEY_MATTER_DL_APPROACH,
						    &wrong, sizeof(wrong));
		memset(&got, 0, sizeof(got));
		CHECK("a stored record of the wrong width is ignored on load",
		      settings_load_subtree_direct("mdl/apd", load_cb, &got) == 0 && !got.called);
	}
}

/* All six subscription slots, each its own record. */
static void scenario_msub_slots(void)
{
	char path[8];
	uint8_t record[16];
	unsigned i;
	bool all_saved = true;
	bool all_loaded = true;

	(void)ultrawidelock_freertos_kv_init();

	for (i = 0u; i < 6u; i++) {
		(void)snprintf(path, sizeof(path), "msub/%u", i);
		memset(record, (int)(0x10u + i), sizeof(record));
		if (settings_save_one(path, record, sizeof(record)) != 0) {
			all_saved = false;
		}
	}
	CHECK("every subscription slot msub/0..5 has a key", all_saved);

	for (i = 0u; i < 6u; i++) {
		(void)snprintf(path, sizeof(path), "msub/%u", i);
		memset(record, (int)(0x10u + i), sizeof(record));
		if (!loads_back(path, record, sizeof(record))) {
			all_loaded = false;
		}
	}
	CHECK("and each slot loads its own record back", all_loaded);

	{
		struct loaded got;

		CHECK("deleting a slot succeeds", settings_delete("msub/2") == 0);
		memset(&got, 0, sizeof(got));
		CHECK("a deleted slot loads nothing",
		      settings_load_subtree_direct("msub/2", load_cb, &got) == 0 && !got.called);
		CHECK("deleting it again is still success", settings_delete("msub/2") == 0);
	}
}

/* ---- the reboot pair: same flash, fresh statics -------------------------- */

static void scenario_populate_then_reboot(void)
{
	uint32_t relock = 45u;
	uint8_t approach = 0x03u;
	uint8_t sub[16];

	(void)ultrawidelock_freertos_kv_init();
	memset(sub, 0x5a, sizeof(sub));

	CHECK("AutoRelockTime stored before the reboot",
	      settings_save_one("mdl/art", &relock, sizeof(relock)) == 0);
	CHECK("approach direction stored before the reboot",
	      settings_save_one("mdl/apd", &approach, sizeof(approach)) == 0);
	CHECK("a subscription stored before the reboot",
	      settings_save_one("msub/3", sub, sizeof(sub)) == 0);
	CHECK("no flash rule was broken getting there", fake_flash_violations == 0);
}

static void scenario_after_reboot(void)
{
	uint32_t relock = 45u;
	uint8_t approach = 0x03u;
	uint8_t sub[16];
	struct loaded got;

	(void)ultrawidelock_freertos_kv_init();
	memset(sub, 0x5a, sizeof(sub));

	CHECK("AutoRelockTime survives the reboot",
	      loads_back("mdl/art", &relock, sizeof(relock)));
	CHECK("the approach direction survives the reboot",
	      loads_back("mdl/apd", &approach, sizeof(approach)));
	CHECK("the subscription survives the reboot", loads_back("msub/3", sub, sizeof(sub)));

	memset(&got, 0, sizeof(got));
	CHECK("a slot never written still loads nothing",
	      settings_load_subtree_direct("msub/0", load_cb, &got) == 0 && !got.called);
}

/* ---- harness ------------------------------------------------------------- */

enum {
	SCENARIO_UNKNOWN_PATH,
	SCENARIO_DL_ATTRS,
	SCENARIO_MSUB_SLOTS,
	SCENARIO_POPULATE_THEN_REBOOT,
	SCENARIO_AFTER_REBOOT,
};

static int run_scenario(int scenario)
{
	switch (scenario) {
	case SCENARIO_UNKNOWN_PATH:
		scenario_unknown_path();
		break;
	case SCENARIO_DL_ATTRS:
		scenario_dl_attrs();
		break;
	case SCENARIO_MSUB_SLOTS:
		scenario_msub_slots();
		break;
	case SCENARIO_POPULATE_THEN_REBOOT:
		scenario_populate_then_reboot();
		break;
	default:
		scenario_after_reboot();
		break;
	}
	printf("RESULT-PART: %u checks\n", g_checks);
	return g_failures == 0 ? 0 : 1;
}

static bool run_child(int scenario)
{
	pid_t pid;
	int status = 0;

	fflush(stdout);
	pid = fork();
	if (pid == 0) {
		int rc = run_scenario(scenario);

		fflush(stdout);
		_exit(rc);
	}
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		printf("  FAIL could not fork scenario %d\n", scenario);
		return false;
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void)
{
	unsigned failures = 0;
	int scenario;

	/* Each of these starts from a blank part. */
	for (scenario = SCENARIO_UNKNOWN_PATH; scenario <= SCENARIO_MSUB_SLOTS; scenario++) {
		fake_flash_reset();
		failures += run_child(scenario) ? 0u : 1u;
	}

	/* The reboot pair shares one flash on purpose. */
	fake_flash_reset();
	failures += run_child(SCENARIO_POPULATE_THEN_REBOOT) ? 0u : 1u;
	failures += run_child(SCENARIO_AFTER_REBOOT) ? 0u : 1u;

	printf("RESULT: %s (5 scenarios)\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
