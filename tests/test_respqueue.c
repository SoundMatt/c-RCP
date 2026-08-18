/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-RMAP-059
//cfusa:test REQ-RMAP-061
//cfusa:test REQ-RMAP-064
//cfusa:test REQ-RMAP-065
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/rcp.h>
#include <rcp/respqueue.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Basic FIFO push/pop ─────────────────────────────────────────────────── */

static void test_push_pop_is_fifo(void)
{
    rcp_respqueue_t q;
    const uint8_t   a[] = {1, 2, 3};
    const uint8_t   b[] = {4, 5};
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, a, sizeof(a)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, b, sizeof(b)));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(5u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_UINT(3u, out.len);
    TEST_ASSERT_EQUAL_MEMORY(a, out.data, sizeof(a));
    rcp_bytes_free(&out);
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_UINT(2u, out.len);
    TEST_ASSERT_EQUAL_MEMORY(b, out.data, sizeof(b));
    rcp_bytes_free(&out);

    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));

    rcp_respqueue_destroy(&q);
}

static void test_pop_on_empty_queue_returns_false(void)
{
    rcp_respqueue_t q;
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 0, 0);
    out.data = (uint8_t *)0x1; /* sentinel: must stay untouched */
    out.len  = 42;

    TEST_ASSERT_FALSE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_PTR((uint8_t *)0x1, out.data);
    TEST_ASSERT_EQUAL_UINT(42u, out.len);

    rcp_respqueue_destroy(&q);
}

/* ── REQ-RMAP-059: capacity is an octet budget, not an entry-count limit ──── */

static void test_zero_capacity_is_unbounded(void)
{
    rcp_respqueue_t q;
    uint8_t         big[4096] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, big, sizeof(big)));
    TEST_ASSERT_EQUAL_UINT(sizeof(big), rcp_respqueue_octets(&q));

    rcp_respqueue_destroy(&q);
}

static void test_push_refused_outright_when_frame_exceeds_total_capacity_octets(void)
{
    rcp_respqueue_t q;
    const uint8_t   five[5]    = {1, 2, 3, 4, 5};
    const uint8_t   eleven[11] = {0};

    /* TC18 §12.7.9 Table 24: queue_size is a memory reservation ("assigned
     * memory in 32bit words"), not a message-count limit -- capacity is
     * checked in octets. GitHub #446: a frame that exceeds the ENTIRE
     * configured capacity outright can never be admitted no matter how
     * much gets evicted (evicting everything only frees capacity_octets
     * octets total) -- this is the one case that still refuses outright,
     * queue entirely unchanged, no eviction attempted. Once eviction
     * COULD make room (the ordinary case), the queue evicts instead --
     * see the GitHub #446 test group below. */
    rcp_respqueue_init(&q, 10u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, five, sizeof(five)));
    TEST_ASSERT_EQUAL_UINT(5u, rcp_respqueue_octets(&q));

    /* 11 > capacity 10 outright: refused regardless of current occupancy. */
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, eleven, sizeof(eleven)));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(5u, rcp_respqueue_octets(&q));
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    /* Exactly at the remaining budget (5 more octets, total 10): accepted,
     * no eviction needed. */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, five, sizeof(five)));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(10u, rcp_respqueue_octets(&q));
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    rcp_respqueue_destroy(&q);
}

static void test_pop_frees_capacity_for_a_later_push(void)
{
    rcp_respqueue_t q;
    const uint8_t   frame[8]     = {0};
    const uint8_t   oversized[9] = {0};
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 8u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    /* 9 > capacity 8 outright: still refused unconditionally regardless of
     * GitHub #446 -- a frame bigger than the ENTIRE configured budget can
     * never fit no matter how much eviction happens (see the dedicated
     * eviction test group below for the case where eviction WOULD help). */
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, oversized, sizeof(oversized)));

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    rcp_bytes_free(&out);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
}

/* ── Destroy frees every remaining entry ───────────────────────────────────── */

static void test_destroy_on_nonempty_queue_is_safe(void)
{
    rcp_respqueue_t q;
    const uint8_t   frame[3] = {1, 2, 3};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_octets(&q));
}

/* ── REQ-RMAP-061: a per-message Max_AVTPDUsize ceiling, independent
 * of the aggregate queue_size capacity ──────────────────────────────────── */

