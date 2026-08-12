/**
 * @file ultrawidelock_slam_lis2dh12.c — LIS2DH12 impact interrupt, raw registers (Zephyr).
 *
 * Talks to the accelerometer over I2C directly and attaches one GPIO callback.
 * No sensor device, no trigger thread, no work queue. ultrawidelock_slam.h records the
 * measured reason; the short version is that both of Zephyr's lis2dh trigger
 * modes cost more RAM than the CDK Matter image has to spare.
 */

#include "ultrawidelock_slam_hw.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "woz_log.h"

LOG_MODULE_REGISTER(ultrawidelock_slam, CONFIG_ULTRAWIDELOCK_ANCHOR_SLAM_LOG_LEVEL);

#define ACCEL_NODE DT_ALIAS(accel0)

/* LIS2DH12 register map, the subset this file writes. */
#define REG_WHO_AM_I  0x0Fu
#define REG_CTRL1     0x20u
#define REG_CTRL2     0x21u
#define REG_CTRL3     0x22u
#define REG_CTRL4     0x23u
#define REG_CTRL5     0x24u
#define REG_REFERENCE 0x26u
#define REG_INT1_CFG  0x30u
#define REG_INT1_THS  0x32u
#define REG_INT1_DUR  0x33u

#define WHO_AM_I_LIS2DH12 0x33u

/* CTRL1: ODR 10 Hz (0b0010), low-power mode, all three axes enabled. 10 Hz is
 * the slowest rate that still catches a strike, and this part is on a battery
 * budget it does not otherwise contribute to. */
#define CTRL1_10HZ_LP_XYZ    0x2Fu
/* CTRL2: run the interrupt generator off the high-pass filter. Without it the
 * static 1 g of gravity sits inside the threshold budget and the trip point
 * becomes a function of how the sensor is mounted. With it, the threshold means
 * "transient acceleration", which is what a slam is. */
#define CTRL2_HPF_ON_INT1    0x01u
/* CTRL3: route the INT1 generator to the INT1 pin. */
#define CTRL3_I1_IA1         0x40u
/* CTRL4: full scale +/-4 g, which fixes INT1_THS at 32 mg per LSB. */
#define CTRL4_FS_4G          0x10u
#define THS_MG_PER_LSB       32u
#define THS_MAX              0x7Fu
/* CTRL5: interrupt NOT latched. Deliberate: a latched interrupt has to be
 * cleared by reading INT1_SRC over I2C, and the only place that discovers the
 * assertion is the GPIO callback, which runs in interrupt context where I2C is
 * illegal. Unlatched, the pin clears itself and the callback only ever touches
 * an atomic. */
#define CTRL5_NO_LATCH       0x00u
/* INT1_CFG: OR of the three high events. Written last, so the pin cannot fire
 * part-way through configuration. */
#define INT1_CFG_HIGH_OR_XYZ 0x2Au

static const struct i2c_dt_spec s_bus = I2C_DT_SPEC_GET(ACCEL_NODE);
static const struct gpio_dt_spec s_int1 = GPIO_DT_SPEC_GET(ACCEL_NODE, irq_gpios);

/* The whole RAM cost of this file: one callback object and one flag. */
static struct gpio_callback s_cb;
static atomic_t s_struck;

static void int1_isr(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	/* The entire interrupt handler. Nothing here may log or touch I2C: this
	 * runs in interrupt context, and the ranging path's arm deadline is
	 * ~1836 us. */
	atomic_set(&s_struck, 1);
}

static int wr(uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte_dt(&s_bus, reg, val);
}

int ultrawidelock_slam_hw_init(void)
{
	uint8_t who = 0u;
	uint8_t ths;
	int rc;

	if (!i2c_is_ready_dt(&s_bus) || !gpio_is_ready_dt(&s_int1)) {
		return -ENODEV;
	}

	/* Prove the part is there before arming anything. A board without it
	 * should lose the tamper signal, not gain an interrupt that never
	 * fires and a silent assumption that the door is never struck. */
	if (i2c_reg_read_byte_dt(&s_bus, REG_WHO_AM_I, &who) != 0 || who != WHO_AM_I_LIS2DH12) {
		LOG_WRN("lis2dh12: WHO_AM_I 0x%02x, expected 0x%02x", who, WHO_AM_I_LIS2DH12);
		return -ENODEV;
	}

	/* CONFIG_ULTRAWIDELOCK_ANCHOR_SLAM_THRESHOLD_MG is a placeholder until a bench
	 * capture of this door replaces it; the Kconfig help says so. Clamp
	 * rather than truncate, so an out-of-range value fails loudly at the
	 * top of the scale instead of wrapping into a hair trigger. */
	ths = (uint8_t)(CONFIG_ULTRAWIDELOCK_ANCHOR_SLAM_THRESHOLD_MG / THS_MG_PER_LSB);
	if (ths > THS_MAX) {
		ths = THS_MAX;
	}
	if (ths == 0u) {
		ths = 1u;
	}

	rc = wr(REG_CTRL1, CTRL1_10HZ_LP_XYZ);
	rc |= wr(REG_CTRL2, CTRL2_HPF_ON_INT1);
	rc |= wr(REG_CTRL4, CTRL4_FS_4G);
	rc |= wr(REG_CTRL5, CTRL5_NO_LATCH);
	rc |= wr(REG_INT1_THS, ths);
	rc |= wr(REG_INT1_DUR, 0u);
	rc |= wr(REG_CTRL3, CTRL3_I1_IA1);
	if (rc != 0) {
		return -EIO;
	}

	/* Reading REFERENCE initialises the high-pass filter to the current
	 * acceleration. Skipping it leaves the filter charged from whatever was
	 * in it at power-on, which shows up as a burst of spurious interrupts in
	 * the first seconds after boot. */
	(void)i2c_reg_read_byte_dt(&s_bus, REG_REFERENCE, &who);

	if (gpio_pin_configure_dt(&s_int1, GPIO_INPUT) != 0) {
		return -EIO;
	}
	gpio_init_callback(&s_cb, int1_isr, BIT(s_int1.pin));
	if (gpio_add_callback(s_int1.port, &s_cb) != 0) {
		return -EIO;
	}
	if (gpio_pin_interrupt_configure_dt(&s_int1, GPIO_INT_EDGE_TO_ACTIVE) != 0) {
		gpio_remove_callback(s_int1.port, &s_cb);
		return -EIO;
	}

	/* Arm the generator last. */
	if (wr(REG_INT1_CFG, INT1_CFG_HIGH_OR_XYZ) != 0) {
		gpio_pin_interrupt_configure_dt(&s_int1, GPIO_INT_DISABLE);
		gpio_remove_callback(s_int1.port, &s_cb);
		return -EIO;
	}

	/* Boot itself is a mechanical event: a board being screwed to a door
	 * frame will have been knocked about. Start from a clean slate. */
	atomic_clear(&s_struck);
	LOG_INF("lis2dh12: impact interrupt armed at %u mg",
		CONFIG_ULTRAWIDELOCK_ANCHOR_SLAM_THRESHOLD_MG);
	return 0;
}

bool ultrawidelock_slam_hw_take(void)
{
	return atomic_set(&s_struck, 0) != 0;
}
