#include "rcp/discovery.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-DISC-024
const char *rcp_discovery_strerror(rcp_discovery_errc_t e)
{
    switch (e) {
    case RCP_DISCOVERY_OK:               return "rcp/discovery: success";
    case RCP_DISCOVERY_ERR_SHORT_FRAME:  return "rcp/discovery: frame too short";
    case RCP_DISCOVERY_ERR_NOT_NTSCF:    return "rcp/discovery: dropped -- not NTSCF-headed";
    case RCP_DISCOVERY_ERR_BAD_MSG_TYPE: return "rcp/discovery: unexpected ACF message type";
    case RCP_DISCOVERY_ERR_WRONG_BUS:    return "rcp/discovery: wrong byte_bus_id";
    case RCP_DISCOVERY_ERR_WRONG_OP:     return "rcp/discovery: wrong ACF op";
    default:                             return "rcp/discovery: unknown error";
    }
}

/* ── Byte-order helpers (this TU's own copy, matching acf.c's/avtp.c's
 * house convention of not sharing a byte-order util across modules) ────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* ── NTSCF-only rule ────────────────────────────────────────────────────────── */

//cfusa:req REQ-DISC-001
bool rcp_discovery_should_drop(uint8_t avtp_subtype)
{
    return avtp_subtype != RCP_AVTP_SUBTYPE_NTSCF;
}

/* ── Shared request/response ACF-level validation ──────────────────────────── */

/* Common to both rcp_discovery_decode_request() and
 * _decode_response(): peel the NTSCF layer (applying the NTSCF-only rule
 * first), then the ACF_ABB layer, then check byte_bus_id and op. Neither
 * caller is expected to see a non-ABB, wrong-bus, or wrong-op frame in
 * practice, but both validate the same way rather than assuming it. */
static rcp_discovery_errc_t decode_common(const uint8_t *b, size_t len,
                                           rcp_stream_id_t *out_stream_id,
                                           rcp_acf_byte_message_info_t *out_hdr,
                                           const uint8_t **out_payload,
                                           size_t *out_payload_len)
{
    uint8_t                 subtype;
    rcp_avtp_errc_t          avtp_rc;
    rcp_avtp_ntscf_header_t  ntscf_hdr;
    const uint8_t           *ntscf_payload;
    size_t                   ntscf_payload_len;
    rcp_acf_errc_t           acf_rc;

    avtp_rc = rcp_avtp_peek_subtype(b, len, &subtype);
    if (avtp_rc != RCP_AVTP_OK) return RCP_DISCOVERY_ERR_SHORT_FRAME;

    /* Checked before any further parsing is attempted, per the file
     * header: a TSCF-headed (or otherwise non-NTSCF) frame is dropped
     * outright, independent of lifecycle state or time-sync capability. */
    if (rcp_discovery_should_drop(subtype)) return RCP_DISCOVERY_ERR_NOT_NTSCF;

    avtp_rc = rcp_avtp_decode_ntscf(b, len, &ntscf_hdr, &ntscf_payload, &ntscf_payload_len);
    if (avtp_rc == RCP_AVTP_ERR_SHORT_FRAME) return RCP_DISCOVERY_ERR_SHORT_FRAME;
    if (avtp_rc != RCP_AVTP_OK) return RCP_DISCOVERY_ERR_NOT_NTSCF;

    acf_rc = rcp_acf_decode_abb(ntscf_payload, ntscf_payload_len, out_hdr, out_payload, out_payload_len);
    if (acf_rc == RCP_ACF_ERR_SHORT_FRAME) return RCP_DISCOVERY_ERR_SHORT_FRAME;
    if (acf_rc != RCP_ACF_OK) return RCP_DISCOVERY_ERR_BAD_MSG_TYPE;

    if (out_hdr->byte_bus_id != RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID) return RCP_DISCOVERY_ERR_WRONG_BUS;
    if (out_hdr->op != RCP_ACF_OP_READ) return RCP_DISCOVERY_ERR_WRONG_OP;

    *out_stream_id = ntscf_hdr.stream_id;
    return RCP_DISCOVERY_OK;
}

/* ── Discovery request ──────────────────────────────────────────────────────── */

