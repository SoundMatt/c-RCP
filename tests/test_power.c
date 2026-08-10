/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-PWRMODE-001
//cfusa:test REQ-PWRMODE-002
//cfusa:test REQ-PWRMODE-003
//cfusa:test REQ-PWRMODE-004
//cfusa:test REQ-PWRMODE-005
//cfusa:test REQ-PWRMODE-006
//cfusa:test REQ-PWRMODE-007
//cfusa:test REQ-PWRMODE-008
//cfusa:test REQ-PWRMODE-009
//cfusa:test REQ-PWRMODE-010
//cfusa:test REQ-PWRMODE-011
//cfusa:test REQ-PWRMODE-012
//cfusa:test REQ-PWRMODE-013
#include "unity.h"

#include <rcp/lifecycle.h>
#include <rcp/power.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── rcp_pwrmode_string / rcp_pwrmode_strerror ───────────────────────────────── */

static void test_pwrmode_string_unique_nonempty(void)
{
    const rcp_pwrmode_t modes[] = {
        RCP_PWRMODE_NORMAL, RCP_PWRMODE_STANDBY, RCP_PWRMODE_SLEEP, RCP_PWRMODE_UNPOWERED,
    };
    size_t i, j;

    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        const char *si = rcp_pwrmode_string(modes[i]);

        TEST_ASSERT_NOT_NULL(si);
        TEST_ASSERT_TRUE(si[0] != '\0');
        for (j = i + 1; j < sizeof(modes) / sizeof(modes[0]); j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(si, rcp_pwrmode_string(modes[j])));
        }
    }
}

static void test_pwrmode_string_unknown_nonnull(void)
{
    TEST_ASSERT_NOT_NULL(rcp_pwrmode_string((rcp_pwrmode_t)99));
}

static void test_pwrmode_strerror_nonnull(void)
{
    TEST_ASSERT_NOT_NULL(rcp_pwrmode_strerror(RCP_PWRMODE_OK));
    TEST_ASSERT_NOT_NULL(rcp_pwrmode_strerror(RCP_PWRMODE_ERR_INVALID_TRANSITION));
    TEST_ASSERT_NOT_NULL(rcp_pwrmode_strerror((rcp_pwrmode_errc_t)99));
}

/* ── rcp_pwrmode_cold_start_lifecycle_target ─────────────────────────────────── */

static void test_cold_start_target_is_hw_unconfigured(void)
{
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, rcp_pwrmode_cold_start_lifecycle_target());
}

/* ── rcp_pwrmode_transition ──────────────────────────────────────────────────── */

static void test_transition_normal_to_standby_is_hot(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_NORMAL;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_STANDBY, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_STANDBY, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
}

static void test_transition_standby_to_normal_is_hot(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_STANDBY;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_NORMAL, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
}

static void test_transition_normal_to_sleep_is_cold(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_NORMAL;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_SLEEP, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

static void test_transition_standby_to_sleep_is_cold(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_STANDBY;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_SLEEP, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

static void test_transition_any_to_unpowered_is_cold(void)
{
    rcp_pwrmode_t             mode;
    rcp_pwrmode_start_kind_t  kind;

    mode = RCP_PWRMODE_NORMAL;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_UNPOWERED, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);

    mode = RCP_PWRMODE_STANDBY;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_UNPOWERED, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);

    mode = RCP_PWRMODE_SLEEP;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_UNPOWERED, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

static void test_transition_unpowered_to_normal_is_cold(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_UNPOWERED;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_NORMAL, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

static void test_transition_same_mode_is_noop_hot(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_SLEEP;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_SLEEP, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
}

static void test_transition_sleep_to_normal_is_rejected(void)
{
    rcp_pwrmode_t mode = RCP_PWRMODE_SLEEP;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_INVALID_TRANSITION,
                       rcp_pwrmode_transition(&mode, RCP_PWRMODE_NORMAL, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode); /* unchanged on failure */
}

static void test_transition_standby_to_sleep_direct_via_standby_is_ok_but_skip_rejected(void)
{
    /* StandBy -> Sleep is a permitted direct transition (see above); what
     * is rejected is Sleep <-> StandBy attempted the *other* direction,
     * and Unpowered <-> StandBy/Sleep directly. */
    rcp_pwrmode_t mode = RCP_PWRMODE_SLEEP;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_INVALID_TRANSITION,
                       rcp_pwrmode_transition(&mode, RCP_PWRMODE_STANDBY, NULL));

    mode = RCP_PWRMODE_UNPOWERED;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_INVALID_TRANSITION,
                       rcp_pwrmode_transition(&mode, RCP_PWRMODE_STANDBY, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_INVALID_TRANSITION,
                       rcp_pwrmode_transition(&mode, RCP_PWRMODE_SLEEP, NULL));
}

static void test_transition_out_start_kind_may_be_null(void)
{
    rcp_pwrmode_t mode = RCP_PWRMODE_NORMAL;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_STANDBY, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_STANDBY, mode);
}