static void test_zero_max_avtpdu_size_is_unbounded(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[64] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
}

static void test_push_refused_once_a_single_frame_exceeds_max_avtpdu_size(void)
{
    rcp_respqueue_t q;
    uint8_t         ok[10]  = {0};
    uint8_t         over[11] = {0};

    /* max_avtpdu_size_octets is checked independently of capacity_octets:
     * capacity here (1000) is far larger than either frame, so only the
     * per-message ceiling (10) can be refusing these pushes. */
    rcp_respqueue_init(&q, 1000u, 10u);

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, ok, sizeof(ok)));
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, over, sizeof(over)));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(10u, rcp_respqueue_octets(&q));

    rcp_respqueue_destroy(&q);
}

/* ── TC18 §12.9.4/§12.9.5 (GitHub #423): slot-count eviction + overflow bit ─ */

static void test_push_evicts_lowest_sequence_num_not_oldest_inserted(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[1];
    size_t          i;
    rcp_bytes_t     out;
    bool            saw_evicted_payload = false;
    bool            saw_new_payload     = false;

    /* Unbounded byte budget/message ceiling: isolates the NEW slot-count
     * eviction rule from REQ-RMAP-059/061's own separate, UNCHANGED
     * byte-budget rules (covered by their own tests above). */
    rcp_respqueue_init(&q, 0, 0);

    /* Fill all RCP_RESPQUEUE_MAX_ENTRIES slots. The first
     * MAX_ENTRIES-1 pushes get INCREASING sequence numbers (1..63); the
     * LAST one pushed (the FIFO-newest entry) is deliberately given
     * sequence_num 0 -- lower than every entry already queued. This
     * makes "lowest sequence_num" and "FIFO-oldest" name two DIFFERENT
     * entries, so an implementation that merely evicts index 0
     * (oldest-inserted) instead of genuinely comparing sequence_num
     * values is caught. */
    for (i = 0; i + 1 < RCP_RESPQUEUE_MAX_ENTRIES; i++) {
        frame[0] = (uint8_t)i;
        TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, frame, 1, (uint8_t)(i + 1)));
    }
    frame[0] = 0xAAu; /* the FIFO-newest entry's own payload marker */
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, frame, 1, 0u));
    TEST_ASSERT_EQUAL_UINT(RCP_RESPQUEUE_MAX_ENTRIES, rcp_respqueue_len(&q));
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    /* Queue is now completely full by slot count (not byte budget).
     * TC18 §12.9.4/§12.9.5 requires evicting the LOWEST-sequence_num
     * entry to make room -- that is the entry just pushed above (seq 0),
     * NOT index 0 (seq 1, the true FIFO-oldest entry). */
    frame[0] = 0xBBu;
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, frame, 1, 200u));
    TEST_ASSERT_EQUAL_UINT(RCP_RESPQUEUE_MAX_ENTRIES, rcp_respqueue_len(&q));
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    /* The true FIFO-oldest entry (payload 0, sequence_num 1) must still
     * be present and still at the front -- only the 0xAA entry (the
     * genuine lowest sequence_num) was evicted. */
    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_UINT(1u, out.len);
    TEST_ASSERT_EQUAL_UINT8(0u, out.data[0]);
    rcp_bytes_free(&out);

    /* Drain the rest: the 0xAA payload (evicted) must never reappear;
     * the 0xBB payload (the push that triggered the eviction) must. */
    while (rcp_respqueue_pop(&q, &out)) {
        if (out.len == 1 && out.data[0] == 0xAAu) saw_evicted_payload = true;
        if (out.len == 1 && out.data[0] == 0xBBu) saw_new_payload = true;
        rcp_bytes_free(&out);
    }
    TEST_ASSERT_FALSE(saw_evicted_payload);
    TEST_ASSERT_TRUE(saw_new_payload);

    rcp_respqueue_destroy(&q);
}

