/** @file aliro_shell.c — `aliro` UART shell command: colored console over the UWB engine. */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "ccc_shim.h"        /* ccc_shim_active */
#include "fira_session.h"    /* fira_session_last_range, fira_session_get_ursk */
#include "flight_recorder.h" /* fr_set_enabled / fr_dump — walk-up record/replay (gated) */
#include "uwb_cirdiag.h"     /* per-reception CIA diagnostics stream toggle */
#include "uwb_min.h"         /* uwb_min_read_chipid, uwb_min_selftest, DEV_ID */
#include "uwb_rxdiag.h"      /* counters + ranging-log stream toggle */

/* Short commit SHA baked in by CMake; "unknown" for a git-less build. */
#ifndef WOZ_GIT_SHA
#define WOZ_GIT_SHA "unknown"
#endif

/* --- palette --------------------------------------------------------------- */
#define C_RST  "\x1b[0m"
#define C_B    "\x1b[1m"
#define C_DIM  "\x1b[2m"
#define C_GRN  "\x1b[32m"
#define C_YEL  "\x1b[33m"
#define C_RED  "\x1b[31m"
#define C_CYN  "\x1b[36m"
#define C_RULE C_DIM "  ────────────────────────────────────" C_RST

/** @brief Section header: green "aliro · <title>" over a dim rule. */
static void hdr(const struct shell *sh, const char *title)
{
	shell_print(sh, "");
	shell_print(sh, C_GRN C_B "  aliro" C_RST C_DIM " · " C_RST C_B "%s" C_RST, title);
	shell_print(sh, C_RULE);
}

/* pass/fail and yes/no chips reused across panels. */
#define CHIP_PASS C_GRN "pass ✓" C_RST
#define CHIP_FAIL C_RED "fail ✗" C_RST

/**
 * @brief Read and display the DW3110 DEV_ID register; verify chip identification.
 * @param sh Shell context.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success; nonzero SPI error code on read failure.
 */
static int cmd_chip(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint32_t id = 0;
	int rc = uwb_min_read_chipid(&id);

	hdr(sh, "chip");
	if (rc != 0) {
		shell_print(sh, "  " C_DIM "dev_id  " C_RST C_RED "SPI read failed (%d)" C_RST, rc);
		return rc;
	}
	bool ok = (id == UWB_DW3110_DEV_ID);
	shell_print(sh, "  " C_DIM "dev_id  " C_RST C_CYN "0x%08X" C_RST "   %s", id,
		    ok ? C_GRN "DW3110 ✓" C_RST : C_YEL "unknown ✗" C_RST);
	return 0;
}

/**
 * @brief Display RX/TX frame tally and error counters.
 * @param sh Shell context.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success.
 */
static int cmd_rx(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint32_t ok = 0, err = 0, to = 0, tx = 0, last_err = 0, last_ok = 0;
	uwb_rxdiag_get_counts(&ok, &err, &to, &tx, &last_err, &last_ok);

	hdr(sh, "rx tally");
	shell_print(sh, "  " C_DIM "good    " C_RST C_GRN "%u" C_RST, ok);
	shell_print(sh, "  " C_DIM "error   " C_RST "%s%u" C_RST, err ? C_RED : C_DIM, err);
	shell_print(sh, "  " C_DIM "timeout " C_RST C_YEL "%u" C_RST, to);
	shell_print(sh, "  " C_DIM "tx done " C_RST "%u", tx);
	shell_print(sh, "  " C_DIM "last_ok 0x%08X   last_err 0x%08X" C_RST, last_ok, last_err);
	return 0;
}

/**
 * @brief Display the last valid DS-TWR distance measurement and metadata.
 * @param sh Shell context.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success.
 */
static int cmd_range(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int32_t cm = 0;
	uint16_t addr = 0;
	uint8_t nlos = 0;
	uint32_t block = 0;
	int64_t age_ms = 0;
	bool have = fira_session_last_range(&cm, &addr, &nlos, &block, &age_ms);

	hdr(sh, "range");
	if (!have) {
		shell_print(sh, "  " C_DIM "no valid range since boot" C_RST);
		return 0;
	}
	shell_print(sh, "  " C_DIM "distance " C_RST C_B C_CYN "%d cm" C_RST, cm);
	shell_print(sh, "  " C_DIM "peer     " C_RST "0x%04X", addr);
	shell_print(sh, "  " C_DIM "nlos     " C_RST "%s",
		    nlos ? C_YEL "yes" C_RST : C_GRN "no" C_RST);
	shell_print(sh, "  " C_DIM "block    " C_RST "%u", block);
	shell_print(sh, "  " C_DIM "age      " C_RST "%lld ms", (long long)age_ms);
	shell_print(sh, "  " C_DIM "trusted  " C_RST "%s",
		    fira_session_range_trusted() ? C_GRN "yes ●" C_RST : C_YEL "no ○" C_RST);
	return 0;
}

