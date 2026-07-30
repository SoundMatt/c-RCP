/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/request_triggered.h"

#include <stdlib.h>
#include <string.h>

/* ── Byte-order helpers (this TU's own copy -- see request_compound.c's identical
 * comment for the house convention this follows) ─────────────────────────── */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

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

static uint64_t pack_ts(uint8_t request_type, const rcp_triggered_step_t *step)
{
    return (((uint64_t)request_type) << 56) |
           (((uint64_t)step->sequencer_index) << 48) |
           (((uint64_t)step->start_state) << 40) |
           (((uint64_t)step->next_state) << 32) |
           ((((uint64_t)step->trigger_exec_delay_ms) & 0xFFFFu) << 16) |
           ((uint64_t)step->repeat_count);
}

static void unpack_ts(uint64_t ts, rcp_triggered_step_t *out_step)
{
    out_step->sequencer_index      = (uint8_t)((ts >> 48) & 0xFFu);
    out_step->start_state          = (uint8_t)((ts >> 40) & 0xFFu);
    out_step->next_state           = (uint8_t)((ts >> 32) & 0xFFu);
    out_step->trigger_exec_delay_ms = (uint16_t)((ts >> 16) & 0xFFFFu);
    out_step->repeat_count         = (uint16_t)(ts & 0xFFFFu);
}

/* ── Triggered request encode/decode ──────────────────────────────────────── */

//cfusa:req REQ-TRIG-003
//cfusa:req REQ-TRIG-004
rcp_bytes_t rcp_triggered_encode_request(uint8_t request_type, rcp_byte_bus_id_t byte_bus_id,
                                          const rcp_triggered_step_t *step, uint8_t transaction_num,
                                          const uint8_t *payload, size_t payload_len)
{
    rcp_bytes_t frame = {0};
    size_t n;
    uint8_t *b;
    uint64_t ts;

    if (!rcp_request_type_is_triggered(request_type)) return frame;
    if (payload_len > RCP_ACF_MAX_PAYLOAD) return frame;

    n = RCP_ACF_GBB_HEADER_LEN + payload_len;
    b = (uint8_t *)malloc(n);
    if (!b) return frame;

    b[0] = RCP_ACF_MSG_TYPE_GBB;
    put_u16(&b[1], (uint16_t)payload_len);
    b[3] = byte_bus_id;
    /* mtv=RCP_ACF_MTV_UNTIMED(0) -- see the file header. */
    b[4] = 0x00u;
    b[5] = 0x00u;
    b[6] = transaction_num;
    b[7] = 0x00u;

    ts = pack_ts(request_type, step);
    put_u64(&b[RCP_ACF_ABB_HEADER_LEN], ts);

    if (payload_len > 0) memcpy(&b[RCP_ACF_GBB_HEADER_LEN], payload, payload_len);

    frame.data = b;
    frame.len  = n;
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
bool rcp_triggered_runtime_record_occurrence(rcp_triggered_runtime_t *rt)
{
    if (!rt->started) return false;
    rt->occurrence_count++;
    return true;
}

//cfusa:req REQ-TRIG-010
bool rcp_triggered_advance_guard(const rcp_sequencer_table_t *table,
                                  const rcp_triggered_step_t *step)
{
    uint8_t current;

    if (!rcp_sequencer_get_state(table, step->sequencer_index, &current)) return false;
    return current == step->start_state;
}

//cfusa:req REQ-TRIG-011
bool rcp_triggered_exec_delay_elapsed(const rcp_triggered_step_t *step, uint32_t elapsed_ms)
{
    return elapsed_ms >= (uint32_t)step->trigger_exec_delay_ms;
}

//cfusa:req REQ-TRIG-012
//cfusa:req REQ-TRIG-013
bool rcp_triggered_tick(rcp_sequencer_table_t *table, const rcp_triggered_step_t *step,
                         rcp_triggered_runtime_t *rt, uint32_t elapsed_ms, bool endpoint_idle)
{
    if (!rt->started) return false;
    if (rt->occurrence_count == 0u) return false;
    if (!rcp_triggered_exec_delay_elapsed(step, elapsed_ms)) return false;
    if (!endpoint_idle) return false;
    if (!rcp_triggered_advance_guard(table, step)) return false;

    if (!rcp_sequencer_set_state(table, step->sequencer_index, step->next_state)) return false;

    rt->occurrence_count = 0;
    rt->started           = false;
    return true;
}
