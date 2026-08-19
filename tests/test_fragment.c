/* SPDX-License-Identifier: MPL-2.0 */
#include "unity.h"

#include <rcp/alloc.h>
#include <rcp/e2e.h>
#include <rcp/fragment.h>

#include <string.h>

void setUp(void) { rcp_alloc_reset_hooks(); }
void tearDown(void) { rcp_alloc_reset_hooks(); } /* never leak a hook across tests */

/* ── strerror / result_string ─────────────────────────────────────────────── */

//cfusa:test REQ-FRAG-001
static void test_strerror_never_null_and_distinct(void)
{
    const char *a   = rcp_fragment_strerror(RCP_FRAGMENT_OK);
    const char *b   = rcp_fragment_strerror(RCP_FRAGMENT_ERR_DISABLED);
    const char *c   = rcp_fragment_strerror(RCP_FRAGMENT_ERR_TOO_MANY_SEGMENTS);
    const char *d   = rcp_fragment_strerror(RCP_FRAGMENT_ERR_BAD_SEGMENT_COUNT);
    const char *unk = rcp_fragment_strerror((rcp_fragment_errc_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
    TEST_ASSERT_TRUE(strcmp(c, d) != 0);
}

//cfusa:test REQ-FRAG-007
static void test_reasm_result_string_never_null_and_distinct(void)
{
    const char *a = rcp_fragment_reasm_result_string(RCP_FRAGMENT_REASM_CONTINUE);
    const char *b = rcp_fragment_reasm_result_string(RCP_FRAGMENT_REASM_COMPLETE);
    const char *c = rcp_fragment_reasm_result_string(RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER);
    const char *d = rcp_fragment_reasm_result_string(RCP_FRAGMENT_REASM_ERR_TOO_LARGE);
    const char *e = rcp_fragment_reasm_result_string(RCP_FRAGMENT_REASM_ERR_ALLOC);
    const char *unk = rcp_fragment_reasm_result_string((rcp_fragment_reasm_result_t)999);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_NOT_NULL(unk);

    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
    TEST_ASSERT_TRUE(strcmp(b, c) != 0);
    TEST_ASSERT_TRUE(strcmp(c, d) != 0);
    TEST_ASSERT_TRUE(strcmp(d, e) != 0);
}

/* ── plan_count ────────────────────────────────────────────────────────────── */

//cfusa:test REQ-FRAG-002
static void test_plan_count_empty_payload_always_one_segment(void)
{
    TEST_ASSERT_EQUAL_UINT(1, rcp_fragment_plan_count(0, 0));
    TEST_ASSERT_EQUAL_UINT(1, rcp_fragment_plan_count(0, 10));
}

//cfusa:test REQ-FRAG-002
static void test_plan_count_fits_in_one_fragment(void)
{
    TEST_ASSERT_EQUAL_UINT(1, rcp_fragment_plan_count(10, 10));
    TEST_ASSERT_EQUAL_UINT(1, rcp_fragment_plan_count(5, 10));
}

//cfusa:test REQ-FRAG-003
static void test_plan_count_disabled_when_payload_exceeds_zero_cap(void)
{
    TEST_ASSERT_EQUAL_UINT(0, rcp_fragment_plan_count(1, 0));
}

//cfusa:test REQ-FRAG-002
static void test_plan_count_exact_multiple(void)
{
    TEST_ASSERT_EQUAL_UINT(3, rcp_fragment_plan_count(30, 10));
}

//cfusa:test REQ-FRAG-002
static void test_plan_count_remainder(void)
{
    TEST_ASSERT_EQUAL_UINT(4, rcp_fragment_plan_count(31, 10));
    TEST_ASSERT_EQUAL_UINT(2, rcp_fragment_plan_count(11, 10));
}

//cfusa:test REQ-FRAG-003
static void test_plan_count_too_many_segments(void)
{
    /* RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 2 fragments of 1 byte each
     * needs RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 1 intermediate
     * segments, one more than segment_num's own 12-bit wire width
     * (0..4095) allows -- see the macro's own FIXED note (issue #256
     * Group K: this constant was 256 before the fix, matching an 8-bit
     * assumption acf.h's own field never actually had). */
    TEST_ASSERT_EQUAL_UINT(0, rcp_fragment_plan_count(
        RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 2, 1));
}

//cfusa:test REQ-FRAG-003
static void test_plan_count_exactly_at_max_intermediate_boundary(void)
{
    /* RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 1 fragments of 1 byte:
     * RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS intermediate + 1 final --
     * exactly at the boundary, must succeed. */
    TEST_ASSERT_EQUAL_UINT(RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 1,
        rcp_fragment_plan_count(RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 1, 1));
}

/* ── plan ──────────────────────────────────────────────────────────────────── */

//cfusa:test REQ-FRAG-004
static void test_plan_single_segment_no_fragmentation_needed(void)
{
    rcp_fragment_segment_t seg;
    rcp_fragment_errc_t    rc;

    rc = rcp_fragment_plan(7, 10, &seg, 1);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_OK, rc);
    TEST_ASSERT_EQUAL_UINT(0, seg.offset);
    TEST_ASSERT_EQUAL_UINT(7, seg.len);
    TEST_ASSERT_FALSE(seg.ms);
}

//cfusa:test REQ-FRAG-004
static void test_plan_empty_payload(void)
{
    rcp_fragment_segment_t seg;
    rcp_fragment_errc_t    rc;

    rc = rcp_fragment_plan(0, 4, &seg, 1);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_OK, rc);
    TEST_ASSERT_EQUAL_UINT(0, seg.offset);
    TEST_ASSERT_EQUAL_UINT(0, seg.len);
    TEST_ASSERT_FALSE(seg.ms);
}

