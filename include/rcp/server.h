/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
//cfusa:req REQ-SRV-004
//cfusa:req REQ-SRV-005
//cfusa:req REQ-SRV-006
//cfusa:req REQ-SRV-007
//cfusa:req REQ-SRV-008
//cfusa:req REQ-SRV-009
//cfusa:req REQ-SRV-010
//cfusa:req REQ-SRV-011
//cfusa:req REQ-SRV-012
//cfusa:req REQ-SRV-013
//cfusa:req REQ-SRV-014
/*
 * server.h -- Per-endpoint ep_enable pre-load-then-drain request queue for
 * the TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 14, "RC
 * Server Lifecycle & Register Map", milestone 61).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59) and ACF message format (acf.h/acf.c,
 * milestone 60). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, or any
 * satellite package is touched here.
 *
 * The RC Server lifecycle state machine this module originally shipped
 * alongside (HW_UNCONFIGURED/HW_CONFIGURED/RCP_CONFIGURED and the
 * transition/plausibility/field-writability rules tied to it) moved to
 * lifecycle.h/lifecycle.c as part of the module-naming reconciliation
 * tracked at github.com/SoundMatt/c-RCP/issues/87: RELAY spec v1.14's
 * §13.7.2 registry names `lifecycle` for that concern specifically. This
 * remaining per-endpoint request queue -- an RC-Server-as-endpoint
 * concern, not itself the lifecycle state machine -- has no entry of its
 * own in that registry, so it keeps its original name. No queueing
 * behavior changed; this is a pure relocation. See lifecycle.h for the
 * state-machine surface this module used to also provide.
 *
 * ── The endpoint request store: conditional-request dispatch ────────────────
 *
 * As of v0.102.0 this module owns a second, richer per-endpoint structure
 * alongside the original ep_enable queue: the *request store*, which holds
 * conditional requests (request_compound.h, request_triggered.h,
 * request_timed.h, request_chained.h) from the moment they are admitted
 * until their execution condition is satisfied and they run.
 *
 * Before v0.102.0 nothing in this library ever consulted a request's
 * request_type when deciding what to do with it. Every ACF message that
 * reached an endpoint was executed unconditionally, in arrival order --
 * so a compound request bound to a sequencer state, a triggered request
 * waiting on a trigger signal, a timed request with a future
 * presentation_time, and a plain standard request were all indistinguishable
 * at dispatch time. scheduler.h's priority ordering and e2e.h's
 * safety-request gating both existed and were individually correct, but
 * neither had a caller. This module is where all of that is now actually
 * joined up:
 *
 *   1. rcp_server_endpoint_admit() classifies each arriving ACF message
 *      (scheduler.h's rcp_sched_classify() over the peeked request_type)
 *      and routes it: a standard request keeps the original
 *      execute-now-or-queue behavior exactly; a conditional request is
 *      decoded through its own module's decode_* function and stored;
 *      a cancellation request is reported to the caller to apply.
 *
 *   2. rcp_server_endpoint_select_due() evaluates every stored request's
 *      own execution condition, through that request kind's own predicate
 *      (rcp_compound_start_condition_met()/rcp_compound_exec_delay_elapsed(),
 *      rcp_triggered_threshold_reached()/rcp_triggered_exec_delay_elapsed(),
 *      rcp_timed_due(), rcp_chained_exec_delay_elapsed()), gates
 *      safety-tagged requests on e2e.h's rcp_e2e_request_may_execute(),
 *      and returns the single highest-priority due request per
 *      scheduler.h's rcp_sched_compare() -- rank first, arrival order
 *      within a rank.
 *
 *   3. rcp_server_endpoint_complete() applies the executed request's own
 *      completion action (a compound or compound-wait request advances its
 *      sequencer via rcp_compound_tick()/rcp_compound_wait_tick(), subject
 *      to the advance guard) and then the repetition rule: an infinite
 *      repeat_count is left alone, a zero one removes the request from the
 *      store, and anything else is decremented and re-armed.
 *
 * The store owns a byte copy of each request it holds, so a caller may
 * execute a due request's frame long after the buffer it arrived in is
 * gone. Time is expressed in whatever unit the caller's own endpoint
 * ep_delay_time is configured for -- this module reads exec_delay fields
 * and elapsed counters in that same unit and never converts between units
 * or reads a clock of its own.
 */
#ifndef RCP_SERVER_H
#define RCP_SERVER_H