//cfusa:req REQ-DISC-002
rcp_bytes_t rcp_discovery_encode_request(rcp_stream_id_t requester_stream_id,
                                          uint8_t read_size,
                                          uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr = {0};
    rcp_avtp_ntscf_header_t     ntscf_hdr = {0};
    rcp_bytes_t                 acf_frame;
    rcp_bytes_t                 frame = {0};

    hdr.byte_bus_id               = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    hdr.op                        = RCP_ACF_OP_READ;
    hdr.read_size_or_segment_num  = read_size;
    hdr.transaction_num           = transaction_num;

    acf_frame = rcp_acf_encode_abb(&hdr, NULL, 0);
    if (!acf_frame.data) return frame;

    ntscf_hdr.sv        = 1;
    ntscf_hdr.stream_id = requester_stream_id;

    frame = rcp_avtp_encode_ntscf(&ntscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);
    return frame;
}

//cfusa:req REQ-DISC-003
//cfusa:req REQ-DISC-004
//cfusa:req REQ-DISC-005
//cfusa:req REQ-DISC-006
//cfusa:req REQ-DISC-007
//cfusa:req REQ-DISC-008
rcp_discovery_errc_t rcp_discovery_decode_request(const uint8_t *b, size_t len,
                                                   rcp_discovery_request_t *out_req)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_stream_id_t              requester;
    rcp_discovery_errc_t         rc;

    rc = decode_common(b, len, &requester, &hdr, &payload, &payload_len);
    if (rc != RCP_DISCOVERY_OK) return rc;

    /* A discovery request carries no payload of its own -- read_size and
     * the address (always 0, implicit for discovery) are conveyed by the
     * ACF header alone -- so payload/payload_len are not consulted here. */
    (void)payload;
    (void)payload_len;

    out_req->requester       = requester;
    out_req->read_size       = hdr.read_size_or_segment_num;
    out_req->transaction_num = hdr.transaction_num;
    return RCP_DISCOVERY_OK;
}

/* ── Discovery response ─────────────────────────────────────────────────────── */

//cfusa:req REQ-DISC-009
//cfusa:req REQ-DISC-010
//cfusa:req REQ-DISC-011
rcp_bytes_t rcp_discovery_encode_response(const rcp_regmap_general_t *map,
                                           uint8_t read_size,
                                           uint8_t transaction_num,
                                           rcp_stream_id_t server_stream_id)
{
    uint8_t                      slice[RCP_DISCOVERY_GENERAL_SLICE_LEN];
    uint8_t                      payload[256];
    size_t                       copy_len;
    rcp_acf_byte_message_info_t  hdr = {0};
    rcp_avtp_ntscf_header_t      ntscf_hdr = {0};
    rcp_bytes_t                  acf_frame;
    rcp_bytes_t                  frame = {0};

    put_u32(&slice[0],  map->magic);
    put_u16(&slice[4],  map->svr_version);
    put_u16(&slice[6],  map->vendor_id);
    put_u16(&slice[8],  map->device_id);
    put_u16(&slice[10], map->svr_ep_count);

    /* A response's payload always spans exactly read_size octets -- see
     * RCP_DISCOVERY_GENERAL_SLICE_LEN's own comment in discovery.h. Any
     * octets beyond the populated slice are left as the zero-fill below. */
    memset(payload, 0, sizeof(payload));
    copy_len = ((size_t)read_size < RCP_DISCOVERY_GENERAL_SLICE_LEN)
                   ? (size_t)read_size
                   : RCP_DISCOVERY_GENERAL_SLICE_LEN;
    memcpy(payload, slice, copy_len);

    hdr.byte_bus_id              = RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID;
    hdr.op                       = RCP_ACF_OP_READ;
    hdr.read_size_or_segment_num = read_size;
    hdr.transaction_num          = transaction_num;

    acf_frame = rcp_acf_encode_abb(&hdr, payload, read_size);
    if (!acf_frame.data) return frame;

    ntscf_hdr.sv        = 1;
    ntscf_hdr.stream_id = server_stream_id;

    frame = rcp_avtp_encode_ntscf(&ntscf_hdr, acf_frame.data, acf_frame.len);
    rcp_bytes_free(&acf_frame);
    return frame;
}

//cfusa:req REQ-DISC-012
//cfusa:req REQ-DISC-013
//cfusa:req REQ-DISC-014
rcp_discovery_errc_t rcp_discovery_decode_response(const uint8_t *b, size_t len,
                                                    rcp_discovery_result_t *out_result)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               *payload;
    size_t                       payload_len;
    rcp_stream_id_t              server_stream_id;
    rcp_discovery_errc_t         rc;

    rc = decode_common(b, len, &server_stream_id, &hdr, &payload, &payload_len);
    if (rc != RCP_DISCOVERY_OK) return rc;

    if (payload_len < RCP_DISCOVERY_GENERAL_SLICE_LEN) return RCP_DISCOVERY_ERR_SHORT_FRAME;

    out_result->valid           = true;
    out_result->server_stream_id = server_stream_id;
    out_result->magic           = get_u32(&payload[0]);
    out_result->svr_version     = get_u16(&payload[4]);
    out_result->vendor_id       = get_u16(&payload[6]);
    out_result->device_id       = get_u16(&payload[8]);
    out_result->svr_ep_count    = get_u16(&payload[10]);
    return RCP_DISCOVERY_OK;
}

