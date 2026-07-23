// aliro-presence-setup — pairing helper for the presence second factor.
//   keygen  : mint the 32-byte symmetric pairing key, write it to the host key
//             file (0600), and print the hex to load onto the dongle.
//   cred-id : derive the 8-byte credential id from a credential public key, for
//             binding a config to one enrolled iPhone (cred_id = ...).
/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "aliro_assert.h"
#include "aliro_presence.h"

static int rand_bytes(uint8_t *out, size_t len)
{
	int fd = open("/dev/urandom", O_RDONLY);

	if (fd < 0) {
		return -1;
	}
	size_t got = 0;
	while (got < len) {
		ssize_t n = read(fd, out + got, len - got);
		if (n <= 0) {
			close(fd);
			return -1;
		}
		got += (size_t)n;
	}
	close(fd);
	return 0;
}

static void print_hex(const uint8_t *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		printf("%02x", b[i]);
	}
	printf("\n");
}

static int hexnib(char c)
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

static int hexdecode(const char *s, uint8_t *out, size_t want)
{
	if (strlen(s) != want * 2u) {
		return -1;
	}
	for (size_t i = 0; i < want; i++) {
		int hi = hexnib(s[2u * i]);
		int lo = hexnib(s[2u * i + 1u]);
		if (hi < 0 || lo < 0) {
			return -1;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

// Writes the 34-byte key-load frame to the dongle device. 0 on success, -1 else.
static int pair_device(const char *dev, const uint8_t key[ALIRO_ASSERT_KEY_LEN])
{
	int fd = open(dev, O_RDWR | O_NOCTTY);

	if (fd < 0) {
		fprintf(stderr, "pair: cannot open %s\n", dev);
		return -1;
	}
	uint8_t frame[PRESENCE_KEYSET_LEN];
	presence_build_keyset(key, frame);
	ssize_t n = write(fd, frame, sizeof(frame));
	close(fd);
	if (n != (ssize_t)sizeof(frame)) {
		fprintf(stderr, "pair: short write to %s\n", dev);
		return -1;
	}
	return 0;
}

static int cmd_keygen(const char *out_path, const char *device)
{
	uint8_t key[ALIRO_ASSERT_KEY_LEN];

	if (rand_bytes(key, sizeof(key)) != 0) {
		fprintf(stderr, "keygen: could not read /dev/urandom\n");
		return 1;
	}
	int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		fprintf(stderr, "keygen: cannot open %s for writing (need root?)\n", out_path);
		return 1;
	}
	if (write(fd, key, sizeof(key)) != (ssize_t)sizeof(key)) {
		fprintf(stderr, "keygen: short write to %s\n", out_path);
		close(fd);
		return 1;
	}
	close(fd);
	printf("wrote 32-byte pairing key to %s (mode 0600)\n", out_path);

	if (device != NULL) {
		if (pair_device(device, key) != 0) {
			return 1;
		}
		printf("loaded the pairing key onto the dongle at %s\n", device);
	} else {
		printf("pair the dongle later with:  aliro-presence-setup keygen "
		       "--out %s --device <dev>\nor load this key over its link:  ",
		       out_path);
		print_hex(key, sizeof(key));
	}
	printf("keep this key secret: anyone holding it can forge presence assertions.\n");
	return 0;
}

static int cmd_cred_id(const char *pub_hex)
{
	uint8_t pub[65];

	if (hexdecode(pub_hex, pub, sizeof(pub)) != 0) {
		fprintf(stderr, "cred-id: expected 130 hex chars (a 65-byte P-256 point)\n");
		return 1;
	}
	uint8_t cid[ALIRO_ASSERT_CREDID_LEN];
	aliro_assert_cred_id(pub, cid);
	printf("cred_id = ");
	print_hex(cid, sizeof(cid));
	return 0;
}

static int usage(void)
{
	fprintf(stderr,
		"usage:\n"
		"  aliro-presence-setup keygen [--out /etc/aliro-presence/key] [--device <dev>]\n"
		"  aliro-presence-setup cred-id --pub <130-hex credential public key>\n");
	return 2;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		return usage();
	}
	if (strcmp(argv[1], "keygen") == 0) {
		const char *out = "/etc/aliro-presence/key";
		const char *device = NULL;
		for (int i = 2; i < argc; i++) {
			if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
				out = argv[++i];
			} else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
				device = argv[++i];
			} else {
				return usage();
			}
		}
		return cmd_keygen(out, device);
	}
	if (strcmp(argv[1], "cred-id") == 0) {
		if (argc != 4 || strcmp(argv[2], "--pub") != 0) {
			return usage();
		}
		return cmd_cred_id(argv[3]);
	}
	return usage();
}