//cfusa:test REQ-FRAG-005
static void test_plan_multi_segment_layout_and_numbering(void)
{
    rcp_fragment_segment_t segs[3];
    rcp_fragment_errc_t    rc;

    /* 25 octets, 10 per fragment -> 3 fragments: [0,10) ms=1 seg0,
     * [10,20) ms=1 seg1, [20,25) ms=0 final. */
    rc = rcp_fragment_plan(25, 10, segs, 3);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_OK, rc);

    TEST_ASSERT_EQUAL_UINT(0, segs[0].offset);
    TEST_ASSERT_EQUAL_UINT(10, segs[0].len);
    TEST_ASSERT_TRUE(segs[0].ms);
    TEST_ASSERT_EQUAL_UINT16(0, segs[0].segment_num);

    TEST_ASSERT_EQUAL_UINT(10, segs[1].offset);
    TEST_ASSERT_EQUAL_UINT(10, segs[1].len);
    TEST_ASSERT_TRUE(segs[1].ms);
    TEST_ASSERT_EQUAL_UINT16(1, segs[1].segment_num);

    TEST_ASSERT_EQUAL_UINT(20, segs[2].offset);
    TEST_ASSERT_EQUAL_UINT(5, segs[2].len);
    TEST_ASSERT_FALSE(segs[2].ms);
}

/* FIXED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group K): segment_num is
 * 12 bits wide (0..4095), not one octet -- this test proves a segment_num
 * above 255 (previously silently truncated by the old uint8_t storage)
 * now plans and round-trips correctly. 300 fragments of 1 byte each: 299
 * intermediate (segment_num 0..298, well past the old 8-bit ceiling) plus
 * 1 final. */
//cfusa:test REQ-FRAG-005
static void test_plan_segment_num_above_255_does_not_truncate(void)
{
    static rcp_fragment_segment_t segs[300];
    rcp_fragment_errc_t           rc;
    size_t                        count = rcp_fragment_plan_count(300, 1);

    TEST_ASSERT_EQUAL_UINT(300, count);
    rc = rcp_fragment_plan(300, 1, segs, count);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_OK, rc);

    TEST_ASSERT_TRUE(segs[255].ms);
    TEST_ASSERT_EQUAL_UINT16(255, segs[255].segment_num);
    TEST_ASSERT_TRUE(segs[298].ms);
    TEST_ASSERT_EQUAL_UINT16(298, segs[298].segment_num);
    TEST_ASSERT_FALSE(segs[299].ms);
}

//cfusa:test REQ-FRAG-005
static void test_plan_covers_entire_payload_contiguously(void)
{
    rcp_fragment_segment_t segs[7];
    rcp_fragment_errc_t    rc;
    size_t                 payload_len = 67;
    size_t                 max_frag    = 10;
    size_t                 count = rcp_fragment_plan_count(payload_len, max_frag);
    size_t                 i;
    size_t                 covered = 0;

    TEST_ASSERT_EQUAL_UINT(7, count);
    rc = rcp_fragment_plan(payload_len, max_frag, segs, count);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_OK, rc);

    for (i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_UINT(covered, segs[i].offset);
        covered += segs[i].len;
        if (i + 1 < count) {
            TEST_ASSERT_TRUE(segs[i].ms);
            TEST_ASSERT_EQUAL_UINT16((uint16_t)i, segs[i].segment_num);
        } else {
            TEST_ASSERT_FALSE(segs[i].ms);
        }
    }
    TEST_ASSERT_EQUAL_UINT(payload_len, covered);
}

