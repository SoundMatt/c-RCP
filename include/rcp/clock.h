#ifndef RCP_CLOCK_H
#define RCP_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic clock in milliseconds since an arbitrary epoch (process start on
 * most platforms). Never goes backwards; suitable for deadlines and elapsed-
 * time measurement, never for wall-clock/calendar time. */
uint64_t rcp_monotonic_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* RCP_CLOCK_H */
