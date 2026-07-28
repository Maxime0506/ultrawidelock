<!-- generated documentation — edit the source, not this file -->
# `ports/esp32/components/aliro_reader/aliro_stepup_worker.c`

@file aliro_stepup_worker.c
Step-up document verification worker for ESP32. Runs on a dedicated FreeRTOS task (6 KB stack, priority 4). Lazily creates a single-slot queue on first submission. Non-blocking submission: if a previous job is still enqueued, the new job is dropped. Verdict and connection handle are stored in shared state (spinlock-protected) and retrieved via aliro_stepup_worker_last(). Logging includes decrypted DeviceResponse hex and verdict breakdown (validity, element count, issuer found, signature OK, doctype OK, time OK, iteration OK).

## API

### `static void store_verdict(const struct aliro_stepup_verdict *v, uint16_t conn)`
`ports/esp32/components/aliro_reader/aliro_stepup_worker.c:44`

Store a verdict and connection handle to shared state, protected by spinlock. Called by the worker task when verification completes.

**called by** `run_job`

### `int aliro_stepup_worker_last(struct aliro_stepup_verdict *verdict, uint16_t *conn)`
`ports/esp32/components/aliro_reader/aliro_stepup_worker.c:56`

Retrieve the most recent step-up verdict and connection handle from the worker. Return 1 if a verdict is available (verdict and conn populated), 0 otherwise. Thread-safe via spinlock.

### `static void run_job(const struct aliro_stepup_job *job)`
`ports/esp32/components/aliro_reader/aliro_stepup_worker.c:75`

Decrypt and parse a step-up job: open the secure channel with reader and device keys, decrypt SessionData to extract DeviceResponse, parse it, verify the signature and document integrity, and store the verdict. Log the DeviceResponse hex and verdict details.

**called by** `worker_task`  ·  **calls** `store_verdict`

### `static void worker_task(void *arg)`
`ports/esp32/components/aliro_reader/aliro_stepup_worker.c:141`

Worker task: infinite loop receiving jobs from the queue and calling run_job to process each one.

**calls** `run_job`

### `int aliro_stepup_worker_submit(const struct aliro_stepup_job *job)`
`ports/esp32/components/aliro_reader/aliro_stepup_worker.c:156`

Submit a step-up verification job to the worker queue. Create the queue and task on first use. Non-blocking: if a previous job is still queued, drop this one and return -1 (the reader must never stall). Return 0 on success, -1 on queue full or creation failure.
