/*
 * Liveness deadline monitor for zone controller Status streams (SG-001, SG-004).
 *
 * A Monitor subscribes to each registered zone controller and resets a
 * per-zone deadline timer on every incoming Status frame. If no Status
 * arrives within the deadline, the zone transitions to dead and a
 * rcp_liveness_event_t is emitted. Recovery to alive is reported as soon as
 * the next Status arrives.
 */
#ifndef RCP_DEADLINE_H
#define RCP_DEADLINE_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    rcp_zone_t zone;
    bool       alive;
    int        err; /* non-zero (an rcp_errc_t) only when the initial subscribe() itself failed */
} rcp_liveness_event_t;

typedef struct {
    uint64_t deadline_ms; /* time without a Status before a zone is dead (default: 50, 20 Hz cadence) */
} rcp_deadline_config_t;

/* { deadline_ms = 50 }. */
rcp_deadline_config_t rcp_deadline_default_config(void);

/* User-supplied callback fired on every liveness transition. user_data is
 * the opaque pointer passed to rcp_deadline_monitor_subscribe(). */
typedef void (*rcp_deadline_liveness_fn)(const rcp_liveness_event_t *ev, void *user_data);

typedef struct rcp_deadline_monitor rcp_deadline_monitor_t;

/* Creates a Monitor over the given controllers (retains each) and starts one
 * background watch thread per controller immediately, subscribing to its
 * Status stream. ctrls/n_ctrls may describe zero controllers. Returns NULL
 * on allocation failure. */
rcp_deadline_monitor_t *rcp_deadline_monitor_new(rcp_deadline_config_t cfg,
                                                  rcp_controller_t *const *ctrls, size_t n_ctrls);

/* Returns whether zone is currently considered alive. False for any zone not
 * registered with m, and false for a registered zone before its first
 * Status arrives. */
bool rcp_deadline_monitor_alive(rcp_deadline_monitor_t *m, rcp_zone_t zone);

/* Registers cb to be invoked on every liveness transition, across all zones.
 * Not thread-safe with close()/destroy(); register before handing m to
 * other threads. Returns false on allocation failure (cb not added). */
bool rcp_deadline_monitor_subscribe(rcp_deadline_monitor_t *m, rcp_deadline_liveness_fn cb, void *user_data);

/* Signals every watch thread to stop and joins them. Idempotent; safe to
 * call before rcp_deadline_monitor_destroy(). */
void rcp_deadline_monitor_close(rcp_deadline_monitor_t *m);

/* Closes m (if not already) and frees it, releasing its references to every
 * registered controller. Call exactly once. */
void rcp_deadline_monitor_destroy(rcp_deadline_monitor_t *m);

#ifdef __cplusplus
}
#endif

#endif /* RCP_DEADLINE_H */
