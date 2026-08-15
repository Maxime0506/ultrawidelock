/* SPDX-License-Identifier: ISC */

/*
 * The provisioning console, over USB CDC ACM.
 *
 * This board cannot be commissioned into Apple Home on its own (no Matter image
 * fits an nRF52833), so it adopts a credential exported from a board that was.
 * The point of this file is WHERE that credential lives: in the key-value
 * store, as per-device data typed in over USB, never in the firmware image. One
 * image is then the same for everybody, and carries no key.
 *
 * Reachable only in provisioning mode -- hold SW2 through reset -- where main()
 * brings up USB and deliberately never starts the radios. Everything here
 * therefore reads and writes the settings store directly (ultrawidelock_prov_*) rather
 * than the running engine's state, which in this mode does not exist. The one
 * exception is `import`, which routes through ultrawidelock_reader_import_blob so the
 * engine's own commit path stays the single place that adopts identity.
 *
 * THE SAME FOUR COMMANDS AS THE ZEPHYR IMAGE, and the same refusals, because
 * the operator procedure and the exported blobs are shared between them: a blob
 * exported from a Zephyr board is typed into this one. What is not shared is the
 * shell -- Zephyr's costs a subsystem this port does not have, and four commands
 * with no completion, no history and no arguments beyond one token do not need
 * one. See apps/dwm3001cdk-lock/src/prov_shell.c for the other half.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>

#include "ultrawidelock_prov.h"
#include <ultrawidelock/reader.h>

#include "ultrawidelock_freertos_platform.h"
#include "ultrawidelock_freertos_usb.h"

/* One blob each way, static because the hex form of a full blob is over 950
 * characters on its own and the console task's stack is sized for the P-256
 * derive inside `import`, not for this. */
static uint8_t s_blob[ULTRAWIDELOCK_PROV_BLOB_MAX];
static char s_hex[2u * ULTRAWIDELOCK_PROV_BLOB_MAX + 1u];
static char s_line[2u * ULTRAWIDELOCK_PROV_BLOB_MAX + 16u];
static char s_out[160];

static bool all_zero(const uint8_t *p, size_t len)
{
	uint8_t acc = 0;

	for (size_t i = 0; i < len; i++) {
		acc |= p[i];
	}
	return acc == 0;
}

/* The three ways a syntactically valid blob is still useless, named rather than
 * left for the walk-up to discover. Word for word the Zephyr image's list,
 * because an operator reading one message on one board and a different message
 * on the other would reasonably conclude the boards disagree. */
static const char *dead_blob_reason(const struct ultrawidelock_reader_identity *id,
				    const struct ultrawidelock_trust_store *ts)
{
	if (id->is_dev) {
		return "it is the built-in DEV identity: the source board was never "
		       "provisioned, or was factory-reset after it was";
	}
	if (all_zero(id->grk, ULTRAWIDELOCK_GRK_LEN)) {
		return "the GroupResolvingKey is all zero: SetAliroReaderConfig never "
		       "landed on the source, so no phone can resolve this reader";
	}
	if (ts->count == 0u) {
		return "there are no trust anchors: no phone key was enrolled on the "
		       "source";
	}
	return NULL;
}

static void say(const char *s)
{
	(void)ultrawidelock_freertos_usb_print(s);
}

/* ---- hex, both ways ------------------------------------------------------ */

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

/* Returns the byte count, or 0 for an odd length, a non-hex character, or more
 * than @p max bytes. Zephyr's hex2bin has the same three refusals and the same
 * single return value for them, which is what the caller's message says. */
