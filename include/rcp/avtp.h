/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-AVTP-001
//cfusa:req REQ-AVTP-002
//cfusa:req REQ-AVTP-003
//cfusa:req REQ-AVTP-004
//cfusa:req REQ-AVTP-005
//cfusa:req REQ-AVTP-006
//cfusa:req REQ-AVTP-007
//cfusa:req REQ-AVTP-008
//cfusa:req REQ-AVTP-009
//cfusa:req REQ-AVTP-010
//cfusa:req REQ-AVTP-011
//cfusa:req REQ-AVTP-012
//cfusa:req REQ-AVTP-013
//cfusa:req REQ-AVTP-014
//cfusa:req REQ-AVTP-015
//cfusa:req REQ-AVTP-016
//cfusa:req REQ-AVTP-017
//cfusa:req REQ-AVTP-018
//cfusa:req REQ-AVTP-019
//cfusa:req REQ-AVTP-020
/*
 * avtp.h -- IEEE 1722 AVTPDU framing for the TC18 Remote Control Protocol
 * wire layer (ROADMAP.md Phase 13, "Wire Format Core", milestone 59).
 *
 * This is new, additive protocol-core surface for the OPEN Alliance TC18
 * Remote Control Protocol replacement program described in ROADMAP.md's
 * Protocol Replacement Notice. It is independent of the legacy Zone /
 * Command / Response / Status model in rcp.h and the length-framed codec
 * in wire.c -- neither is touched or depended on here. Terminology used
 * by this header and its implementation follows the TC18 vocabulary
 * going forward: RC System / RC Node / RC Client / RC Server / RC Edge
 * Node / Endpoint (EP) / RCP Message / RCP Frame -- not the legacy
 * zone-controller / HPC / Command / Response / Status terms this
 * codebase's older modules use.
 *
 * Two AVTPDU header variants are modeled, both drawn from the public
 * IEEE 1722-2016 base standard's own subtype registry (not the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC, which layers ACF messages -- ROADMAP.md milestone 60 -- on
 * top of whichever of these two an RC Node sends):
 *
 *   - NTSCF (Non-Time-Synchronous Control Format, AVTP subtype 0x82): the
 *     *only* header format an RC Server itself ever sends, and one an
 *     RC Client may also use. Carries an ntscf_data_length and a
 *     sequence_num; no timestamp field at all.
 *   - TSCF (Time-Synchronous Control Format, AVTP subtype 0x05):
 *     client-to-server only. Carries a stream_data_length and an
 *     avtp_timestamp, whose presentation-time semantics mean "the
 *     earliest time this request may execute" -- an RC Server remains
 *     free to execute later, but never earlier; it is not a hard
 *     deadline.
 *
 * Addressing: TC18 addresses an RCP Message by (stream_id, byte_bus_id)
 * rather than the legacy rcp_zone_t used elsewhere in this codebase.
 * stream_id is the IEEE 1722 64-bit StreamID (a 48-bit sender MAC
 * followed by a 16-bit suffix the sender assigns uniquely among its own
 * streams); byte_bus_id addresses one endpoint within a stream and is
 * only guaranteed unique *within* that stream_id, never globally. These
 * two types -- plus the rcp_avtp_addr_t pair below -- are the addressing
 * model every module built against this wire layer uses from here on.
 *
 * Transport independence is deliberate from this milestone forward: the
 * rcp_avtp_transport_t vtable abstracts how a fully-framed AVTPDU
 * actually reaches the wire, so that adding the IEEE1722-over-UDP/IP
 * (spec Annex J) and CAN(FD/XL)-as-underlying-network carriers alongside
 * native Ethernet (extraction §2.1, §3.13) never requires touching the
 * codec above this line -- the mistake this project's legacy
 * Ethernet-only udp.c/tsn.c pair made for the old protocol and must not
 * repeat here. Only an in-process loopback carrier
 * (rcp_avtp_loopback_transport_new(), below) ships in this milestone, as
 * a reference implementation of the vtable contract and a test double;
 * real Ethernet/UDP/CAN carriers are tracked across this milestone and
 * the next per ROADMAP.md and are not yet implemented.
 */