/**
 * @brief Run radio TX/RX self-test and display results.
 * @param sh Shell context.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success; nonzero error code if self-test fails.
 */
static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	// Uninitialized self-test result structure, filled by uwb_min_selftest.
	struct uwb_selftest_result r = {0};
	int rc = uwb_min_selftest(&r);

	hdr(sh, "selftest");
	if (rc != 0) {
		shell_print(sh, "  " C_RED "selftest error (%d)" C_RST, rc);
		return rc;
	}
	shell_print(sh, "  " C_DIM "tx done  " C_RST "%s", r.tx_done ? CHIP_PASS : CHIP_FAIL);
	shell_print(sh, "  " C_DIM "rx armed " C_RST "%s", r.rx_armed ? CHIP_PASS : CHIP_FAIL);
	shell_print(sh, "  " C_DIM "rx event " C_RST "%s", r.rx_event ? CHIP_PASS : CHIP_FAIL);
	shell_print(sh, "  " C_DIM "tx_status 0x%08X   rx_status 0x%08X" C_RST, r.tx_status,
		    r.rx_status);
	return 0;
}

/**
 * @brief Enable, disable, or query the ranging heartbeat log output stream.
 * @param sh Shell context.
 * @param argc Argument count; if ≥2, argv[1] must be "on" or "off".
 * @param argv Command arguments; argv[1] optionally specifies "on" or "off".
 * @return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".
 */
static int cmd_log(const struct shell *sh, size_t argc, char **argv)
{
	if (argc >= 2) {
		if (strcmp(argv[1], "on") == 0) {
			uwb_rxdiag_stream_set(true);
		} else if (strcmp(argv[1], "off") == 0) {
			uwb_rxdiag_stream_set(false);
		} else {
			shell_print(sh, "  " C_YEL "usage: aliro log [on|off]" C_RST);
			return -EINVAL;
		}
	}
	bool on = uwb_rxdiag_stream_get();
	shell_print(sh, "  ranging heartbeat %s", on ? C_GRN "● on" C_RST : C_DIM "○ off" C_RST);
	return 0;
}

/**
 * @brief Enable, disable, or query the per-block distance stream output.
 * @param sh Shell context.
 * @param argc Argument count; if ≥2, argv[1] must be "on" or "off".
 * @param argv Command arguments; argv[1] optionally specifies "on" or "off".
 * @return 0 on success; -EINVAL if argv[1] is neither "on" nor "off".
 */
static int cmd_frames(const struct shell *sh, size_t argc, char **argv)
{
	if (argc >= 2) {
		if (strcmp(argv[1], "on") == 0) {
			uwb_rxdiag_rng_set(true);
		} else if (strcmp(argv[1], "off") == 0) {
			uwb_rxdiag_rng_set(false);
		} else {
			shell_print(sh, "  " C_YEL "usage: aliro frames [on|off]" C_RST);
			return -EINVAL;
		}
	}
	bool on = uwb_rxdiag_rng_get();
	shell_print(sh, "  per-block distance stream %s",
		    on ? C_GRN "● on" C_RST : C_DIM "○ off" C_RST);
	return 0;
}

#if defined(CONFIG_WOZ_FLIGHT_RECORDER)
/**
 * @brief Arm/disarm the flight recorder, or dump/clear its RAM ring.
 * @param sh Shell context.
 * @param argc Argument count; if ≥2, argv[1] is on|off|dump|clear.
 * @param argv Command arguments.
 * @return 0 on success; -EINVAL on an unrecognised sub-argument.
 */
static int cmd_frec(const struct shell *sh, size_t argc, char **argv)
{
	if (argc >= 2) {
		if (strcmp(argv[1], "on") == 0) {
			fr_set_enabled(true);
		} else if (strcmp(argv[1], "off") == 0) {
			fr_set_enabled(false);
		} else if (strcmp(argv[1], "dump") == 0) {
			fr_dump(); /* hex `[FREC]` lines for tools/flight_recorder.py */
			return 0;
		} else if (strcmp(argv[1], "clear") == 0) {
			fr_clear();
		} else {
			shell_print(sh, "  " C_YEL "usage: aliro frec [on|off|dump|clear]" C_RST);
			return -EINVAL;
		}
	}
	shell_print(sh, "  flight recorder %s",
		    fr_enabled() ? C_GRN "● armed" C_RST : C_DIM "○ off" C_RST);
	return 0;
}
#endif /* CONFIG_WOZ_FLIGHT_RECORDER */

