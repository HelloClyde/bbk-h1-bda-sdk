#ifndef H1_POWER_H
#define H1_POWER_H

#include "h1_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return 1 on a shutdown request so the caller can clean up and return to the
 * firmware. Return 0 when run() finishes normally. Call on the BDA GUI thread. */
int h1_power_run(void (*run)(void));

/* Optional for custom builds that use only SDK headers. */
void h1_power_unwind(void) __attribute__((weak));

#ifdef __cplusplus
}
#endif

/* Check at a foreground polling boundary. The V1.41 power IRQ turns off the
 * display before setting this latch. Reading it leaves other system events alone. */
static inline void h1_power_poll(void)
{
    void *table = h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT);
    if (table != (void *)0x802aa110u) {
        return;
    }

    typedef int (*event_fn)(int *, int *);
    event_fn service = (event_fn)h1_runtime_entry(table, 0x720u);
    if (service != (event_fn)0x8001f3e8u ||
        !*(volatile const h1_u32 *)0x80584a30u) {
        return;
    }

    if (h1_power_unwind) {
        h1_power_unwind();
    }

    /* The native GUI event service handles the latched power request first. */
    int code, value;
    service(&code, &value);
}

#endif
