/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/request_triggered.h"
#include "rcp/alloc.h"

#include "mem_bounded.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy -- see request_compound.c's identical
 * comment for the house convention this follows) ─────────────────────────── */

static void put_u64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)((v >> 56) & 0xFFu);
    p[1] = (uint8_t)((v >> 48) & 0xFFu);
    p[2] = (uint8_t)((v >> 40) & 0xFFu);
    p[3] = (uint8_t)((v >> 32) & 0xFFu);
    p[4] = (uint8_t)((v >> 24) & 0xFFu);
    p[5] = (uint8_t)((v >> 16) & 0xFFu);
    p[6] = (uint8_t)((v >> 8) & 0xFFu);
    p[7] = (uint8_t)(v & 0xFFu);
}

/* ── request_type classification ─────────────────────────────────────────── */

//cfusa:req REQ-TRIG-001
bool rcp_request_type_is_triggered(uint8_t request_type)
{
    return request_type == RCP_REQUEST_TYPE_TRIGGERED ||
           request_type == RCP_REQUEST_TYPE_TRIGGERED_SAFETY;
}

/* ── Errors ─────────────────────────────────────────────────────────────────── */

//cfusa:req REQ-TRIG-002
const char *rcp_triggered_strerror(rcp_triggered_errc_t e)
{
    switch (e) {
    case RCP_TRIGGERED_OK:                 return "rcp/triggered: success";
    case RCP_TRIGGERED_ERR_SHORT_FRAME:    return "rcp/triggered: frame too short";
    case RCP_TRIGGERED_ERR_BAD_MSG_TYPE:   return "rcp/triggered: unexpected ACF message type";
    case RCP_TRIGGERED_ERR_NOT_REPURPOSED: return "rcp/triggered: message_timestamp not repurposed";
    case RCP_TRIGGERED_ERR_UNKNOWN_TYPE:   return "rcp/triggered: unrecognized request_type";
    default:                               return "rcp/triggered: unknown error";
    }
}

/* ── rcp_triggered_step_t <-> the repurposed 8-byte message_timestamp region ── */

/* Octet offsets within the repurposed 8-byte message_timestamp region --
 * see request_triggered.h's "trigger-selection sub-fields" section:
 *   0 request_type | 1 trigger_source_ep | 2 trigger_signal_nr |
 *   3 trigger_threshold | 4..5 exec_delay (BE) | 6..7 repeat_count (BE) */
static uint64_t pack_ts(uint8_t request_type, const rcp_triggered_step_t *step)
{
    return (((uint64_t)request_type) << 56) |
           (((uint64_t)step->trigger_source_ep) << 48) |
           (((uint64_t)step->trigger_signal_nr) << 40) |
           (((uint64_t)step->trigger_threshold) << 32) |
           (((uint64_t)step->exec_delay) << 16) |
           ((uint64_t)step->repeat_count);
}

static void unpack_ts(uint64_t ts, rcp_triggered_step_t *out_step)
{
    out_step->trigger_source_ep = (uint8_t)((ts >> 48) & 0xFFu);
    out_step->trigger_signal_nr = (uint8_t)((ts >> 40) & 0xFFu);
    out_step->trigger_threshold = (uint8_t)((ts >> 32) & 0xFFu);
    out_step->exec_delay        = (uint16_t)((ts >> 16) & 0xFFFFu);
    out_step->repeat_count      = (uint16_t)(ts & 0xFFFFu);
}

/* ── Triggered request encode/decode ──────────────────────────────────────── */

//cfusa:req REQ-TRIG-003
//cfusa:req REQ-TRIG-004
rcp_bytes_t rcp_triggered_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                          const rcp_triggered_step_t *step, uint8_t transaction_num,
                                          const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t                  frame = {0};
    rcp_acf_byte_message_info_t  info  = {0};
    size_t                       unpadded, total;
    uint16_t                     quadlets;
    uint8_t                      pad;
    uint8_t                     *b;
    uint64_t                     ts;

    if (!rcp_request_type_is_triggered(request_type)) return frame;
    if (payload_len > RCP_ACF_GBB_MAX_PAYLOAD) return frame;

    unpadded = RCP_ACF_GBB_HEADER_LEN + payload_len;
    pad      = rcp_acf_pad_len(unpadded);
    total    = unpadded + pad;
    quadlets = (uint16_t)(total / 4u);

    b = (uint8_t *)rcp_malloc(total);
    if (!b) return frame;

    /* mtv=RCP_ACF_MTV_UNTIMED(0) -- see the file header: this repurposes
     * message_timestamp, so this module builds the header directly
     * (rcp_acf_pack_header()) rather than calling rcp_acf_encode_gbb(),
     * which would zero it. */
    info.byte_bus_id     = byte_bus_id;
    info.transaction_num  = transaction_num;
    info.pad               = pad;
    rcp_acf_pack_header(b, RCP_ACF_MSG_TYPE_GBB, quadlets, &info);

    ts = pack_ts(request_type, step);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) rcp_memcpy_bounded(&b[RCP_ACF_GBB_HEADER_LEN], payload_len, payload, payload_len);
    if (pad > 0) memset(&b[RCP_ACF_GBB_HEADER_LEN + payload_len], 0, pad);

    frame.data = b;
    frame.len  = total;
    return frame;
}