#ifndef RCP_AVTP_H
#define RCP_AVTP_H

#include "rcp/rcp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AVTP subtype byte values, per the public IEEE 1722-2016 subtype
 * registry (not TC18-specific): occupies byte 0 of every AVTPDU. */
#define RCP_AVTP_SUBTYPE_TSCF  ((uint8_t)0x05u)
#define RCP_AVTP_SUBTYPE_NTSCF ((uint8_t)0x82u)

/* Fixed header lengths in octets, excluding the variable-length payload
 * that follows. */
#define RCP_AVTP_NTSCF_HEADER_LEN ((size_t)12u)
#define RCP_AVTP_TSCF_HEADER_LEN  ((size_t)24u)

/* Largest payload rcp_avtp_encode_ntscf()/_tscf() can represent, bounded
 * by the width of ntscf_data_length (11 bits) and stream_data_length
 * (16 bits) respectively. Encoding a larger payload fails (see below). */
#define RCP_AVTP_NTSCF_MAX_PAYLOAD ((size_t)2047u)
#define RCP_AVTP_TSCF_MAX_PAYLOAD  ((size_t)65535u)

typedef enum {
    RCP_AVTP_OK              = 0,
    RCP_AVTP_ERR_SHORT_FRAME = 1,
    RCP_AVTP_ERR_BAD_SUBTYPE = 2,
} rcp_avtp_errc_t;

/* Human-readable message for an rcp_avtp_errc_t value. Never returns NULL. */
const char *rcp_avtp_strerror(rcp_avtp_errc_t e);

/* ── stream_id / byte_bus_id addressing ───────────────────────────────────── */

/* IEEE 1722 64-bit StreamID: a sender's 48-bit MAC address (network byte
 * order) followed by a 16-bit suffix the sender assigns, unique only
 * among the streams that sender itself originates. */
typedef struct {
    uint8_t  mac[6];
    uint16_t unique_id;
} rcp_stream_id_t;

/* Endpoint-within-stream address. Unique only within the stream_id it is
 * paired with (see rcp_avtp_addr_t) -- never globally, and never on its
 * own. */
typedef uint8_t rcp_byte_bus_id_t;

/* Builds a stream_id from a 6-byte MAC and a caller-assigned unique_id. */
rcp_stream_id_t rcp_stream_id_make(const uint8_t mac[6], uint16_t unique_id);

/* Packs/unpacks a stream_id to/from its 64-bit on-wire big-endian form
 * (the 48-bit MAC in the high-order bits, unique_id in the low-order
 * 16 bits), matching the STREAM_ID field layout both header variants
 * below embed. */
uint64_t        rcp_stream_id_to_u64(rcp_stream_id_t id);
rcp_stream_id_t rcp_stream_id_from_u64(uint64_t v);

bool rcp_stream_id_equal(rcp_stream_id_t a, rcp_stream_id_t b);

/* (stream_id, byte_bus_id) address pair -- replaces rcp_zone_t for every
 * new module built against this AVTP wire layer. Two addresses are equal
 * only if both components match; a shared stream_id alone does not make
 * two endpoints the same address. */
typedef struct {
    rcp_stream_id_t   stream_id;
    rcp_byte_bus_id_t byte_bus_id;
} rcp_avtp_addr_t;

bool rcp_avtp_addr_equal(rcp_avtp_addr_t a, rcp_avtp_addr_t b);

/* ── NTSCF header ──────────────────────────────────────────────────────────── */

/* the *only* AVTPDU header format an RC Server itself ever sends; an RC
 * Client may also use it for requests that carry no presentation-time
 * semantics. */