static void test_overflow_flag_latches_until_cleared(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[1] = {0};
    size_t          i;
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 0, 0);

    for (i = 0; i < RCP_RESPQUEUE_MAX_ENTRIES; i++) {
        TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, 1));
    }
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q)); /* not full-and-pushed-past yet */

    /* One more push while completely full: triggers eviction, latches
     * the overflow bit. */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, 1));
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    rcp_respqueue_clear_overflow(&q);
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    /* Pop one entry so the queue is no longer completely full, then
     * push again: no eviction this time, so overflow must stay clear
     * (it is a latch a caller clears explicitly, not one that
     * auto-clears on every push -- but a push that never evicts must
     * never itself set it either). */
    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    rcp_bytes_free(&out);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, 1));
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    rcp_respqueue_destroy(&q);
}

static void test_capacity_octets_nonzero_still_enforces_max_entries_slot_bound(void)
{
    /* Issue #521 (ASIL-D-oriented no-dynamic-allocation push):
     * entries[]/entries_seq[] are now fixed-capacity arrays embedded in
     * rcp_respqueue_t, sized to RCP_RESPQUEUE_MAX_ENTRIES, so that bound
     * is enforced UNCONDITIONALLY -- not only as a capacity_octets == 0
     * fallback (GitHub #446's original behavior, superseded here). A
     * capacity_octets budget generous enough in bytes but fed tiny
     * per-entry frames therefore still tops out at exactly
     * RCP_RESPQUEUE_MAX_ENTRIES entries: the (n+1)th push evicts the
     * lowest-sequence_num entry (sequence_num 0, pushed first) to make
     * room, latching overflow, even though plenty of byte budget
     * remains unused. */
    rcp_respqueue_t q;
    uint8_t         frame[1] = {0};
    size_t          i;
    size_t          n = RCP_RESPQUEUE_MAX_ENTRIES + 10u;

    rcp_respqueue_init(&q, n, 0); /* n octets capacity, 1 octet per entry */
    for (i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, 1));
    }
    TEST_ASSERT_EQUAL_UINT(RCP_RESPQUEUE_MAX_ENTRIES, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(RCP_RESPQUEUE_MAX_ENTRIES, rcp_respqueue_octets(&q));
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    rcp_respqueue_destroy(&q);
}

static void test_max_avtpdu_size_rejection_unaffected_by_slot_count_eviction(void)
{
    /* Same guarantee as above, for REQ-RMAP-061's own per-message
     * Max_AVTPDUsize ceiling. */
    rcp_respqueue_t q;
    uint8_t         ok[10]   = {0};
    uint8_t         over[11] = {0};

    rcp_respqueue_init(&q, 1000u, 10u);
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, ok, sizeof(ok), 1u));
    TEST_ASSERT_FALSE(rcp_respqueue_push_seq(&q, over, sizeof(over), 2u));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(10u, rcp_respqueue_octets(&q));
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    rcp_respqueue_destroy(&q);
}

/* ── GitHub #446: capacity_octets (the real TC18 queue_size bound) exhaustion
 * triggers eviction, not reject-and-unchanged ──────────────────────────── */

static void test_push_evicts_single_entry_when_capacity_octets_would_be_exceeded(void)
{
    /* A realistically-configured queue_size (well under
     * RCP_RESPQUEUE_MAX_ENTRIES entries) is the binding constraint here --
     * exactly the "in any realistically-configured server" scenario
     * GitHub #446 is about. One eviction is enough to free the room the
     * incoming frame needs. */
    rcp_respqueue_t q;
    const uint8_t   five[5] = {1, 2, 3, 4, 5};
    const uint8_t   six[6]  = {1, 2, 3, 4, 5, 6};

    rcp_respqueue_init(&q, 10u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, five, sizeof(five), 7u));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(5u, rcp_respqueue_octets(&q));
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    /* 5 (queued) + 6 (incoming) = 11 > capacity 10: the old behavior
     * rejected this outright. GitHub #446: eviction should fire instead
     * -- the only queued entry (seq 7) is evicted, freeing 5 octets, and
     * the 6-octet frame is then admitted. */
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, six, sizeof(six), 8u));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(6u, rcp_respqueue_octets(&q));
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    rcp_respqueue_destroy(&q);
}