//cfusa:test REQ-FRAG-006
static void test_plan_disabled(void)
{
    rcp_fragment_segment_t seg;
    rcp_fragment_errc_t    rc = rcp_fragment_plan(5, 0, &seg, 999);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_ERR_DISABLED, rc);
}

//cfusa:test REQ-FRAG-006
static void test_plan_too_many_segments(void)
{
    /* rcp_fragment_plan_count() returns 0 (hence ERR_TOO_MANY_SEGMENTS)
     * before ever consulting segment_count or writing to out_segments --
     * same reasoning as test_plan_disabled()'s own single-element dummy
     * array, avoiding a RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS-sized
     * stack allocation for a value that is never actually used. */
    rcp_fragment_segment_t seg;
    rcp_fragment_errc_t    rc = rcp_fragment_plan(
        RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 2, 1, &seg,
        RCP_FRAGMENT_MAX_INTERMEDIATE_SEGMENTS + 2);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_ERR_TOO_MANY_SEGMENTS, rc);
}

//cfusa:test REQ-FRAG-006
static void test_plan_bad_segment_count(void)
{
    rcp_fragment_segment_t segs[3];
    rcp_fragment_errc_t    rc = rcp_fragment_plan(25, 10, segs, 2);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_ERR_BAD_SEGMENT_COUNT, rc);
}

/* REQ-FRAG-008: init()'s own not-collecting/empty-buffer postcondition,
 * checked before any feed() call at all -- every other test in this file
 * inits a reassembler and then immediately feeds it, so none of them
 * observe this raw post-init state on its own. */
//cfusa:test REQ-FRAG-008
static void test_reassembler_init_starts_empty_and_not_collecting(void)
{
    rcp_fragment_reassembler_t r;
    const uint8_t               *out;
    size_t                       out_len = 0xFFu; /* deliberately non-zero sentinel */

    rcp_fragment_reassembler_init(&r, 1024);
    TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(&r));

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(0, out_len);

    rcp_fragment_reassembler_destroy(&r);
}

/* ── reassembler: single-segment (never fragmented) messages ─────────────── */

//cfusa:test REQ-FRAG-010
//cfusa:test REQ-FRAG-011
static void test_reassembler_single_segment_completes_immediately(void)
{
    rcp_fragment_reassembler_t r;
    const uint8_t               data[] = {0xAA, 0xBB, 0xCC};
    rcp_fragment_reasm_result_t rc;
    const uint8_t               *out;
    size_t                       out_len;

    rcp_fragment_reassembler_init(&r, 1024);
    rc = rcp_fragment_reassembler_feed(&r, false, 0, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);
    TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(&r));

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(sizeof(data), out_len);
    TEST_ASSERT_EQUAL_MEMORY(data, out, sizeof(data));

    rcp_fragment_reassembler_destroy(&r);
}

//cfusa:test REQ-FRAG-011
static void test_reassembler_empty_single_segment(void)
{
    rcp_fragment_reassembler_t  r;
    rcp_fragment_reasm_result_t rc;
    const uint8_t               *out;
    size_t                       out_len;

    rcp_fragment_reassembler_init(&r, 16);
    rc = rcp_fragment_reassembler_feed(&r, false, 0, NULL, 0);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(0, out_len);

    rcp_fragment_reassembler_destroy(&r);
}

/* ── reassembler: multi-segment sequences ─────────────────────────────────── */

//cfusa:test REQ-FRAG-013
//cfusa:test REQ-FRAG-014
//cfusa:test REQ-FRAG-018
static void test_reassembler_multi_segment_round_trips_plan(void)
{
    uint8_t                     payload[67];
    rcp_fragment_segment_t      segs[7];
    rcp_fragment_reassembler_t  r;
    rcp_fragment_reasm_result_t rc;
    const uint8_t               *out;
    size_t                       out_len;
    size_t                       i;
    size_t                       count;

    for (i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)(i * 7 + 1);

    count = rcp_fragment_plan_count(sizeof(payload), 10);
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_OK,
                           rcp_fragment_plan(sizeof(payload), 10, segs, count));

    rcp_fragment_reassembler_init(&r, 1024);
    for (i = 0; i < count; i++) {
        rc = rcp_fragment_reassembler_feed(&r, segs[i].ms, segs[i].segment_num,
                                            &payload[segs[i].offset], segs[i].len);
        if (i + 1 < count) {
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);
            TEST_ASSERT_TRUE(rcp_fragment_reassembler_is_collecting(&r));
        } else {
            TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);
            TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(&r));
        }
    }

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(sizeof(payload), out_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, out, sizeof(payload));

    rcp_fragment_reassembler_destroy(&r);
}

