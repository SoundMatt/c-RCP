/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/fragment.h"

#include "rcp/alloc.h"

#include <string.h>

//cfusa:req REQ-FRAG-001
const char *rcp_fragment_strerror(rcp_fragment_errc_t e)
{
    switch (e) {
    case RCP_FRAGMENT_OK:                    return "rcp/fragment: success";
    case RCP_FRAGMENT_ERR_DISABLED:          return "rcp/fragment: fragmentation disabled for this max_fragment_payload";
    case RCP_FRAGMENT_ERR_TOO_MANY_SEGMENTS: return "rcp/fragment: payload needs more segments than segment_num can address";
    case RCP_FRAGMENT_ERR_BAD_SEGMENT_COUNT: return "rcp/fragment: segment_count does not match rcp_fragment_plan_count()";
    default:                                 return "rcp/fragment: unknown error";
    }
}

/* ── Planning (encode side) ────────────────────────────────────────────────── */

//cfusa:req REQ-FRAG-002
//cfusa:req REQ-FRAG-003
size_t rcp_fragment_plan_count(size_t payload_len, size_t max_fragment_payload)
{
    size_t count;
    size_t intermediate;

    if (payload_len == 0) return 1;
    if (max_fragment_payload == 0) return 0;
    if (payload_len <= max_fragment_payload) return 1;

    count = (payload_len + max_fragment_payload - 1) / max_fragment_payload;
    intermediate = count - 1; /* every segment but the last is intermediate */
    if (intermediate > RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS) return 0;

    return count;
}

//cfusa:req REQ-FRAG-004
//cfusa:req REQ-FRAG-005
//cfusa:req REQ-FRAG-006
rcp_fragment_errc_t rcp_fragment_plan(size_t payload_len, size_t max_fragment_payload,
                                       rcp_fragment_segment_t *out_segments,
                                       size_t segment_count)
{
    size_t expected = rcp_fragment_plan_count(payload_len, max_fragment_payload);
    size_t off;
    size_t i;

    if (expected == 0) {
        return (max_fragment_payload == 0) ? RCP_FRAGMENT_ERR_DISABLED
                                            : RCP_FRAGMENT_ERR_TOO_MANY_SEGMENTS;
    }
    if (segment_count != expected) return RCP_FRAGMENT_ERR_BAD_SEGMENT_COUNT;

    if (expected == 1) {
        out_segments[0].offset      = 0;
        out_segments[0].len         = payload_len;
        out_segments[0].ms          = false;
        out_segments[0].segment_num = 0;
        return RCP_FRAGMENT_OK;
    }

    off = 0;
    for (i = 0; i + 1 < expected; i++) {
        out_segments[i].offset      = off;
        out_segments[i].len         = max_fragment_payload;
        out_segments[i].ms          = true;
        out_segments[i].segment_num = (uint16_t)i;
        off += max_fragment_payload;
    }

    out_segments[expected - 1].offset      = off;
    out_segments[expected - 1].len         = payload_len - off;
    out_segments[expected - 1].ms          = false;
    out_segments[expected - 1].segment_num = 0;

    return RCP_FRAGMENT_OK;
}

/* ── Reassembly (decode side) ─────────────────────────────────────────────── */

//cfusa:req REQ-FRAG-007
const char *rcp_fragment_reasm_result_string(rcp_fragment_reasm_result_t r)
{
    switch (r) {
    case RCP_FRAGMENT_REASM_CONTINUE:         return "rcp/fragment: fragment accepted, more expected";
    case RCP_FRAGMENT_REASM_COMPLETE:         return "rcp/fragment: fragment accepted, reassembly complete";
    case RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER: return "rcp/fragment: out-of-order segment_num";
    case RCP_FRAGMENT_REASM_ERR_TOO_LARGE:    return "rcp/fragment: reassembled payload would exceed max_total_len";
    case RCP_FRAGMENT_REASM_ERR_ALLOC:        return "rcp/fragment: internal allocation failure";
    default:                                  return "rcp/fragment: unknown result";
    }
}