/* ── Discovery-stream claiming ──────────────────────────────────────────────── */

//cfusa:req REQ-DISC-015
void rcp_discovery_claim_init(rcp_discovery_claim_t *claim, uint32_t timeout_ms)
{
    memset(claim, 0, sizeof(*claim));
    claim->timeout_ms = timeout_ms;
}

//cfusa:req REQ-DISC-016
bool rcp_discovery_claim_is_open(const rcp_discovery_claim_t *claim, uint64_t now_ms)
{
    if (!claim->held) return true;
    return now_ms >= claim->deadline_ms;
}

//cfusa:req REQ-DISC-017
//cfusa:req REQ-DISC-018
void rcp_discovery_claim_note_request(rcp_discovery_claim_t *claim,
                                      rcp_stream_id_t requester, uint64_t now_ms)
{
    if (!rcp_discovery_claim_is_open(claim, now_ms)) return; /* held by an
        unlapsed different claimant -- not preempted, see the file header */

    claim->held        = true;
    claim->claimant     = requester;
    claim->deadline_ms  = now_ms + (uint64_t)claim->timeout_ms;
}

//cfusa:req REQ-DISC-019
bool rcp_discovery_claim_is_claimant(const rcp_discovery_claim_t *claim,
                                     rcp_stream_id_t writer, uint64_t now_ms)
{
    if (!claim->held) return false;
    if (now_ms >= claim->deadline_ms) return false;
    return rcp_stream_id_equal(claim->claimant, writer);
}

//cfusa:req REQ-DISC-020
//cfusa:req REQ-DISC-021
bool rcp_discovery_claim_note_config_write(rcp_discovery_claim_t *claim,
                                           rcp_stream_id_t writer, uint64_t now_ms)
{
    if (!rcp_discovery_claim_is_claimant(claim, writer, now_ms)) return false;

    claim->deadline_ms = now_ms + (uint64_t)claim->timeout_ms;
    return true;
}

//cfusa:req REQ-DISC-022
void rcp_discovery_claim_release(rcp_discovery_claim_t *claim)
{
    claim->held = false;
}

/* ── Client-side discovery result persistence ──────────────────────────────── */

//cfusa:req REQ-DISC-023
void rcp_discovery_cache_init(rcp_discovery_cache_t *cache)
{
    cache->entries = NULL;
    cache->len     = 0;
    cache->cap     = 0;
}

//cfusa:req REQ-DISC-023
void rcp_discovery_cache_destroy(rcp_discovery_cache_t *cache)
{
    free(cache->entries);
    cache->entries = NULL;
    cache->len     = 0;
    cache->cap     = 0;
}

//cfusa:req REQ-DISC-023
bool rcp_discovery_cache_put(rcp_discovery_cache_t *cache,
                             const rcp_discovery_result_t *result)
{
    rcp_discovery_result_t *grown;
    size_t                   i;

    for (i = 0; i < cache->len; i++) {
        if (rcp_stream_id_equal(cache->entries[i].server_stream_id, result->server_stream_id)) {
            cache->entries[i] = *result;
            return true;
        }
    }

    if (cache->len == cache->cap) {
        size_t new_cap = (cache->cap == 0) ? 4 : cache->cap * 2;

        grown = (rcp_discovery_result_t *)realloc(cache->entries, new_cap * sizeof(*grown));
        if (!grown) return false;
        cache->entries = grown;
        cache->cap     = new_cap;
    }

    cache->entries[cache->len] = *result;
    cache->len++;
    return true;
}

//cfusa:req REQ-DISC-023
const rcp_discovery_result_t *rcp_discovery_cache_find(const rcp_discovery_cache_t *cache,
                                                        rcp_stream_id_t stream_id)
{
    size_t i;

    for (i = 0; i < cache->len; i++) {
        if (rcp_stream_id_equal(cache->entries[i].server_stream_id, stream_id)) {
            return &cache->entries[i];
        }
    }
    return NULL;
}

//cfusa:req REQ-DISC-023
size_t rcp_discovery_cache_len(const rcp_discovery_cache_t *cache)
{
    return cache->len;
}
