/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/server.h"

#include <stdlib.h>
#include <string.h>

/* ── Per-endpoint ep_enable: pre-load-then-drain-on-enable ─────────────────── */

//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
void rcp_server_endpoint_init(rcp_server_endpoint_t *ep, bool ep_enable)
{
    memset(ep, 0, sizeof(*ep));
    ep->ep_enable = ep_enable;
}

//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
//cfusa:req REQ-SRV-003
void rcp_server_endpoint_destroy(rcp_server_endpoint_t *ep)
{
    size_t i;

    for (i = 0; i < ep->queue_len; i++) {
        rcp_bytes_free(&ep->queue[i]);
    }
    free(ep->queue);
    ep->queue     = NULL;
    ep->queue_len = 0;
    ep->queue_cap = 0;

    /* The request store owns a byte copy of every conditional request it
     * still holds. */
    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        if (ep->pending[i].in_use) rcp_bytes_free(&ep->pending[i].frame);
    }
    memset(ep->pending, 0, sizeof(ep->pending));
    ep->pending_count = 0;
}

//cfusa:req REQ-SRV-001
//cfusa:req REQ-SRV-002
bool rcp_server_endpoint_submit(rcp_server_endpoint_t *ep,
                                const uint8_t *frame, size_t frame_len)
{
    rcp_bytes_t *grown;

    if (ep->ep_enable) return true; /* caller must execute this now */

    if (ep->queue_len == ep->queue_cap) {
        size_t new_cap = (ep->queue_cap == 0) ? 4 : ep->queue_cap * 2;

        grown = (rcp_bytes_t *)realloc(ep->queue, new_cap * sizeof(*grown));
        if (!grown) return false; /* still "queued": nothing to execute now */
        ep->queue     = grown;
        ep->queue_cap = new_cap;
    }

    ep->queue[ep->queue_len] = rcp_bytes_dup(frame, frame_len);
    ep->queue_len++;
    return false;
}

//cfusa:req REQ-SRV-003
void rcp_server_endpoint_set_enable(rcp_server_endpoint_t *ep, bool enable)
{
    ep->ep_enable = enable;
}

//cfusa:req REQ-SRV-003
bool rcp_server_endpoint_drain_one(rcp_server_endpoint_t *ep, rcp_bytes_t *out_frame)
{
    size_t i;

    if (!ep->ep_enable) return false;
    if (ep->queue_len == 0) return false;

    *out_frame = ep->queue[0];
    for (i = 1; i < ep->queue_len; i++) {
        ep->queue[i - 1] = ep->queue[i];
    }
    ep->queue_len--;
    return true;
}

//cfusa:req REQ-SRV-001
size_t rcp_server_endpoint_queue_len(const rcp_server_endpoint_t *ep)
{
    return ep->queue_len;
}

/* ── The conditional-request store ────────────────────────────────────────── */

/* Frees slot's owned frame copy and marks it free. */
static void release_slot(rcp_server_endpoint_t *ep, rcp_server_pending_t *slot)
{
    rcp_bytes_free(&slot->frame);
    memset(slot, 0, sizeof(*slot));
    ep->pending_count--;
}

/* Claims a free store slot, or NULL if the store is full. */
static rcp_server_pending_t *claim_slot(rcp_server_endpoint_t *ep)
{
    size_t i;

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        if (!ep->pending[i].in_use) {
            memset(&ep->pending[i], 0, sizeof(ep->pending[i]));
            ep->pending[i].in_use   = true;
            ep->pending[i].sequence = ep->next_sequence++;
            ep->pending_count++;
            return &ep->pending[i];
        }
    }
    return NULL;
}

