/*
 * The dispatcher check compiles the pinned Nordic sources against the real
 * nrfxlib headers, so this directory holds only what those headers need from
 * the target toolchain. It is kept out of fake/ proper because the recording
 * doubles there shadow the real sdc headers on purpose.
 *
 * On target these two spellings come from cmsis_compiler.h by way of nrfx.h.
 */
#ifndef ULTRAWIDELOCK_DISPATCHER_NRFX_H
#define ULTRAWIDELOCK_DISPATCHER_NRFX_H

/* The real nrfx.h reaches these transitively, and vendor sources rely on it. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#define __PACKED   __attribute__((packed))
#define __ALIGN(n) __attribute__((aligned(n)))

#endif /* ULTRAWIDELOCK_DISPATCHER_NRFX_H */