#include "rcp/e2e.h"
#include "rcp/rcp.h"
#include "rcp/request_sequencer.h"
#include "rcp/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── The per-endpoint conditional-request store ────────────────────────────── */

/* How many conditional requests one endpoint's request store can hold at
 * once. A fixed table keeps this module's memory behavior trivial to
 * reason about, matching mock.h's RCP_MOCK_MAX_ENDPOINTS convention. */
#define RCP_SERVER_MAX_PENDING ((size_t)32u)

/* One conditional request held in an endpoint's request store, from
 * admission until its condition is met and it executes (and, for a
 * repeating request, until its repetitions are exhausted). Only the union
 * member matching kind is meaningful. */
typedef struct {
    bool             in_use;
    rcp_sched_kind_t kind;
    uint8_t          request_type;
    uint8_t          transaction_num;
    uint64_t         sequence;  /* arrival order, for scheduler.h FIFO tie-breaking */
    rcp_bytes_t      frame;     /* owned copy of the whole ACF message */

    /* The decoded execution condition. Which of these is live is
     * determined by kind -- a plain struct rather than a union so a
     * caller can inspect a stored request without first branching. */
    rcp_compound_step_t     compound;          /* COMPOUND / COMPOUND_WAIT */
    rcp_triggered_step_t    triggered;         /* TRIGGERED */
    rcp_triggered_runtime_t triggered_runtime; /* TRIGGERED occurrence counter */
    uint64_t                presentation_time; /* TIMED */
    uint16_t                chain_exec_delay;  /* CHAINED */
    uint8_t                 cs;                /* CHAINED abort/continue selector */

    /* Runtime bookkeeping. armed becomes true the moment this request's
     * own start condition first holds; armed_at records the tick count at
     * that instant, and the exec_delay timer runs from there. */
    bool     armed;
    uint32_t armed_at;
    /* CHAINED only: set once this member's predecessor has finalized. */
    bool     predecessor_done;
} rcp_server_pending_t;

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

/* A single endpoint's enable flag, its ep_enable pre-load queue, and its
 * conditional-request store. A disabled endpoint still accepts (queues)
 * incoming standard requests without executing them; queued requests drain
 * out, in FIFO order, once the endpoint is re-enabled. This struct owns
 * the byte copies held in both the queue and the store. */
typedef struct {
    bool         ep_enable;
    rcp_bytes_t *queue;
    size_t       queue_len;
    size_t       queue_cap;

    rcp_server_pending_t pending[RCP_SERVER_MAX_PENDING];
    size_t               pending_count;
    uint64_t             next_sequence;
} rcp_server_endpoint_t;

/* Initializes ep with an empty queue and the given initial ep_enable value. */
void rcp_server_endpoint_init(rcp_server_endpoint_t *ep, bool ep_enable);

/* Frees every queued request and ep's internal queue storage. Safe to call
 * on an endpoint with an empty queue. Does not free ep itself. */
void rcp_server_endpoint_destroy(rcp_server_endpoint_t *ep);

/* Submits one already-framed request of frame_len octets (frame may be NULL
 * iff frame_len == 0) to ep. If ep->ep_enable is true, this is a no-op on
 * the queue and the function returns true, meaning the caller must execute
 * the request itself right now. If ep->ep_enable is false, a copy of the
 * request is appended to ep's queue and the function returns false,
 * meaning the request has been queued rather than executed. Returns false
 * (still meaning "queued") without actually growing the queue if the
 * internal reallocation fails -- callers relying on eventual delivery under
 * allocation failure must check rcp_server_endpoint_queue_len() themselves. */
bool rcp_server_endpoint_submit(rcp_server_endpoint_t *ep,
                                const uint8_t *frame, size_t frame_len);

/* Sets ep->ep_enable. Toggling it does not itself execute or discard
 * anything queued; call rcp_server_endpoint_drain_one() afterward to pull
 * queued requests back out once re-enabled. */
void rcp_server_endpoint_set_enable(rcp_server_endpoint_t *ep, bool enable);

/* If ep->ep_enable is true and ep's queue is non-empty, dequeues the
 * oldest queued request into *out_frame (caller takes ownership; free with
 * rcp_bytes_free()) and returns true. Otherwise returns false and leaves
 * *out_frame untouched -- including while ep->ep_enable is false, so a
 * disabled endpoint's queue can never be silently drained out from under it. */
bool rcp_server_endpoint_drain_one(rcp_server_endpoint_t *ep, rcp_bytes_t *out_frame);

/* Number of requests currently queued (awaiting drain) on ep. */
size_t rcp_server_endpoint_queue_len(const rcp_server_endpoint_t *ep);