//cfusa:req REQ-SRV-004
//cfusa:req REQ-SRV-005
rcp_server_admit_t rcp_server_endpoint_admit(rcp_server_endpoint_t *ep,
                                              const uint8_t *frame, size_t frame_len,
                                              uint32_t now, uint8_t *out_request_type,
                                              size_t *out_index)
{
    uint8_t               request_type = 0;
    rcp_sched_kind_t      kind;
    rcp_server_pending_t *slot;
    const uint8_t        *payload;
    size_t                payload_len;
    uint8_t               decoded_rt;
    rcp_byte_bus_id_t     bus;
    uint8_t               tn;

    *out_request_type = 0;

    /* Not a repurposed-timestamp ACF_GBB at all: a standard request, and
     * the original submit path handles it unchanged. */
    if (rcp_compound_peek_request_type(frame, frame_len, &request_type) != RCP_COMPOUND_OK) {
        return rcp_server_endpoint_submit(ep, frame, frame_len) ? RCP_SERVER_ADMIT_EXECUTE_NOW
                                                                 : RCP_SERVER_ADMIT_QUEUED;
    }

    kind = rcp_sched_classify(true, request_type);
    if (kind == RCP_SCHED_KIND_STANDARD) {
        /* A repurposed region carrying an opcode byte this build does not
         * recognize: treated as standard, never over-privileged (see
         * rcp_sched_classify()'s own fail-safe rule). */
        return rcp_server_endpoint_submit(ep, frame, frame_len) ? RCP_SERVER_ADMIT_EXECUTE_NOW
                                                                 : RCP_SERVER_ADMIT_QUEUED;
    }

    *out_request_type = request_type;
    if (kind == RCP_SCHED_KIND_CANCELLATION) return RCP_SERVER_ADMIT_CANCELLATION;

    slot = claim_slot(ep);
    if (!slot) return RCP_SERVER_ADMIT_REJECTED;

    slot->kind         = kind;
    slot->request_type = request_type;

    switch (kind) {
    case RCP_SCHED_KIND_COMPOUND:
    case RCP_SCHED_KIND_COMPOUND_WAIT:
        if (rcp_compound_decode_request(frame, frame_len, &decoded_rt, &bus, &slot->compound,
                                         &payload, &payload_len, &tn) != RCP_COMPOUND_OK) {
            release_slot(ep, slot);
            return RCP_SERVER_ADMIT_REJECTED;
        }
        slot->transaction_num = tn;
        break;

    case RCP_SCHED_KIND_TRIGGERED:
        if (rcp_triggered_decode_request(frame, frame_len, &decoded_rt, &bus, &slot->triggered,
                                          &payload, &payload_len, &tn) != RCP_TRIGGERED_OK) {
            release_slot(ep, slot);
            return RCP_SERVER_ADMIT_REJECTED;
        }
        slot->transaction_num = tn;
        /* A triggered request begins counting occurrences the moment it is
         * admitted, and its exec_delay runs from the moment its threshold
         * is reached -- see rcp_server_endpoint_select_due(). */
        rcp_triggered_runtime_enter_started(&slot->triggered_runtime);
        break;

    case RCP_SCHED_KIND_TIMED:
        if (rcp_timed_decode_request(frame, frame_len, &bus, &slot->presentation_time, &payload,
                                      &payload_len, &tn) != RCP_TIMED_OK) {
            release_slot(ep, slot);
            return RCP_SERVER_ADMIT_REJECTED;
        }
        slot->transaction_num = tn;
        break;

    case RCP_SCHED_KIND_CHAINED:
        if (rcp_chained_decode_member(frame, frame_len, &bus, &slot->chain_exec_delay, &slot->cs,
                                       &payload, &payload_len, &tn) != RCP_CHAINED_OK) {
            release_slot(ep, slot);
            return RCP_SERVER_ADMIT_REJECTED;
        }
        slot->transaction_num = tn;
        break;

    default:
        release_slot(ep, slot);
        return RCP_SERVER_ADMIT_REJECTED;
    }

    slot->armed_at = now;
    slot->frame    = rcp_bytes_dup(frame, frame_len);
    if (!slot->frame.data && frame_len > 0) {
        release_slot(ep, slot);
        return RCP_SERVER_ADMIT_REJECTED;
    }

    if (out_index) *out_index = (size_t)(slot - &ep->pending[0]);
    return RCP_SERVER_ADMIT_PENDING;
}

/* Arms slot's exec_delay timer at ctx->now if its own start condition has
 * just begun to hold. Returns whether slot is armed afterwards. */
static bool arm_if_startable(rcp_server_pending_t *slot, const rcp_server_tick_ctx_t *ctx)
{
    if (slot->armed) return true;

    switch (slot->kind) {
    case RCP_SCHED_KIND_COMPOUND:
    case RCP_SCHED_KIND_COMPOUND_WAIT:
        /* The sequencer has to actually reach start_state (or start_state
         * has to be the any-state value) before the delay starts running. */
        if (!ctx->sequencers) return false;
        if (!rcp_compound_start_condition_met(ctx->sequencers, &slot->compound)) return false;
        break;

    case RCP_SCHED_KIND_TRIGGERED:
        /* trigger_exec_delay runs from the moment the threshold is met. */
        if (!rcp_triggered_threshold_reached(&slot->triggered, &slot->triggered_runtime)) {
            return false;
        }
        break;

    case RCP_SCHED_KIND_TIMED:
        /* A timed request's condition is the presentation_time itself; it
         * has no separate arming step. */
        break;

    case RCP_SCHED_KIND_CHAINED:
        /* chain_exec_delay runs from the moment the predecessor finalized,
         * not from this tick -- rcp_server_endpoint_chain_predecessor_done()
         * has already recorded that instant in armed_at, so this branch
         * must leave it alone rather than restart the timer here. */
        if (!slot->predecessor_done) return false;
        slot->armed = true;
        return true;

    default:
        return false;
    }

    slot->armed    = true;
    slot->armed_at = ctx->now;
    return true;
}

