/*
 * canbr.h -- CAN / CAN-FD protocol bridge interface stub (SG-006) for the
 * TC18 Remote Control Protocol wire layer (ROADMAP.md Phase 21,
 * "Satellite Package Rework", milestone 81, "Protocol bridges").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: framing an RCP request
 * for an external CAN segment is exactly as useful a job under TC18 as
 * it was before -- this module's config shape (rcp_can_config_t) and its
 * compile-time-stub nature are unchanged. What changes is the call shape
 * it's framed around, the same rebind every other bridge stub in this
 * milestone gets: the old rcp_can_controller_new() wrapped a whole
 * rcp_controller_t's send()/subscribe()/close() vtable, a choke point
 * that no longer exists (ROADMAP.md's Protocol Replacement Notice
 * retires rcp_controller_t's vtable along with
 * rcp_zone_t/rcp_command_t/rcp_response_t). rcp_can_bridge_send() is now
 * the whole entry point, called directly by the caller in place of
 * whichever endpoint-specific encode/send call it would otherwise be
 * driving, the same caller-driven shape milestone 80's
 * authz.c/faultinject.c/observe.c already established.
 *
 * A request frame is identified the same way every other rebound module
 * in this codebase identifies one: avtp.h's rcp_avtp_addr_t (stream_id +
 * byte_bus_id) plus a caller-supplied, deliberately opaque request_type
 * byte this module never itself interprets. payload/payload_len are the
 * already-encoded ACF request body the caller would otherwise hand to a
 * native endpoint's own send path -- this module's only job is carrying
 * that opaque byte string to/from the external CAN segment cfg
 * describes, never decoding it.
 *
 * This is still a compile-time interface stub: no SocketCAN (Linux) or
 * hardware CAN driver backend is linked, so rcp_can_bridge_send() always
 * returns RCP_ERR_NOT_SUPPORTED rather than attempting a frame send over
 * an unconfigured channel, leaving *out_response untouched -- mirrors
 * cpp-RCP's own canbr.hpp, which ships the same stub absent a linked CAN
 * backend. Wiring in a real CAN backend is future work (see ROADMAP.md).
 *
 * ── Narrowed role: three genuinely distinct CAN-shaped things (carried from Phase 19) ──
 *
 * This codebase has three genuinely distinct things that all happen to
 * share the CAN/CAN-FD/CAN-XL bus technology, and this milestone's own
 * roadmap entry (ROADMAP.md's Satellite Disposition table) requires this
 * paragraph to spell the distinction out explicitly so nobody rebuilds
 * the same CAN support a third time:
 *
 *   1. ep_can.h/ep_can.c: a *native RCP endpoint* -- an RC Server exposes
 *      a physical CAN/CAN-FD/CAN-XL bus it owns as one of its own
 *      byte_bus_id-addressed endpoints, reached the same way every other
 *      endpoint type in this codebase is reached (ACF request/response
 *      over an AVTPDU). See ep_can.h's own file header for the fuller
 *      three-way discussion this paragraph mirrors.
 *   2. CAN(FD/XL)-as-RCP's-own-underlying-transport: avtp.h's
 *      rcp_avtp_transport_t vtable (milestone 59) documents CAN(FD/XL)
 *      as an eventual carrier for AVTPDUs *themselves* between RC Nodes
 *      -- a transport-layer role, not an endpoint exposed over the wire.
 *      No such transport ships yet.
 *   3. This module (canbr.h/canbr.c): this bridge's only remaining,
 *      non-overlapping role -- gatewaying to an *external* CAN segment
 *      that is reachable through neither path above (not a byte_bus_id
 *      this RC Server itself owns, and not the transport carrying this
 *      RC Node's own AVTPDUs). rcp_can_config_t (can_id_base/fd_mode/
 *      timeout_ms) models exactly that job: framing an already-encoded
 *      RCP request onto a foreign CAN segment's own arbitration-ID
 *      space, not exposing or carrying this protocol's own traffic.
 *      Every call this module exposes currently returns
 *      RCP_ERR_NOT_SUPPORTED (no backend linked).
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_CANBR_H
#define RCP_CANBR_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t can_id_base;  /* base arbitration ID, default: 0x100 */
    bool     fd_mode;      /* CAN-FD frames, default: false */
    uint64_t timeout_ms;   /* default: 100 */
} rcp_can_config_t;

/* { can_id_base = 0x100, fd_mode = false, timeout_ms = 100 }. */
rcp_can_config_t rcp_can_default_config(void);

/* Bridges a single already-encoded RCP request (payload/payload_len,
 * exactly what the caller would otherwise hand to a native endpoint's own
 * send path) for the endpoint at addr to an external CAN segment
 * described by cfg. request_type is passed through opaque, unexamined,
 * purely so a real backend could one day route/label the call. On
 * success, populates *out_response with a freshly heap-allocated
 * response the caller frees with rcp_bytes_free(); on failure,
 * *out_response is left untouched. Currently always returns
 * RCP_ERR_NOT_SUPPORTED (no CAN backend linked). */
int rcp_can_bridge_send(rcp_can_config_t cfg, rcp_avtp_addr_t addr,
                         uint8_t request_type,
                         const uint8_t *payload, size_t payload_len,
                         rcp_bytes_t *out_response);

#ifdef __cplusplus
}
#endif

#endif /* RCP_CANBR_H */
