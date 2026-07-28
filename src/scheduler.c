#include "rcp/scheduler.h"

/* ── Request kind classification ─────────────────────────────────────────── */

//cfusa:req REQ-SCHED-001
rcp_sched_kind_t rcp_sched_classify(bool is_repurposed, uint8_t request_type)
{
    if (!is_repurposed) return RCP_SCHED_KIND_STANDARD;

    if (request_type == RCP_REQUEST_TYPE_CLEAR_ALL ||
        request_type == RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE ||
        request_type == RCP_REQUEST_TYPE_CLEAR_SINGLE) {
        return RCP_SCHED_KIND_CANCELLATION;
    }
    if (rcp_request_type_is_triggered(request_type)) return RCP_SCHED_KIND_TRIGGERED;
    if (request_type == RCP_REQUEST_TYPE_TIMED) return RCP_SCHED_KIND_TIMED;
    if (rcp_request_type_is_compound(request_type)) return RCP_SCHED_KIND_COMPOUND;
    if (rcp_request_type_is_compound_wait(request_type)) return RCP_SCHED_KIND_COMPOUND_WAIT;
    if (request_type == RCP_REQUEST_TYPE_CHAINED) return RCP_SCHED_KIND_CHAINED;

    /* An opcode byte this milestone (and milestone 68) does not
     * recognize is treated as Standard -- the lowest priority, never a
     * fabricated higher one -- matching this project's fail-safe
     * convention of never over-privileging unrecognized input. */
    return RCP_SCHED_KIND_STANDARD;
}

//cfusa:req REQ-SCHED-002
uint8_t rcp_sched_kind_rank(rcp_sched_kind_t kind)
{
    switch (kind) {
    case RCP_SCHED_KIND_CANCELLATION:  return 6;
    case RCP_SCHED_KIND_TRIGGERED:     return 5;
    case RCP_SCHED_KIND_TIMED:         return 4;
    case RCP_SCHED_KIND_COMPOUND:      return 3;
    case RCP_SCHED_KIND_COMPOUND_WAIT: return 2;
    case RCP_SCHED_KIND_CHAINED:       return 1;
    case RCP_SCHED_KIND_STANDARD:      return 0;
    default:                           return 0;
    }
}

/* ── Total ordering: rank, then FIFO ─────────────────────────────────────── */

//cfusa:req REQ-SCHED-003
int rcp_sched_compare(const rcp_sched_entry_t *a, const rcp_sched_entry_t *b)
{
    uint8_t ra = rcp_sched_kind_rank(a->kind);
    uint8_t rb = rcp_sched_kind_rank(b->kind);

    if (ra != rb) return (ra > rb) ? -1 : 1; /* higher rank sorts first */

    if (a->sequence != b->sequence) return (a->sequence < b->sequence) ? -1 : 1;
    return 0;
}

/* ── Multi-request-per-frame handling ─────────────────────────────────────── */

//cfusa:req REQ-SCHED-004
//cfusa:req REQ-SCHED-005
//cfusa:req REQ-SCHED-006
size_t rcp_sched_split_frame_members(const uint8_t *b, size_t len, size_t *out_offsets,
                                      size_t out_cap)
{
    size_t offset = 0;
    size_t found  = 0;

    while (offset < len) {
        size_t header_len;
        size_t msg_len;
        uint16_t declared_payload;

        if (b[offset] == RCP_ACF_MSG_TYPE_ABB) {
            header_len = RCP_ACF_ABB_HEADER_LEN;
        } else if (b[offset] == RCP_ACF_MSG_TYPE_GBB) {
            header_len = RCP_ACF_GBB_HEADER_LEN;
        } else {
            return 0; /* malformed: not a recognized acf_msg_type */
        }

        if (len - offset < header_len) return 0; /* malformed: header truncated */

        declared_payload = (uint16_t)(((uint16_t)b[offset + 1] << 8) | (uint16_t)b[offset + 2]);
        msg_len          = header_len + (size_t)declared_payload;

        if (len - offset < msg_len) return 0; /* malformed: payload truncated */

        if (found < out_cap) out_offsets[found] = offset;
        found++;
        offset += msg_len;
    }

    return found;
}

//cfusa:req REQ-SCHED-007
//cfusa:req REQ-SCHED-008
bool rcp_sched_frame_timing_consistent(bool is_tscf, const bool *member_is_timed, size_t count)
{
    size_t i;

    if (!is_tscf) return true;
    if (count == 0) return true;

    for (i = 1; i < count; i++) {
        if (member_is_timed[i] != member_is_timed[0]) return false;
    }
    return true;
}
