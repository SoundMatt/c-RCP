/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-RMAP-001
//cfusa:test REQ-RMAP-002
//cfusa:test REQ-RMAP-003
//cfusa:test REQ-RMAP-004
//cfusa:test REQ-RMAP-005
//cfusa:test REQ-RMAP-006
//cfusa:test REQ-RMAP-007
//cfusa:test REQ-RMAP-008
//cfusa:test REQ-RMAP-030
//cfusa:test REQ-RMAP-009
//cfusa:test REQ-RMAP-010
//cfusa:test REQ-RMAP-011
//cfusa:test REQ-RMAP-012
//cfusa:test REQ-RMAP-013
//cfusa:test REQ-RMAP-014
//cfusa:test REQ-RMAP-015
//cfusa:test REQ-RMAP-016
//cfusa:test REQ-RMAP-017
//cfusa:test REQ-RMAP-018
//cfusa:test REQ-RMAP-019
//cfusa:test REQ-RMAP-020
//cfusa:test REQ-RMAP-021
//cfusa:test REQ-RMAP-022
#include "unity.h"

#include <rcp/avtp.h>
#include <rcp/regmap.h>
#include <rcp/rcp.h>
#include <rcp/lifecycle.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── EP0 ────────────────────────────────────────────────────────────────────── */

static void test_is_ep0_true_for_ep0_index(void)
{
    TEST_ASSERT_TRUE(rcp_regmap_is_ep0(RCP_REGMAP_EP0_INDEX));
}

static void test_is_ep0_false_for_other_indices(void)
{
    TEST_ASSERT_FALSE(rcp_regmap_is_ep0(1));
    TEST_ASSERT_FALSE(rcp_regmap_is_ep0(42));
    TEST_ASSERT_FALSE(rcp_regmap_is_ep0(0xFFFF));
}

static void test_ep0_index_matches_discovery_byte_bus_id(void)
{
    TEST_ASSERT_EQUAL_UINT16((uint16_t)RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID,
                              RCP_REGMAP_EP0_INDEX);
}

/* ── rcp_regmap_general_init() ─────────────────────────────────────────────── */

static void test_general_init_zeroes_with_no_root_client(void)
{
    rcp_regmap_general_t map;

    memset(&map, 0xAA, sizeof(map));
    rcp_regmap_general_init(&map);

    TEST_ASSERT_EQUAL_UINT32(0, map.magic);
    TEST_ASSERT_EQUAL_UINT32(0, map.svr_version); /* 32 bit wide -- TC18 Table 18 */
    TEST_ASSERT_EQUAL_UINT16(0, map.vendor_id);
    TEST_ASSERT_EQUAL_UINT16(0, map.device_id);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_ep_count);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_req_stream_max);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_responder_streams_max);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_sequencers_max);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_configuration_lock);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_responder_mem_size);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_req_mem_size);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_implemented_options);
    TEST_ASSERT_EQUAL_UINT16(RCP_REGMAP_NO_ROOT_CLIENT, map.svr_root_client_index);

    TEST_ASSERT_EQUAL_UINT16(0, map.svr_hw_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_request_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_response_stream_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_request_stream_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_response_stream_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_ep_generic_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_ep_generic_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_ep_functional_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_ep_bytebus_id_map_ptr);
    TEST_ASSERT_EQUAL_UINT8(0, map.svr_ep_bytebus_id_map_capacity);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_sequencer_state_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_network_interface_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_network_interface_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_physical_layer_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_physical_layer_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_time_synch_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_time_synch_cfg_capacity);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_security_cfg_ptr);
    TEST_ASSERT_EQUAL_UINT16(0, map.svr_security_cfg_capacity);
}

/* ── svr_implemented_options bit assignments (REQ-RMAP-030) ───────────────── */

/* REQ-RMAP-004: the five TC18 Table 18 bits (0x0016) are pairwise
 * distinct -- still a genuinely worthwhile correctness property to
 * assert directly, independent of REQ-RMAP-004's own former (incorrect)
 * §12.9.1.1 citation; see this file's own retirement note just below
 * for the primary-source finding that corrected it. */
