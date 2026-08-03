/* dfufake: <zephyr/sys/reboot.h>. Recording only — the receiver's reboot is
 * deferred precisely so the reply reaches the host first, and a suite proves
 * that ordering by checking the reply bytes and then this counter. */
#ifndef DFUFAKE_ZEPHYR_SYS_REBOOT_H
#define DFUFAKE_ZEPHYR_SYS_REBOOT_H

#define SYS_REBOOT_WARM 0
#define SYS_REBOOT_COLD 1

void sys_reboot(int type);

#endif /* DFUFAKE_ZEPHYR_SYS_REBOOT_H */
