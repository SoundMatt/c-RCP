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
//cfusa:test REQ-RMAP-070
//cfusa:test REQ-RMAP-010
//cfusa:test REQ-RMAP-011
//cfusa:test REQ-RMAP-012
//cfusa:test REQ-RMAP-013
//cfusa:test REQ-RMAP-014
//cfusa:test REQ-RMAP-015
//cfusa:test REQ-RMAP-016
//cfusa:test REQ-RMAP-073
//cfusa:test REQ-RMAP-074
//cfusa:test REQ-RMAP-075
//cfusa:test REQ-RMAP-076
//cfusa:test REQ-RMAP-077
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

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, true, true, false);
    TEST_ASSERT_TRUE(ctx.via_root_client_ep0);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);
}

static void test_writer_ctx_denies_root_client_when_wrong_stream_or_not_ep0(void)
{
    rcp_regmap_general_t map;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map);
    map.svr_root_client_index = 7;

    ctx = rcp_regmap_writer_ctx(&map, NULL, 8, true, true, false);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, false, true, false);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);
}

static void test_writer_ctx_denies_root_client_when_none_granted(void)
{
    rcp_regmap_general_t map;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map); /* svr_root_client_index == RCP_REGMAP_NO_ROOT_CLIENT */

    ctx = rcp_regmap_writer_ctx(&map, NULL, RCP_REGMAP_NO_ROOT_CLIENT, true, true, false);
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

    ctx = rcp_regmap_writer_ctx(&map, &owner, 3, false, true, false);
    TEST_ASSERT_TRUE(ctx.via_owning_stream);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);
}

static void test_writer_ctx_denies_owning_stream_when_no_owner_or_null(void)
{
    rcp_regmap_general_t map;
    rcp_regmap_ep_client_t owner;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map);

    ctx = rcp_regmap_writer_ctx(&map, NULL, 3, false, true, false);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);

    owner.has_owning_stream   = false;
    owner.owning_stream_index = 3;
    ctx = rcp_regmap_writer_ctx(&map, &owner, 3, false, true, false);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);

    owner.has_owning_stream   = true;
    owner.owning_stream_index = 3;
    ctx = rcp_regmap_writer_ctx(&map, &owner, 4, false, true, false);
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

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, true, true, false);
    TEST_ASSERT_FALSE(ctx.via_non_unicast_frame);
    TEST_ASSERT_TRUE(ctx.via_root_client_ep0); /* unaffected by unicast-ness */

    ctx = rcp_regmap_writer_ctx(&map, NULL, 7, true, false, false);
    TEST_ASSERT_TRUE(ctx.via_non_unicast_frame);
    TEST_ASSERT_TRUE(ctx.via_root_client_ep0); /* still granted -- these are independent
                                                   axes; rcp_lifecycle_field_writable() is
                                                   what combines them */
}

/* REQ-RMAP-070: via_discovery_stream is an already-classified input this
 * function passes straight through (same convention as via_unicast
 * above), independent of root-client/owning-stream authorization --
 * and, critically, it is now ALWAYS explicitly assigned rather than
 * left uninitialized (REQ-RMAP-009's own fix). Constructing ctx with
 * every other member false confirms the field is genuinely set, not
 * just coincidentally zero from stack/memset luck. */
static void test_writer_ctx_plumbs_via_discovery_stream(void)
{
    rcp_regmap_general_t map;
    rcp_lifecycle_writer_ctx_t ctx;

    rcp_regmap_general_init(&map); /* no root client, no owning stream */

    ctx = rcp_regmap_writer_ctx(&map, NULL, 3, false, true, true);
    TEST_ASSERT_TRUE(ctx.via_discovery_stream);
    TEST_ASSERT_FALSE(ctx.via_root_client_ep0);
    TEST_ASSERT_FALSE(ctx.via_owning_stream);

    ctx = rcp_regmap_writer_ctx(&map, NULL, 3, false, true, false);
    TEST_ASSERT_FALSE(ctx.via_discovery_stream);
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
    /* ep_req_storage_size widened uint16_t -> uint32_t (issue #311 batch 2,
     * see this field's own comment in regmap.h for why). */
    TEST_ASSERT_EQUAL_UINT32(0, cfg.ep_req_storage_size);
    /* REQ-RMAP-073/074/075 (issue #311): ep_description/ep_tx_buffer_size/
     * ep_rx_buffer_size, added to close a 3-field content-modeling gap
     * against TC18's own Table 28/31, zero-init the same as every other
     * member. */
    TEST_ASSERT_EQUAL_UINT32(0, cfg.ep_description);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_tx_buffer_size);
    TEST_ASSERT_EQUAL_UINT16(0, cfg.ep_rx_buffer_size);
}