/* ── rcp_pwrmode_hotstart_required ───────────────────────────────────────────── */

/* As of the REQ-PWRMODE-020 fix (TC18 §12.4.1: a network wake "will...
 * proceed as before", i.e. the same handshake a pin wake runs, not a
 * skip), true for every path -- not just RCP_PWRMODE_WAKE_VIA_PIN. */
static void test_hotstart_required_true_for_every_path(void)
{
    TEST_ASSERT_TRUE(rcp_pwrmode_hotstart_required(RCP_PWRMODE_WAKE_VIA_PIN));
    TEST_ASSERT_TRUE(rcp_pwrmode_hotstart_required(RCP_PWRMODE_WAKE_VIA_NETWORK));
}

/* ── rcp_pwrmode_handshake_* ──────────────────────────────────────────────────── */

static void test_handshake_init_state(void)
{
    rcp_pwrmode_handshake_t hs;

    rcp_pwrmode_handshake_init(&hs, 3);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_NOT_STARTED, hs.step);
    TEST_ASSERT_EQUAL_UINT32(0, hs.wakeup_attempts);
    TEST_ASSERT_EQUAL_UINT32(3, hs.wakeup_repeat_limit);
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_is_complete(&hs));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_has_failed(&hs));
}

static void test_handshake_full_success_sequence(void)
{
    rcp_pwrmode_handshake_t hs;

    rcp_pwrmode_handshake_init(&hs, 3);

    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED, hs.step);

    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, true));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_ECHOED, hs.step);
    TEST_ASSERT_EQUAL_UINT32(1, hs.wakeup_attempts);

    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_resume_queues(&hs));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_COMPLETE, hs.step);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_is_complete(&hs));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_has_failed(&hs));
}

static void test_handshake_wakeup_attempt_pending_before_limit(void)
{
    rcp_pwrmode_handshake_t hs;

    rcp_pwrmode_handshake_init(&hs, 3);
    rcp_pwrmode_handshake_iface_reenabled(&hs);

    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_IFACE_REENABLED, hs.step); /* still pending */
    TEST_ASSERT_EQUAL_UINT32(1, hs.wakeup_attempts);

    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_EQUAL_UINT32(2, hs.wakeup_attempts);
}

static void test_handshake_wakeup_attempt_fails_at_repeat_limit(void)
{
    rcp_pwrmode_handshake_t hs;

    rcp_pwrmode_handshake_init(&hs, 2);
    rcp_pwrmode_handshake_iface_reenabled(&hs);

    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));  /* attempt 1/2, pending */
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false)); /* attempt 2/2, failed */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_FAILED, hs.step);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_has_failed(&hs));
}

static void test_handshake_zero_repeat_limit_fails_first_attempt(void)
{
    rcp_pwrmode_handshake_t hs;

    rcp_pwrmode_handshake_init(&hs, 0);
    rcp_pwrmode_handshake_iface_reenabled(&hs);

    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_FAILED, hs.step);
}