/* Whether slot's execution condition is fully satisfied right now. */
static bool is_due(rcp_server_pending_t *slot, const rcp_server_tick_ctx_t *ctx)
{
    uint32_t elapsed;

    /* Safety-tagged requests stay in the store until the endpoint has
     * actually reached its configured safe state -- e2e.h's own gate. */
    if (!rcp_e2e_request_may_execute(slot->request_type, ctx->in_safe_state)) return false;

    if (!arm_if_startable(slot, ctx)) return false;
    elapsed = ctx->now - slot->armed_at;

    switch (slot->kind) {
    case RCP_SCHED_KIND_COMPOUND:
        return rcp_compound_exec_delay_elapsed(&slot->compound, elapsed);

    case RCP_SCHED_KIND_COMPOUND_WAIT:
        /* A compound-wait request needs its caller-evaluated comparison to
         * hold as well as its delay to elapse. */
        if (!ctx->wait_condition_met) return false;
        return rcp_compound_exec_delay_elapsed(&slot->compound, elapsed);

    case RCP_SCHED_KIND_TRIGGERED:
        if (!ctx->endpoint_idle) return false;
        return rcp_triggered_exec_delay_elapsed(&slot->triggered, elapsed);

    case RCP_SCHED_KIND_TIMED:
        if (!ctx->gptp_locked) return false;
        return rcp_timed_due(slot->presentation_time, ctx->gptp_now);

    case RCP_SCHED_KIND_CHAINED:
        if (!ctx->endpoint_idle) return false;
        return rcp_chained_exec_delay_elapsed(slot->chain_exec_delay, elapsed);

    default:
        return false;
    }
}

//cfusa:req REQ-SRV-006
//cfusa:req REQ-SRV-007
//cfusa:req REQ-SRV-008
bool rcp_server_endpoint_select_due(rcp_server_endpoint_t *ep,
                                     const rcp_server_tick_ctx_t *ctx, size_t *out_index)
{
    size_t            i;
    bool              found = false;
    size_t            best  = 0;
    rcp_sched_entry_t best_entry = {RCP_SCHED_KIND_STANDARD, 0};

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        rcp_sched_entry_t entry;

        if (!ep->pending[i].in_use) continue;
        if (!is_due(&ep->pending[i], ctx)) continue;

        entry.kind     = ep->pending[i].kind;
        entry.sequence = ep->pending[i].sequence;

        /* scheduler.h's own total order decides: rank first, then FIFO. */
        if (!found || rcp_sched_compare(&entry, &best_entry) < 0) {
            found      = true;
            best       = i;
            best_entry = entry;
        }
    }

    if (found) *out_index = best;
    return found;
}