/* ── Admission: request_type-aware routing ────────────────────────────────── */

/* What rcp_server_endpoint_admit() decided to do with one arriving ACF
 * message. */
typedef enum {
    /* Standard request, endpoint enabled: the caller must execute it now,
     * exactly as rcp_server_endpoint_submit() returning true has always
     * meant. */
    RCP_SERVER_ADMIT_EXECUTE_NOW  = 0,
    /* Standard request, endpoint disabled: queued for
     * rcp_server_endpoint_drain_one(), unchanged pre-v0.102.0 behavior. */
    RCP_SERVER_ADMIT_QUEUED       = 1,
    /* Conditional request: decoded and placed in ep's request store. It
     * will surface from rcp_server_endpoint_select_due() once its own
     * execution condition is satisfied. */
    RCP_SERVER_ADMIT_PENDING      = 2,
    /* Cancellation request (clear-all / clear-single /
     * clear-non-safestate): not stored. The caller applies it with the
     * matching rcp_server_endpoint_cancel_*() function -- which of the
     * three is identified by *out_request_type. */
    RCP_SERVER_ADMIT_CANCELLATION = 3,
    /* The message did not decode as the request kind its own opcode byte
     * claims, or ep's request store is full. Nothing was stored and
     * nothing is to be executed. */
    RCP_SERVER_ADMIT_REJECTED     = 4,
} rcp_server_admit_t;

/* Inspects frame[0..frame_len)'s request_type and routes it. A message
 * that is not a repurposed-timestamp ACF_GBB at all -- i.e. any ordinary
 * ACF_ABB or timestamped ACF_GBB message -- is a standard request and
 * takes the original rcp_server_endpoint_submit() path unchanged, so
 * every pre-v0.102.0 caller keeps its exact previous behavior.
 *
 * A conditional request is decoded through its own module's decode_*
 * function and stored, with its exec_delay timer left unarmed (a compound
 * or compound-wait request arms when its sequencer first reaches
 * start_state; a triggered request arms immediately and begins counting
 * occurrences; a timed request needs no arming; a chained request arms
 * when its predecessor finalizes -- see
 * rcp_server_endpoint_chain_predecessor_done()). now is the caller's
 * current tick count, in the endpoint's own ep_delay_time unit.
 *
 * *out_request_type is always written: the repurposed opcode byte for a
 * conditional or cancellation request, or 0 for a standard one.
 * *out_index, when non-NULL, receives the store index a
 * RCP_SERVER_ADMIT_PENDING request was placed at. */
rcp_server_admit_t rcp_server_endpoint_admit(rcp_server_endpoint_t *ep,
                                              const uint8_t *frame, size_t frame_len,
                                              uint32_t now, uint8_t *out_request_type,
                                              size_t *out_index);

/* ── The execution-condition tick ─────────────────────────────────────────── */

/* Everything rcp_server_endpoint_select_due() needs to evaluate a stored
 * request's execution condition. The caller owns every field; this module
 * reads no clock and holds no sequencer table of its own. */
typedef struct {
    /* Current tick count, in the endpoint's own ep_delay_time unit --
     * the same unit every exec_delay sub-field is expressed in. */
    uint32_t               now;
    /* Current gPTP time, nanoseconds modulo 2^48, for timed requests. */
    uint64_t               gptp_now;
    /* Whether a gPTP time base is locked. Timed requests never become due
     * while this is false. */
    bool                   gptp_locked;
    /* The sequencer-state table compound/compound-wait requests read and
     * advance. May be an unsupported ({NULL,0}) table, in which case no
     * compound or compound-wait request ever becomes due. */
    rcp_sequencer_table_t *sequencers;
    /* Whether the endpoint is idle right now. A triggered or chained
     * request never becomes due while the endpoint is busy. */
    bool                   endpoint_idle;
    /* Whether the endpoint has reached its configured safe state. Gates
     * every safety-tagged (0x8x) request through e2e.h's
     * rcp_e2e_request_may_execute(). */
    bool                   in_safe_state;
    /* The caller's own already-evaluated compound-wait comparison result
     * (see request_compound.h's rcp_compound_wait_tick()) -- this module
     * owns no endpoint-specific comparison logic. */
    bool                   wait_condition_met;
} rcp_server_tick_ctx_t;