//cfusa:test REQ-FRAG-012
static void test_reassembler_out_of_order_first_segment(void)
{
    rcp_fragment_reassembler_t  r;
    const uint8_t               data[4] = {1, 2, 3, 4};
    rcp_fragment_reasm_result_t rc;

    rcp_fragment_reassembler_init(&r, 1024);
    rc = rcp_fragment_reassembler_feed(&r, true, 1 /* should be 0 */, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER, rc);
    TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(&r));

    rcp_fragment_reassembler_destroy(&r);
}

//cfusa:test REQ-FRAG-013
static void test_reassembler_out_of_order_mid_sequence(void)
{
    rcp_fragment_reassembler_t  r;
    const uint8_t               data[4] = {1, 2, 3, 4};
    rcp_fragment_reasm_result_t rc;

    rcp_fragment_reassembler_init(&r, 1024);
    rc = rcp_fragment_reassembler_feed(&r, true, 0, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);

    /* Expected next is 1; skip to 2. */
    rc = rcp_fragment_reassembler_feed(&r, true, 2, data, sizeof(data));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER, rc);
    /* Already-collected state (segment 0) is untouched -- still collecting. */
    TEST_ASSERT_TRUE(rcp_fragment_reassembler_is_collecting(&r));

    rcp_fragment_reassembler_destroy(&r);
}

//cfusa:test REQ-FRAG-014
static void test_reassembler_final_ms0_segment_num_field_ignored(void)
{
    rcp_fragment_reassembler_t  r;
    const uint8_t               seg0[4] = {1, 2, 3, 4};
    const uint8_t               fin[2]  = {9, 9};
    rcp_fragment_reasm_result_t rc;
    const uint8_t               *out;
    size_t                       out_len;

    rcp_fragment_reassembler_init(&r, 1024);
    rc = rcp_fragment_reassembler_feed(&r, true, 0, seg0, sizeof(seg0));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);

    /* Final fragment's segment_num field means something else entirely
     * (e.g. read_size) once ms=0 -- an arbitrary value here (77) must not
     * be checked against the sequence counter. */
    rc = rcp_fragment_reassembler_feed(&r, false, 77, fin, sizeof(fin));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(6, out_len);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    TEST_ASSERT_EQUAL_UINT8(9, out[5]);

    rcp_fragment_reassembler_destroy(&r);
}

//cfusa:test REQ-FRAG-015
static void test_reassembler_too_large_rejects_and_preserves_state(void)
{
    rcp_fragment_reassembler_t  r;
    const uint8_t               seg0[4] = {1, 2, 3, 4};
    const uint8_t               big[10] = {0};
    rcp_fragment_reasm_result_t rc;

    rcp_fragment_reassembler_init(&r, 8); /* max_total_len == 8 */
    rc = rcp_fragment_reassembler_feed(&r, true, 0, seg0, sizeof(seg0));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);

    /* 4 already accumulated + 10 more would exceed 8. */
    rc = rcp_fragment_reassembler_feed(&r, true, 1, big, sizeof(big));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_ERR_TOO_LARGE, rc);
    TEST_ASSERT_TRUE(rcp_fragment_reassembler_is_collecting(&r));

    rcp_fragment_reassembler_destroy(&r);
}

/* REQ-FRAG-016: a genuine internal buffer-growth failure returns
 * RCP_FRAGMENT_REASM_ERR_ALLOC, distinct from every other result value,
 * and leaves r's already-collected state untouched.
 *
 * An absurdly large payload_len (comfortably under max_total_len, so
 * ERR_TOO_LARGE doesn't fire first) does force a genuine libc realloc()
 * failure on a plain debug build -- but NOT portably under this
 * project's own CI AddressSanitizer configuration
 * (ASAN_OPTIONS=halt_on_error=1:abort_on_error=1, no
 * allocator_may_return_null override): ASan treats a request over its
 * own internal max-supported-size ceiling as a hard abort, not a NULL
 * return. rcp/alloc.h's rcp_realloc() hook (added alongside this test)
 * sidesteps that the same way REQ-SEQ-002's rcp_malloc()/rcp_calloc()
 * hooks already do -- a real, portable, ASan-safe failure with no
 * absurd size involved at all. */