//cfusa:req REQ-SRV-009
//cfusa:req REQ-SRV-010
bool rcp_server_endpoint_complete(rcp_server_endpoint_t *ep, size_t index,
                                   const rcp_server_tick_ctx_t *ctx)
{
    rcp_server_pending_t *slot;
    uint16_t             *repeat = NULL;
    uint32_t              elapsed;

    if (index >= RCP_SERVER_MAX_PENDING) return false;
    slot = &ep->pending[index];
    if (!slot->in_use) return false;

    elapsed = ctx->now - slot->armed_at;

    switch (slot->kind) {
    case RCP_SCHED_KIND_COMPOUND:
        if (ctx->sequencers) {
            (void)rcp_compound_tick(ctx->sequencers, &slot->compound, elapsed);
        }
        repeat = &slot->compound.repeat_count;
        break;

    case RCP_SCHED_KIND_COMPOUND_WAIT:
        if (ctx->sequencers) {
            (void)rcp_compound_wait_tick(ctx->sequencers, &slot->compound,
                                          ctx->wait_condition_met);
        }
        repeat = &slot->compound.repeat_count;
        break;

    case RCP_SCHED_KIND_TRIGGERED:
        (void)rcp_triggered_tick(&slot->triggered, &slot->triggered_runtime, elapsed, true);
        repeat = &slot->triggered.repeat_count;
        break;

    case RCP_SCHED_KIND_TIMED:
    case RCP_SCHED_KIND_CHAINED:
    default:
        /* Neither kind carries a repetition sub-field of its own: a timed
         * request names one instant and a chain's repetition is driven by
         * the chain's first request, not by its chained members. */
        release_slot(ep, slot);
        return false;
    }

    if (*repeat == RCP_COMPOUND_REPEAT_INFINITE) {
        /* Infinite: never decremented, never removed. */
    } else if (*repeat == 0u) {
        release_slot(ep, slot);
        return false;
    } else {
        (*repeat)--;
    }

    /* Re-arm for the next repetition: the start condition has to be
     * satisfied again from scratch. */
    slot->armed    = false;
    slot->armed_at = ctx->now;
    if (slot->kind == RCP_SCHED_KIND_TRIGGERED) {
        rcp_triggered_runtime_enter_started(&slot->triggered_runtime);
    }
    return true;
}

//cfusa:req REQ-SRV-011
size_t rcp_server_endpoint_notify_trigger(rcp_server_endpoint_t *ep, uint8_t source_ep,
                                           uint8_t signal_nr)
{
    size_t i;
    size_t matched = 0;

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        rcp_server_pending_t *slot = &ep->pending[i];

        if (!slot->in_use) continue;
        if (slot->kind != RCP_SCHED_KIND_TRIGGERED) continue;

        if (rcp_triggered_runtime_record_occurrence(&slot->triggered_runtime, &slot->triggered,
                                                     source_ep, signal_nr)) {
            matched++;
        }
    }
    return matched;
}

//cfusa:req REQ-SRV-012
bool rcp_server_endpoint_chain_predecessor_done(rcp_server_endpoint_t *ep, size_t index,
                                                 uint32_t now)
{
    rcp_server_pending_t *slot;

    if (index >= RCP_SERVER_MAX_PENDING) return false;
    slot = &ep->pending[index];
    if (!slot->in_use || slot->kind != RCP_SCHED_KIND_CHAINED) return false;

    slot->predecessor_done = true;
    slot->armed_at         = now;
    return true;
}

size_t rcp_server_endpoint_pending_count(const rcp_server_endpoint_t *ep)
{
    return ep->pending_count;
}

/* ── Cancellation and watchdog purge ──────────────────────────────────────── */

//cfusa:req REQ-SRV-013
size_t rcp_server_endpoint_cancel_all(rcp_server_endpoint_t *ep)
{
    size_t i;
    size_t removed = 0;

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        if (!ep->pending[i].in_use) continue;
        release_slot(ep, &ep->pending[i]);
        removed++;
    }
    return removed;
}

//cfusa:req REQ-SRV-013
rcp_cancel_result_t rcp_server_endpoint_cancel_single(rcp_server_endpoint_t *ep,
                                                       uint8_t clear_transaction_num,
                                                       rcp_cancel_lifecycle_t state)
{
    size_t              i;
    bool                found = false;
    rcp_cancel_result_t result;

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        if (ep->pending[i].in_use && ep->pending[i].transaction_num == clear_transaction_num) {
            found = true;
            break;
        }
    }

    result = rcp_cancel_attempt(found, state);
    if (result == RCP_CANCEL_RESULT_CANCELED) release_slot(ep, &ep->pending[i]);
    return result;
}

/* Shared body of clear-non-safestate and the watchdog purge: both remove
 * exactly the requests e2e.h declines to keep. */
static size_t purge_non_safety(rcp_server_endpoint_t *ep)
{
    size_t i;
    size_t removed = 0;

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        if (!ep->pending[i].in_use) continue;
        if (rcp_e2e_watchdog_purge_should_keep(ep->pending[i].request_type)) continue;

        release_slot(ep, &ep->pending[i]);
        removed++;
    }
    return removed;
}

//cfusa:req REQ-SRV-013
size_t rcp_server_endpoint_cancel_non_safestate(rcp_server_endpoint_t *ep)
{
    return purge_non_safety(ep);
}

//cfusa:req REQ-SRV-014
size_t rcp_server_endpoint_watchdog_purge(rcp_server_endpoint_t *ep)
{
    return purge_non_safety(ep);
}