//cfusa:req REQ-FRAG-008
void rcp_fragment_reassembler_init(rcp_fragment_reassembler_t *r, size_t max_total_len)
{
    r->collecting            = false;
    r->expected_segment_num  = 0;
    r->buf                   = NULL;
    r->len                   = 0;
    r->cap                   = 0;
    r->max_total_len         = max_total_len;
}

//cfusa:req REQ-FRAG-009
void rcp_fragment_reassembler_reset(rcp_fragment_reassembler_t *r)
{
    size_t max_total_len = r->max_total_len;

    rcp_free(r->buf);
    rcp_fragment_reassembler_init(r, max_total_len);
}

//cfusa:req REQ-FRAG-010
void rcp_fragment_reassembler_destroy(rcp_fragment_reassembler_t *r)
{
    rcp_free(r->buf);
    memset(r, 0, sizeof(*r));
}

/* Grows r->buf (if needed) so that appending append_len more octets past
 * r->len fits within r->cap, then copies payload in and advances r->len.
 * Returns false (r left entirely untouched) on allocation failure. Does
 * not itself enforce max_total_len -- the caller checks that first. */
static bool append(rcp_fragment_reassembler_t *r, const uint8_t *payload, size_t append_len)
{
    if (append_len == 0) return true;

    if (r->len + append_len > r->cap) {
        size_t   new_cap = (r->cap == 0) ? append_len : r->cap * 2;
        uint8_t *grown;

        if (new_cap < r->len + append_len) new_cap = r->len + append_len;

        grown = (uint8_t *)rcp_realloc(r->buf, new_cap);
        if (!grown) return false;

        r->buf = grown;
        r->cap = new_cap;
    }

    memcpy(&r->buf[r->len], payload, append_len);
    r->len += append_len;
    return true;
}

//cfusa:req REQ-FRAG-011
//cfusa:req REQ-FRAG-012
//cfusa:req REQ-FRAG-013
//cfusa:req REQ-FRAG-014
//cfusa:req REQ-FRAG-015
//cfusa:req REQ-FRAG-016
rcp_fragment_reasm_result_t rcp_fragment_reassembler_feed(rcp_fragment_reassembler_t *r,
                                                            bool ms, uint16_t segment_num,
                                                            const uint8_t *payload,
                                                            size_t payload_len)
{
    if (!r->collecting) {
        if (payload_len > r->max_total_len - r->len) return RCP_FRAGMENT_REASM_ERR_TOO_LARGE;

        if (!ms) {
            /* Never-fragmented, single-segment message. */
            if (!append(r, payload, payload_len)) return RCP_FRAGMENT_REASM_ERR_ALLOC;
            return RCP_FRAGMENT_REASM_COMPLETE;
        }

        if (segment_num != 0) return RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER;

        if (!append(r, payload, payload_len)) return RCP_FRAGMENT_REASM_ERR_ALLOC;
        r->collecting           = true;
        r->expected_segment_num = 1;
        return RCP_FRAGMENT_REASM_CONTINUE;
    }

    if (ms && segment_num != r->expected_segment_num) return RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER;
    if (payload_len > r->max_total_len - r->len) return RCP_FRAGMENT_REASM_ERR_TOO_LARGE;

    if (!append(r, payload, payload_len)) return RCP_FRAGMENT_REASM_ERR_ALLOC;

    if (ms) {
        r->expected_segment_num = (uint16_t)(r->expected_segment_num + 1);
        return RCP_FRAGMENT_REASM_CONTINUE;
    }

    r->collecting = false;
    return RCP_FRAGMENT_REASM_COMPLETE;
}

//cfusa:req REQ-FRAG-017
bool rcp_fragment_reassembler_is_collecting(const rcp_fragment_reassembler_t *r)
{
    return r->collecting;
}

//cfusa:req REQ-FRAG-018
void rcp_fragment_reassembler_get(const rcp_fragment_reassembler_t *r,
                                   const uint8_t **out_payload, size_t *out_payload_len)
{
    *out_payload     = r->buf;
    *out_payload_len = r->len;
}
