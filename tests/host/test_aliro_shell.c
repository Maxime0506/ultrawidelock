/**
 * @file test_aliro_shell.c — the `aliro` console (aliro_shell.c) through the
 * fake shell registry (logfake zephyr/shell/shell.h). Every handler runs for
 * real against drvfake-backed uwb_min/ccc/fira state; shell_print lands in the
 * shellfake capture buffer, so the checks pin return codes and the substance
 * of each panel (values + state words), not the ANSI art.
 */
#include <string.h>

#include "drvfake.h"
#include "test.h"
#include "uwb_min.h"
#include "uwb_rxdiag.h"

extern const struct shellfake_root shellfake_cmd_aliro;

/** Dispatch `aliro <sub>` with the given argv tail; returns the handler rc. */
static int run(const char *sub, int argc, char **argv)
{
	const struct shellfake_entry *e;
	struct shell sh = {0};

	shellfake_reset();
	for (e = shellfake_cmd_aliro.sub; e->syntax != NULL; e++) {
		if (strcmp(e->syntax, sub) == 0) {
			return e->handler(&sh, (size_t)argc, argv);
		}
	}
	return -1000; /* command missing from the registry */
}

static int has(const char *needle)
{
	return strstr(shellfake_out, needle) != NULL;
}

/** Point the fake DEV_ID surfaces at a healthy DW3110. */
static void chip_ok(void)
{
	drvfake.spi_devid[0] = 0x02;
	drvfake.spi_devid[1] = 0x03;
	drvfake.spi_devid[2] = 0xCA;
	drvfake.spi_devid[3] = 0xDE;
	drvfake.devid = UWB_DW3110_DEV_ID;
}