static size_t hex_to_bin(const char *hex, size_t hex_len, uint8_t *out, size_t max)
{
	size_t n = hex_len / 2u;

	if (hex_len == 0u || (hex_len % 2u) != 0u || n > max) {
		return 0u;
	}
	for (size_t i = 0; i < n; i++) {
		int hi = hex_nibble(hex[2u * i]);
		int lo = hex_nibble(hex[2u * i + 1u]);

		if (hi < 0 || lo < 0) {
			return 0u;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return n;
}

static bool bin_to_hex(const uint8_t *bin, size_t len, char *out, size_t max)
{
	static const char digits[] = "0123456789abcdef";

	if (2u * len + 1u > max) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		out[2u * i] = digits[bin[i] >> 4];
		out[2u * i + 1u] = digits[bin[i] & 0x0fu];
	}
	out[2u * len] = '\0';
	return true;
}

/* ---- the commands -------------------------------------------------------- */

static void cmd_prov(void)
{
	struct ultrawidelock_reader_identity id;
	struct ultrawidelock_trust_store ts;
	const char *why;

	if (ultrawidelock_prov_load(&id, &ts) < 0) {
		say("settings store unreadable; showing the dev fallback");
	}

	(void)snprintf(s_out, sizeof(s_out), "identity   : %s",
		       id.is_dev ? "DEV (not provisioned)" : "provisioned");
	say(s_out);
	(void)snprintf(s_out, sizeof(s_out),
		       "reader_id  : %02x%02x%02x%02x...%02x%02x%02x%02x", id.reader_id[0],
		       id.reader_id[1], id.reader_id[2], id.reader_id[3], id.reader_id[28],
		       id.reader_id[29], id.reader_id[30], id.reader_id[31]);
	say(s_out);
	(void)snprintf(s_out, sizeof(s_out), "GRK        : %s",
		       all_zero(id.grk, ULTRAWIDELOCK_GRK_LEN)
			       ? "all zero (no phone will approach)"
			       : "set");
	say(s_out);
	(void)snprintf(s_out, sizeof(s_out), "trust      : %u of %u anchor(s)",
		       (unsigned)ts.count, (unsigned)ULTRAWIDELOCK_TRUST_MAX);
	say(s_out);

	why = dead_blob_reason(&id, &ts);
	(void)snprintf(s_out, sizeof(s_out), "verdict    : %s",
		       why ? why : "will unlock a phone enrolled on the source home");
	say(s_out);
}

static void cmd_import(const char *hex)
{
	struct ultrawidelock_reader_identity id;
	struct ultrawidelock_trust_store ts;
	const char *why;
	size_t hex_len;
	size_t len;
	int rc;

	if (hex == NULL || hex[0] == '\0') {
		say("usage: import <hex>");
		return;
	}

	hex_len = strlen(hex);
	len = hex_to_bin(hex, hex_len, s_blob, sizeof(s_blob));
	if (len == 0u) {
		(void)snprintf(s_out, sizeof(s_out),
			       "%u hex chars did not decode (odd length, non-hex, or over the "
			       "%u-byte cap)",
			       (unsigned)hex_len, (unsigned)sizeof(s_blob));
		say(s_out);
		return;
	}

	if (ultrawidelock_prov_deserialize(s_blob, len, &id, &ts) != 0) {
		say("not an APRV blob (bad magic, version, or length)");
		return;
	}

	/* Refuse before writing, not after: a board that silently adopted a blob
	 * which cannot unlock is the failure this whole path exists to avoid. */
	why = dead_blob_reason(&id, &ts);
	if (why != NULL) {
		(void)snprintf(s_out, sizeof(s_out), "refusing: %s", why);
		say(s_out);
		return;
	}

	rc = ultrawidelock_reader_import_blob(s_blob, len);
	if (rc != 0) {
		/* -1 malformed (already ruled out above), -2 settings write failed. */
		(void)snprintf(s_out, sizeof(s_out), "import of %u bytes failed rc=%d",
			       (unsigned)len, rc);
		say(s_out);
		return;
	}

	(void)snprintf(s_out, sizeof(s_out),
		       "adopted %u bytes: %u trust anchor(s). Reboot without SW2 to run.",
		       (unsigned)len, (unsigned)ts.count);
	say(s_out);
}

static void cmd_export(const char *arg)
{
	struct ultrawidelock_reader_identity id;
	struct ultrawidelock_trust_store ts;
	size_t len = 0;

	if (arg == NULL || strcmp(arg, "yes") != 0) {
		say("this prints the reader PRIVATE KEY. Confirm: export yes");
		return;
	}

	(void)ultrawidelock_prov_load(&id, &ts);
	if (ultrawidelock_prov_serialize(&id, &ts, s_blob, sizeof(s_blob), &len) != 0) {
		say("serialise failed");
		return;
	}
	if (!bin_to_hex(s_blob, len, s_hex, sizeof(s_hex))) {
		say("hex buffer too small");
		return;
	}

	say(s_hex);
	say("that string is the reader identity: whoever holds it can impersonate this lock");
}

static void cmd_erase(const char *arg)
{
	int rc;

	if (arg == NULL || strcmp(arg, "yes") != 0) {
		say("this erases the identity and every trust anchor. Confirm: erase yes");
		return;
	}

	rc = ultrawidelock_reader_provision_clear();
	if (rc != 0) {
		(void)snprintf(s_out, sizeof(s_out), "erase failed rc=%d", rc);
		say(s_out);
		return;
	}
	say("erased: back to the DEV identity with no trust anchors");
}

static void cmd_help(void)
{
	say("prov          show the stored reader identity and whether it can unlock");
	say("import <hex>  adopt an exported identity");
	say("export yes    print the stored identity, private key and all");
	say("erase yes     erase identity and trust anchors");
}

/* ---- the loop ------------------------------------------------------------ */

/*
 * One token, then the rest of the line.
 *
 * Not a general tokeniser, because no command here takes two arguments and the
 * one that takes a long one takes hex -- which must not be split, trimmed or
 * otherwise touched. Splitting on the first space and handing over everything
 * after it is the whole requirement.
 */
static char *split(char *line)
{
	char *sp = strchr(line, ' ');

	if (sp == NULL) {
		return NULL;
	}
	*sp = '\0';
	sp++;
	while (*sp == ' ') {
		sp++;
	}
	return (*sp != '\0') ? sp : NULL;
}

void ultrawidelock_freertos_prov_console_run(void)
{
	for (;;) {
		char *arg;

		if (!ultrawidelock_freertos_usb_ready()) {
			/* No host has opened the port. Nothing to say and
			 * nowhere to say it, so wait rather than spin. */
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}

		say("");
		say("UltraWideLock provisioning console. `help` for commands.");

		while (ultrawidelock_freertos_usb_ready()) {
			int n;

			(void)ultrawidelock_freertos_usb_write("> ", 2);
			n = ultrawidelock_freertos_usb_readline(s_line, sizeof(s_line));
			if (n < 0) {
				break; /* the host closed the port */
			}
			if (n == 0) {
				continue;
			}

			arg = split(s_line);

			if (strcmp(s_line, "prov") == 0) {
				cmd_prov();
			} else if (strcmp(s_line, "import") == 0) {
				cmd_import(arg);
			} else if (strcmp(s_line, "export") == 0) {
				cmd_export(arg);
			} else if (strcmp(s_line, "erase") == 0) {
				cmd_erase(arg);
			} else if (strcmp(s_line, "help") == 0) {
				cmd_help();
			} else {
				say("unknown command; `help` for the list");
			}
		}
	}
}