static void test_handshake_steps_reject_out_of_order_calls(void)
{
    rcp_pwrmode_handshake_t hs;

    rcp_pwrmode_handshake_init(&hs, 3);

    /* wakeup_attempt before iface_reenabled */
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_wakeup_attempt(&hs, true));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_HANDSHAKE_NOT_STARTED, hs.step);

    /* resume_queues before echoed */
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_resume_queues(&hs));

    rcp_pwrmode_handshake_iface_reenabled(&hs);
    /* iface_reenabled again -- already past NOT_STARTED */
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_iface_reenabled(&hs));
}

/* ── rcp_pwrmode_wake_from_sleep ─────────────────────────────────────────────── */

static void test_wake_from_sleep_requires_sleep_mode(void)
{
    rcp_pwrmode_t mode = RCP_PWRMODE_NORMAL;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ERR_INVALID_TRANSITION,
                       rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_NETWORK, NULL, NULL));
}

/* As of the REQ-PWRMODE-020 fix, a network wake is classified by the
 * SAME handshake-completion rule a pin wake already used -- a NULL
 * handshake (this test's own former "always hot" pin) now correctly
 * falls back to the module's documented safe default, COLD, matching
 * test_wake_from_sleep_via_pin_hot_only_when_handshake_complete()'s own
 * "handshake not yet complete -> cold" case exactly. */
static void test_wake_from_sleep_via_network_cold_without_handshake(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_SLEEP;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                       rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_NETWORK, NULL, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

/* ...but a completed handshake yields hot for a network wake exactly as
 * it already did for a pin wake -- REQ-PWRMODE-020's own point: both
 * paths now share one rule. */
static void test_wake_from_sleep_via_network_hot_when_handshake_complete(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_SLEEP;
    rcp_pwrmode_start_kind_t  kind;
    rcp_pwrmode_handshake_t   hs;

    rcp_pwrmode_handshake_init(&hs, 3);
    rcp_pwrmode_handshake_iface_reenabled(&hs);
    rcp_pwrmode_handshake_wakeup_attempt(&hs, true);
    rcp_pwrmode_handshake_resume_queues(&hs);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_is_complete(&hs));

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                       rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_NETWORK, &hs, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
}

static void test_wake_from_sleep_via_pin_hot_only_when_handshake_complete(void)
{
    rcp_pwrmode_t             mode;
    rcp_pwrmode_start_kind_t  kind;
    rcp_pwrmode_handshake_t   hs;

    rcp_pwrmode_handshake_init(&hs, 3);
    rcp_pwrmode_handshake_iface_reenabled(&hs);
    rcp_pwrmode_handshake_wakeup_attempt(&hs, true);
    rcp_pwrmode_handshake_resume_queues(&hs);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_is_complete(&hs));

    mode = RCP_PWRMODE_SLEEP;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                       rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_PIN, &hs, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
}

static void test_wake_from_sleep_via_pin_cold_when_handshake_incomplete(void)
{
    rcp_pwrmode_t             mode;
    rcp_pwrmode_start_kind_t  kind;
    rcp_pwrmode_handshake_t   hs;

    rcp_pwrmode_handshake_init(&hs, 3);

    mode = RCP_PWRMODE_SLEEP;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                       rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_PIN, &hs, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

static void test_wake_from_sleep_via_pin_cold_when_handshake_failed(void)
{
    rcp_pwrmode_t             mode;
    rcp_pwrmode_start_kind_t  kind;
    rcp_pwrmode_handshake_t   hs;

    rcp_pwrmode_handshake_init(&hs, 1);
    rcp_pwrmode_handshake_iface_reenabled(&hs);
    rcp_pwrmode_handshake_wakeup_attempt(&hs, false); /* limit == 1, fails immediately */
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_has_failed(&hs));

    mode = RCP_PWRMODE_SLEEP;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                       rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_PIN, &hs, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

static void test_wake_from_sleep_via_pin_null_handshake_is_cold(void)
{
    rcp_pwrmode_t             mode = RCP_PWRMODE_SLEEP;
    rcp_pwrmode_start_kind_t  kind;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                       rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_PIN, NULL, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_COLD, kind);
}