typedef struct {
    uint8_t         sv;      /* stream_id valid; always 1 for TC18 use */
    uint8_t         version; /* AVTP version; always 0 in this revision */
    uint16_t        ntscf_data_length; /* payload length in octets */
    uint8_t         sequence_num;
    rcp_stream_id_t stream_id;
} rcp_avtp_ntscf_header_t;

/* Encodes hdr plus a payload of payload_len octets (payload may be NULL
 * iff payload_len == 0) into a freshly heap-allocated AVTPDU. hdr's own
 * ntscf_data_length is ignored on encode and recomputed from payload_len,
 * mirroring wire.c's encode_* convention of deriving the length field
 * rather than trusting a caller-supplied one. Returns a zeroed
 * rcp_bytes_t (data=NULL) on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_avtp_encode_ntscf(const rcp_avtp_ntscf_header_t *hdr,
                                   const uint8_t *payload, size_t payload_len);

/* Decodes an NTSCF AVTPDU from b[0..len). On RCP_AVTP_OK, *out_hdr is
 * populated and *out_payload / *out_payload_len point *into* b (borrowed,
 * not copied -- unlike wire.c's decode_* functions, which allocate; the
 * caller must keep b alive at least as long as it uses *out_payload).
 * Returns RCP_AVTP_ERR_SHORT_FRAME if b is shorter than the fixed header
 * or than the header-plus-declared-payload length. Returns
 * RCP_AVTP_ERR_BAD_SUBTYPE if byte 0 is not RCP_AVTP_SUBTYPE_NTSCF. */
rcp_avtp_errc_t rcp_avtp_decode_ntscf(const uint8_t *b, size_t len,
                                      rcp_avtp_ntscf_header_t *out_hdr,
                                      const uint8_t **out_payload, size_t *out_payload_len);

/* ── TSCF header ───────────────────────────────────────────────────────────── */

/* Client-to-server only. Never sent by an RC Server. */
typedef struct {
    uint8_t         sv;
    uint8_t         version;
    uint8_t         mr;  /* media clock restart; carried through unmodified */
    uint8_t         tv;  /* avtp_timestamp valid */
    uint8_t         sequence_num;
    uint8_t         tu;  /* avtp_timestamp uncertain */
    rcp_stream_id_t stream_id;
    /* Presentation time: the earliest moment the carried request may
     * execute. Never a hard deadline -- an RC Server that only gets to it
     * later is still conforming, it just must not act on it early. */
    uint32_t        avtp_timestamp;
    uint16_t        stream_data_length; /* payload length in octets */
} rcp_avtp_tscf_header_t;

/* Same conventions as rcp_avtp_encode_ntscf(): stream_data_length is
 * recomputed from payload_len, not read from hdr. */
rcp_bytes_t rcp_avtp_encode_tscf(const rcp_avtp_tscf_header_t *hdr,
                                  const uint8_t *payload, size_t payload_len);

/* Same conventions as rcp_avtp_decode_ntscf(): *out_payload borrows from b. */
rcp_avtp_errc_t rcp_avtp_decode_tscf(const uint8_t *b, size_t len,
                                     rcp_avtp_tscf_header_t *out_hdr,
                                     const uint8_t **out_payload, size_t *out_payload_len);

/* ── Subtype dispatch & the TSCF-without-time-sync drop rule ──────────────── */

/* Reads just the subtype byte (offset 0) from a received AVTPDU, so a
 * caller can decide which of rcp_avtp_decode_ntscf()/_tscf() to invoke
 * without a full decode attempt first. Returns RCP_AVTP_ERR_SHORT_FRAME
 * if len == 0. */
rcp_avtp_errc_t rcp_avtp_peek_subtype(const uint8_t *b, size_t len, uint8_t *out_subtype);

/* Unconditional rule (extraction §2.2): an RC Server that has not enabled
 * gPTP time synchronization support MUST silently drop any AVTPDU
 * carrying a TSCF header -- no response, no error frame, nothing
 * observable on the wire. This is its own function (rather than folded
 * into a transport's receive loop) precisely so the rule is a deliberate,
 * directly-testable code path and can never regress into an incidental
 * side effect of, say, an unimplemented-timestamp branch elsewhere.
 * Returns false for every subtype other than RCP_AVTP_SUBTYPE_TSCF,
 * regardless of server_time_sync_supported. */