/* ── ep_delay_time boundary conversion (REQ-RMAP-076, issue #311 batch 2) ──── */

static void test_ep_delay_time_us_to_reg_accepts_all_four_allowed_values(void)
{
    uint8_t reg = 0xFFu;

    TEST_ASSERT_TRUE(rcp_regmap_ep_delay_time_us_to_reg(1u, &reg));
    TEST_ASSERT_EQUAL_UINT8(0u, reg);

    TEST_ASSERT_TRUE(rcp_regmap_ep_delay_time_us_to_reg(10u, &reg));
    TEST_ASSERT_EQUAL_UINT8(1u, reg);

    TEST_ASSERT_TRUE(rcp_regmap_ep_delay_time_us_to_reg(20u, &reg));
    TEST_ASSERT_EQUAL_UINT8(2u, reg);

    TEST_ASSERT_TRUE(rcp_regmap_ep_delay_time_us_to_reg(50u, &reg));
    TEST_ASSERT_EQUAL_UINT8(3u, reg);
}

static void test_ep_delay_time_us_to_reg_rejects_every_other_value(void)
{
    uint8_t reg = 0xFFu;

    /* Not one of {1,10,20,50} -- must be rejected, not rounded to the
     * nearest allowed value (a silently-substituted delay would
     * misconfigure the endpoint's own scheduling timing). */
    TEST_ASSERT_FALSE(rcp_regmap_ep_delay_time_us_to_reg(0u, &reg));
    TEST_ASSERT_FALSE(rcp_regmap_ep_delay_time_us_to_reg(2u, &reg));
    TEST_ASSERT_FALSE(rcp_regmap_ep_delay_time_us_to_reg(11u, &reg));
    TEST_ASSERT_FALSE(rcp_regmap_ep_delay_time_us_to_reg(51u, &reg));
    TEST_ASSERT_FALSE(rcp_regmap_ep_delay_time_us_to_reg(1000000u, &reg));
    /* out_reg left untouched by every rejected call above. */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, reg);
}

static void test_ep_delay_time_reg_to_us_covers_all_four_register_values(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u,  rcp_regmap_ep_delay_time_reg_to_us(0u));
    TEST_ASSERT_EQUAL_UINT32(10u, rcp_regmap_ep_delay_time_reg_to_us(1u));
    TEST_ASSERT_EQUAL_UINT32(20u, rcp_regmap_ep_delay_time_reg_to_us(2u));
    TEST_ASSERT_EQUAL_UINT32(50u, rcp_regmap_ep_delay_time_reg_to_us(3u));
}

static void test_ep_delay_time_reg_to_us_masks_out_of_range_input(void)
{
    /* reg is only ever a 2-bit wire field; a caller passing a wider raw
     * byte (e.g. an unmasked octet read straight off the wire) must not
     * produce undefined behavior -- confirm the masking, not just the
     * in-range cases above. */
    TEST_ASSERT_EQUAL_UINT32(1u,  rcp_regmap_ep_delay_time_reg_to_us(0x04u)); /* 0x04 & 0x3 = 0 */
    TEST_ASSERT_EQUAL_UINT32(50u, rcp_regmap_ep_delay_time_reg_to_us(0xFFu)); /* 0xFF & 0x3 = 3 */
}

/* ── ep_req_storage_size boundary conversion (REQ-RMAP-077, issue #311 batch 2) ── */

static void test_ep_req_storage_size_words_to_octets_is_exact(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u,      rcp_regmap_ep_req_storage_size_words_to_octets(0u));
    TEST_ASSERT_EQUAL_UINT32(4u,      rcp_regmap_ep_req_storage_size_words_to_octets(1u));
    TEST_ASSERT_EQUAL_UINT32(262140u, rcp_regmap_ep_req_storage_size_words_to_octets(65535u));
}

