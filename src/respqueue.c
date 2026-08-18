/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/respqueue.h"

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
    /* entries[]/entries_seq[] are fixed-capacity arrays embedded in *q
     * (issue #521) -- no array storage of their own to free. */
    q->entries_len       = 0;
    q->octets            = 0;
    q->next_sequence_num = 0;
    q->overflow          = false;
}

//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
bool rcp_respqueue_push(rcp_respqueue_t *q, const uint8_t *frame, size_t frame_len)
{
    bool ok = rcp_respqueue_push_seq(q, frame, frame_len, q->next_sequence_num);

    if (ok) {
        q->next_sequence_num++; /* wraps mod 256: q->next_sequence_num is
                                    uint8_t */
    }
    return ok;
}

//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
bool rcp_respqueue_push_seq(rcp_respqueue_t *q, const uint8_t *frame, size_t frame_len,
                             uint8_t sequence_num)
{
    rcp_bytes_t copy;

    /* REQ-RMAP-061: per-message Max_AVTPDUsize ceiling. UNCHANGED by
     * GitHub #423/#446 -- still checked first, still rejects (queue
     * untouched) on its own, independently of everything below. */
    if (q->max_avtpdu_size_octets != 0 && frame_len > q->max_avtpdu_size_octets) return false;
    /* REQ-RMAP-059: queue_size octet-budget ceiling -- TC18 §12.7.9's
     * own, and the only, "queue is completely full" concept this queue
     * has. GitHub #446: a frame_len that exceeds the ENTIRE configured
     * capacity_octets can never be admitted no matter how much gets
     * evicted below (evicting every last entry still only frees
     * capacity_octets octets total), so that case alone still rejects
     * outright, queue untouched. Anything else that merely doesn't fit
     * against the CURRENT octet total is handled by evicting below,
     * not by rejecting -- see that block's own comment. */
    if (q->capacity_octets != 0 && frame_len > q->capacity_octets) return false;

    copy = rcp_bytes_dup(frame, frame_len);
    if (frame_len != 0 && copy.data == NULL) return false; /* alloc failure: q untouched */

    /* TC18 §12.9.4/§12.9.5 (REQ-RMAP-059/061, GitHub #423, corrected by
     * GitHub #446, made universal by issue #521): evict lowest-
     * sequence_num entries, one at a time, for as long as EITHER (a)
     * capacity_octets is nonzero (TC18's own "queue is completely full"
     * condition, §12.7.9) and frame_len still doesn't fit the remaining
     * byte budget, OR (b) q->entries_len has reached
     * RCP_RESPQUEUE_MAX_ENTRIES, q's own fixed-capacity slot-count
     * ceiling -- enforced unconditionally now that entries[]/
     * entries_seq[] are fixed-size arrays embedded in *q rather than
     * realloc()-grown heap storage, so entries_len can never exceed it
     * regardless of capacity_octets. A single eviction frees only its
     * own evicted entry's own byte length, which may be smaller than
     * frame_len, so condition (a) alone is not always satisfied by one
     * eviction; this may evict several entries, or none at all if
     * frame_len already fits and there is a free slot. The `frame_len >
     * q->capacity_octets` case was already rejected above, so once
     * q->entries_len reaches 0 (octets == 0), frame_len <=
     * capacity_octets - 0 trivially holds and condition (a) cannot
     * still be true; RCP_RESPQUEUE_MAX_ENTRIES >= 1 so condition (b)
     * also always clears after enough evictions -- this loop always
     * terminates without running off the front of an empty queue. */
    while ((q->capacity_octets != 0 && frame_len > q->capacity_octets - q->octets) ||
           q->entries_len == RCP_RESPQUEUE_MAX_ENTRIES) {
        size_t lowest_idx = 0;
        size_t i;

        for (i = 1; i < q->entries_len; i++) {
            if (q->entries_seq[i] < q->entries_seq[lowest_idx]) {
                lowest_idx = i;
            }
        }

        q->octets -= q->entries[lowest_idx].len;
        rcp_bytes_free(&q->entries[lowest_idx]);

        /* Close the gap left by the evicted slot, preserving FIFO
         * order for the remaining entries -- mirrors
         * rcp_respqueue_pop()'s own shift-down convention below. */
        for (i = lowest_idx + 1; i < q->entries_len; i++) {
            q->entries[i - 1]     = q->entries[i];
            q->entries_seq[i - 1] = q->entries_seq[i];
        }
        q->entries_len--;
        q->overflow = true;
    }

    q->entries[q->entries_len]     = copy;
    q->entries_seq[q->entries_len] = sequence_num;
    q->entries_len++;
    q->octets += frame_len;
    return true;
}

//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
bool rcp_respqueue_overflow(const rcp_respqueue_t *q)
{
    return q->overflow;
}

//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-061
void rcp_respqueue_clear_overflow(rcp_respqueue_t *q)
{
    q->overflow = false;
}

//cfusa:req REQ-RMAP-061
bool rcp_respqueue_max_avtpdu_size_within_mtu(size_t max_avtpdu_size_octets,
                                               size_t mtu_budget_octets)
{
    if (max_avtpdu_size_octets == 0) return mtu_budget_octets == 0;
    return max_avtpdu_size_octets <= mtu_budget_octets;
}

//cfusa:req REQ-RMAP-059
bool rcp_respqueue_pop(rcp_respqueue_t *q, rcp_bytes_t *out_frame)
{
    size_t i;

    if (q->entries_len == 0) return false;

    *out_frame = q->entries[0];
    q->octets -= out_frame->len;
    for (i = 1; i < q->entries_len; i++) {
        q->entries[i - 1]     = q->entries[i];
        q->entries_seq[i - 1] = q->entries_seq[i];
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