static void test_option_bits_are_pairwise_distinct(void)
{
    uint8_t bits[5] = {
        RCP_REGMAP_OPT_COMPOUND_WAIT,
        RCP_REGMAP_OPT_TRIGGER,
        RCP_REGMAP_OPT_CHAINED,
        RCP_REGMAP_OPT_TIME_SYNC,
        RCP_REGMAP_OPT_ENH_CANCEL,
    };
    size_t i, j;

    for (i = 0; i < 5; i++) {
        TEST_ASSERT_NOT_EQUAL(0, bits[i]);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(bits[i] != bits[j]);
        }
    }
}

/* ── REQ-RMAP-005..008 retired ──────────────────────────────────────────────── */

/* REQ-RMAP-005..008 (formerly: rcp_regmap_options_group_consistent()'s
 * four accept/reject behaviors for an invented three-pair grouping of
 * svr_implemented_options' bits) are RETIRED as of REQ-RMAP-030.
 * Primary-source verification (OA_TC18_specification_v_0.5.1_RC.pdf,
 * §12.9.1.1, page 64) found their shared citation incorrect: that
 * section is entirely about an RC Server handling multiple ACF-type
 * requests packed into one AVTPDU frame, and says nothing about this
 * register, feature advertisement, or bit pairing. Each of the five
 * real bits is independently settable with no sibling requirement --
 * proven directly here rather than via a retired consistency-checker
 * function that no longer exists. */
static void test_option_bits_are_independently_settable_no_pairing_required(void)
{
    rcp_regmap_general_t map;

    rcp_regmap_general_init(&map);

    map.svr_implemented_options = RCP_REGMAP_OPT_TRIGGER; /* just one bit -- no sibling needed */
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_OPT_TRIGGER, map.svr_implemented_options);

    map.svr_implemented_options = RCP_REGMAP_OPT_COMPOUND_WAIT | RCP_REGMAP_OPT_CHAINED;
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(RCP_REGMAP_OPT_COMPOUND_WAIT | RCP_REGMAP_OPT_CHAINED),
                           map.svr_implemented_options);

    map.svr_implemented_options = (uint8_t)(RCP_REGMAP_OPT_COMPOUND_WAIT | RCP_REGMAP_OPT_TRIGGER |
                                             RCP_REGMAP_OPT_CHAINED | RCP_REGMAP_OPT_TIME_SYNC |
                                             RCP_REGMAP_OPT_ENH_CANCEL);
    TEST_ASSERT_EQUAL_HEX8(0x1Fu, map.svr_implemented_options); /* all five bits, 0b00011111 */
}

/* ── rcp_regmap_writer_ctx() ────────────────────────────────────────────────── */

static void test_writer_ctx_grants_root_client_via_ep0(void)
{
    rcp_regmap_general_t map;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map);
    map.svr_root_client_index = 7;

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, true, true);
    TEST_ASSERT_TRUE(ctx.via_root_client_ep0);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);
}

static void test_writer_ctx_denies_root_client_when_wrong_stream_or_not_ep0(void)
{
    rcp_regmap_general_t map;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map);
    map.svr_root_client_index = 7;

    ctx = rcp_regmap_writer_ctx(&map, NULL, 8, true, true);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, false, true);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);
}

static void test_writer_ctx_denies_root_client_when_none_granted(void)
{
    rcp_regmap_general_t map;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map); /* svr_root_client_index == RCP_REGMAP_NO_ROOT_CLIENT */

    ctx = rcp_regmap_writer_ctx(&map, NULL, RCP_REGMAP_NO_ROOT_CLIENT, true, true);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);
}

static void test_writer_ctx_grants_owning_stream(void)
{
    rcp_regmap_general_t map;
    rcp_regmap_ep_client_t owner;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map);
    owner.has_owning_stream   = true;
    owner.owning_stream_index = 3;

    ctx = rcp_regmap_writer_ctx(&map, &owner, 3, false, true);
    TEST_ASSERT_TRUE(ctx.via_owning_stream);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);
}