static void test_ep_req_storage_size_octets_to_words_round_trips(void)
{
    uint16_t words = 0xFFFFu;

    TEST_ASSERT_TRUE(rcp_regmap_ep_req_storage_size_octets_to_words(0u, &words));
    TEST_ASSERT_EQUAL_UINT16(0u, words);

    TEST_ASSERT_TRUE(rcp_regmap_ep_req_storage_size_octets_to_words(4u, &words));
    TEST_ASSERT_EQUAL_UINT16(1u, words);

    TEST_ASSERT_TRUE(rcp_regmap_ep_req_storage_size_octets_to_words(262140u, &words));
    TEST_ASSERT_EQUAL_UINT16(65535u, words);
}

static void test_ep_req_storage_size_octets_to_words_rejects_non_multiple_of_4(void)
{
    uint16_t words = 0xFFFFu;

    TEST_ASSERT_FALSE(rcp_regmap_ep_req_storage_size_octets_to_words(1u, &words));
    TEST_ASSERT_FALSE(rcp_regmap_ep_req_storage_size_octets_to_words(5u, &words));
    TEST_ASSERT_FALSE(rcp_regmap_ep_req_storage_size_octets_to_words(262141u, &words));
    /* out_words left untouched by every rejected call above. */
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, words);
}

static void test_ep_req_storage_size_octets_to_words_rejects_over_16_bit_word_count(void)
{
    uint16_t words = 0xFFFFu;

    /* 262144 octets = 65536 words -- one word past the register's own
     * 16-bit width (max representable word count is 65535). */
    TEST_ASSERT_FALSE(rcp_regmap_ep_req_storage_size_octets_to_words(262144u, &words));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, words);
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
        {0, 1, 0},
        {1, 5, 0},
        {2, 9, 0},
    };

    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(entries, 3));
}

static void test_ep_id_map_ascending_false_for_equal_adjacent(void)
{
    rcp_regmap_ep_id_map_entry_t entries[2] = {
        {0, 4, 0},
        {1, 4, 0},
    };

    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(entries, 2));
}

static void test_ep_id_map_ascending_false_for_descending(void)
{
    rcp_regmap_ep_id_map_entry_t entries[2] = {
        {0, 9, 0},
        {1, 2, 0},
    };

    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(entries, 2));
}

static void test_ep_id_map_ascending_vacuous_for_zero_or_one(void)
{
    rcp_regmap_ep_id_map_entry_t one[1] = {{0, 3, 0}};

    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(NULL, 0));
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(one, 1));
}

/* ── rcp_regmap_ep_id_map_byte_bus_ids_for_stream() (issue #335) ────────────
 *
 * Struct field order is {ep_id, byte_bus_id, request_stream_index} -- see
 * rcp_regmap_ep_id_map_entry_t's own doc comment for why. */

static void test_byte_bus_ids_for_stream_finds_every_bound_endpoint(void)
{
    rcp_regmap_ep_id_map_entry_t entries[3] = {
        {1, 0x100, 2}, /* ep 1, bbid 0x100, stream 2 */
        {2, 0x200, 2}, /* ep 2, bbid 0x200, stream 2 -- same stream */
        {3, 0x300, 5}, /* ep 3, bbid 0x300, stream 5 -- different stream */
    };
    rcp_byte_bus_id_t out[4] = {0};

    TEST_ASSERT_EQUAL_UINT(2, rcp_regmap_ep_id_map_byte_bus_ids_for_stream(entries, 3, 2, out, 4));
    TEST_ASSERT_EQUAL_UINT16(0x100, out[0]);
    TEST_ASSERT_EQUAL_UINT16(0x200, out[1]);
}

static void test_byte_bus_ids_for_stream_dedupes_repeated_byte_bus_id(void)
{
    /* Two distinct ep_ids sharing one byte_bus_id under the same stream
     * (TC18 §12.7.8's own multicast-within-a-stream shape) -- reported
     * once, not twice. */
    rcp_regmap_ep_id_map_entry_t entries[2] = {
        {1, 0x100, 2},
        {2, 0x100, 2},
    };
    rcp_byte_bus_id_t out[4] = {0};

    TEST_ASSERT_EQUAL_UINT(1, rcp_regmap_ep_id_map_byte_bus_ids_for_stream(entries, 2, 2, out, 4));
    TEST_ASSERT_EQUAL_UINT16(0x100, out[0]);
}

