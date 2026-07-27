/*
 * RELAY application interface adapter for c-RCP (RELAY spec §10.3, §18.2).
 *
 * rcp_adapt() wraps an rcp_controller_t as a relay Caller (rcp_relay_caller_t)
 * so application code can use the protocol-agnostic relay interface and
 * swap the underlying protocol with a single constructor change.
 *
 * Usage:
 *   rcp_controller_t *ctrl = rcp_mock_controller_new(RCP_ZONE_FRONT_LEFT);
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
