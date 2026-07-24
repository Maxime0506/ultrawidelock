/*
 * Copyright (c) 2026 asxeem
 * SPDX-License-Identifier: ISC
 *
 * sdkfake — shared plumbing for the host-side ESP-IDF/NimBLE fakes under this
 * directory. These headers let the target-only ESP32 sources compile with plain
 * cc so their branch logic can be unit-tested; every SDK entry point is a
 * recording double implemented by the test binary (or fake_nvs.c). Nothing here
 * talks to hardware, so a passing suite proves wiring and branch logic against
 * the fakes, NOT hardware truth.
 */
#ifndef SDKFAKE_H
#define SDKFAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#endif /* SDKFAKE_H */
