/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * Host test for the ESP32 step-up worker (aliro_stepup_worker.c) against the
 * sdkfake FreeRTOS doubles. "Theatre" suite: the queue/task are synchronous
 * fakes pumped from the test (the worker's forever-loop is exited via a
 * longjmp hook), so passing proves the submit/drop/verdict-store wiring and
 * run_job's decrypt->parse->verify branch logic — not real task scheduling.
 * The decrypt, CBOR parse and section-7.4 verifier underneath run the REAL
 * shared-core code on the KAT vectors from stepup_vectors.h (fake-EC prim
 * double for ES256, as in test_aliro_stepup.c).
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "aliro_crypto.h"
#include "aliro_prim.h"
#include "aliro_stepup.h"
#include "stepup_vectors.h"

static int fails;

static void okc(const char *name, int cond)
{
	if (!cond) {
		printf("  FAIL %s\n", name);
		fails++;
	} else {
		printf("  ok   %s\n", name);
	}
}

/* Pump the recorded worker task until its queue empties, then longjmp back. */
static jmp_buf s_pump_out;

static void pump_break(void)
{
	longjmp(s_pump_out, 1);
}

static void pump_worker(void)
{
	if (fake_task_count == 0) {
		return;
	}
	fake_queue_block_hook = pump_break;
	if (setjmp(s_pump_out) == 0) {
		fake_tasks[0].fn(fake_tasks[0].arg);
	}
	fake_queue_block_hook = NULL;
}

/* Seal a DeviceResponse in the device direction (SKDevice, dir 1, ctr `ctr`)
 * into a SessionData wrapper {"data": bstr(ct||tag)} the worker can open. */
static size_t seal_device_sd(const uint8_t skd[32], uint32_t ctr, const uint8_t *plain,
			     size_t plain_len, uint8_t *out, size_t cap)
{
	size_t ct_len = plain_len + ALIRO_GCM_TAG_LEN;
	uint8_t hdr[16];
	size_t h = 0;

	hdr[h++] = 0xa1; /* map(1) */
	hdr[h++] = 0x64; /* text(4) "data" */
	hdr[h++] = 'd';
	hdr[h++] = 'a';
	hdr[h++] = 't';
	hdr[h++] = 'a';
	if (ct_len < 24) {
		hdr[h++] = (uint8_t)(0x40 + ct_len);
	} else if (ct_len < 256) {
		hdr[h++] = 0x58;
		hdr[h++] = (uint8_t)ct_len;
	} else {
		hdr[h++] = 0x59;
		hdr[h++] = (uint8_t)(ct_len >> 8);
		hdr[h++] = (uint8_t)ct_len;
	}
	if (h + ct_len > cap) {
		return 0;
	}
	memcpy(out, hdr, h);

	uint8_t nonce[ALIRO_GCM_NONCE_LEN];

	aliro_crypto_gcm_nonce(1, ctr, nonce);
	if (aliro_aes256_gcm_encrypt(skd, nonce, sizeof(nonce), NULL, 0, plain, plain_len,
				     out + h, out + h + plain_len, ALIRO_GCM_TAG_LEN) != 0) {
		return 0;
	}
	return h + ct_len;
}

/* SV_GOOD carries a synthetic golden signature the fake-EC prim cannot verify.
 * Re-sign the COSE Sig_structure with the prim double's own keypair and patch
 * the signature bytes in a copy, so the worker's hardcoded
 * ctx.ecdsa_verify = aliro_ecdsa_p256_verify accepts it end-to-end. */
static uint8_t s_good[600];
static size_t s_good_len;
static uint8_t s_issuer_pub[65];

static int resign_good_vector(void)
{
	uint8_t priv[32], sig[64];

	memset(priv, 0x42, sizeof(priv));
	if (aliro_ec_p256_pub_from_priv(priv, s_issuer_pub) != 0 ||
	    aliro_ecdsa_p256_sign(priv, SV_GOLDEN_SIGSTRUCT, SV_GOLDEN_SIGSTRUCT_len, sig) != 0) {
		return -1;
	}
	memcpy(s_good, SV_GOOD, SV_GOOD_len);
	s_good_len = SV_GOOD_len;
	for (size_t i = 0; i + 64 <= s_good_len; i++) {
		if (memcmp(s_good + i, SV_GOLDEN_SIG, 64) == 0) {
			memcpy(s_good + i, sig, 64);
			return 0;
		}
	}
	return -1;
}

static struct aliro_stepup_job base_job(const uint8_t skr[32], const uint8_t skd[32])
{
	struct aliro_stepup_job job;

	memset(&job, 0, sizeof(job));
	memcpy(job.sk_reader, skr, 32);
	memcpy(job.sk_device, skd, 32);
	memcpy(job.issuer_pub, s_issuer_pub, sizeof(s_issuer_pub));
	memcpy(job.issuer_kid, SV_KID, SV_KID_len);
	job.issuer_kid_len = SV_KID_len;
	job.have_issuer = 1;
	job.time_valid = 1;
	job.now_epoch = SV_EPOCH_NOW; /* inside the vectors' validity window */
	job.conn_handle = 33;
	return job;
}