static void test_push_evicts_multiple_entries_when_one_is_not_enough_bytes(void)
{
    /* Byte-size varies per evicted entry -- a single eviction is not
     * always enough to free room for a byte-larger incoming frame.
     * capacity_octets = 10; three small entries (2+3+2 = 7 octets)
     * queued; an incoming 8-octet frame needs evicting TWO of them
     * (the two lowest sequence_num entries) before it fits, not just
     * one. */
    rcp_respqueue_t q;
    const uint8_t   a[2] = {0xA1, 0xA2};
    const uint8_t   b[3] = {0xB1, 0xB2, 0xB3};
    const uint8_t   c[2] = {0xC1, 0xC2};
    const uint8_t   d[8] = {0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8};
    rcp_bytes_t     out;
    bool            saw_a = false, saw_b = false, saw_c = false, saw_d = false;

    rcp_respqueue_init(&q, 10u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, a, sizeof(a), 1u)); /* octets: 2 */
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, b, sizeof(b), 2u)); /* octets: 5 */
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, c, sizeof(c), 3u)); /* octets: 7 */
    TEST_ASSERT_EQUAL_UINT(3u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(7u, rcp_respqueue_octets(&q));
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    /* Incoming d needs 8 octets; only 10 - 7 = 3 free. Evicting just `a`
     * (seq 1, lowest, 2 octets) only frees 2 more (5 free) -- still not
     * enough for 8. `b` (seq 2, now the lowest remaining) must also be
     * evicted (3 more octets, 8 free total) before d fits. `c` (seq 3)
     * must survive -- it was never the lowest remaining sequence_num. */
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, d, sizeof(d), 4u));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_len(&q)); /* c and d remain */
    TEST_ASSERT_EQUAL_UINT(10u, rcp_respqueue_octets(&q)); /* 2 + 8: exact budget, no leak/stale total */
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    while (rcp_respqueue_pop(&q, &out)) {
        if (out.len == sizeof(a) && memcmp(out.data, a, sizeof(a)) == 0) saw_a = true;
        if (out.len == sizeof(b) && memcmp(out.data, b, sizeof(b)) == 0) saw_b = true;
        if (out.len == sizeof(c) && memcmp(out.data, c, sizeof(c)) == 0) saw_c = true;
        if (out.len == sizeof(d) && memcmp(out.data, d, sizeof(d)) == 0) saw_d = true;
        rcp_bytes_free(&out);
    }
    TEST_ASSERT_FALSE(saw_a); /* evicted */
    TEST_ASSERT_FALSE(saw_b); /* evicted */
    TEST_ASSERT_TRUE(saw_c);  /* survived: never the lowest remaining seq */
    TEST_ASSERT_TRUE(saw_d);  /* the push that triggered the eviction */

    rcp_respqueue_destroy(&q);
}

static void test_capacity_octets_eviction_prefers_lowest_sequence_num_not_fifo_oldest(void)
{
    /* Same "lowest sequence_num, not FIFO-oldest" guarantee the #423
     * slot-count eviction test above proves, but for the capacity_octets
     * path: the FIFO-oldest entry is given the HIGHEST sequence_num, so
     * an implementation that evicts index 0 instead of genuinely
     * comparing q->entries_seq[] is caught. */
    rcp_respqueue_t q;
    const uint8_t   oldest_but_highest_seq[4] = {1, 1, 1, 1};
    const uint8_t   newest_but_lowest_seq[4]  = {2, 2, 2, 2};
    const uint8_t   incoming[4]               = {3, 3, 3, 3};
    rcp_bytes_t     out;

    rcp_respqueue_init(&q, 8u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, oldest_but_highest_seq, 4, 200u));
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, newest_but_lowest_seq, 4, 5u));
    TEST_ASSERT_EQUAL_UINT(8u, rcp_respqueue_octets(&q));

    /* capacity 8, already full: incoming 4-octet frame needs an eviction.
     * Lowest sequence_num is 5 (the FIFO-newest entry) -- it must be the
     * one evicted, not index 0 (the FIFO-oldest, seq 200). */
    TEST_ASSERT_TRUE(rcp_respqueue_push_seq(&q, incoming, 4, 201u));
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_len(&q));
    TEST_ASSERT_EQUAL_UINT(8u, rcp_respqueue_octets(&q));
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_MEMORY(oldest_but_highest_seq, out.data, 4); /* survived */
    rcp_bytes_free(&out);

    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    TEST_ASSERT_EQUAL_MEMORY(incoming, out.data, 4);
    rcp_bytes_free(&out);

    rcp_respqueue_destroy(&q);
}