void test_aliro_shell(void)
{
	char on_s[] = "on", off_s[] = "off", junk_s[] = "sideways";
	char *argv2[2];

	t_group("chip panel");
	(void)uwb_min_hw_reset();
	drvfake_reset();
	chip_ok();
	T_EQ("chip rc", run("chip", 1, NULL), 0);
	T_OK("id shown", has("0xDECA0302"));
	T_OK("recognised", has("DW3110"));
	drvfake.devid = 0xDECA0999u;
	T_EQ("foreign id rc", run("chip", 1, NULL), 0);
	T_OK("flagged unknown", has("unknown"));
	(void)uwb_min_hw_reset();
	drvfake_reset(); /* DEV_ID poll reads zeros */
	drvfake.probe_fail_times = 3;
	T_EQ("spi failure rc", run("chip", 1, NULL), -5);
	T_OK("spi failure shown", has("SPI read failed"));

	t_group("rx panel");
	T_EQ("rx rc", run("rx", 1, NULL), 0);
	T_OK("tally labels", has("good") && has("error") && has("timeout") && has("tx done"));

	t_group("range panel");
	drvfake.fira_have = false;
	T_EQ("no-range rc", run("range", 1, NULL), 0);
	T_OK("no range yet", has("no valid range"));
	drvfake.fira_have = true;
	drvfake.fira_cm = 87;
	drvfake.fira_addr = 0xBEEF;
	drvfake.fira_nlos = 1;
	drvfake.fira_block = 7;
	drvfake.fira_age_ms = 250;
	drvfake.fira_trusted = true;
	T_EQ("range rc", run("range", 1, NULL), 0);
	T_OK("distance", has("87 cm"));
	T_OK("peer", has("0xBEEF"));
	T_OK("nlos yes", has("yes"));
	T_OK("age", has("250 ms"));
	drvfake.fira_nlos = 0;
	drvfake.fira_trusted = false;
	T_EQ("range rc 2", run("range", 1, NULL), 0);
	T_OK("untrusted marker", has("no ○"));

	t_group("selftest panel");
	(void)uwb_min_hw_reset();
	drvfake_reset();
	chip_ok();
	drvfake.status_seq[0] = DWT_INT_TXFRS_BIT_MASK;
	drvfake.status_seq[1] = DWT_INT_RXFTO_BIT_MASK;
	drvfake.status_n = 2;
	T_EQ("selftest rc", run("selftest", 1, NULL), 0);
	T_OK("passes shown", has("pass"));
	(void)uwb_min_hw_reset();
	drvfake_reset();
	drvfake.probe_fail_times = 3;
	T_EQ("selftest error rc", run("selftest", 1, NULL), -5);
	T_OK("error shown", has("selftest error"));

	t_group("log + frames toggles");
	argv2[0] = on_s;
	argv2[1] = on_s;
	T_EQ("log on rc", run("log", 2, argv2), 0);
	T_OK("log reads on", uwb_rxdiag_stream_get() && has("on"));
	argv2[1] = off_s;
	T_EQ("log off rc", run("log", 2, argv2), 0);
	T_OK("log reads off", !uwb_rxdiag_stream_get() && has("off"));
	argv2[1] = junk_s;
	T_EQ("log junk rc", run("log", 2, argv2), -22);
	T_OK("usage shown", has("usage: aliro log"));
	T_EQ("log query rc", run("log", 1, NULL), 0);
	argv2[1] = on_s;
	T_EQ("frames on rc", run("frames", 2, argv2), 0);
	T_OK("frames on", uwb_rxdiag_rng_get());
	argv2[1] = off_s;
	T_EQ("frames off rc", run("frames", 2, argv2), 0);
	T_OK("frames off", !uwb_rxdiag_rng_get());
	argv2[1] = junk_s;
	T_EQ("frames junk rc", run("frames", 2, argv2), -22);

	t_group("version");
	T_EQ("version rc", run("version", 1, NULL), 0);
	T_OK("commit line", has("commit"));

	t_group("status panel: all green");
	(void)uwb_min_hw_reset();
	drvfake_reset();
	chip_ok();
	drvfake.ccc_active = true;
	static const uint8_t ursk[32] = {1};

	drvfake.fira_ursk = ursk;
	drvfake.fira_have = true;
	drvfake.fira_cm = 142;
	drvfake.fira_age_ms = 90;
	drvfake.fira_trusted = true;
	uwb_rxdiag_stream_set(true);
	uwb_rxdiag_rng_set(true);
	T_EQ("status rc", run("status", 1, NULL), 0);
	T_OK("chip line", has("0xDECA0302"));
	T_OK("ccc bound", has("bound"));
	T_OK("ursk provisioned", has("provisioned"));
	T_OK("range trusted", has("142 cm") && has("trusted"));
	uwb_rxdiag_stream_set(false);
	uwb_rxdiag_rng_set(false);

	t_group("status panel: all idle");
	(void)uwb_min_hw_reset();
	drvfake_reset(); /* zero DEV_ID, probe fine but id unknown */
	drvfake.probe_fail_times = 3;
	drvfake.ccc_active = false;
	drvfake.fira_ursk = NULL;
	drvfake.fira_have = false;
	T_EQ("status rc", run("status", 1, NULL), 0);
	T_OK("chip error line", has("SPI error"));
	T_OK("ccc idle", has("idle"));
	T_OK("no ursk", has("none"));
	T_OK("no range", has("none yet"));

	t_group("status panel: untrusted range");
	(void)uwb_min_hw_reset();
	drvfake_reset();
	chip_ok();
	drvfake.fira_have = true;
	drvfake.fira_cm = 999;
	drvfake.fira_trusted = false;
	T_EQ("status rc", run("status", 1, NULL), 0);
	T_OK("untrusted word", has("untrusted"));

	t_group("root command");
	{
		struct shell sh = {0};

		shellfake_reset();
		T_EQ("help rc", shellfake_cmd_aliro.handler(&sh, 1, NULL), 0);
		T_OK("lists subcommands", has("status") && has("selftest") && has("version"));
		char *argv[2] = {(char *)"aliro", junk_s};

		shellfake_reset();
		T_EQ("unknown sub rc", shellfake_cmd_aliro.handler(&sh, 2, argv), -22);
		T_OK("unknown reported", has("unknown subcommand: sideways"));
	}
}
