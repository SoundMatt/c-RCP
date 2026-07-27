/*
 * Watchdog keeper for ASIL-B zone controller liveness (SG-001, SG-003, SG-007).
 *
 * A Keeper periodically sends RCP_CMD_WATCHDOG to each registered zone
 * controller. Consecutive failures transition a zone through the health
 * state machine: Healthy -> Degraded -> Faulted; a subsequent success
 * transitions it directly back to Healthy.
 *
 * Deviation from cpp-RCP's watchdog.hpp: cpp-RCP dispatches each zone's kick
 * on its own detached thread every interval, and its destructor only joins
 * the run thread — spawned kick threads are never waited on, so a kick still
 * in flight when the Keeper is destroyed captures a dangling `this`. This
 * port kicks zones sequentially within the single run thread instead (each
 * kick is already bounded by its own rcp_context_t timeout), which fully
 * avoids that lifetime hazard at the cost of only kicking zones one at a
 * time per cycle rather than in parallel — acceptable given the small,
 * fixed zone count and short per-kick timeouts this protocol targets.
 */
#ifndef RCP_WATCHDOG_H
#define RCP_WATCHDOG_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RCP_HEALTH_HEALTHY  = 0,
    RCP_HEALTH_DEGRADED = 1,
    RCP_HEALTH_FAULTED  = 2,
} rcp_health_state_t;

/* Never returns NULL. */
const char *rcp_health_state_string(rcp_health_state_t h);

typedef struct {
    rcp_zone_t         zone;
    rcp_health_state_t state;
    int                err; /* the rcp_errc_t from the kick that caused this transition */
} rcp_health_event_t;

typedef struct {
    uint64_t interval_ms;   /* time between kick cycles (default: 10) */
    uint64_t timeout_ms;    /* per-kick deadline (default: 5) */
    int      degrade_after; /* consecutive misses before Degraded (default: 3) */
    int      fault_after;   /* consecutive misses before Faulted (default: 5) */
} rcp_watchdog_config_t;

/* { interval_ms = 10, timeout_ms = 5, degrade_after = 3, fault_after = 5 }. */
rcp_watchdog_config_t rcp_watchdog_default_config(void);

/* User-supplied callback fired on every health state change. user_data is
 * the opaque pointer passed to rcp_watchdog_keeper_subscribe(). */
typedef void (*rcp_watchdog_health_fn)(const rcp_health_event_t *ev, void *user_data);

typedef struct rcp_watchdog_keeper rcp_watchdog_keeper_t;

/* Creates a Keeper over the given controllers (retains each) and starts its
 * background kick thread immediately. ctrls/n_ctrls may describe zero
 * controllers. Returns NULL on allocation failure. */
rcp_watchdog_keeper_t *rcp_watchdog_keeper_new(rcp_watchdog_config_t cfg,
                                                rcp_controller_t *const *ctrls, size_t n_ctrls);

/* Returns the current health state for zone, or RCP_HEALTH_FAULTED if zone
 * was not registered with k (matching cpp-RCP's own "unknown -> assume the
 * worst" default). */
rcp_health_state_t rcp_watchdog_keeper_health(rcp_watchdog_keeper_t *k, rcp_zone_t zone);

/* Registers cb to be invoked on every health state transition, across all
 * zones. Not thread-safe with close()/destroy(); register before handing k
 * to other threads. Returns false on allocation failure (cb not added). */
bool rcp_watchdog_keeper_subscribe(rcp_watchdog_keeper_t *k, rcp_watchdog_health_fn cb, void *user_data);

/* Stops the background kick thread. Idempotent; safe to call before
 * rcp_watchdog_keeper_destroy(). Blocks until the current kick cycle (if
 * any) finishes. */
void rcp_watchdog_keeper_close(rcp_watchdog_keeper_t *k);

/* Closes k (if not already) and frees it, releasing its references to every
 * registered controller. Call exactly once. */
void rcp_watchdog_keeper_destroy(rcp_watchdog_keeper_t *k);

#ifdef __cplusplus
}
#endif

#endif /* RCP_WATCHDOG_H */