static void test_capacity_octets_zero_falls_back_to_max_entries_bound(void)
{
    /* GitHub #446's own design recommendation: with capacity_octets == 0
     * (unbounded), there is no byte budget to evict against, so eviction
     * falls back to the RCP_RESPQUEUE_MAX_ENTRIES slot-count bound --
     * exactly the #423 behavior, still exercised end-to-end here for the
     * unbounded case specifically (the general slot-count mechanics are
     * already covered in detail by
     * test_push_evicts_lowest_sequence_num_not_oldest_inserted() above). */
    rcp_respqueue_t q;
    uint8_t         frame[1] = {0};
    size_t          i;

    rcp_respqueue_init(&q, 0, 0);
    for (i = 0; i < RCP_RESPQUEUE_MAX_ENTRIES; i++) {
        TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, 1));
    }
    TEST_ASSERT_FALSE(rcp_respqueue_overflow(&q));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, 1));
    TEST_ASSERT_EQUAL_UINT(RCP_RESPQUEUE_MAX_ENTRIES, rcp_respqueue_len(&q));
    TEST_ASSERT_TRUE(rcp_respqueue_overflow(&q));

    rcp_respqueue_destroy(&q);
}

/* ── REQ-RMAP-062: the fragmentation-budget helper ─────────────────────── */

static void test_max_fragment_payload_reserves_header_and_worst_case_pad(void)
{
    /* max_avtpdu_size_octets=32, ABB header=8: 32 - 8 - 3(pad) = 21. */
    TEST_ASSERT_EQUAL_UINT((size_t)21u,
                           rcp_respqueue_max_fragment_payload(32u, RCP_ACF_ABB_HEADER_LEN));

    /* Same total, but the larger GBB header (16) leaves less budget:
     * 32 - 16 - 3 = 13. */
    TEST_ASSERT_EQUAL_UINT((size_t)13u,
                           rcp_respqueue_max_fragment_payload(32u, RCP_ACF_GBB_HEADER_LEN));
}

static void test_max_fragment_payload_is_zero_when_unbounded_or_no_budget_remains(void)
{
    /* max_avtpdu_size_octets == 0: unbounded, matches
     * RCP_FRAGMENT_ERR_DISABLED's own "fragmentation disabled" reading. */
    TEST_ASSERT_EQUAL_UINT((size_t)0u,
                           rcp_respqueue_max_fragment_payload(0u, RCP_ACF_ABB_HEADER_LEN));

    /* header_len + worst-case pad (3) already meets max_avtpdu_size_octets:
     * no payload budget remains at all. */
    TEST_ASSERT_EQUAL_UINT((size_t)0u,
                           rcp_respqueue_max_fragment_payload(11u, RCP_ACF_ABB_HEADER_LEN));
    TEST_ASSERT_EQUAL_UINT((size_t)0u,
                           rcp_respqueue_max_fragment_payload(5u, RCP_ACF_ABB_HEADER_LEN));
}

/* ── REQ-RMAP-063: the flush_on_count trigger and AVTPDU packing plan ─────── */

static void test_should_flush_triggers_once_octets_reach_the_threshold(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[5] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush(&q, 8u)); /* empty */

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush(&q, 8u)); /* 5 < 8 */

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush(&q, 8u)); /* 10 >= 8 */

    rcp_respqueue_destroy(&q);
}

static void test_should_flush_zero_threshold_means_any_nonempty_queue(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[1] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush(&q, 0u));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush(&q, 0u));

    rcp_respqueue_destroy(&q);
}

static void test_plan_batch_is_zero_for_an_empty_queue(void)
{
    rcp_respqueue_t q;

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_plan_batch(&q, 100u));

    rcp_respqueue_destroy(&q);
}

static void test_plan_batch_always_keeps_at_least_one_entry(void)
{
    rcp_respqueue_t q;
    uint8_t         big[50] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, big, sizeof(big)));

    /* A budget smaller than the one queued entry still plans that one
     * entry -- rcp_respqueue_push()'s own REQ-RMAP-061 enforcement
     * already guarantees every queued entry individually fits within
     * whatever max_avtpdu_size_octets this queue was configured with,
     * so this function never needs to represent "0 entries fit". */
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_plan_batch(&q, 10u));

    rcp_respqueue_destroy(&q);
}

