/* nfcfake: <zephyr/device.h> plus the devicetree accessors pn532_bus_spi.c
 * needs. There is no devicetree here, so the *_INST_GET macros expand to
 * initializers pointing at the fake bus and IRQ devices. */
#ifndef NFCFAKE_ZEPHYR_DEVICE_H
#define NFCFAKE_ZEPHYR_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Zephyr's device handle, trimmed to the one field the driver logs. */
struct device {
	const char *name;
};

/* C linkage: the devicetree initializers below are expanded in a C driver but
 * these objects are defined in the C++ fake. */
#ifdef __cplusplus
extern "C" {
#endif
extern const struct device nfcfake_spi_bus;
extern const struct device nfcfake_gpio_port;
#ifdef __cplusplus
}
#endif

/* One enabled instance: the BUILD_ASSERT in pn532_bus_spi.c checks exactly
 * this, and a build with none or two is a real configuration error. */
#define DT_NUM_INST_STATUS_OKAY(compat) 1

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

#ifndef BUILD_ASSERT
#define BUILD_ASSERT(cond, ...) _Static_assert((cond), "" __VA_ARGS__)
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, field)                                                             \
	((type *)(void *)((char *)(ptr) - offsetof(type, field)))
#endif

#endif /* NFCFAKE_ZEPHYR_DEVICE_H */
