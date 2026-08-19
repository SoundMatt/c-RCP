/* SPDX-License-Identifier: MPL-2.0 */
// Security-relevant subset (CYBERSECURITY.md §1.6, Layer 6 — Rate
// Limiting): per-(stream_id, byte_bus_id) token-bucket admission
// control mitigating request-flood DoS, with safety-tagged requests
// exempt by default. See CYBERSECURITY.md §3 (Request-flood DoS row).
// Each rate-limit test function below carries its own security-test
// marker alongside its regular test marker where relevant, placed at
// that function instead of listed here.
#include "unity.h"

#include <rcp/clock.h>
#include <rcp/ratelimit.h>

void setUp(void) {}
void tearDown(void) {}

static void test_sleep_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();
    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait */
    }
}

static rcp_avtp_addr_t make_addr(uint16_t unique_id, uint8_t byte_bus_id)
{
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    rcp_avtp_addr_t a;
    a.stream_id   = rcp_stream_id_make(mac, unique_id);
    a.byte_bus_id = byte_bus_id;
    return a;
}

/* ── Default config ───────────────────────────────────────────────────────── */

//cfusa:test REQ-RL-010
static void test_default_config_values(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();

    /* This project's Unity build has double-precision asserts disabled
     * (embedded-target convention); compare directly instead. */
    TEST_ASSERT_TRUE(cfg.rate == 100.0);
    TEST_ASSERT_EQUAL_INT(20, cfg.burst);
    TEST_ASSERT_TRUE(cfg.exempt_safety);
}

/* ── Basic admission ──────────────────────────────────────────────────────── */

//cfusa:test REQ-RL-001
//cfusa:test REQ-RL-007
//cfusa:test REQ-RL-011
static void test_first_use_seeds_a_full_bucket(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_ratelimit_limiter_t *rl;

    cfg.rate  = 1000;
    cfg.burst = 10;
    rl = rcp_ratelimit_limiter_new(cfg);

    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, make_addr(1, 0), 0x00));

    rcp_ratelimit_limiter_destroy(rl);
}

/* ── Token exhaustion ─────────────────────────────────────────────────────── */

//cfusa:test REQ-RL-003
//cfusa:test REQ-RL-008
//cfusa:sec-test REQ-RL-003
static void test_allow_returns_false_when_bucket_exhausted(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_ratelimit_limiter_t *rl;
    rcp_avtp_addr_t addr = make_addr(2, 0);

    cfg.rate          = 0.001; /* nearly zero refill */
    cfg.burst         = 2;
    cfg.exempt_safety = false;
    rl = rcp_ratelimit_limiter_new(cfg);

    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, addr, 0x00));
    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, addr, 0x00));
    TEST_ASSERT_FALSE(rcp_ratelimit_limiter_allow(rl, addr, 0x00));

    rcp_ratelimit_limiter_destroy(rl);
}

//cfusa:test REQ-RL-002
static void test_tokens_refill_over_time(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_ratelimit_limiter_t *rl;
    rcp_avtp_addr_t addr = make_addr(3, 0);

    cfg.rate          = 100.0; /* one token every 10ms */
    cfg.burst         = 1;
    cfg.exempt_safety = false;
    rl = rcp_ratelimit_limiter_new(cfg);

    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, addr, 0x00));
    TEST_ASSERT_FALSE(rcp_ratelimit_limiter_allow(rl, addr, 0x00));

    test_sleep_ms(50);
    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, addr, 0x00));

    rcp_ratelimit_limiter_destroy(rl);
}

/* ── Safety exemption ─────────────────────────────────────────────────────── */

//cfusa:test REQ-RL-004
//cfusa:sec-test REQ-RL-004
static void test_safety_tagged_bypasses_bucket_when_exempt(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_ratelimit_limiter_t *rl;
    rcp_avtp_addr_t addr = make_addr(4, 0);

    cfg.rate          = 0.001;
    cfg.burst         = 1;
    cfg.exempt_safety = true;
    rl = rcp_ratelimit_limiter_new(cfg);

    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, addr, 0x00)); /* exhaust the 1 token */

    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, addr, 0x8F)); /* safety-tagged compound */

    rcp_ratelimit_limiter_destroy(rl);
}

//cfusa:test REQ-RL-005
static void test_safety_tagged_does_not_bypass_when_not_exempt(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_ratelimit_limiter_t *rl;
    rcp_avtp_addr_t addr = make_addr(5, 0);

    cfg.rate          = 0.001;
    cfg.burst         = 1;
    cfg.exempt_safety = false;
    rl = rcp_ratelimit_limiter_new(cfg);

    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, addr, 0x00));

    TEST_ASSERT_FALSE(rcp_ratelimit_limiter_allow(rl, addr, 0x8F));

    rcp_ratelimit_limiter_destroy(rl);
}

/* ── Per-endpoint independence ────────────────────────────────────────────── */

//cfusa:test REQ-RL-006
static void test_each_address_has_an_independent_bucket(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_ratelimit_limiter_t *rl;
    rcp_avtp_addr_t a = make_addr(6, 0);
    rcp_avtp_addr_t b = make_addr(6, 1); /* same stream, different byte_bus_id */

    cfg.rate          = 0.001;
    cfg.burst         = 1;
    cfg.exempt_safety = false;
    rl = rcp_ratelimit_limiter_new(cfg);

    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, a, 0x00));  /* exhausts a's bucket only */
    TEST_ASSERT_FALSE(rcp_ratelimit_limiter_allow(rl, a, 0x00));
    TEST_ASSERT_TRUE(rcp_ratelimit_limiter_allow(rl, b, 0x00));  /* b's own bucket is still full */

    rcp_ratelimit_limiter_destroy(rl);
}

/* ── destroy() ─────────────────────────────────────────────────────────────── */

//cfusa:test REQ-RL-009
static void test_destroy_frees_every_bucket(void)
{
    rcp_ratelimit_config_t cfg = rcp_ratelimit_default_config();
    rcp_ratelimit_limiter_t *rl = rcp_ratelimit_limiter_new(cfg);
    int i;

    for (i = 0; i < 32; i++) {
        (void)rcp_ratelimit_limiter_allow(rl, make_addr((uint16_t)i, 0), 0x00);
    }

    rcp_ratelimit_limiter_destroy(rl); /* must not leak or crash (ASan-checked in CI) */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_config_values);
    RUN_TEST(test_first_use_seeds_a_full_bucket);
    RUN_TEST(test_allow_returns_false_when_bucket_exhausted);
    RUN_TEST(test_tokens_refill_over_time);
    RUN_TEST(test_safety_tagged_bypasses_bucket_when_exempt);
    RUN_TEST(test_safety_tagged_does_not_bypass_when_not_exempt);
    RUN_TEST(test_each_address_has_an_independent_bucket);
    RUN_TEST(test_destroy_frees_every_bucket);

    return UNITY_END();
}
