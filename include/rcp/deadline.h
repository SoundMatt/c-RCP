/* SPDX-License-Identifier: MPL-2.0 */
/*
 * Per-stream liveness deadline monitor for the TC18 Remote Control
 * Protocol (SG-001, SG-004) -- ROADMAP.md Phase 21, "Satellite Package
 * Rework", milestone 79.
 *
 * This is this module's own full REPLACE of its pre-TC18 content: the old
 * rcp_deadline_monitor_t subscribed to each zone controller's Status
 * stream and declared a zone dead once no Status arrived within a fixed
 * deadline. There is no generic "Status stream" left to subscribe to in
 * the TC18 model this codebase now targets (ROADMAP.md's Protocol
 * Replacement Notice); the closest re-derivable liveness signals are the
 * response/ack-queue's own flush cadence (regmap.h's
 * rcp_regmap_response_queue_cfg_t::flush_time_us, ~line 447) and e2e.h's
 * per-stream watchdog overflow notification (rx_wd_info_enable, ~line
 * 395), per this milestone's own roadmap scope.
 *
 * rcp_deadline_monitor_t's job is accordingly re-shaped around two
 * caller-pushed signals rather than one subscribed one:
 *
 *   - rcp_deadline_monitor_heartbeat() -- a caller calls this every time
 *     it observes a response/ack-queue flush for a stream (the
 *     flush_time_us-cadenced heartbeat), resetting that stream's deadline
 *     timer and reporting it alive if it was previously dead.
 *   - rcp_deadline_monitor_notify_overflow() -- a caller calls this when
 *     it observes an e2e.h rcp_e2e_wd_result_t.notify == true event for a
 *     stream (an rx_wd_info_enable-driven watchdog-overflow notification,
 *     e.g. relayed from watchdog.h's own rcp_watchdog_keeper_t), declaring
 *     the stream dead immediately rather than waiting out its deadline
 *     timer.
 *
 * A background thread still evaluates each stream's own deadline timer
 * against the current time and declares it dead once too much time has
 * passed without a heartbeat, preserving this module's original "silence
 * itself means dead" contract -- only the signal that resets the timer has
 * changed (a pushed heartbeat call, not a subscribed Status frame). As
 * before, this module owns no transport or subscription mechanism of its
 * own; a caller observes the underlying wire traffic (however it does
 * that -- this module is deliberately transport-agnostic) and pushes the
 * two signals above.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_DEADLINE_H
#define RCP_DEADLINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One registered request stream's own deadline override. deadline_ms == 0
 * means "use rcp_deadline_config_t::default_deadline_ms instead". */
typedef struct {
    uint64_t stream_id; /* IEEE 1722 StreamID this request stream listens
                            on; same addressing model as avtp.h */
    uint64_t deadline_ms;
} rcp_deadline_stream_cfg_t;

/* Fired on every liveness transition. */
typedef struct {
    uint64_t stream_id;
    bool     alive;
} rcp_liveness_event_t;

typedef struct {
    uint64_t default_deadline_ms; /* per-stream deadline_ms == 0 fallback (default: 50, 20 Hz cadence) */
    uint64_t poll_interval_ms;    /* time between deadline re-checks (default: 5) */
} rcp_deadline_config_t;

/* { default_deadline_ms = 50, poll_interval_ms = 5 }. */
rcp_deadline_config_t rcp_deadline_default_config(void);

/* User-supplied callback fired on every liveness transition, across all
 * streams. user_data is the opaque pointer passed to
 * rcp_deadline_monitor_subscribe(). */
typedef void (*rcp_deadline_liveness_fn)(const rcp_liveness_event_t *ev, void *user_data);

typedef struct rcp_deadline_monitor rcp_deadline_monitor_t;

/* Creates a Monitor over the given streams (copied by value) and starts
 * its background deadline-check thread immediately, treating construction
 * time as every stream's initial (heartbeat-less) reference point.
 * streams/n_streams may describe zero streams. Returns NULL on allocation
 * failure. */
rcp_deadline_monitor_t *rcp_deadline_monitor_new(rcp_deadline_config_t cfg,
                                                  const rcp_deadline_stream_cfg_t *streams,
                                                  size_t n_streams);

/* Records a response/ack-queue flush heartbeat for stream_id -- see the
 * file header -- resetting its deadline timer to zero and, if it was
 * dead, reporting it alive again. Returns false if stream_id was not
 * registered with m (no state changed). */
bool rcp_deadline_monitor_heartbeat(rcp_deadline_monitor_t *m, uint64_t stream_id);

/* Records an rx_wd_info_enable watchdog-overflow notification for
 * stream_id -- see the file header -- immediately declaring it dead
 * regardless of how much of its deadline window remains. Returns false if
 * stream_id was not registered with m (no state changed). */
bool rcp_deadline_monitor_notify_overflow(rcp_deadline_monitor_t *m, uint64_t stream_id);

/* Returns whether stream_id is currently considered alive. False for any
 * stream not registered with m, and false for a registered stream before
 * its first heartbeat (matching the pre-replacement module's own
 * "false before the first signal arrives" convention). */
bool rcp_deadline_monitor_alive(rcp_deadline_monitor_t *m, uint64_t stream_id);

/* Registers cb to be invoked on every liveness transition, across all
 * streams. Not thread-safe with close()/destroy(); register before
 * handing m to other threads. Returns false on allocation failure (cb not
 * added). */
bool rcp_deadline_monitor_subscribe(rcp_deadline_monitor_t *m, rcp_deadline_liveness_fn cb, void *user_data);

/* Stops the background deadline-check thread. Idempotent; safe to call
 * before rcp_deadline_monitor_destroy(). Blocks until the current check
 * cycle (if any) finishes. */
void rcp_deadline_monitor_close(rcp_deadline_monitor_t *m);

/* Closes m (if not already) and frees it. Call exactly once. */
void rcp_deadline_monitor_destroy(rcp_deadline_monitor_t *m);

#ifdef __cplusplus
}
#endif

#endif /* RCP_DEADLINE_H */