bool rcp_avtp_should_drop_tscf(bool server_time_sync_supported, uint8_t subtype);

/* ── Transport vtable ──────────────────────────────────────────────────────── */

typedef struct rcp_avtp_transport rcp_avtp_transport_t;

typedef struct {
    /* Sends one already-framed AVTPDU (as produced by
     * rcp_avtp_encode_ntscf()/_tscf()) as a single unit on the underlying
     * carrier. Returns RCP_OK, or an rcp_errc_t value on failure
     * (RCP_ERR_CLOSED if the transport has been closed). */
    int (*send)(rcp_avtp_transport_t *self, const uint8_t *frame, size_t frame_len);

    /* Blocks (subject to ctx) until one AVTPDU is available, copying it
     * into buf (up to buf_cap bytes) and reporting the actual length via
     * *out_len. Returns RCP_ERR_TIMEOUT if ctx becomes done first,
     * RCP_ERR_CLOSED if the transport has been closed with nothing
     * pending, RCP_ERR_BUSY if the received frame is larger than buf_cap. */
    int (*recv)(rcp_avtp_transport_t *self, const rcp_context_t *ctx,
                uint8_t *buf, size_t buf_cap, size_t *out_len);

    /* Releases transport resources and unblocks any in-progress recv().
     * Safe to call multiple times. */
    int (*close)(rcp_avtp_transport_t *self);

    /* Frees the concrete implementation once its refcount reaches 0.
     * Never called directly; invoked by rcp_avtp_transport_release(). */
    void (*destroy)(rcp_avtp_transport_t *self);
} rcp_avtp_transport_vtable_t;

/* Base "class": concrete implementations embed this as their first
 * member, mirroring rcp_controller_t's convention in rcp.h. */
struct rcp_avtp_transport {
    const rcp_avtp_transport_vtable_t *vt;
    int                                 refcount;
    /* Whether this RC Node instance has gPTP time synchronization
     * enabled; gates rcp_avtp_should_drop_tscf() at call sites that hold
     * only a transport handle. Concrete transports set this at
     * construction time; it is not mutated afterward. */
    bool                                time_sync_supported;
};

rcp_avtp_transport_t *rcp_avtp_transport_retain(rcp_avtp_transport_t *t);
void                  rcp_avtp_transport_release(rcp_avtp_transport_t *t);

static inline int rcp_avtp_transport_send(rcp_avtp_transport_t *t,
                                           const uint8_t *frame, size_t frame_len)
{
    return t->vt->send(t, frame, frame_len);
}

static inline int rcp_avtp_transport_recv(rcp_avtp_transport_t *t, const rcp_context_t *ctx,
                                           uint8_t *buf, size_t buf_cap, size_t *out_len)
{
    return t->vt->recv(t, ctx, buf, buf_cap, out_len);
}

static inline int rcp_avtp_transport_close(rcp_avtp_transport_t *t)
{
    return t->vt->close(t);
}

/* ── Loopback transport (reference implementation / test double) ─────────── */

/* In-process, zero-I/O transport backed by a bounded FIFO of frames:
 * rcp_avtp_transport_send() enqueues a copy, rcp_avtp_transport_recv()
 * dequeues it. Exercises the vtable contract (including the ctx-timeout
 * and closed-transport paths) without any real Ethernet/UDP/CAN carrier.
 * Mirrors mock.h's role for rcp_controller_t. Returned with refcount 1;
 * release with rcp_avtp_transport_release(). */
rcp_avtp_transport_t *rcp_avtp_loopback_transport_new(bool time_sync_supported,
                                                       size_t queue_capacity);

#ifdef __cplusplus
}
#endif

#endif /* RCP_AVTP_H */
