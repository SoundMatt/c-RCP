/*
 * RELAY application interface adapter for c-RCP (RELAY spec §10.3, §18.2).
 *
 * rcp_adapt() wraps an rcp_controller_t as a relay Caller (rcp_relay_caller_t)
 * so application code can use the protocol-agnostic relay interface and
 * swap the underlying protocol with a single constructor change.
 *
 * Usage (rcp_mock_controller_new() here is tests/legacy_mock.h's own
 * legacy zone-controller double -- see that header's file comment; adapt.c
 * itself, like the rcp_controller_t interface it wraps, is scheduled for
 * its own TC18 rework at ROADMAP.md milestone 84):
 *   rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT, NULL, NULL);
 *   rcp_relay_caller_t *caller = rcp_adapt(ctrl);   // retains ctrl
 *   relay_context_t ctx = relay_context_background();
 *   relay_message_t req = {0}, resp = {0};
 *   relay_message_set_id(&req, "FrontLeft");
 *   rcp_relay_caller_call(caller, &ctx, &req, &resp);
 *   relay_message_free(&req);
 *   relay_message_free(&resp);
 *   rcp_relay_caller_release(caller);  // also releases ctrl
 */
#ifndef RCP_ADAPT_H
#define RCP_ADAPT_H

#include "relay/relay.h"
#include "rcp/rcp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── ToMessage / FromMessage (§15.7.5) ─────────────────────────────────────── */

/* Converts a Status update to a relay_message_t (subscribe direction).
 * Caller owns the result and must relay_message_free() it. */
relay_message_t rcp_status_to_message(const rcp_status_t *s);

/* Converts a Response to a relay_message_t (call direction).
 * Caller owns the result and must relay_message_free() it. */
relay_message_t rcp_response_to_message(const rcp_response_t *r);

/* Converts a relay_message_t to a Command (call direction). The returned
 * command's payload BORROWS msg's payload buffer -- matching rcp_command_t's
 * existing borrowed-payload convention elsewhere in this API -- so it is
 * only valid as long as msg outlives it; dup the bytes yourself if you need
 * the command to outlive msg. */
rcp_command_t rcp_message_to_command(const relay_message_t *msg);

/* ── Error wrapping (§5.2) ──────────────────────────────────────────────────
 *
 * Reports whether rcp_ec (an rcp_errc_t) is equivalent to one of RELAY's
 * four mandatory common-error sentinels (§5.1), writing the corresponding
 * relay_errc_t to *out and returning true if so; returns false (leaving
 * *out untouched) for RCP_OK or any RCP-specific condition with no RELAY
 * equivalent (RCP_ERR_NOT_FOUND, RCP_ERR_ALREADY_EXISTS, RCP_ERR_BUSY,
 * RCP_ERR_ZONE_MISMATCH, RCP_ERR_NOT_SUPPORTED, RCP_ERR_FORBIDDEN).
 *
 * A query function rather than an in-place value substitution: other
 * language bindings satisfy §5.2 by mapping protocol-specific error
 * *types* onto the RELAY sentinel space (Go's errors.Is, C++'s
 * std::error_condition equivalence), where "no error" is a distinct
 * concept from "error code 0 in category X". This codebase uses plain
 * int rcp_errc_t codes with no such category machinery, and
 * RELAY_ERRC_CLOSED is numerically 0 -- the same value
 * rcp_relay_caller_send()/_call()/_subscribe()/_close() (like every
 * other rcp_errc_t-returning function in this codebase) already use for
 * success ("RELAY_OK-equivalent 0 return", per their own vtable
 * doc-comment). If those four functions returned raw relay_errc_t values
 * in place of their rcp_errc_t ones, a real "closed" failure would
 * become numerically indistinguishable from success -- turning a real
 * error into an apparent success, not a cosmetic issue. Call this on any
 * non-RCP_OK return from those four functions to test RELAY-sentinel
 * equivalence safely instead of comparing the raw return value directly
 * against RELAY_ERRC_CLOSED/etc.
 */
bool rcp_errc_to_relay_errc(int rcp_ec, relay_errc_t *out);

/* ── Adapt() (§10.3) ────────────────────────────────────────────────────────
 *
 * Wraps ctrl (taking a reference via rcp_controller_retain) as a relay
 * Caller. The returned caller also satisfies the relay Node role. Returned
 * with refcount 1; release with rcp_relay_caller_release(), which also
 * releases the wrapped controller. Does NOT block or connect -- it wraps
 * the given controller immediately. Returns NULL on allocation failure.
 */
rcp_relay_caller_t *rcp_adapt(rcp_controller_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ADAPT_H */