/* Re-evaluates every stored request's start condition against ctx (arming
 * exec_delay timers that have just become armable) and returns the single
 * highest-priority request that is due to execute right now, per
 * scheduler.h's rcp_sched_compare(): higher rcp_sched_kind_rank() first,
 * and among equal ranks the earliest arrival. Returns true and sets
 * *out_index when one is due; returns false, leaving *out_index untouched,
 * when nothing is. Safety-tagged requests are held back until
 * ctx->in_safe_state, via rcp_e2e_request_may_execute().
 *
 * This function never executes anything and never advances a sequencer --
 * the caller runs the selected request's own frame (ep->pending[*out_index].frame)
 * and then reports the outcome back through
 * rcp_server_endpoint_complete(). */
bool rcp_server_endpoint_select_due(rcp_server_endpoint_t *ep,
                                     const rcp_server_tick_ctx_t *ctx, size_t *out_index);

/* Finalizes the request at index after the caller has executed it.
 * Applies that request kind's own completion action -- a compound request
 * advances its sequencer through rcp_compound_tick() and a compound-wait
 * request through rcp_compound_wait_tick(), both of which decline to
 * advance a sequencer that has already left start_state -- and then the
 * repetition rule: a repeat_count of RCP_COMPOUND_REPEAT_INFINITE /
 * RCP_TRIGGERED_REPEAT_INFINITE is left untouched and the request re-arms,
 * a repeat_count of zero removes the request from the store, and any other
 * value is decremented and the request re-arms. Timed and chained
 * requests carry no repetition sub-field of their own and are always
 * removed. Returns true iff the request remains in the store afterwards
 * (i.e. it will repeat). */
bool rcp_server_endpoint_complete(rcp_server_endpoint_t *ep, size_t index,
                                   const rcp_server_tick_ctx_t *ctx);

/* Records one observed trigger occurrence, emitted by endpoint source_ep
 * as its trigger signal number signal_nr, against every stored triggered
 * request on ep whose own trigger_source_ep/trigger_signal_nr selection
 * matches (request_triggered.h's
 * rcp_triggered_runtime_record_occurrence()). Returns how many stored
 * requests counted it. */
size_t rcp_server_endpoint_notify_trigger(rcp_server_endpoint_t *ep, uint8_t source_ep,
                                           uint8_t signal_nr);

/* Marks the stored chained request at index as having had its predecessor
 * finalize, at tick count now: its chain_exec_delay timer starts running
 * from there and it becomes due once that delay elapses. Returns false,
 * changing nothing, if index does not name a stored chained request. */
bool rcp_server_endpoint_chain_predecessor_done(rcp_server_endpoint_t *ep, size_t index,
                                                 uint32_t now);

/* Number of conditional requests currently held in ep's request store. */
size_t rcp_server_endpoint_pending_count(const rcp_server_endpoint_t *ep);

/* ── Cancellation and watchdog purge ──────────────────────────────────────── */

/* Clear-all (0x05): removes every stored conditional request from ep,
 * returning how many were removed. */
size_t rcp_server_endpoint_cancel_all(rcp_server_endpoint_t *ep);

/* Clear-single (0x07): removes the stored conditional request whose own
 * transaction_num equals clear_transaction_num. Reports
 * request_cancel.h's rcp_cancel_attempt() outcome --
 * RCP_CANCEL_RESULT_NOT_FOUND when no stored request carries that
 * transaction_num, RCP_CANCEL_RESULT_CANCELED when one was found and
 * removed. A request already selected for execution is past the
 * cancellable window and is reported RCP_CANCEL_RESULT_NOT_CANCELLABLE
 * without being removed. */
rcp_cancel_result_t rcp_server_endpoint_cancel_single(rcp_server_endpoint_t *ep,
                                                       uint8_t clear_transaction_num,
                                                       rcp_cancel_lifecycle_t state);

/* Clear-non-safestate (0x06): removes every stored conditional request
 * that is not safety-tagged, leaving the 0x8x ones in place. Returns how
 * many were removed. */
size_t rcp_server_endpoint_cancel_non_safestate(rcp_server_endpoint_t *ep);

/* The watchdog-overflow purge: removes every stored request e2e.h's
 * rcp_e2e_watchdog_purge_should_keep() does not keep -- i.e. every
 * non-safety-tagged one -- so that only the safety sequence survives to
 * drive the endpoint into its safe state. Returns how many were removed.
 * Identical in effect to rcp_server_endpoint_cancel_non_safestate(), but
 * reached by a different event and stated in e2e.h's own terms. */
size_t rcp_server_endpoint_watchdog_purge(rcp_server_endpoint_t *ep);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SERVER_H */