/* ── rcp_pwrmode_check_entry ──────────────────────────────────────────────────── */

static void test_check_entry_ok_when_all_clear(void)
{
    rcp_pwrmode_entry_gate_t gate = { true, true, true };

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_OK, rcp_pwrmode_check_entry(&gate));
}

static void test_check_entry_refused_when_wup_status_uncleared(void)
{
    rcp_pwrmode_entry_gate_t gate = { false, true, true };

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, rcp_pwrmode_check_entry(&gate));
}

static void test_check_entry_refused_when_endpoint_not_idle(void)
{
    rcp_pwrmode_entry_gate_t gate = { true, false, true };

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, rcp_pwrmode_check_entry(&gate));
}

static void test_check_entry_refused_when_response_queue_nonempty(void)
{
    rcp_pwrmode_entry_gate_t gate = { true, true, false };

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, rcp_pwrmode_check_entry(&gate));
}

static void test_check_entry_refused_on_null_gate(void)
{
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, rcp_pwrmode_check_entry(NULL));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_pwrmode_string_unique_nonempty);
    RUN_TEST(test_pwrmode_string_unknown_nonnull);
    RUN_TEST(test_pwrmode_strerror_nonnull);

    RUN_TEST(test_cold_start_target_is_hw_unconfigured);

    RUN_TEST(test_transition_normal_to_standby_is_hot);
    RUN_TEST(test_transition_standby_to_normal_is_hot);
    RUN_TEST(test_transition_normal_to_sleep_is_cold);
    RUN_TEST(test_transition_standby_to_sleep_is_cold);
    RUN_TEST(test_transition_any_to_unpowered_is_cold);
    RUN_TEST(test_transition_unpowered_to_normal_is_cold);
    RUN_TEST(test_transition_same_mode_is_noop_hot);
    RUN_TEST(test_transition_sleep_to_normal_is_rejected);
    RUN_TEST(test_transition_standby_to_sleep_direct_via_standby_is_ok_but_skip_rejected);
    RUN_TEST(test_transition_out_start_kind_may_be_null);

    RUN_TEST(test_hotstart_required_true_for_every_path);

    RUN_TEST(test_handshake_init_state);
    RUN_TEST(test_handshake_full_success_sequence);
    RUN_TEST(test_handshake_wakeup_attempt_pending_before_limit);
    RUN_TEST(test_handshake_wakeup_attempt_fails_at_repeat_limit);
    RUN_TEST(test_handshake_zero_repeat_limit_fails_first_attempt);
    RUN_TEST(test_handshake_steps_reject_out_of_order_calls);

    RUN_TEST(test_wake_from_sleep_requires_sleep_mode);
    RUN_TEST(test_wake_from_sleep_via_network_cold_without_handshake);
    RUN_TEST(test_wake_from_sleep_via_network_hot_when_handshake_complete);
    RUN_TEST(test_wake_from_sleep_via_pin_hot_only_when_handshake_complete);
    RUN_TEST(test_wake_from_sleep_via_pin_cold_when_handshake_incomplete);
    RUN_TEST(test_wake_from_sleep_via_pin_cold_when_handshake_failed);
    RUN_TEST(test_wake_from_sleep_via_pin_null_handshake_is_cold);

    RUN_TEST(test_check_entry_ok_when_all_clear);
    RUN_TEST(test_check_entry_refused_when_wup_status_uncleared);
    RUN_TEST(test_check_entry_refused_when_endpoint_not_idle);
    RUN_TEST(test_check_entry_refused_when_response_queue_nonempty);
    RUN_TEST(test_check_entry_refused_on_null_gate);

    return UNITY_END();
}