static void test_writer_ctx_denies_owning_stream_when_no_owner_or_null(void)
{
    rcp_regmap_general_t map;
    rcp_regmap_ep_client_t owner;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map);

    ctx = rcp_regmap_writer_ctx(&map, NULL, 3, false, true);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);

    owner.has_owning_stream   = false;
    owner.owning_stream_index = 3;
    ctx = rcp_regmap_writer_ctx(&map, &owner, 3, false, true);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);

    owner.has_owning_stream   = true;
    owner.owning_stream_index = 3;
    ctx = rcp_regmap_writer_ctx(&map, &owner, 4, false, true);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);
}

/* REQ-LIFECYCLE-027: via_unicast plumbs straight through to the derived
 * writer_ctx's via_non_unicast_frame, independent of root-client/owning-
 * stream authorization -- this is what an integrator's real dispatch
 * path relies on to close the write-request-unicast-only gap (see
 * l2.h's rcp_l2_mac_is_unicast() for how a real frame's destination MAC
 * is classified into this boolean). */
static void test_writer_ctx_plumbs_via_unicast_to_non_unicast_frame_flag(void)
{
    rcp_regmap_general_t map;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map);
    map.svr_root_client_index = 7;

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, true, true);
    TEST_ASSERT_FALSE(ctx.via_non_unicast_frame);
    TEST_ASSERT_TRUE(ctx.via_root_client_ep0); /* unaffected by unicast-ness */

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, true, false);
    TEST_ASSERT_TRUE(ctx.via_non_unicast_frame);
    TEST_ASSERT_TRUE(ctx.via_root_client_ep0); /* still granted -- these are independent
                                                   axes; rcp_lifecycle_field_writable() is
                                                   what combines them */
}

/* ── HW pin-property bit assignments ───────────────────────────────────────── */

static void test_pin_property_bits_are_pairwise_distinct(void)
{
    uint8_t bits[6] = {
        RCP_REGMAP_PIN_PROP_OUTPUT,
        RCP_REGMAP_PIN_PROP_INPUT,
        RCP_REGMAP_PIN_PROP_OPEN_DRAIN,
        RCP_REGMAP_PIN_PROP_PULL_UP,
        RCP_REGMAP_PIN_PROP_PULL_DOWN,
        RCP_REGMAP_PIN_PROP_ACTIVE_LOW,
    };
    size_t i, j;

    for (i = 0; i < 6; i++) {
        TEST_ASSERT_NOT_EQUAL(0, bits[i]);
        for (j = 0; j < i; j++) {
            TEST_ASSERT_TRUE(bits[i] != bits[j]);
        }
    }
}

/* ── rcp_regmap_named_signal_string() ──────────────────────────────────────── */

static void test_named_signal_string_never_null(void)
{
    int i;

    for (i = 0; i < (int)RCP_REGMAP_SIGNAL_COUNT; i++) {
        const char *s = rcp_regmap_named_signal_string((rcp_regmap_named_signal_t)i);
        TEST_ASSERT_NOT_NULL(s);
        TEST_ASSERT_TRUE(strlen(s) > 0);
    }

    TEST_ASSERT_EQUAL_STRING("unknown",
        rcp_regmap_named_signal_string((rcp_regmap_named_signal_t)RCP_REGMAP_SIGNAL_COUNT));
    TEST_ASSERT_EQUAL_STRING("unknown",
        rcp_regmap_named_signal_string((rcp_regmap_named_signal_t)9999));
}

static void test_named_signal_string_unique(void)
{
    int i, j;

    for (i = 0; i < (int)RCP_REGMAP_SIGNAL_COUNT; i++) {
        const char *si = rcp_regmap_named_signal_string((rcp_regmap_named_signal_t)i);
        for (j = 0; j < i; j++) {
            const char *sj = rcp_regmap_named_signal_string((rcp_regmap_named_signal_t)j);
            TEST_ASSERT_TRUE(strcmp(si, sj) != 0);
        }
    }
}

/* ── Generic/functional per-endpoint config init ───────────────────────────── */

static void test_ep_generic_cfg_init_zeroes(void)
{
    rcp_regmap_ep_generic_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_ep_generic_cfg_init(&cfg);

    TEST_ASSERT_EQUAL_UINT8(0, cfg.ep_type);
    TEST_ASSERT_FALSE(cfg.ep_used);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.ep_delay_time);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_req_storage_size);
}