static void *always_fails_realloc(void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    return NULL;
}

//cfusa:test REQ-FRAG-016
static void test_reassembler_alloc_failure_is_distinct_and_preserves_state(void)
{
    rcp_alloc_hooks_t            hooks = {0};
    rcp_fragment_reassembler_t   r;
    const uint8_t                seg0[4]  = {1, 2, 3, 4};
    const uint8_t                seg1[4]  = {5, 6, 7, 8};
    const uint8_t                fin[2]   = {9, 10};
    rcp_fragment_reasm_result_t  rc;
    const uint8_t                *out;
    size_t                        out_len;

    rcp_fragment_reassembler_init(&r, 1024);
    rc = rcp_fragment_reassembler_feed(&r, true, 0, seg0, sizeof(seg0));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);

    hooks.realloc_fn = always_fails_realloc;
    rcp_alloc_set_hooks(&hooks);

    rc = rcp_fragment_reassembler_feed(&r, true, 1, seg1, sizeof(seg1));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_ERR_ALLOC, rc);
    TEST_ASSERT_NOT_EQUAL(RCP_FRAGMENT_REASM_CONTINUE, rc);
    TEST_ASSERT_NOT_EQUAL(RCP_FRAGMENT_REASM_COMPLETE, rc);
    TEST_ASSERT_NOT_EQUAL(RCP_FRAGMENT_REASM_ERR_OUT_OF_ORDER, rc);
    TEST_ASSERT_NOT_EQUAL(RCP_FRAGMENT_REASM_ERR_TOO_LARGE, rc);
    TEST_ASSERT_TRUE(rcp_fragment_reassembler_is_collecting(&r));

    /* State genuinely preserved, not merely flagged as such: once the
     * real allocator is restored, the same next segment_num (1, same as
     * the one that just failed) is still accepted, and the final
     * reassembled result is exactly seg0+seg1+fin -- the failed attempt
     * never landed. */
    rcp_alloc_reset_hooks();
    rc = rcp_fragment_reassembler_feed(&r, true, 1, seg1, sizeof(seg1));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);

    rc = rcp_fragment_reassembler_feed(&r, false, 0, fin, sizeof(fin));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(10, out_len);
    TEST_ASSERT_EQUAL_UINT8(1, out[0]);
    TEST_ASSERT_EQUAL_UINT8(8, out[7]);
    TEST_ASSERT_EQUAL_UINT8(10, out[9]);

    rcp_fragment_reassembler_destroy(&r);
}

//cfusa:test REQ-FRAG-015
static void test_reassembler_exactly_at_max_total_len_succeeds(void)
{
    rcp_fragment_reassembler_t  r;
    const uint8_t               seg0[4] = {1, 2, 3, 4};
    const uint8_t               fin[4]  = {5, 6, 7, 8};
    rcp_fragment_reasm_result_t rc;
    const uint8_t               *out;
    size_t                       out_len;

    rcp_fragment_reassembler_init(&r, 8);
    rc = rcp_fragment_reassembler_feed(&r, true, 0, seg0, sizeof(seg0));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);

    rc = rcp_fragment_reassembler_feed(&r, false, 0, fin, sizeof(fin));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(8, out_len);

    rcp_fragment_reassembler_destroy(&r);
}

/* ── reassembler: reset/reuse across messages ─────────────────────────────── */

//cfusa:test REQ-FRAG-009
//cfusa:test REQ-FRAG-017
static void test_reassembler_reset_allows_reuse(void)
{
    rcp_fragment_reassembler_t  r;
    const uint8_t               a[3] = {1, 2, 3};
    const uint8_t               b[2] = {9, 8};
    rcp_fragment_reasm_result_t rc;
    const uint8_t               *out;
    size_t                       out_len;

    rcp_fragment_reassembler_init(&r, 1024);
    rc = rcp_fragment_reassembler_feed(&r, true, 0, a, sizeof(a));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_CONTINUE, rc);
    TEST_ASSERT_TRUE(rcp_fragment_reassembler_is_collecting(&r));

    rcp_fragment_reassembler_reset(&r);
    TEST_ASSERT_FALSE(rcp_fragment_reassembler_is_collecting(&r));

    /* A fresh, unrelated single-segment message must work cleanly after
     * reset, with no leftover state from the abandoned sequence. */
    rc = rcp_fragment_reassembler_feed(&r, false, 0, b, sizeof(b));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(sizeof(b), out_len);
    TEST_ASSERT_EQUAL_MEMORY(b, out, sizeof(b));

    rcp_fragment_reassembler_destroy(&r);
}

