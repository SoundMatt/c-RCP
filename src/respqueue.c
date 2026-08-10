/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/respqueue.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
void rcp_respqueue_init(rcp_respqueue_t *q, size_t capacity_octets,
                         size_t max_avtpdu_size_octets)
{
    memset(q, 0, sizeof(*q));
    q->capacity_octets        = capacity_octets;
    q->max_avtpdu_size_octets = max_avtpdu_size_octets;
}

//cfusa:req REQ-RMAP-059
void rcp_respqueue_destroy(rcp_respqueue_t *q)
{
    size_t i;

    for (i = 0; i < q->entries_len; i++) {
        rcp_bytes_free(&q->entries[i]);
    }
    free(q->entries);
    q->entries     = NULL;
    q->entries_len = 0;
    q->entries_cap = 0;
    q->octets      = 0;
}

//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
bool rcp_respqueue_push(rcp_respqueue_t *q, const uint8_t *frame, size_t frame_len)
{
    rcp_bytes_t *grown;

    if (q->max_avtpdu_size_octets != 0 && frame_len > q->max_avtpdu_size_octets) return false;
    if (q->capacity_octets != 0 && frame_len > q->capacity_octets - q->octets) return false;

    if (q->entries_len == q->entries_cap) {
        size_t new_cap = (q->entries_cap == 0) ? 4 : q->entries_cap * 2;

        grown = (rcp_bytes_t *)realloc(q->entries, new_cap * sizeof(*grown));
        if (!grown) return false;
        q->entries     = grown;
        q->entries_cap = new_cap;
    }

    q->entries[q->entries_len] = rcp_bytes_dup(frame, frame_len);
    q->entries_len++;
    q->octets += frame_len;
    return true;
}

//cfusa:req REQ-RMAP-059
bool rcp_respqueue_pop(rcp_respqueue_t *q, rcp_bytes_t *out_frame)
{
    size_t i;

    if (q->entries_len == 0) return false;

    *out_frame = q->entries[0];
    q->octets -= out_frame->len;
    for (i = 1; i < q->entries_len; i++) {
        q->entries[i - 1] = q->entries[i];
    }
    q->entries_len--;
    return true;
}

//cfusa:req REQ-RMAP-059
size_t rcp_respqueue_len(const rcp_respqueue_t *q)
{
    return q->entries_len;
}

//cfusa:req REQ-RMAP-059
size_t rcp_respqueue_octets(const rcp_respqueue_t *q)
{
    return q->octets;
}

//cfusa:req REQ-RMAP-062
size_t rcp_respqueue_max_fragment_payload(size_t max_avtpdu_size_octets, size_t header_len)
{
    size_t reserved;

    if (max_avtpdu_size_octets == 0) return 0;

    reserved = header_len + 3u; /* fixed header + worst-case trailing pad */
    if (reserved >= max_avtpdu_size_octets) return 0;

    return max_avtpdu_size_octets - reserved;
}

//cfusa:req REQ-RMAP-063
bool rcp_respqueue_should_flush(const rcp_respqueue_t *q, size_t flush_on_count_octets)
{
    if (q->entries_len == 0) return false;
    if (flush_on_count_octets == 0) return true;

    return q->octets >= flush_on_count_octets;
}

//cfusa:req REQ-RMAP-063
size_t rcp_respqueue_plan_batch(const rcp_respqueue_t *q, size_t max_avtpdu_size_octets)
{
    size_t i;
    size_t total = 0;

    if (q->entries_len == 0) return 0;
    if (max_avtpdu_size_octets == 0) return q->entries_len;

    for (i = 0; i < q->entries_len; i++) {
        size_t next_total = total + q->entries[i].len;

        if (i > 0 && next_total > max_avtpdu_size_octets) break;
        total = next_total;
    }

    return i;
}

//cfusa:req REQ-RMAP-064
//cfusa:req REQ-RMAP-065
bool rcp_respqueue_should_flush_by_time(uint64_t elapsed_since_last_transmit_us,
                                         uint64_t flush_time_us)
{
    if (flush_time_us == 0) return false;

    return elapsed_since_last_transmit_us >= flush_time_us;
}
