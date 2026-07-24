/* logfake: minimal <zephyr/sys/cbprintf.h>. A "package" in this fake is the
 * already-rendered NUL-terminated string; cbpprintf streams it char by char. */
#ifndef LOGFAKE_ZEPHYR_SYS_CBPRINTF_H
#define LOGFAKE_ZEPHYR_SYS_CBPRINTF_H

typedef int (*cbprintf_cb)(int c, void *ctx);

int cbpprintf(cbprintf_cb out, void *ctx, void *packaged);

#endif /* LOGFAKE_ZEPHYR_SYS_CBPRINTF_H */
