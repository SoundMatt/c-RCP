/* SPDX-License-Identifier: MPL-2.0 */

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-WDG-010
/*
 * Per-stream watchdog kicker for the TC18 Remote Control Protocol
 * (SG-001, SG-003, SG-007) -- ROADMAP.md Phase 21, "Satellite Package
 * Rework", milestone 79.
 *
 * This is this module's own full REPLACE of its pre-TC18 content: the old
 * rcp_watchdog_keeper_t sent a dedicated RCP_CMD_WATCHDOG command to each
 * registered zone controller and drove its own Healthy/Degraded/Faulted
 * health state machine from consecutive send failures. Neither
 * RCP_CMD_WATCHDOG nor rcp_zone_t/rcp_controller_t survive in the TC18
 * model this codebase now targets (ROADMAP.md's Protocol Replacement
 * Notice) -- there is no dedicated watchdog side channel at all. Instead,
 * regmap.h's per-request-stream rx_wd_enable/rx_wd_timeout_ms/
 * rx_wd_safestate_enable/rx_wd_info_enable register family (Phase 18) is
 * the watchdog, and e2e.h's rcp_e2e_wd_evaluate() is the single pure
 * function that already ties that family together into an overflow
 * verdict given an elapsed-since-last-kick duration.
 *
 * rcp_watchdog_keeper_t's job shrinks accordingly: it is a thin client
 * convenience that (a) remembers, per registered stream, when it was last
 * kicked and that stream's own rx_wd_* configuration, and (b) periodically
 * re-runs rcp_e2e_wd_evaluate() against the elapsed time since that kick,
 * firing an event whenever the result changes. "Kicking" a stream here
 * means recording that a safety-request sequence for that stream just
 * completed (per this milestone's own roadmap scope: "not an independent
 * RCP_CMD_WATCHDOG side channel") -- a caller drives
 * rcp_watchdog_keeper_kick() itself, typically right after a safety-tagged
 * request/response round trip for that stream succeeds; this module sends
 * no wire traffic of its own and owns no transport, mirroring e2e.h's own
 * "operate on caller-owned data" layering (see e2e.h's file header, which
 * explicitly names this module -- under its pre-replacement shape -- as a
 * distinct concept from rcp_e2e_wd_evaluate() itself: a client-side
 * liveness *kicker*, not the pure per-request-stream evaluator).
 *
 * This module's own Healthy/Degraded/Faulted enum is dropped along with
 * RCP_CMD_WATCHDOG: e2e.h's rcp_e2e_wd_result_t (overflowed/
 * enter_safe_state/notify) is already the TC18-shaped verdict a stream's
 * rx_wd_* configuration produces, and inventing a parallel severity
 * ladder on top of it here would only duplicate what rx_wd_safestate_enable
 * and rx_wd_info_enable already independently express. This module
 * therefore reports that verdict directly, not a re-derived label.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_WATCHDOG_H
#define RCP_WATCHDOG_H

#include "rcp/e2e.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One registered request stream's rx_wd_* configuration (regmap.h,
 * ~line 390), copied in at rcp_watchdog_keeper_new() time -- this module
 * does not read a live rcp_regmap_request_stream_cfg_t itself, matching
 * every request-kind module's "operate on caller-owned data" convention
 * (see e2e.h's own file header). */
typedef struct {
    uint64_t stream_id; /* IEEE 1722 StreamID this request stream listens
                            on; same addressing model as avtp.h */
    bool     rx_wd_enable;
    uint32_t rx_wd_timeout_ms;
    bool     rx_wd_safestate_enable;
    bool     rx_wd_info_enable;
} rcp_watchdog_stream_cfg_t;

/* Fired every time a stream's rcp_e2e_wd_result_t changes (including its
 * very first evaluation at construction time). */
typedef struct {
    uint64_t             stream_id;
    rcp_e2e_wd_result_t  result;
} rcp_watchdog_event_t;

typedef struct {
    uint64_t poll_interval_ms; /* time between re-evaluation cycles (default: 10) */
} rcp_watchdog_config_t;

/* { poll_interval_ms = 10 }. */
rcp_watchdog_config_t rcp_watchdog_default_config(void);

/* User-supplied callback fired on every rcp_e2e_wd_result_t change, across
 * all streams. user_data is the opaque pointer passed to
 * rcp_watchdog_keeper_subscribe(). */
typedef void (*rcp_watchdog_event_fn)(const rcp_watchdog_event_t *ev, void *user_data);

typedef struct rcp_watchdog_keeper rcp_watchdog_keeper_t;

/* Creates a Keeper over the given streams (copied by value) and starts its
 * background re-evaluation thread immediately, treating construction time
 * as an implicit initial kick for every stream. streams/n_streams may
 * describe zero streams. Returns NULL on allocation failure. */
rcp_watchdog_keeper_t *rcp_watchdog_keeper_new(rcp_watchdog_config_t cfg,
                                                const rcp_watchdog_stream_cfg_t *streams,
                                                size_t n_streams);

/* Records a kick for stream_id -- see the file header's "not an
 * independent RCP_CMD_WATCHDOG side channel" note -- resetting its
 * elapsed-since-last-kick clock to zero. Returns false if stream_id was
 * not registered with k (no state changed). */
bool rcp_watchdog_keeper_kick(rcp_watchdog_keeper_t *k, uint64_t stream_id);

/* Returns the most recently computed rcp_e2e_wd_result_t for stream_id, or
 * an all-false result (matching a disabled watchdog's own verdict) if
 * stream_id was not registered with k. */
rcp_e2e_wd_result_t rcp_watchdog_keeper_status(rcp_watchdog_keeper_t *k, uint64_t stream_id);

/* Registers cb to be invoked on every result change, across all streams.
 * Not thread-safe with close()/destroy(); register before handing k to
 * other threads. Returns false on allocation failure (cb not added). */
bool rcp_watchdog_keeper_subscribe(rcp_watchdog_keeper_t *k, rcp_watchdog_event_fn cb, void *user_data);

/* Stops the background re-evaluation thread. Idempotent; safe to call
 * before rcp_watchdog_keeper_destroy(). Blocks until the current
 * evaluation cycle (if any) finishes. */
void rcp_watchdog_keeper_close(rcp_watchdog_keeper_t *k);

/* Closes k (if not already) and frees it. Call exactly once. */
void rcp_watchdog_keeper_destroy(rcp_watchdog_keeper_t *k);

#ifdef __cplusplus
}
#endif

#endif /* RCP_WATCHDOG_H */