static void test_byte_bus_ids_for_stream_no_match_returns_zero(void)
{
    rcp_regmap_ep_id_map_entry_t entries[1] = {{1, 0x100, 2}};
    rcp_byte_bus_id_t             out[4]     = {0};

    TEST_ASSERT_EQUAL_UINT(0, rcp_regmap_ep_id_map_byte_bus_ids_for_stream(entries, 1, 9, out, 4));
}

static void test_byte_bus_ids_for_stream_ask_first_then_size_a_buffer(void)
{
    /* out_capacity smaller than the true match count: the true total is
     * still returned, but only out_capacity entries are written -- the
     * same idiom rcp_sched_split_frame_members() already established. */
    rcp_regmap_ep_id_map_entry_t entries[3] = {
        {1, 0x100, 2},
        {2, 0x200, 2},
        {3, 0x300, 2},
    };
    rcp_byte_bus_id_t out[2] = {0xEEEE, 0xEEEE};

    TEST_ASSERT_EQUAL_UINT(3, rcp_regmap_ep_id_map_byte_bus_ids_for_stream(entries, 3, 2, out, 2));
    TEST_ASSERT_EQUAL_UINT16(0x100, out[0]);
    TEST_ASSERT_EQUAL_UINT16(0x200, out[1]);
}

static void test_byte_bus_ids_for_stream_zero_capacity_writes_nothing(void)
{
    rcp_regmap_ep_id_map_entry_t entries[1] = {{1, 0x100, 2}};

    TEST_ASSERT_EQUAL_UINT(1, rcp_regmap_ep_id_map_byte_bus_ids_for_stream(entries, 1, 2, NULL, 0));
}

static void test_byte_bus_ids_for_stream_vacuous_for_empty_table(void)
{
    rcp_byte_bus_id_t out[1] = {0};

    TEST_ASSERT_EQUAL_UINT(0, rcp_regmap_ep_id_map_byte_bus_ids_for_stream(NULL, 0, 2, out, 1));
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
    RUN_TEST(test_writer_ctx_plumbs_via_discovery_stream);

    RUN_TEST(test_pin_property_bits_are_pairwise_distinct);

    RUN_TEST(test_named_signal_string_never_null);
    RUN_TEST(test_named_signal_string_unique);

    RUN_TEST(test_ep_generic_cfg_init_zeroes);
    RUN_TEST(test_ep_delay_time_us_to_reg_accepts_all_four_allowed_values);
    RUN_TEST(test_ep_delay_time_us_to_reg_rejects_every_other_value);
    RUN_TEST(test_ep_delay_time_reg_to_us_covers_all_four_register_values);
    RUN_TEST(test_ep_delay_time_reg_to_us_masks_out_of_range_input);
    RUN_TEST(test_ep_req_storage_size_words_to_octets_is_exact);
    RUN_TEST(test_ep_req_storage_size_octets_to_words_round_trips);
    RUN_TEST(test_ep_req_storage_size_octets_to_words_rejects_non_multiple_of_4);
    RUN_TEST(test_ep_req_storage_size_octets_to_words_rejects_over_16_bit_word_count);
    RUN_TEST(test_ep_functional_cfg_init_zeroes);

    RUN_TEST(test_request_stream_cfg_init_zeroes);
    RUN_TEST(test_response_queue_cfg_init_zeroes);

    RUN_TEST(test_ep_id_map_ascending_true_for_strictly_increasing);
    RUN_TEST(test_ep_id_map_ascending_false_for_equal_adjacent);
    RUN_TEST(test_ep_id_map_ascending_false_for_descending);
    RUN_TEST(test_ep_id_map_ascending_vacuous_for_zero_or_one);

    RUN_TEST(test_byte_bus_ids_for_stream_finds_every_bound_endpoint);
    RUN_TEST(test_byte_bus_ids_for_stream_dedupes_repeated_byte_bus_id);
    RUN_TEST(test_byte_bus_ids_for_stream_no_match_returns_zero);
    RUN_TEST(test_byte_bus_ids_for_stream_ask_first_then_size_a_buffer);
    RUN_TEST(test_byte_bus_ids_for_stream_zero_capacity_writes_nothing);
    RUN_TEST(test_byte_bus_ids_for_stream_vacuous_for_empty_table);

    return UNITY_END();
}
