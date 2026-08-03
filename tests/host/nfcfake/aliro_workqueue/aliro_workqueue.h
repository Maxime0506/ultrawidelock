/* nfcfake: aliro_workqueue/aliro_workqueue.h.
 *
 * ON TARGET THIS DEFERS; HERE IT RUNS THE HANDLER INLINE. That is a real
 * fidelity gap and it is deliberate: the three work handlers in
 * transport_pn532.cpp are the only path from the polling thread into the
 * stack, and nothing in a host binary is going to drain a Zephyr workqueue.
 * Running them synchronously reaches them and preserves their ORDER, which is
 * what the transport's contract is actually about. It does not model the
 * thread hand-off, so nothing here says anything about races. */
#ifndef NFCFAKE_ALIRO_WORKQUEUE_H
#define NFCFAKE_ALIRO_WORKQUEUE_H

#include <zephyr/kernel.h>

int AliroWorkqueueSubmit(struct k_work *work);

#endif /* NFCFAKE_ALIRO_WORKQUEUE_H */