int main(void)
{
	uint8_t skr[32], skd[32];

	/* Any deterministic key pair works: run_job re-derives nothing. */
	memset(skr, 0x11, sizeof(skr));
	memset(skd, 0x22, sizeof(skd));

	fake_freertos_reset();

	printf("-- worker lifecycle --\n");
	okc("re-sign vector for the fake EC", resign_good_vector() == 0);

	struct aliro_stepup_verdict v;
	uint16_t conn = 0;

	okc("no verdict before any job", aliro_stepup_worker_last(&v, &conn) == 0);

	/* Queue-create failure -> submit fails, nothing recorded. */
	struct aliro_stepup_job job = base_job(skr, skd);

	job.sd_len = seal_device_sd(skd, 1, s_good, s_good_len, job.sd, sizeof(job.sd));
	okc("device-direction seal", job.sd_len > 0);

	fake_queue_create_rc = 0;
	okc("submit w/ queue-create failure", aliro_stepup_worker_submit(&job) == -1);
	fake_queue_create_rc = 1;

	/* NOT covered: the xTaskCreate-failure branch. Failing it once leaves the
	 * module's static s_queue created, so every later submit would bypass task
	 * creation and this suite could never run the worker afterwards. */

	printf("-- verify pipeline (real codec + verifier on KAT vectors) --\n");

	okc("submit good job", aliro_stepup_worker_submit(&job) == 0);
	okc("worker task created", fake_task_count == 1 &&
	    strcmp(fake_tasks[0].name, "aliro_stepup") == 0);

	/* Second submit while the first is still queued: dropped, not queued. */
	okc("second submit drops", aliro_stepup_worker_submit(&job) == -1);

	pump_worker();
	okc("verdict recorded", aliro_stepup_worker_last(&v, &conn) == 1);
	okc("verdict VALID on SV_GOOD", v.valid == 1 && conn == 33);
	okc("verdict detail (sig+digests+doctype+time)",
	    v.sig_ok == 1 && v.digests_ok == 1 && v.doctype_ok == 1 && v.time_ok == 1);

	/* Tampered digest -> invalid verdict, still recorded. */
	job = base_job(skr, skd);
	job.conn_handle = 44;
	job.sd_len = seal_device_sd(skd, 1, SV_TAMPERED, SV_TAMPERED_len, job.sd, sizeof(job.sd));
	okc("tampered seal", job.sd_len > 0);
	okc("submit tampered", aliro_stepup_worker_submit(&job) == 0);
	pump_worker();
	okc("tampered verdict recorded", aliro_stepup_worker_last(&v, &conn) == 1 && conn == 44);
	okc("tampered verdict invalid", v.valid == 0);

	/* No issuer provisioned -> issuer_key_found = 0. */
	job = base_job(skr, skd);
	job.have_issuer = 0;
	job.conn_handle = 55;
	job.sd_len = seal_device_sd(skd, 1, s_good, s_good_len, job.sd, sizeof(job.sd));
	okc("submit no-issuer", aliro_stepup_worker_submit(&job) == 0);
	pump_worker();
	okc("no-issuer verdict", aliro_stepup_worker_last(&v, &conn) == 1 && conn == 55 &&
	    v.issuer_key_found == 0 && v.valid == 0);

	printf("-- early-out branches --\n");

	/* Decrypt failure (wrong counter): aborted before parse, verdict unchanged. */
	job = base_job(skr, skd);
	job.conn_handle = 66;
	job.sd_len = seal_device_sd(skd, 9, s_good, s_good_len, job.sd, sizeof(job.sd));
	okc("submit bad-counter", aliro_stepup_worker_submit(&job) == 0);
	pump_worker();
	okc("decrypt failure keeps last verdict",
	    aliro_stepup_worker_last(&v, &conn) == 1 && conn == 55);

	/* Parse failure: decrypt succeeds (ctr 1) but plaintext is not CBOR. */
	static const uint8_t junk[4] = {0xFF, 0xFF, 0xFF, 0xFF};

	job = base_job(skr, skd);
	job.conn_handle = 77;
	job.sd_len = seal_device_sd(skd, 1, junk, sizeof(junk), job.sd, sizeof(job.sd));
	okc("submit junk-plaintext", aliro_stepup_worker_submit(&job) == 0);
	pump_worker();
	okc("parse failure keeps last verdict",
	    aliro_stepup_worker_last(&v, &conn) == 1 && conn == 55);

	okc("last with NULL conn ptr", aliro_stepup_worker_last(&v, NULL) == 1);

	printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
	return fails == 0 ? 0 : 1;
}