#if defined(CONFIG_WOZ_UWB_CIRDIAG)
/**
 * @brief Control the per-reception CIA diagnostics stream ([ALAB] uwb.diag) and, via the `dump`
 * sub-form, the windowed-CIR tap dump ([ALAB] uwb.cir).
 * @param sh Shell context.
 * @param argc Argument count. `cir [on|off]` toggles the summary stream; `cir dump [on|off]`
 * toggles the windowed-CIR dump; `cir probe` runs the one-shot accumulator read diagnostic.
 * @param argv Command arguments.
 * @return 0 on success; -EINVAL on a malformed argument.
 */
static int cmd_cir(const struct shell *sh, size_t argc, char **argv)
{
	if (argc >= 2 && strcmp(argv[1], "probe") == 0) {
		uwb_cirdiag_probe();
		return 0;
	}
	if (argc >= 2 && strcmp(argv[1], "dump") == 0) {
		if (argc >= 3) {
			if (strcmp(argv[2], "on") == 0) {
				uwb_cirdiag_dump_set_enabled(true);
			} else if (strcmp(argv[2], "off") == 0) {
				uwb_cirdiag_dump_set_enabled(false);
			} else {
				shell_print(sh, "  " C_YEL "usage: aliro cir dump [on|off]" C_RST);
				return -EINVAL;
			}
		}
		bool don = uwb_cirdiag_dump_enabled();

		shell_print(sh, "  windowed-CIR dump %s%s",
			    don ? C_GRN "● on" C_RST : C_DIM "○ off" C_RST,
			    don ? C_DIM "  (taps buffer to RAM; dumped on `cir dump off`)" C_RST
				: "");
		return 0;
	}
	if (argc >= 2) {
		if (strcmp(argv[1], "on") == 0) {
			uwb_cirdiag_set_enabled(true);
		} else if (strcmp(argv[1], "off") == 0) {
			uwb_cirdiag_set_enabled(false);
		} else {
			shell_print(sh, "  " C_YEL "usage: aliro cir [on|off] | cir dump "
					"[on|off] | cir probe" C_RST);
			return -EINVAL;
		}
	}
	bool on = uwb_cirdiag_enabled();
	shell_print(sh, "  CIA first-path diag stream %s",
		    on ? C_GRN "● on" C_RST : C_DIM "○ off" C_RST);
	return 0;
}
#endif /* CONFIG_WOZ_UWB_CIRDIAG */

/**
 * @brief Display the build commit SHA.
 * @param sh Shell context.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success.
 */
static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	hdr(sh, "version");
	shell_print(sh, "  " C_DIM "commit  " C_RST C_CYN WOZ_GIT_SHA C_RST);
	return 0;
}

/**
 * @brief Display all system status: chip ID, CCC bind state, URSK provisioning, last range, RX
 * tally, and stream state.
 * @param sh Shell context.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on success.
 */