//cfusa:req REQ-TRIG-005
//cfusa:req REQ-TRIG-006
//cfusa:req REQ-TRIG-007
rcp_triggered_errc_t rcp_triggered_decode_request(const uint8_t *b, size_t len,
                                                   uint8_t *out_request_type,
                                                   rcp_byte_bus_id_t *out_byte_bus_id,
                                                   rcp_triggered_step_t *out_step,
                                                   const uint8_t **out_payload, size_t *out_payload_len,
                                                   uint8_t *out_transaction_num)
{
    rcp_acf_gbb_header_t hdr;
    rcp_acf_errc_t ae;
    uint8_t rt;

    ae = rcp_acf_decode_gbb(b, len, &hdr, out_payload, out_payload_len);
    if (ae == RCP_ACF_ERR_SHORT_FRAME) return RCP_TRIGGERED_ERR_SHORT_FRAME;
    if (ae == RCP_ACF_ERR_BAD_MSG_TYPE) return RCP_TRIGGERED_ERR_BAD_MSG_TYPE;

    if (hdr.info.mtv != RCP_ACF_MTV_UNTIMED) return RCP_TRIGGERED_ERR_NOT_REPURPOSED;

    rt = (uint8_t)((hdr.message_timestamp >> 56) & 0xFFu);
    if (!rcp_request_type_is_triggered(rt)) return RCP_TRIGGERED_ERR_UNKNOWN_TYPE;

    unpack_ts(hdr.message_timestamp, out_step);

    *out_request_type   = rt;
    *out_byte_bus_id     = hdr.info.byte_bus_id;
    *out_transaction_num = hdr.info.transaction_num;
    return RCP_TRIGGERED_OK;
}

/* ── The trigger-occurrence counter and fire tick ─────────────────────────── */

//cfusa:req REQ-TRIG-008
void rcp_triggered_runtime_enter_started(rcp_triggered_runtime_t *rt)
{
    rt->occurrence_count = 0;
    rt->started           = true;
}

//cfusa:req REQ-TRIG-009
bool rcp_triggered_runtime_record_occurrence(rcp_triggered_runtime_t *rt,
                                              const rcp_triggered_step_t *step,
                                              uint8_t source_ep, uint8_t signal_nr)
{
    if (!rt->started) return false;
    if (source_ep != step->trigger_source_ep) return false;
    if (signal_nr != step->trigger_signal_nr) return false;

    rt->occurrence_count++;
    return true;
}

//cfusa:req REQ-TRIG-010
bool rcp_triggered_threshold_reached(const rcp_triggered_step_t *step,
                                      const rcp_triggered_runtime_t *rt)
{
    return rt->occurrence_count > (uint32_t)step->trigger_threshold;
}

//cfusa:req REQ-TRIG-011
bool rcp_triggered_exec_delay_elapsed(const rcp_triggered_step_t *step, uint32_t elapsed)
{
    return elapsed >= (uint32_t)step->exec_delay;
}

//cfusa:req REQ-TRIG-012
//cfusa:req REQ-TRIG-013
bool rcp_triggered_tick(const rcp_triggered_step_t *step, rcp_triggered_runtime_t *rt,
                         uint32_t elapsed, bool endpoint_idle)
{
    if (!rt->started) return false;
    if (!rcp_triggered_threshold_reached(step, rt)) return false;
    if (!rcp_triggered_exec_delay_elapsed(step, elapsed)) return false;
    if (!endpoint_idle) return false;

    rt->occurrence_count = 0;
    rt->started           = false;
    return true;
}
