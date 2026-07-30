/* SPDX-License-Identifier: MPL-2.0 */
/*
 * linbr.h -- LIN (Local Interconnect Network) bus bridge interface stub
 * (SG-006) for the TC18 Remote Control Protocol wire layer (ROADMAP.md
 * Phase 21, "Satellite Package Rework", milestone 81, "Protocol
 * bridges").
 *
 * ADAPT-class rebind, not a from-scratch REPLACE: framing an RCP request
 * for an external LIN segment is exactly as useful a job under TC18 as
 * it was before -- this module's config shape (rcp_lin_config_t) and its
 * compile-time-stub nature are unchanged. What changes is the call shape
 * it's framed around, the same rebind every other bridge stub in this
 * milestone gets: the old rcp_lin_controller_new() wrapped a whole
 * rcp_controller_t's send()/subscribe()/close() vtable, a choke point
 * that no longer exists (ROADMAP.md's Protocol Replacement Notice
 * retires rcp_controller_t's vtable along with
 * rcp_zone_t/rcp_command_t/rcp_response_t). rcp_lin_bridge_send() is now
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
 * that opaque byte string to/from the external LIN segment cfg
 * describes, never decoding it.
 *
 * This is still a compile-time interface stub: no SocketCAN LIN driver
 * or dedicated LIN hardware API backend is linked, so
 * rcp_lin_bridge_send() always returns RCP_ERR_NOT_SUPPORTED rather than
 * attempting a master-frame request over an unconfigured bus, leaving
 * *out_response untouched -- mirrors cpp-RCP's own linbr.hpp, which
 * ships the same stub absent a linked LIN backend. Wiring in a real LIN
 * backend is future work (see ROADMAP.md).
 *
 * ── Narrowed role: two genuinely distinct LIN-shaped things (carried from Phase 19) ──
 *
 * Unlike CAN (see canbr.h's own three-way discussion), LIN has no
 * transport-network role in this codebase -- avtp.h's
 * rcp_avtp_transport_t only ever documents CAN(FD/XL) as a candidate
 * AVTPDU carrier, never LIN. This codebase therefore has exactly two
 * genuinely distinct LIN-shaped things, and this milestone's own roadmap
 * entry (ROADMAP.md's Satellite Disposition table) requires this
 * paragraph to spell the distinction out explicitly so nobody rebuilds
 * the same LIN support twice:
 *
 *   1. ep_lin.h/ep_lin.c: a *native RCP endpoint* -- an RC Server exposes
 *      a physical LIN bus it owns as one of its own byte_bus_id-
 *      addressed endpoints, reached the same way every other endpoint
 *      type in this codebase is reached (ACF request/response over an
 *      AVTPDU), as a raw-byte pass-through LIN master with no classic
 *      LIN-frame concept (checksum mode, PID generation, schedule
 *      table) at this layer. See ep_lin.h's own file header for the
 *      fuller discussion this paragraph mirrors.
 *   2. This module (linbr.h/linbr.c): this bridge's only remaining,
 *      non-overlapping role -- gatewaying to an *external* LIN segment
 *      that is reachable through neither an RC Server's own native LIN
 *      endpoint nor any transport role (LIN has none). rcp_lin_config_t
 *      (frame_id/timeout_ms) models exactly that job: framing an
 *      already-encoded RCP request as a classic LIN master-frame request
 *      (identifier/PID byte) on a foreign LIN segment, not exposing this
 *      protocol's own traffic. Every call this module exposes currently
 *      returns RCP_ERR_NOT_SUPPORTED (no backend linked).
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 */
#ifndef RCP_LINBR_H
#define RCP_LINBR_H

#include "rcp/avtp.h"
#include "rcp/rcp.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  frame_id;   /* default: 0x10 */
    uint64_t timeout_ms; /* default: 50 */
} rcp_lin_config_t;

/* { frame_id = 0x10, timeout_ms = 50 }. */
rcp_lin_config_t rcp_lin_default_config(void);

/* Bridges a single already-encoded RCP request (payload/payload_len,
 * exactly what the caller would otherwise hand to a native endpoint's own
 * send path) for the endpoint at addr to an external LIN segment
 * described by cfg. request_type is passed through opaque, unexamined,
 * purely so a real backend could one day route/label the call. On
 * success, populates *out_response with a freshly heap-allocated
 * response the caller frees with rcp_bytes_free(); on failure,
 * *out_response is left untouched. Currently always returns
 * RCP_ERR_NOT_SUPPORTED (no LIN backend linked). */
int rcp_lin_bridge_send(rcp_lin_config_t cfg, rcp_avtp_addr_t addr,
                         uint8_t request_type,
                         const uint8_t *payload, size_t payload_len,
                         rcp_bytes_t *out_response);

#ifdef __cplusplus
}
#endif

#endif /* RCP_LINBR_H */