static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	hdr(sh, "status");

	/* chip */
	uint32_t id = 0;
	if (uwb_min_read_chipid(&id) == 0) {
		bool ok = (id == UWB_DW3110_DEV_ID);
		shell_print(sh, "  " C_DIM "chip    " C_RST C_CYN "0x%08X" C_RST " %s", id,
			    ok ? C_GRN "DW3110 ✓" C_RST : C_YEL "unknown ✗" C_RST);
	} else {
		shell_print(sh, "  " C_DIM "chip    " C_RST C_RED "SPI error" C_RST);
	}

	/* ccc bind + ursk */
	shell_print(sh, "  " C_DIM "ccc     " C_RST "%s",
		    ccc_shim_active() ? C_GRN "bound ●" C_RST : C_DIM "idle ○" C_RST);
	shell_print(sh, "  " C_DIM "ursk    " C_RST "%s",
		    fira_session_get_ursk() != NULL ? C_GRN "provisioned ●" C_RST
						    : C_DIM "none ○" C_RST);

	/* range */
	int32_t cm = 0;
	uint32_t block = 0;
	int64_t age_ms = 0;
	if (fira_session_last_range(&cm, NULL, NULL, &block, &age_ms)) {
		shell_print(sh,
			    "  " C_DIM "range   " C_RST C_CYN "%d cm" C_RST C_DIM
			    " (blk %u, %lld ms ago)" C_RST " %s",
			    cm, block, (long long)age_ms,
			    fira_session_range_trusted() ? C_GRN "trusted" C_RST
							 : C_YEL "untrusted" C_RST);
	} else {
		shell_print(sh, "  " C_DIM "range   " C_RST C_DIM "none yet" C_RST);
	}

	/* rx tally */
	uint32_t ok = 0, err = 0, to = 0, tx = 0;
	uwb_rxdiag_get_counts(&ok, &err, &to, &tx, NULL, NULL);
	shell_print(sh,
		    "  " C_DIM "rx      " C_RST C_GRN "✓%u" C_RST " %s✗%u" C_RST " " C_YEL
		    "⧗%u" C_RST C_DIM " tx%u" C_RST,
		    ok, err ? C_RED : C_DIM, err, to, tx);

	/* streams */
	shell_print(sh, "  " C_DIM "log     " C_RST "%s",
		    uwb_rxdiag_stream_get() ? C_GRN "● on" C_RST : C_DIM "○ off" C_RST);
	shell_print(sh, "  " C_DIM "frames  " C_RST "%s",
		    uwb_rxdiag_rng_get() ? C_GRN "● on" C_RST : C_DIM "○ off" C_RST);
	return 0;
}

/**
 * @brief Print aliro shell command help and list all available subcommands.
 * @param sh Shell context.
 * @param argc Argument count; if >1, returns error for unknown subcommand.
 * @param argv Argument vector; argv[1] if present must be empty or help request.
 * @return 0 on success; -EINVAL if argc >1.
 */
static int cmd_aliro(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		shell_print(sh, "  " C_YEL "unknown subcommand: %s" C_RST, argv[1]);
		return -EINVAL;
	}
	shell_print(sh, "");
	shell_print(sh, C_GRN C_B "  aliro" C_RST C_DIM "  ·  DIY Aliro NFC + UWB unlock" C_RST);
	shell_print(sh, C_RULE);
	shell_print(sh, "  " C_CYN "status  " C_RST C_DIM "everything at a glance" C_RST);
	shell_print(sh, "  " C_CYN "rx      " C_RST C_DIM "on-air RX/TX tally" C_RST);
	shell_print(sh, "  " C_CYN "range   " C_RST C_DIM "last DS-TWR distance" C_RST);
	shell_print(sh, "  " C_CYN "chip    " C_RST C_DIM "read the DW3110 DEV_ID" C_RST);
	shell_print(sh, "  " C_CYN "selftest" C_RST C_DIM "  radio TX/RX self-test" C_RST);
	shell_print(sh, "  " C_CYN "log     " C_RST C_DIM "ranging heartbeat on|off" C_RST);
	shell_print(sh, "  " C_CYN "frames  " C_RST C_DIM "per-block distance stream on|off" C_RST);
	shell_print(sh, "  " C_CYN "version " C_RST C_DIM "build commit SHA" C_RST);
	shell_print(sh, "");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_aliro,
	SHELL_CMD(status, NULL, "One-glance panel: chip, ccc, ursk, range, rx.", cmd_status),
	SHELL_CMD(rx, NULL, "On-air RX/TX event tally.", cmd_rx),
	SHELL_CMD(range, NULL, "Last measured DS-TWR distance.", cmd_range),
	SHELL_CMD(chip, NULL, "Read the DW3110 DEV_ID over SPI.", cmd_chip),
	SHELL_CMD(selftest, NULL, "Run the radio TX/RX self-test.", cmd_selftest),
	SHELL_CMD(log, NULL, "Ranging heartbeat: `log on` | `log off`.", cmd_log),
#if defined(CONFIG_WOZ_UWB_CIRDIAG)
	SHELL_CMD(cir, NULL,
		  "CIA diag stream + windowed CIR: `cir on|off` | `cir dump on|off` | `cir probe`.",
		  cmd_cir),
#endif
	SHELL_CMD(frames, NULL, "Per-block distance stream: `frames on` | `frames off`.",
		  cmd_frames),
#if defined(CONFIG_WOZ_FLIGHT_RECORDER)
	SHELL_CMD(frec, NULL, "Flight recorder: `frec on|off|dump|clear`.", cmd_frec),
#endif
	SHELL_CMD(version, NULL, "Firmware build: short commit SHA.", cmd_version),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(aliro, &sub_aliro, "Aliro UWB firmware console.", cmd_aliro);