//cfusa:test REQ-FRAG-011
static void test_reassembler_completed_then_reused_for_next_message(void)
{
    rcp_fragment_reassembler_t  r;
    const uint8_t               a[3] = {1, 2, 3};
    const uint8_t               b[3] = {4, 5, 6};
    rcp_fragment_reasm_result_t rc;
    const uint8_t               *out;
    size_t                       out_len;

    rcp_fragment_reassembler_init(&r, 1024);

    rc = rcp_fragment_reassembler_feed(&r, false, 0, a, sizeof(a));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);

    /* Without an explicit reset, a caller normally starts feeding the next
     * logical message's own fragment sequence right away -- an ms=false
     * single-segment message again completes immediately, reusing (and
     * overwriting, not appending atop) the previous result. */
    rc = rcp_fragment_reassembler_feed(&r, false, 0, b, sizeof(b));
    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_REASM_COMPLETE, rc);

    rcp_fragment_reassembler_get(&r, &out, &out_len);
    TEST_ASSERT_EQUAL_UINT(sizeof(a) + sizeof(b), out_len);

    rcp_fragment_reassembler_destroy(&r);
}

/* ── e2e.h integration: only the final planned segment carries a CRC ─────── */

static void test_plan_composes_with_e2e_fragment_carries_crc_rule(void)
{
    rcp_fragment_segment_t segs[3];
    size_t                 count = rcp_fragment_plan_count(25, 10);
    size_t                 i;

    TEST_ASSERT_EQUAL_INT(RCP_FRAGMENT_OK, rcp_fragment_plan(25, 10, segs, count));

    for (i = 0; i < count; i++) {
        bool is_last = !segs[i].ms;
        bool expect_crc = (i + 1 == count);

        TEST_ASSERT_EQUAL(expect_crc, rcp_e2e_fragment_carries_crc(is_last));
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_strerror_never_null_and_distinct);
    RUN_TEST(test_reasm_result_string_never_null_and_distinct);

    RUN_TEST(test_plan_count_empty_payload_always_one_segment);
    RUN_TEST(test_plan_count_fits_in_one_fragment);
    RUN_TEST(test_plan_count_disabled_when_payload_exceeds_zero_cap);
    RUN_TEST(test_plan_count_exact_multiple);
    RUN_TEST(test_plan_count_remainder);
    RUN_TEST(test_plan_count_too_many_segments);
    RUN_TEST(test_plan_count_exactly_at_max_intermediate_boundary);

    RUN_TEST(test_plan_single_segment_no_fragmentation_needed);
    RUN_TEST(test_plan_empty_payload);
    RUN_TEST(test_plan_multi_segment_layout_and_numbering);
    RUN_TEST(test_plan_segment_num_above_255_does_not_truncate);
    RUN_TEST(test_plan_covers_entire_payload_contiguously);
    RUN_TEST(test_plan_disabled);
    RUN_TEST(test_plan_too_many_segments);
    RUN_TEST(test_plan_bad_segment_count);

    RUN_TEST(test_reassembler_init_starts_empty_and_not_collecting);
    RUN_TEST(test_reassembler_single_segment_completes_immediately);
    RUN_TEST(test_reassembler_empty_single_segment);

    RUN_TEST(test_reassembler_multi_segment_round_trips_plan);
    RUN_TEST(test_reassembler_out_of_order_first_segment);
    RUN_TEST(test_reassembler_out_of_order_mid_sequence);
    RUN_TEST(test_reassembler_final_ms0_segment_num_field_ignored);
    RUN_TEST(test_reassembler_too_large_rejects_and_preserves_state);
    RUN_TEST(test_reassembler_alloc_failure_is_distinct_and_preserves_state);
    RUN_TEST(test_reassembler_exactly_at_max_total_len_succeeds);

    RUN_TEST(test_reassembler_reset_allows_reuse);
    RUN_TEST(test_reassembler_completed_then_reused_for_next_message);

    RUN_TEST(test_plan_composes_with_e2e_fragment_carries_crc_rule);

    return UNITY_END();
}
