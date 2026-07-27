/*
 * Timing-realistic zone controller simulator for SiL/HIL testing.
 *
 * rcp_sim_controller_new() returns a full rcp_controller_t implementation
 * that adds fault()/recover() controls for deterministic scenario testing.
 * Configurable latency (constant or jitter), periodic Status publishing,
 * and watchdog-miss detection enable validation of the safety mechanisms
 * from v0.11.0-v0.16.0 (watchdog, deadline, powerstate, e2e, prioqueue,
 * ratelimit).
 */
#ifndef RCP_SIM_H
#define RCP_SIM_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RCP_SIM_LATENCY_CONSTANT = 0,
    RCP_SIM_LATENCY_JITTER   = 1,
} rcp_sim_latency_model_t;

typedef struct {
    rcp_zone_t              zone;
    uint64_t                 base_latency_ms;     /* default: 2 */
    uint64_t                 jitter_ms;            /* default: 1 */
    uint64_t                 status_interval_ms;   /* default: 10, 0 = disabled */
    uint64_t                 watchdog_timeout_ms;  /* default: 50, 0 = disabled */
    rcp_sim_latency_model_t  latency_model;        /* default: RCP_SIM_LATENCY_JITTER */
} rcp_sim_config_t;

/* { zone = z, base_latency_ms = 2, jitter_ms = 1, status_interval_ms = 10,
 * watchdog_timeout_ms = 50, latency_model = RCP_SIM_LATENCY_JITTER }. */
rcp_sim_config_t rcp_sim_default_config(rcp_zone_t z);

/* User-supplied function producing a Response for a Command, same
 * convention as rcp_mock_handler_fn (see mock.h). If NULL, the controller
 * returns RCP_RESPONSE_OK with an empty payload. */
typedef void (*rcp_sim_handler_fn)(const rcp_command_t *cmd, rcp_response_t *out, void *user_data);

/* Creates a simulated zone controller. Returned with refcount 1 (release
 * with rcp_controller_release()). handler may be NULL. Starts a background
 * status-publishing thread if cfg.status_interval_ms > 0, and a background
 * watchdog-miss-detection thread if cfg.watchdog_timeout_ms > 0. */
rcp_controller_t *rcp_sim_controller_new(rcp_sim_config_t cfg, rcp_sim_handler_fn handler, void *user_data);

/* Injects err on all subsequent send() calls until rcp_sim_controller_recover()
 * is called. ctrl must have been created by rcp_sim_controller_new(). */
void rcp_sim_controller_fault(rcp_controller_t *ctrl, int err);

/* Clears any injected fault. ctrl must have been created by
 * rcp_sim_controller_new(). */
void rcp_sim_controller_recover(rcp_controller_t *ctrl);

/* Returns whether the background watchdog thread currently believes a
 * RCP_CMD_WATCHDOG kick is overdue (always false if cfg.watchdog_timeout_ms
 * was 0). ctrl must have been created by rcp_sim_controller_new(). */
bool rcp_sim_controller_watchdog_missed(rcp_controller_t *ctrl);

/* Pushes a Status update (seq = incrementing, healthy = !closed) to all
 * active subscribers. payload may be NULL iff len==0. Safe to call after
 * close() (a no-op in that case). ctrl must have been created by
 * rcp_sim_controller_new(). Called by the background status thread when
 * cfg.status_interval_ms > 0, and may also be called directly by test
 * harnesses. */
void rcp_sim_controller_publish(rcp_controller_t *ctrl, const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SIM_H */