static void test_ep_functional_cfg_init_zeroes(void)
{
    rcp_regmap_ep_functional_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.ep_enable);
    TEST_ASSERT_FALSE(cfg.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.ep_suppress_response);
}

/* ── Request-stream / response-queue config init ───────────────────────────── */

static void test_request_stream_cfg_init_zeroes(void)
{
    rcp_regmap_request_stream_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_request_stream_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.configured);
    TEST_ASSERT_EQUAL_UINT64(0, cfg.rx_stream_id);
    TEST_ASSERT_FALSE(cfg.rx_enforce_e2e);
    TEST_ASSERT_FALSE(cfg.rx_wd_enable);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.rx_wd_timeout_ms);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.rx_wd_action);
    TEST_ASSERT_FALSE(cfg.rx_wd_safestate_enable);
    TEST_ASSERT_FALSE(cfg.rx_wd_info_enable);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.rx_safety_measure);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.rx_safestate_sequencer);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.rx_safe_sequencer_state);
    TEST_ASSERT_EQUAL_UINT(0, cfg.rx_stream_max_request_size);
}

static void test_response_queue_cfg_init_zeroes(void)
{
    rcp_regmap_response_queue_cfg_t cfg;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_response_queue_cfg_init(&cfg);

    TEST_ASSERT_EQUAL_UINT16(0, cfg.max_avtpdu_size);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.flush_on_count);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.flush_time_us);
}

/* ── rcp_regmap_ep_id_map_is_ascending() ───────────────────────────────────── */

static void test_ep_id_map_ascending_true_for_strictly_increasing(void)
{
    rcp_regmap_ep_id_map_entry_t entries[3] = {
        {0, 1},
        {1, 5},
        {2, 9},
    };

    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(entries, 3));
}

static void test_ep_id_map_ascending_false_for_equal_adjacent(void)
{
    rcp_regmap_ep_id_map_entry_t entries[2] = {
        {0, 4},
        {1, 4},
    };

    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(entries, 2));
}

static void test_ep_id_map_ascending_false_for_descending(void)
{
    rcp_regmap_ep_id_map_entry_t entries[2] = {
        {0, 9},
        {1, 2},
    };

    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(entries, 2));
}

static void test_ep_id_map_ascending_vacuous_for_zero_or_one(void)
{
    rcp_regmap_ep_id_map_entry_t one[1] = {{0, 3}};

    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(NULL, 0));
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(one, 1));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_is_ep0_true_for_ep0_index);
    RUN_TEST(test_is_ep0_false_for_other_indices);
    RUN_TEST(test_ep0_index_matches_discovery_byte_bus_id);

    RUN_TEST(test_general_init_zeroes_with_no_root_client);

    RUN_TEST(test_option_bits_are_pairwise_distinct);
    RUN_TEST(test_option_bits_are_independently_settable_no_pairing_required);

    RUN_TEST(test_writer_ctx_grants_root_client_via_ep0);
    RUN_TEST(test_writer_ctx_denies_root_client_when_wrong_stream_or_not_ep0);
    RUN_TEST(test_writer_ctx_denies_root_client_when_none_granted);
    RUN_TEST(test_writer_ctx_grants_owning_stream);
    RUN_TEST(test_writer_ctx_denies_owning_stream_when_no_owner_or_null);
    RUN_TEST(test_writer_ctx_plumbs_via_unicast_to_non_unicast_frame_flag);

    RUN_TEST(test_pin_property_bits_are_pairwise_distinct);

    RUN_TEST(test_named_signal_string_never_null);
    RUN_TEST(test_named_signal_string_unique);

    RUN_TEST(test_ep_generic_cfg_init_zeroes);
    RUN_TEST(test_ep_functional_cfg_init_zeroes);

    RUN_TEST(test_request_stream_cfg_init_zeroes);
    RUN_TEST(test_response_queue_cfg_init_zeroes);

    RUN_TEST(test_ep_id_map_ascending_true_for_strictly_increasing);
    RUN_TEST(test_ep_id_map_ascending_false_for_equal_adjacent);
    RUN_TEST(test_ep_id_map_ascending_false_for_descending);
    RUN_TEST(test_ep_id_map_ascending_vacuous_for_zero_or_one);

    return UNITY_END();
}