static void test_plan_batch_packs_as_many_entries_as_fit(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[5] = {0};

    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    /* Budget 12: entries 1+2 (10 octets) fit, entry 3 would push to 15. */
    TEST_ASSERT_EQUAL_UINT(2u, rcp_respqueue_plan_batch(&q, 12u));

    /* Budget 0 (unbounded): every remaining entry packs into one AVTPDU. */
    TEST_ASSERT_EQUAL_UINT(3u, rcp_respqueue_plan_batch(&q, 0u));

    rcp_respqueue_destroy(&q);
}

/* ── REQ-RMAP-064/065: the Flush_time trigger, independent of queue state ─── */

static void test_should_flush_by_time_zero_flush_time_means_count_only(void)
{
    /* flush_time_us == 0: "flush only by count" -- the time trigger never
     * fires, no matter how much elapsed time is reported. */
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush_by_time(0u, 0u));
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush_by_time(1000000u, 0u));
}

static void test_should_flush_by_time_fires_at_or_past_the_configured_interval(void)
{
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush_by_time(999u, 1000u));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(1000u, 1000u));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(1001u, 1000u));
}

static void test_should_flush_by_time_is_independent_of_queue_state(void)
{
    rcp_respqueue_t q;
    uint8_t         frame[3] = {0};

    /* REQ-RMAP-065: the Flush_time trigger must fire the same way whether
     * the queue is empty or not -- an empty queue still needs to emit a
     * heartbeat AVTPDU on expiry, so its own emptiness must never
     * suppress this trigger (unlike rcp_respqueue_should_flush(), the
     * flush_on_count trigger, which is false for an empty queue). */
    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(2000u, 1000u));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_plan_batch(&q, 100u));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(2000u, 1000u));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_respqueue_plan_batch(&q, 100u));

    rcp_respqueue_destroy(&q);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_push_pop_is_fifo);
    RUN_TEST(test_pop_on_empty_queue_returns_false);
    RUN_TEST(test_zero_capacity_is_unbounded);
    RUN_TEST(test_push_refused_outright_when_frame_exceeds_total_capacity_octets);
    RUN_TEST(test_pop_frees_capacity_for_a_later_push);
    RUN_TEST(test_destroy_on_nonempty_queue_is_safe);

    RUN_TEST(test_zero_max_avtpdu_size_is_unbounded);
    RUN_TEST(test_push_refused_once_a_single_frame_exceeds_max_avtpdu_size);

    RUN_TEST(test_push_evicts_lowest_sequence_num_not_oldest_inserted);
    RUN_TEST(test_overflow_flag_latches_until_cleared);
    RUN_TEST(test_capacity_octets_nonzero_still_enforces_max_entries_slot_bound);
    RUN_TEST(test_max_avtpdu_size_rejection_unaffected_by_slot_count_eviction);

    RUN_TEST(test_push_evicts_single_entry_when_capacity_octets_would_be_exceeded);
    RUN_TEST(test_push_evicts_multiple_entries_when_one_is_not_enough_bytes);
    RUN_TEST(test_capacity_octets_eviction_prefers_lowest_sequence_num_not_fifo_oldest);
    RUN_TEST(test_capacity_octets_zero_falls_back_to_max_entries_bound);

    RUN_TEST(test_max_fragment_payload_reserves_header_and_worst_case_pad);
    RUN_TEST(test_max_fragment_payload_is_zero_when_unbounded_or_no_budget_remains);

    RUN_TEST(test_should_flush_triggers_once_octets_reach_the_threshold);
    RUN_TEST(test_should_flush_zero_threshold_means_any_nonempty_queue);
    RUN_TEST(test_plan_batch_is_zero_for_an_empty_queue);
    RUN_TEST(test_plan_batch_always_keeps_at_least_one_entry);
    RUN_TEST(test_plan_batch_packs_as_many_entries_as_fit);

    RUN_TEST(test_should_flush_by_time_zero_flush_time_means_count_only);
    RUN_TEST(test_should_flush_by_time_fires_at_or_past_the_configured_interval);
    RUN_TEST(test_should_flush_by_time_is_independent_of_queue_state);

    return UNITY_END();
}
