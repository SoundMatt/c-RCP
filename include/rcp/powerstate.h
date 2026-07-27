/*
 * Zone controller power state manager (SG-003, ISO 26262 ASIL-B).
 *
 * A Manager sends RCP_CMD_SLEEP / RCP_CMD_WAKE to zone controllers and
 * tracks the resulting power state. When a command fails the zone
 * transitions to RCP_POWER_BUS_OFF and the Manager retries RCP_CMD_WAKE at
 * the configured recovery interval until it succeeds.
 */
#ifndef RCP_POWERSTATE_H
#define RCP_POWERSTATE_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RCP_POWER_ACTIVE   = 0,
    RCP_POWER_SLEEPING = 1,
    RCP_POWER_BUS_OFF  = 2,
} rcp_power_state_t;

/* Never returns NULL. */
const char *rcp_power_state_string(rcp_power_state_t p);

typedef struct {
    rcp_zone_t        zone;
    rcp_power_state_t state;
    int               err; /* the rcp_errc_t from the command that caused this transition */
} rcp_power_event_t;

typedef struct {
    uint64_t recovery_interval_ms; /* time between BusOff recovery attempts (default: 100) */
    uint64_t recovery_timeout_ms;  /* per-recovery-attempt deadline (default: 50) */
} rcp_powerstate_config_t;

/* { recovery_interval_ms = 100, recovery_timeout_ms = 50 }. */
rcp_powerstate_config_t rcp_powerstate_default_config(void);

/* User-supplied callback fired on every power state transition. user_data
 * is the opaque pointer passed to rcp_powerstate_manager_subscribe(). */
typedef void (*rcp_powerstate_power_fn)(const rcp_power_event_t *ev, void *user_data);

typedef struct rcp_powerstate_manager rcp_powerstate_manager_t;

/* Creates a Manager over the given controllers (retains each), starting
 * every zone at RCP_POWER_ACTIVE, and starts its background recovery thread
 * immediately. ctrls/n_ctrls may describe zero controllers. Returns NULL on
 * allocation failure. */
rcp_powerstate_manager_t *rcp_powerstate_manager_new(rcp_powerstate_config_t cfg,
                                                      rcp_controller_t *const *ctrls, size_t n_ctrls);

/* Returns the current power state for zone, or RCP_POWER_BUS_OFF if zone
 * was not registered with m (matching this project's "unknown -> assume
 * the worst" convention, also used by rcp_watchdog_keeper_health()). Safe
 * to call from multiple threads concurrently. */
rcp_power_state_t rcp_powerstate_manager_state(rcp_powerstate_manager_t *m, rcp_zone_t zone);

/* Sends RCP_CMD_SLEEP to zone. Returns RCP_ERR_NOT_FOUND if zone was not
 * registered with m. Returns RCP_ERR_BUSY without sending if zone is not
 * currently RCP_POWER_ACTIVE. On send failure, zone transitions to
 * RCP_POWER_BUS_OFF and the send's error is returned; on success it
 * transitions to RCP_POWER_SLEEPING and RCP_OK is returned. */
int rcp_powerstate_manager_sleep(rcp_powerstate_manager_t *m, const rcp_context_t *ctx, rcp_zone_t zone);

/* Sends RCP_CMD_WAKE to zone. Returns RCP_ERR_NOT_FOUND if zone was not
 * registered with m. Returns RCP_ERR_BUSY without sending if zone is
 * already RCP_POWER_ACTIVE. On send failure, zone transitions to
 * RCP_POWER_BUS_OFF and the send's error is returned; on success it
 * transitions to RCP_POWER_ACTIVE and RCP_OK is returned. */
int rcp_powerstate_manager_wake(rcp_powerstate_manager_t *m, const rcp_context_t *ctx, rcp_zone_t zone);

/* Registers cb to be invoked on every power state transition, across all
 * zones. Not thread-safe with close()/destroy(); register before handing m
 * to other threads. Returns false on allocation failure (cb not added). */
bool rcp_powerstate_manager_subscribe(rcp_powerstate_manager_t *m, rcp_powerstate_power_fn cb, void *user_data);

/* Stops the background recovery thread. Idempotent; safe to call before
 * rcp_powerstate_manager_destroy(). Blocks until any in-flight recovery
 * attempt finishes. */
void rcp_powerstate_manager_close(rcp_powerstate_manager_t *m);

/* Closes m (if not already) and frees it, releasing its references to
 * every registered controller. Call exactly once. */
void rcp_powerstate_manager_destroy(rcp_powerstate_manager_t *m);

#ifdef __cplusplus
}
#endif

#endif /* RCP_POWERSTATE_H */
