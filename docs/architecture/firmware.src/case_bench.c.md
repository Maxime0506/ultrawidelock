<!-- generated documentation — edit the source, not this file -->
# `firmware/src/case_bench.c`

```mermaid
flowchart TD
  case_bench_run --> case_round
```

## API

### `static void psa_check(const char *what, psa_status_t st)`
`firmware/src/case_bench.c:116`

Check a PSA operation status. If nonzero, log an error with the operation name and status code,
and set bench_ok to false.

**called by** `case_round`

### `static psa_status_t make_ecc_key(psa_key_usage_t usage, psa_algorithm_t alg, psa_key_id_t *out)`
`firmware/src/case_bench.c:127`

An ECC key pair usable for one algorithm. The bench needs several: two
ephemeral pairs for the ECDH, and three signing pairs standing in for RCAC,
ICAC and the initiator's NOC.

**called by** `case_round`

### `static psa_status_t import_pub(const uint8_t *pub, size_t len, psa_algorithm_t alg, psa_key_id_t *out)`
`firmware/src/case_bench.c:144`

Import a P-256 public key for signature verification. Sets key usage to
PSA_KEY_USAGE_VERIFY_MESSAGE, returns PSA_SUCCESS on success, and writes the key ID to *out.

**called by** `case_round`

### `static void hkdf(const uint8_t *secret, size_t secret_len, const uint8_t *salt, size_t salt_len, const char *info, uint8_t *out, size_t out_len)`
`firmware/src/case_bench.c:165`

HKDF-SHA256, the KDF CASE uses for S2K, S3K and the session keys.
The repo's own aliro_hkdf(), not PSA key derivation, for two reasons. It is
what a hand-written node on this board would actually call: pure C11, already
linked, no extra flash. And PSA key derivation is not available in this image
at all -- PSA_WANT_ALG_HKDF depends on PSA_WANT_ALG_HMAC, which is off, so
psa_key_derivation_setup() returns PSA_ERROR_NOT_SUPPORTED (-134). The first
run of this bench found that the hard way; see prj.conf.

**called by** `case_round`

### `static uint32_t case_round(void)`
`firmware/src/case_bench.c:180`

One CASE handshake, its setup included, returning the microseconds spent in
the measured part. The certificate keys and the signatures over them are
regenerated on every call where a real responder would have them resident, so
a repeated round costs slightly MORE than the real thing rather than less --
which is the direction a contention test should err in.

**called by** `case_bench_run`  ·  **calls** `hkdf`, `import_pub`, `make_ecc_key`, `psa_check`

### `static void case_bench_run(void *a, void *b, void *c)`
`firmware/src/case_bench.c:292`

Thread entry point for the CASE responder benchmark. Sleeps 8 seconds to allow the reader to
initialize, then runs one round of CASE (5 P-256 ops + 2 HKDF + 2 AES-CCM). If
CONFIG_ALIRO_CASE_BENCH_PERIOD_MS is nonzero, enters contention mode: repeats the benchmark every
N milliseconds to measure whether P-256 running on the same core blocks walk-up ranging.

**calls** `case_round`

### `static int case_bench_start(void)`
`firmware/src/case_bench.c:370`

Initialize the CASE benchmark thread at the lowest application priority. Returns 0.
