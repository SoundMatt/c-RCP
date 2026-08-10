/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-CFG-011
//cfusa:test REQ-CFG-012
//cfusa:test REQ-DISC-029
//cfusa:test REQ-RMAP-023
//cfusa:test REQ-RMAP-024
//cfusa:test REQ-RMAP-025
//cfusa:test REQ-RMAP-026
//cfusa:test REQ-RMAP-027
//cfusa:test REQ-RMAP-028
//cfusa:test REQ-RMAP-029
//cfusa:test REQ-RMAP-030
//cfusa:test REQ-RMAP-031
//cfusa:test REQ-RMAP-032
//cfusa:test REQ-RMAP-033
//cfusa:test REQ-RMAP-034
//cfusa:test REQ-RMAP-035
//cfusa:test REQ-RMAP-036
//cfusa:test REQ-RMAP-037
//cfusa:test REQ-RMAP-038
//cfusa:test REQ-RMAP-039
//cfusa:test REQ-RMAP-040
//cfusa:test REQ-RMAP-041
//cfusa:test REQ-RMAP-042
//cfusa:test REQ-RMAP-043
//cfusa:test REQ-RMAP-044
//cfusa:test REQ-RMAP-045
//cfusa:test REQ-RMAP-046
//cfusa:test REQ-RMAP-047
//cfusa:test REQ-RMAP-048
//cfusa:test REQ-RMAP-049
//cfusa:test REQ-RMAP-050
//cfusa:test REQ-RMAP-051
//cfusa:test REQ-RMAP-052
//cfusa:test REQ-RMAP-053
//cfusa:test REQ-RMAP-054
//cfusa:test REQ-RMAP-055
//cfusa:test REQ-RMAP-056
//cfusa:test REQ-RMAP-057
//cfusa:test REQ-RMAP-058
//cfusa:test REQ-RMAP-059
//cfusa:test REQ-RMAP-060
//cfusa:test REQ-RMAP-061
//cfusa:test REQ-RMAP-062
//cfusa:test REQ-RMAP-063
//cfusa:test REQ-RMAP-064
//cfusa:test REQ-RMAP-065
//cfusa:test REQ-RMAP-066
//cfusa:test REQ-RMAP-067
//cfusa:test REQ-RMAP-068
//cfusa:test REQ-RMAP-069

/*
 * test_tc18_gaps_regmap.c -- spec-literal conformance-and-deviation suite
 * for the register-map, HW-pin-mapping, stream-configuration, EP_ID-map,
 * configuration-request and discovery clauses of the OPEN Alliance TC18
 * Remote Control Protocol newly catalogued in .fusa-reqs.json (TC18
 * §12.3, §12.7.1, §12.7.5 Table 18, §12.7.6 Tables 19-21, §12.7.7
 * Table 22, §12.7.8 Table 23, §12.7.9 Table 24, §12.7.11-§12.7.14 and
 * §13.7.1.2 Table 33).
 *
 * Requirements catalogued "implemented" are asserted literally, at the
 * addresses/widths/encodings their TC18 citation gives. Requirements
 * catalogued "partial" or "not-implemented" get a *deviation-pinning*
 * test instead: the assertion states the current, real, observable
 * behaviour of this codebase, and the comment above it names the TC18
 * clause that is not met and what a conforming implementation would do
 * instead. Such a test is expected to fail on the day the gap is closed
 * -- that is the point: closing a catalogued gap must be a deliberate,
 * visible edit here and not a silent drift.
 */

#include "unity.h"

#include <rcp/rcp.h>
#include <rcp/avtp.h>
#include <rcp/acf.h>
#include <rcp/errors.h>
#include <rcp/lifecycle.h>
#include <rcp/regmap.h>
#include <rcp/respqueue.h>
#include <rcp/discovery.h>
#include <rcp/config.h>
#include <rcp/mock.h>
#include <rcp/request_sequencer.h>
#include <rcp/ep_gpio.h>
#include <rcp/ep_pwm.h>
#include <rcp/e2e.h>
#include <rcp/fragment.h>
#include <rcp/deadline.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const uint8_t SERVER_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x11};
static const uint8_t CLIENT_A_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0xA1};
static const uint8_t CLIENT_B_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0xB2};

/* Writable/unauthorized writer contexts, reused throughout. */
static const rcp_lifecycle_writer_ctx_t ROOT_WRITER      = {true, true};
static const rcp_lifecycle_writer_ctx_t PLAIN_WRITER     = {false, false};
/* The discovery claimant -- the only writer HW_GENERIC's HW_UNCONFIGURED
 * branch admits as of the REQ-LIFECYCLE-026/035 fix (see lifecycle.c's
 * own doc comment). */
static const rcp_lifecycle_writer_ctx_t DISCOVERY_WRITER = {false, false, false, true};

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Every rcp_regmap_general_t field set to a distinctive nonzero value, so
 * a register that is genuinely readable at its TC18 absolute address is
 * distinguishable from zero-fill. */
static rcp_regmap_general_t populated_map(void)
{
    rcp_regmap_general_t map;
    rcp_regmap_table_ref_t ref;

    rcp_regmap_general_init(&map);
    map.magic                     = 0xC0FFEE01u;
    map.svr_version               = 0x00010501u;
    map.vendor_id                 = 0x1234u;
    map.device_id                 = 0x5678u;
    map.svr_ep_count              = 0x0009u;
    map.svr_req_stream_max        = 0xABu;
    map.svr_responder_streams_max = 0xCDu;
    map.svr_sequencers_max        = 0x07u;
    map.svr_responder_mem_size    = 0x1122u;
    map.svr_req_mem_size          = 0x3344u;
    map.svr_implemented_options   = 0x1Fu; /* all five REQ-RMAP-030 bits set */
    map.svr_root_client_index     = 0x0002u;

    ref.offset = 0x00000040u;
    ref.capacity = 0x0008u;
    map.hw_pin_map = ref;
    map.request_stream_cfg = ref;
    map.response_queue_cfg = ref;
    map.ep_generic_cfg = ref;
    map.ep_functional_cfg = ref;
    map.ep_id_bus_map = ref;
    map.sequencer_state = ref;
    return map;
}

/* The whole of the general register map that is actually reachable over
 * ACF_ABB: encodes a discovery read response of read_size octets and
 * copies the response payload into out[0..read_size). */
static void read_general(const rcp_regmap_general_t *map, uint8_t read_size, uint8_t *out)
{
    rcp_stream_id_t             server = rcp_stream_id_make(SERVER_MAC, 1);
    rcp_bytes_t                 frame  = rcp_discovery_encode_response(map, read_size, 7, server);
    rcp_avtp_ntscf_header_t     ntscf;
    rcp_acf_byte_message_info_t hdr;
    const uint8_t              *np, *p;
    size_t                      nlen, plen;

    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_AVTP_OK, rcp_avtp_decode_ntscf(frame.data, frame.len, &ntscf, &np, &nlen));
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(np, nlen, &hdr, &p, &plen));
    TEST_ASSERT_EQUAL_UINT((size_t)read_size, plen);
    memcpy(out, p, plen);
    rcp_bytes_free(&frame);
}

/* True iff buf[from..to] is all zero -- "nothing is readable here". */
static bool span_is_zero(const uint8_t *buf, size_t from, size_t to)
{
    size_t i;

    for (i = from; i <= to; i++) {
        if (buf[i] != 0u) return false;
    }
    return true;
}

/* ── §13.7.1.2: effective register-write payload length ───────────────────── */

static void test_reg_write_len_matches_the_formula(void)
{
    /* REQ-RMAP-069 (TC18 §13.7.1.2): "Effective number of bytes to be
     * written = (acf_msg_length - 3) x 4 - pad." acf_msg_length=3 (the
     * fixed address(+CRC) region alone, no data) with no pad yields 0
     * data octets. acf_msg_length=5 (2 quadlets = 8 octets of data
     * region) with pad=2 yields 8-2=6 data octets. */
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(3u, 0u));
    TEST_ASSERT_EQUAL_UINT(6u, rcp_acf_reg_write_len(5u, 2u));
    TEST_ASSERT_EQUAL_UINT(4u, rcp_acf_reg_write_len(4u, 0u));

    /* Fail-safe: a malformed/adversarial frame (acf_msg_length too small
     * to hold the fixed region, or pad exceeding what remains) never
     * underflows to a huge size_t -- it reads as 0 effective octets. */
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(2u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(0u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_acf_reg_write_len(4u, 5u));
}

/* ── §12.7.1: the generic evt[2:0] == 111b configuration request ──────────── */

/* TC18 §12.7.1 / Figure 18 defines ONE generic configuration request --
 * evt[2:0] = 111b, payload = a 16-bit relative start address within the
 * addressed endpoint's EP_func section followed by configuration data --
 * usable against EVERY endpoint type. c-RCP implements that shape for
 * PWM_OUT only (asserted first, spec-literally: clk_divider at relative
 * 0x0008, signal_flags at 0x0009). The deviation is asserted second:
 * GPIO's reconfiguration entry point takes a pin BITMASK and carries no
 * relative start address at all, so no address-addressed EP_func write
 * exists for GPIO -- nor for SPI/I2C/UART/LIN/CAN/ADC/ISELED/MDIO/wakeup,
 * none of which has a reconfig entry point of any kind. A conforming
 * implementation would decode the same address+data payload for all of
 * them. */
static void test_generic_config_request_is_pwm_out_only(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    const uint8_t                   write[4] = {0x00, 0x08, 0x33, 0x05};
    uint8_t                         pins[RCP_EP_GPIO_MAX_PINS];

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_RECONFIG_OK,
                      rcp_ep_pwm_out_apply_reconfig(&cfg, write, sizeof(write)));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0008u, RCP_EP_PWM_OUT_REG_CLK_DIVIDER);
    TEST_ASSERT_EQUAL_HEX8(0x33, cfg.clk_divider);
    TEST_ASSERT_EQUAL_HEX8(0x05, cfg.signal_flags);

    memset(pins, 0, sizeof(pins));
    pins[0] = RCP_REGMAP_PIN_PROP_OUTPUT;
    /* Deviation: the argument is a bitmask over pins, not a start address.
     * Bit 0 selects pin 0; there is no octet of the payload that could be
     * a relative EP_func address. */
    rcp_ep_gpio_apply_reconfig(pins, 0x00000001u);
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_INPUT, pins[0]);
}

/* TC18 §12.7.1 requires EVERY endpoint to publish EP_LEN at EP_func
 * relative address 0x0000 and to ignore, in its entirety, a write whose
 * start_address + length exceeds it. PWM_OUT satisfies this literally
 * (asserted here: EP_LEN lives at 0x0000, reports 0x0F, and a write of 2
 * octets at 0x000E is refused whole). Deviation: `grep -rn EP_LEN src`
 * matches ep_pwm.c alone -- no other endpoint type defines an EP_LEN
 * register or enforces the overrun rule, because none has an addressed
 * EP_func write path at all (see the test above). */
static void test_ep_len_overrun_rule_exists_only_for_pwm_out(void)
{
    rcp_ep_pwm_out_functional_cfg_t cfg;
    uint8_t                         block[RCP_EP_PWM_OUT_EP_FUNC_LEN];
    const uint8_t                   overrun[4] = {0x00, 0x0E, 0xAA, 0xBB};

    rcp_ep_pwm_out_functional_cfg_init(&cfg);
    cfg.skew = 0x42u;

    TEST_ASSERT_EQUAL_UINT16((uint16_t)0x0000u, RCP_EP_PWM_OUT_REG_EP_LEN);
    rcp_ep_pwm_out_render_registers(&cfg, block);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_EP_PWM_OUT_EP_FUNC_LEN, block[RCP_EP_PWM_OUT_REG_EP_LEN]);
    TEST_ASSERT_EQUAL_HEX8(0x0F, block[RCP_EP_PWM_OUT_REG_EP_LEN]);

    /* 0x000E + 2 > 0x000F: the whole write is ignored, skew untouched. */
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_RECONFIG_ERR_OUT_OF_RANGE,
                      rcp_ep_pwm_out_apply_reconfig(&cfg, overrun, sizeof(overrun)));
    TEST_ASSERT_EQUAL_HEX8(0x42, cfg.skew);
}

/* ── §12.3: discovery-stream occupancy ─────────────────────────────────────── */

/* TC18 §12.3 / Figure 16: a discovery request arriving while the
 * discovery stream is already claimed is answered with a stream-occupied
 * error. Deviation: rcp_discovery_claim_note_request() returns void, so a
 * refused claim is indistinguishable from a granted one at the call site
 * -- pinned here by claimant identity, the only observable difference --
 * and rcp_wire_error_t stops at RCP_ERROR_CHAIN_ERROR (17) with no
 * DISCOVERY_STREAM_OCCUPIED code to send back. A conforming server would
 * report the occupied condition to client B. */
static void test_discovery_claim_refusal_is_unreportable(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t       a = rcp_stream_id_make(CLIENT_A_MAC, 1);
    rcp_stream_id_t       b = rcp_stream_id_make(CLIENT_B_MAC, 2);

    rcp_discovery_claim_init(&claim, RCP_DISCOVERY_DEFAULT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT32(20u, claim.timeout_ms);

    rcp_discovery_claim_note_request(&claim, a, 100u);
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 105u));

    /* B's request is silently declined: no return value, no error code. */
    rcp_discovery_claim_note_request(&claim, b, 105u);
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 106u));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, b, 106u));

    /* 17 is the last assigned wire error; 18 -- where a
     * DISCOVERY_STREAM_OCCUPIED code would sit -- is not assigned. */
    TEST_ASSERT_EQUAL_INT(17, (int)RCP_ERROR_CHAIN_ERROR);
    TEST_ASSERT_EQUAL_STRING(rcp_wire_error_string((rcp_wire_error_t)19),
                             rcp_wire_error_string((rcp_wire_error_t)18));
}

/* ── §12.7.5 Table 18: the RC Server general (static) register map ────────── */

/* REQ-RMAP-023 (TC18 §12.3.1.1/§12.3.1.2/§12.4.1): the lifecycle state
 * must be exposed as the svr_lifecycle_state register-map entry.
 * rcp_regmap_general_t now carries that field (content modeling only --
 * this is NOT REQ-RMAP-024's own separate wire-reachability concern,
 * which stays open; see test_general_map_wire_reach_stops_after_0x000d()
 * below, unaffected by this field since it isn't wired into the
 * discovery slice). Proves the field's own default and that
 * rcp_mock_server_transition() -- the one caller composing
 * rcp_lifecycle_transition() with a regmap.h general block today --
 * keeps it in sync with the authoritative rcp_lifecycle_state_t on every
 * transition, success or failure. */
static void test_lifecycle_state_register_field_tracks_the_authoritative_state(void)
{
    rcp_mock_server_t                          *srv;
    rcp_lifecycle_endpoint_plausibility_t       eps[1] = {{true, true, true, true}};
    rcp_lifecycle_request_stream_plausibility_t rs[1]  = {{true, true}};
    rcp_lifecycle_plausibility_snapshot_t       snap;
    rcp_lifecycle_writer_ctx_t                  writer = ROOT_WRITER;

    snap.endpoints            = eps;
    snap.endpoint_count       = 1u;
    snap.request_streams      = rs;
    snap.request_stream_count = 1u;

    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)RCP_LIFECYCLE_HW_UNCONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0x55, (uint8_t)RCP_LIFECYCLE_HW_CONFIGURED);
    TEST_ASSERT_EQUAL_HEX8(0xAA, (uint8_t)RCP_LIFECYCLE_RCP_CONFIGURED);

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    /* A freshly-constructed server's regmap field already matches its
     * lifecycle state's own default -- both are HW_UNCONFIGURED (0) by
     * construction, not by an extra sync call. */
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_LIFECYCLE_HW_UNCONFIGURED,
                           rcp_mock_server_regmap(srv)->svr_lifecycle_state);

    /* A rejected transition (HW_UNCONFIGURED -> RCP_CONFIGURED skips a
     * state -- not a modeled edge regardless of writer or idleness)
     * leaves both fields exactly where they already were -- the sync
     * call is unconditional but never introduces drift on a failure
     * path. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_ERR_INVALID_TRANSITION,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_RCP_CONFIGURED, &snap, writer, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, rcp_mock_server_state(srv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_LIFECYCLE_HW_UNCONFIGURED,
                           rcp_mock_server_regmap(srv)->svr_lifecycle_state);

    /* A successful transition updates both. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_mock_server_transition(srv, RCP_LIFECYCLE_HW_CONFIGURED, &snap, writer, true));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_CONFIGURED, rcp_mock_server_state(srv));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)RCP_LIFECYCLE_HW_CONFIGURED,
                           rcp_mock_server_regmap(srv)->svr_lifecycle_state);

    rcp_mock_server_destroy(srv);
}

/* TC18 §12.7 requires EP0 to be a fully addressable register space.
 * Deviation: the discovery read response is the only ACF_ABB path into
 * rcp_regmap_general_t and it carries exactly the leading
 * RCP_DISCOVERY_GENERAL_SLICE_LEN == 14 octets, absolute addresses
 * 0x0000..0x000D. Everything from 0x000E (svr_req_stream_max) onward is
 * zero-fill, asserted here against a map in which every one of those
 * registers holds a distinctive nonzero value. */
static void test_general_map_wire_reach_stops_after_0x000d(void)
{
    rcp_regmap_general_t map = populated_map();
    uint8_t              buf[0x31];

    TEST_ASSERT_EQUAL_UINT((size_t)14u, RCP_DISCOVERY_GENERAL_SLICE_LEN);
    read_general(&map, (uint8_t)sizeof(buf), buf);

    /* 0x0000 magic, 0x0004 svr_version, 0x0008 vendor_id, 0x000A
     * device_id, 0x000C svr_ep_count -- the readable part. */
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[0x00]);
    TEST_ASSERT_EQUAL_HEX8(0x05, buf[0x06]);
    TEST_ASSERT_EQUAL_HEX8(0x12, buf[0x08]);
    TEST_ASSERT_EQUAL_HEX8(0x56, buf[0x0A]);
    TEST_ASSERT_EQUAL_HEX8(0x09, buf[0x0D]);

    /* 0x000E..0x0030: every remaining Table 18 register reads back 0. */
    TEST_ASSERT_TRUE(span_is_zero(buf, 0x0E, 0x30));
}

/* REQ-RMAP-025 (TC18 §12.7.5 Table 18, access type R): a remote write to
 * the general static part must not take effect. lifecycle.h now models
 * the classification primitive itself (RCP_LIFECYCLE_FIELD_READ_ONLY,
 * proven unconditionally unwritable in every state by every writer --
 * see test_lifecycle.c's own test_read_only_never_writable_in_any_
 * state_by_any_writer()), distinguishing it from the three other kinds
 * (each writable in at least one state, asserted below for contrast).
 * Deviation that remains open: nothing in this codebase's wire dispatch
 * layer classifies Table 18's own fields as READ_ONLY and gates a write
 * attempt through it -- there IS no wire dispatch layer yet at all
 * (REQ-RMAP-024, still open). rcp_mock_server_regmap() therefore still
 * hands out a directly mutable pointer to the whole Table 18 block, with
 * nothing in this test double consulting the new classification -- a
 * write to vendor_id (0x0008, type R) simply lands, exactly as before. */
static void test_general_static_part_has_no_read_only_class(void)
{
    rcp_mock_server_t    *srv;
    rcp_regmap_general_t *map;

    /* As of the REQ-LIFECYCLE-026/035 fix, HW_GENERIC's HW_UNCONFIGURED
     * writability now requires writer.via_discovery_stream -- DISCOVERY_WRITER
     * isolates the state-only property being tested here (any writer that
     * IS the discovery claimant, not about the claim-consultation
     * plumbing itself, which is a caller composition -- see
     * discovery.h). FUNCTIONAL_W/_STAR's HW_CONFIGURED writability
     * requires authorization (REQ-LIFECYCLE-030/036) -- ROOT_WRITER
     * keeps these two assertions about "not read-only by state", not
     * about writer authorization, which test_functional_cfg_writable_
     * hw_configured_requires_authorization_or_discovery_stream()-style
     * tests elsewhere already cover directly. */
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_HW_GENERIC, DISCOVERY_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    map = rcp_mock_server_regmap(srv);
    TEST_ASSERT_NOT_NULL(map);
    map->vendor_id = 0xBEEFu;
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, rcp_mock_server_regmap(srv)->vendor_id);
    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-026 (TC18 §12.7.5 Table 18): svr_req_stream_max is an 8-bit R
 * register at absolute address 0x000E and svr_responder_streams_max an
 * 8-bit R register at 0x000F. rcp_regmap_general_t now carries both at
 * the correct width -- uint8_t, so a value neither register could hold
 * on the wire (e.g. 256) can no longer be constructed in the first
 * place, and svr_responder_streams_max exists at all. Still open (same
 * REQ-RMAP-024 wire-reachability boundary as every other Group 1 item):
 * 0x000E and 0x000F both fall past the discovery slice's own 0x000D
 * ceiling, already covered generically by
 * test_general_map_wire_reach_stops_after_0x000d()'s span_is_zero(buf,
 * 0x0E, 0x30) assertion -- not re-tested here. */
static void test_req_stream_max_and_responder_streams_max_are_now_correctly_sized(void)
{
    rcp_regmap_general_t map = populated_map();

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_req_stream_max));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_responder_streams_max));

    map.svr_req_stream_max = 0xFFu; /* the widest value an 8-bit
                                        register can hold, accepted */
    TEST_ASSERT_EQUAL_UINT8(0xFFu, map.svr_req_stream_max);

    map.svr_responder_streams_max = 0xFFu;
    TEST_ASSERT_EQUAL_UINT8(0xFFu, map.svr_responder_streams_max);

    /* The slice ends at 0x000D, so 0x000E (svr_req_stream_max's own
     * address) is the first still-unreachable one -- REQ-RMAP-024's own
     * boundary, generically covered by
     * test_general_map_wire_reach_stops_after_0x000d() too. */
    TEST_ASSERT_EQUAL_UINT((size_t)0x0Eu, RCP_DISCOVERY_GENERAL_SLICE_LEN);
}

/* REQ-RMAP-027 (TC18 §12.7.5 Table 18): two distinct 16-bit capacity
 * registers, both counted in 32-bit words: svr_responder_mem_size at
 * 0x0010 and svr_req_mem_size at 0x0012. rcp_regmap_general_t now
 * carries both, distinctly addressed and separately settable, replacing
 * the former undifferentiated 32-bit svr_memory_capacity that conflated
 * them into one unaddressed field. Still open (REQ-RMAP-024, same as
 * every other Group 1 item): both addresses fall past the discovery
 * slice's 0x000D ceiling, already covered generically by
 * test_general_map_wire_reach_stops_after_0x000d()'s span_is_zero(buf,
 * 0x0E, 0x30) assertion -- not re-tested here. */
static void test_responder_and_req_mem_size_are_now_distinctly_addressed(void)
{
    rcp_regmap_general_t map = populated_map();

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_responder_mem_size));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.svr_req_mem_size));

    /* Distinctly set and distinctly readable in-process -- the two
     * limits can now be told apart, unlike the former single field. */
    TEST_ASSERT_EQUAL_HEX16(0x1122u, map.svr_responder_mem_size);
    TEST_ASSERT_EQUAL_HEX16(0x3344u, map.svr_req_mem_size);

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_HEX16(0u, map.svr_responder_mem_size);
    TEST_ASSERT_EQUAL_HEX16(0u, map.svr_req_mem_size);
}

/* REQ-RMAP-028 (TC18 §12.7.5 Table 18): svr_sequencers_max is an 8-bit R
 * register at 0x0014 whose value 0 means "sequencer operation not
 * supported"; 1..n gives the number of available sequencer state
 * registers. rcp_regmap_general_t now carries this field at the correct
 * width, and mock.c's rcp_mock_server_set_sequencer_count() keeps it
 * synced with the actual rcp_sequencer_table_t.count it allocates --
 * request_sequencer.h's own rcp_sequencer_table_unsupported()
 * (table->count == 0) already implements the "0 means unsupported"
 * rule this register describes, so once synced, the register correctly
 * reflects that same rule by construction. Still open (REQ-RMAP-024,
 * same as every other Group 1 item): the address falls past the
 * discovery slice's 0x000D ceiling, already covered generically by
 * test_general_map_wire_reach_stops_after_0x000d(). */
static void test_sequencers_max_is_now_correctly_sized_and_synced_from_the_table(void)
{
    rcp_mock_server_t *srv;

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(((rcp_regmap_general_t *)0)->svr_sequencers_max));

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);

    /* A freshly-constructed server has no sequencers: the register
     * already reads 0 ("not supported"), matching
     * rcp_sequencer_table_unsupported()'s own verdict on the table it
     * was never given a count for. */
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_mock_server_regmap(srv)->svr_sequencers_max);
    TEST_ASSERT_TRUE(rcp_sequencer_table_unsupported(rcp_mock_server_sequencers(srv)));

    /* Setting a nonzero count updates both the table and the register in
     * lockstep. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 7u));
    TEST_ASSERT_EQUAL_UINT8(7u, rcp_mock_server_regmap(srv)->svr_sequencers_max);
    TEST_ASSERT_FALSE(rcp_sequencer_table_unsupported(rcp_mock_server_sequencers(srv)));

    /* Setting the count back to 0 re-establishes "not supported" in both
     * places -- the exact deviation the old pin demonstrated (7 and
     * "unsupported" being indistinguishable) is now impossible. */
    TEST_ASSERT_TRUE(rcp_mock_server_set_sequencer_count(srv, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, rcp_mock_server_regmap(srv)->svr_sequencers_max);
    TEST_ASSERT_TRUE(rcp_sequencer_table_unsupported(rcp_mock_server_sequencers(srv)));

    rcp_mock_server_destroy(srv);
}

/* REQ-RMAP-029 (TC18 §12.7.5 Table 18): svr_configuration_lock is an
 * 8-bit R register at 0x0015. rcp_regmap_general_t now carries this
 * field (content modeling only), zero-initializing to 0x00 ("permits
 * write access to R/W+ parameters" -- the correct unlocked default).
 * Proves the field exists, defaults correctly, and round-trips a
 * nonzero (locked) value in-process. Still open (REQ-RMAP-024, same as
 * every other Group 1 item): the address falls past the discovery
 * slice's 0x000D ceiling. */
static void test_configuration_lock_register_now_exists_and_defaults_unlocked(void)
{
    rcp_regmap_general_t map;

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_configuration_lock));
    TEST_ASSERT_EQUAL_HEX8(0x00u, map.svr_configuration_lock); /* unlocked default */

    map.svr_configuration_lock = 0x01u; /* any nonzero value means locked */
    TEST_ASSERT_EQUAL_HEX8(0x01u, map.svr_configuration_lock);
}

/* rcp_lifecycle_field_writable() still takes no lock input, so its
 * verdict for an R/W+ (FUNCTIONAL_W_STAR) field depends only on state
 * and writer authorization, never on the new svr_configuration_lock
 * byte -- asserted here by two differently-authorized writer contexts
 * (root-client-via-EP0 vs. owning-stream) producing the identical TRUE
 * answer in HW_CONFIGURED, where a real lock register would still have
 * to refuse regardless of which authorized path granted access. (As of
 * REQ-LIFECYCLE-030/036, an *unauthorized* writer is now correctly
 * refused here too -- that is the authorization gate working as
 * intended, not a lock register; see PLAIN_WRITER's own FALSE assertion
 * below, kept to document the distinction rather than silently
 * dropped.) Deliberately still open: REQ-RMAP-055 (issue #200 Group 3)
 * is this codebase's own already-identified "shared plumbing, implement
 * once" home for the W+ lockable-access-type primitive this register
 * drives -- see regmap.h's own svr_configuration_lock field comment for
 * the full reasoning. Not re-tested for wire-reachability here (already
 * covered generically by
 * test_general_map_wire_reach_stops_after_0x000d()). */
static void test_configuration_lock_not_yet_consulted_by_field_writable(void)
{
    const rcp_lifecycle_writer_ctx_t owning_stream_writer = {false, true, false, false};

    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  owning_stream_writer));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                   PLAIN_WRITER));
}

/* REQ-RMAP-030 (TC18 §12.7.5 Table 18): svr_implemented_options is an
 * 8-bit R register at 0x0016 with one bit per optional feature (a
 * compound&wait, b trigger requests, c chained requests, d time
 * synch/timed, e enhanced cancellation) -- verified directly against
 * the primary-source PDF (Table 18, page 51 of
 * OA_TC18_specification_v_0.5.1_RC.pdf). rcp_regmap_general_t's field
 * is now 8 bit and carries exactly these five independent bits,
 * replacing a prior 32-bit, six-bit, three-forced-pair design
 * (REQ-RMAP-004..008, now retired -- see test_regmap.c's own
 * retirement note) whose citation for the pairing rule (§12.9.1.1) was
 * found, on the same primary-source check, to say nothing about this
 * register at all: that section is entirely about handling multiple
 * ACF-type requests packed into one AVTPDU frame. The prior design also
 * had no bit at all for trigger or chained requests, even though this
 * codebase implements both -- this test proves that gap is closed too.
 * Still open (REQ-RMAP-024, same as every other Group 1 item): the
 * address falls past the discovery slice's 0x000D ceiling, already
 * covered generically by test_general_map_wire_reach_stops_after_0x000d(). */
static void test_implemented_options_now_matches_table_18_exactly(void)
{
    rcp_regmap_general_t map;

    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(map.svr_implemented_options));

    TEST_ASSERT_EQUAL_HEX8(0x01u, RCP_REGMAP_OPT_COMPOUND_WAIT); /* a */
    TEST_ASSERT_EQUAL_HEX8(0x02u, RCP_REGMAP_OPT_TRIGGER);       /* b -- previously unrepresentable */
    TEST_ASSERT_EQUAL_HEX8(0x04u, RCP_REGMAP_OPT_CHAINED);       /* c -- previously unrepresentable */
    TEST_ASSERT_EQUAL_HEX8(0x08u, RCP_REGMAP_OPT_TIME_SYNC);     /* d */
    TEST_ASSERT_EQUAL_HEX8(0x10u, RCP_REGMAP_OPT_ENH_CANCEL);    /* e */

    /* Every bit is independently settable -- no pairing invariant to
     * satisfy, unlike the retired design. */
    map.svr_implemented_options = RCP_REGMAP_OPT_TRIGGER;
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_OPT_TRIGGER, map.svr_implemented_options);

    map.svr_implemented_options = (uint8_t)(RCP_REGMAP_OPT_COMPOUND_WAIT | RCP_REGMAP_OPT_TRIGGER |
                                             RCP_REGMAP_OPT_CHAINED | RCP_REGMAP_OPT_TIME_SYNC |
                                             RCP_REGMAP_OPT_ENH_CANCEL);
    TEST_ASSERT_EQUAL_HEX8(0x1Fu, map.svr_implemented_options); /* all five, matching Table 18's own count */
}

/* TC18 §12.7.5 Table 18 reserves the 8-bit register at 0x0017 (must read
 * 0x00) and the 16-bit register at 0x0022. Both read back zero here --
 * but only because everything past 0x000D is undifferentiated zero-fill,
 * not because a reserved register is modelled. The distinguishing
 * assertion is that svr_req_stream_max at 0x000E, a REAL Table 18
 * register holding 0xAB in this map, reads back as zero too. A
 * conforming implementation would return 0xAB at 0x000E and a modelled
 * zero at 0x0017/0x0022. */
static void test_reserved_registers_are_indistinguishable_from_zero_fill(void)
{
    rcp_regmap_general_t map = populated_map();
    uint8_t              buf[0x24];

    TEST_ASSERT_EQUAL_UINT8(0xABu, map.svr_req_stream_max);
    read_general(&map, (uint8_t)sizeof(buf), buf);

    TEST_ASSERT_TRUE(span_is_zero(buf, 0x17, 0x17)); /* TC18 reserved, 8 bit */
    TEST_ASSERT_TRUE(span_is_zero(buf, 0x22, 0x23)); /* TC18 reserved, 16 bit */
    TEST_ASSERT_TRUE(span_is_zero(buf, 0x0E, 0x0E)); /* a real register: also 0 */
}

/* TC18 §12.7.5 Table 18: svr_io_pin_count is a 16-bit R register at
 * 0x0018 and is the authoritative extent of the §12.7.6 HW_config table;
 * svr_hw_cfg_ptr is a 16-bit R pointer at 0x001A. Deviation: c-RCP has no
 * svr_io_pin_count at all, and models the pointer as an
 * rcp_regmap_table_ref_t whose offset is 32 bit in this project's own
 * "register word" unit (not a 16-bit octet address) and which carries an
 * extra capacity member TC18 does not define for HW_config. */
static void test_io_pin_count_absent_and_hw_cfg_ptr_mis_shaped(void)
{
    rcp_regmap_general_t map = populated_map();
    uint8_t              buf[0x1C];

    TEST_ASSERT_EQUAL_UINT((size_t)4u, sizeof(map.hw_pin_map.offset));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.hw_pin_map.capacity));
    TEST_ASSERT_EQUAL_HEX32(0x00000040u, map.hw_pin_map.offset);
    TEST_ASSERT_EQUAL_HEX16(0x0008u, map.hw_pin_map.capacity);

    read_general(&map, (uint8_t)sizeof(buf), buf);
    TEST_ASSERT_TRUE(span_is_zero(buf, 0x18, 0x19)); /* svr_io_pin_count */
    TEST_ASSERT_TRUE(span_is_zero(buf, 0x1A, 0x1B)); /* svr_hw_cfg_ptr */
}

/* TC18 §12.7.5 Table 18 defines FOUR separate, non-adjacent registers for
 * the stream-configuration sub-tables: svr_request_stream_cfg_capacity
 * (8 bit, 0x001C), svr_response_stream_cfg_capacity (8 bit, 0x001D),
 * svr_request_stream_cfg_ptr (16 bit, 0x001E) and
 * svr_response_stream_cfg_ptr (16 bit, 0x0020). Deviation: c-RCP
 * collapses each pointer/capacity pair into one rcp_regmap_table_ref_t of
 * identical shape, so a 16-bit capacity stands where TC18 wants 8 bits,
 * and none of the four is bound to an address or encoded. */
static void test_stream_cfg_pointer_capacity_pairs_are_collapsed(void)
{
    rcp_regmap_general_t map = populated_map();
    uint8_t              buf[0x22];

    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_table_ref_t), sizeof(map.request_stream_cfg));
    TEST_ASSERT_EQUAL_UINT(sizeof(map.request_stream_cfg), sizeof(map.response_queue_cfg));

    /* A 16-bit capacity accepts 0x0100, which TC18's 8-bit
     * svr_request_stream_cfg_capacity at 0x001C cannot hold. */
    map.request_stream_cfg.capacity = 0x0100u;
    TEST_ASSERT_EQUAL_HEX16(0x0100u, map.request_stream_cfg.capacity);

    read_general(&map, (uint8_t)sizeof(buf), buf);
    TEST_ASSERT_TRUE(span_is_zero(buf, 0x1C, 0x21));
}

/* TC18 §12.7.5 Table 18: svr_ep_generic_cfg_ptr (16 bit, 0x0024) and
 * svr_ep_generic_cfg_capacity (16 bit, 0x0026, "length of the EP config
 * register section in BYTES"); svr_ep_bytebus_id_map_ptr (16 bit, 0x0028)
 * and svr_ep_bytebus_id_map_capacity (8 bit, 0x002A);
 * svr_ep_functional_cfg_ptr (16 bit, 0x002C) and svr_sequencer_state_ptr
 * (16 bit, 0x002E). Deviation: all six collapse into four identically
 * shaped rcp_regmap_table_ref_t members whose capacity is documented as
 * an ENTRY COUNT, not an octet length -- the exact opposite of 0x0026 --
 * and none is bound to an address. */
static void test_ep_cfg_and_bytebus_map_pointers_are_mis_shaped(void)
{
    rcp_regmap_general_t map = populated_map();
    uint8_t              buf[0x30];

    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_table_ref_t), sizeof(map.ep_generic_cfg));
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_table_ref_t), sizeof(map.ep_id_bus_map));
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_table_ref_t), sizeof(map.ep_functional_cfg));
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_table_ref_t), sizeof(map.sequencer_state));

    /* One capacity shape for all four, so an 8-bit entry count (0x002A)
     * and a 16-bit octet length (0x0026) are the same C field. */
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.ep_id_bus_map.capacity));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(map.ep_generic_cfg.capacity));

    read_general(&map, (uint8_t)sizeof(buf), buf);
    TEST_ASSERT_TRUE(span_is_zero(buf, 0x24, 0x2F));
}

/* TC18 §12.7.5 Table 18 (continued) and §12.7.11-§12.7.14 define four
 * further pointer/capacity register pairs -- network interface, physical
 * layer, time synch, security -- where a zero pointer is the defined
 * encoding for "subsystem not supported" and a section spans pointer..
 * pointer+capacity. Deviation: rcp_regmap_general_t declares exactly
 * seven sub-table refs and none of these four, so a capacity of 0
 * ("section empty") cannot be told from "unadvertised". Pinned by
 * rcp_regmap_general_init() zeroing exactly the seven that exist. */
static void test_four_optional_subsystem_pointer_pairs_are_absent(void)
{
    rcp_regmap_general_t map;

    memset(&map, 0xAA, sizeof(map));
    rcp_regmap_general_init(&map);

    TEST_ASSERT_EQUAL_HEX32(0u, map.hw_pin_map.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, map.hw_pin_map.capacity);
    TEST_ASSERT_EQUAL_HEX32(0u, map.request_stream_cfg.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, map.request_stream_cfg.capacity);
    TEST_ASSERT_EQUAL_HEX32(0u, map.response_queue_cfg.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, map.response_queue_cfg.capacity);
    TEST_ASSERT_EQUAL_HEX32(0u, map.ep_generic_cfg.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, map.ep_generic_cfg.capacity);
    TEST_ASSERT_EQUAL_HEX32(0u, map.ep_functional_cfg.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, map.ep_functional_cfg.capacity);
    TEST_ASSERT_EQUAL_HEX32(0u, map.ep_id_bus_map.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, map.ep_id_bus_map.capacity);
    TEST_ASSERT_EQUAL_HEX32(0u, map.sequencer_state.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, map.sequencer_state.capacity);
    TEST_ASSERT_EQUAL_HEX16(RCP_REGMAP_NO_ROOT_CLIENT, map.svr_root_client_index);
}

/* ── §12.7.6 Tables 19-21: HW pin mapping ──────────────────────────────────── */

/* TC18 §12.7.6 requires an actual HW_config table holding, per used
 * physical IO-pin, its endpoint-signal assignment and pin properties.
 * Deviation: c-RCP models a single row's shape and a pointer/capacity
 * descriptor but keeps no storage, and rcp_config_apply_to_mock()
 * deliberately DISCARDS the parsed hw_pin_map -- asserted here: the
 * manifest carries one pin, the server's hw_pin_map descriptor stays
 * {0, 0} after applying it. */
static void test_hw_config_table_has_no_server_side_storage(void)
{
    static const char json[] =
        "{\"hw_pin_map\":[{\"hw_ep_nr\":4,\"hw_ep_pin_nr\":3,"
        "\"pin_property\":[\"output\",\"pull_up\"]}]}";
    rcp_config_manifest_t m;
    rcp_mock_server_t    *srv;

    TEST_ASSERT_EQUAL_INT(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, m.hw_pin_map_len);
    TEST_ASSERT_EQUAL_HEX8(4, m.hw_pin_map[0].hw_ep_nr);
    TEST_ASSERT_EQUAL_HEX8(3, m.hw_pin_map[0].hw_ep_pin_nr);

    srv = rcp_mock_server_new();
    TEST_ASSERT_NOT_NULL(srv);
    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_apply_to_mock(&m, srv));

    /* The parsed pin never reaches the server: no table exists to hold it. */
    TEST_ASSERT_EQUAL_HEX32(0u, rcp_mock_server_regmap(srv)->hw_pin_map.offset);
    TEST_ASSERT_EQUAL_HEX16(0u, rcp_mock_server_regmap(srv)->hw_pin_map.capacity);

    rcp_mock_server_destroy(srv);
    rcp_config_manifest_free(&m);
}

/* TC18 §12.7.6 Table 19 lays HW_config out as three consecutive 8-bit
 * R/W* registers per IO pin (hw_ep_nr, hw_ep_pin_nr, hw_pin_type), so IO
 * pin N begins at relative address 3*N, and R/W* means writable only
 * while HW_unconfigured. c-RCP's row carries the three 8-bit fields
 * (asserted) but spells the third pin_property, defines no 3-octet stride
 * and no encode/decode. Worse, the only setter that touches a pin's
 * property classifies it FUNCTIONAL_W, whose writability window is the
 * exact INVERSE of R/W*: refused in HW_UNCONFIGURED, granted in
 * HW_CONFIGURED. */
static void test_hw_config_row_stride_absent_and_access_class_inverted(void)
{
    rcp_regmap_hw_pin_map_entry_t entry;
    rcp_ep_gpio_functional_cfg_t  cfg;

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(entry.hw_ep_nr));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(entry.hw_ep_pin_nr));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(entry.pin_property));

    /* Table 19's own access class, R/W* == HW_unconfigured-only, is what
     * RCP_LIFECYCLE_FIELD_HW_GENERIC expresses (DISCOVERY_WRITER, not
     * ROOT_WRITER, since the REQ-LIFECYCLE-026/035 fix restricts
     * HW_UNCONFIGURED writability to the discovery claimant -- no root
     * client can exist yet this early in bring-up): */
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_HW_GENERIC, DISCOVERY_WRITER));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_HW_GENERIC, ROOT_WRITER));

    /* ...but the actual pin-property setter uses FUNCTIONAL_W instead. */
    rcp_ep_gpio_functional_cfg_init(&cfg);
    TEST_ASSERT_FALSE(rcp_ep_gpio_set_pin_property(&cfg, 0, RCP_REGMAP_PIN_PROP_OUTPUT,
                                                   RCP_LIFECYCLE_HW_UNCONFIGURED, ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_ep_gpio_set_pin_property(&cfg, 0, RCP_REGMAP_PIN_PROP_OUTPUT,
                                                  RCP_LIFECYCLE_HW_CONFIGURED, ROOT_WRITER));
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_OUTPUT, cfg.pins[0].pin_property);
}

/* TC18 §12.7.6 Table 20 packs hw_pin_type as four sub-fields: pull-up
 * mode at bits 1:0 (00b float, 01b pull down, 10b pull up), output stage
 * at bits 3:2 (00b input, 01b open drain, 10b open source, 11b push
 * pull), drive strength at bits 5:4, reserved bit 6 reading 0, and
 * Schmitt-trigger enable at bit 7. Deviation: c-RCP defines six
 * independent one-hot flags at incompatible positions, with no open
 * source, no drive strength, no Schmitt trigger and no float encoding,
 * plus an ACTIVE_LOW property TC18 does not define. c-RCP's PULL_UP is
 * bit 3, which a conforming PHY decodes as output-stage value 10b. */
static void test_pin_type_bit_layout_contradicts_table_20(void)
{
    uint8_t all;

    TEST_ASSERT_EQUAL_HEX8(0x01, RCP_REGMAP_PIN_PROP_OUTPUT);
    TEST_ASSERT_EQUAL_HEX8(0x02, RCP_REGMAP_PIN_PROP_INPUT);
    TEST_ASSERT_EQUAL_HEX8(0x04, RCP_REGMAP_PIN_PROP_OPEN_DRAIN);
    TEST_ASSERT_EQUAL_HEX8(0x08, RCP_REGMAP_PIN_PROP_PULL_UP);
    TEST_ASSERT_EQUAL_HEX8(0x10, RCP_REGMAP_PIN_PROP_PULL_DOWN);
    TEST_ASSERT_EQUAL_HEX8(0x20, RCP_REGMAP_PIN_PROP_ACTIVE_LOW);

    /* Nothing is defined at Table 20's reserved bit 6 or its
     * Schmitt-trigger bit 7. */
    all = (uint8_t)(RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_INPUT |
                    RCP_REGMAP_PIN_PROP_OPEN_DRAIN | RCP_REGMAP_PIN_PROP_PULL_UP |
                    RCP_REGMAP_PIN_PROP_PULL_DOWN | RCP_REGMAP_PIN_PROP_ACTIVE_LOW);
    TEST_ASSERT_EQUAL_HEX8(0x3F, all);
    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(all & 0xC0u));

    /* PULL_UP lands inside Table 20's output-stage sub-field (bits 3:2). */
    TEST_ASSERT_EQUAL_HEX8(0x02, (uint8_t)((RCP_REGMAP_PIN_PROP_PULL_UP >> 2) & 0x03u));
}

/* TC18 §12.7.6: ALL OUTPUTS ARE ALWAYS ALSO AN INPUT -- output and input
 * are not mutually exclusive pin states, which is what makes read-back of
 * an output pin's actual level (short/stuck-driver detection) possible.
 * Deviation: c-RCP models OUTPUT and INPUT as two independent flags and
 * rcp_ep_gpio_apply_reconfig() TOGGLES a pin from one to the other, so a
 * pin configured as an output ceases to be readable as an input, and back
 * again. */
static void test_output_pin_loses_its_input_capability(void)
{
    uint8_t pins[RCP_EP_GPIO_MAX_PINS];

    memset(pins, 0, sizeof(pins));
    pins[0] = (uint8_t)(RCP_REGMAP_PIN_PROP_OUTPUT | RCP_REGMAP_PIN_PROP_PULL_UP);

    rcp_ep_gpio_apply_reconfig(pins, 0x00000001u);
    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_OUTPUT));
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_INPUT,
                           (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_INPUT));

    rcp_ep_gpio_apply_reconfig(pins, 0x00000001u);
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_OUTPUT,
                           (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_OUTPUT));
    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_INPUT));
    /* the unrelated pull-up bit survives, so this is a genuine toggle */
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_PIN_PROP_PULL_UP,
                           (uint8_t)(pins[0] & RCP_REGMAP_PIN_PROP_PULL_UP));
}

/* TC18 §12.7.6 Table 21 enumerates EP_Signal_Nr for EVERY endpoint type
 * (UART 0..3, LIN 0..2, PWM_OUT 0..1, PWM_IN 0, ADC 0, DAC 0, CAN 0 RXD /
 * 1 TXD, ISELED 0..1, MDIO 0..1 besides GPIO/SPI/I2C), and restarts the
 * numbering at 0 for each type. Deviation: c-RCP enumerates only GPIO,
 * SPI and I2C, and numbers them in ONE flat global sequence, so SPI_CLK
 * (TC18 EP_Signal_Nr 0) is 32 -- outside the SPI type's whole 0..8 range
 * -- and I2C_SCL (TC18 0) is 41. No converter between the two numbering
 * schemes exists, and CAN's counter-intuitive RXD=0/TXD=1 is unrecorded. */
static void test_named_signal_index_is_global_and_covers_three_types(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)RCP_REGMAP_SIGNAL_GPIO0);
    TEST_ASSERT_EQUAL_INT(31, (int)RCP_REGMAP_SIGNAL_GPIO31);
    TEST_ASSERT_EQUAL_INT(32, (int)RCP_REGMAP_SIGNAL_SPI_CLK);
    TEST_ASSERT_EQUAL_INT(41, (int)RCP_REGMAP_SIGNAL_I2C_SCL);
    TEST_ASSERT_EQUAL_INT(42, (int)RCP_REGMAP_SIGNAL_I2C_SDA);
    TEST_ASSERT_EQUAL_INT(43, (int)RCP_REGMAP_SIGNAL_COUNT);

    /* Per-type numbering would put both of these at EP_Signal_Nr 0; each
     * is instead outside its own type's whole signal range (SPI 0..8,
     * I2C 0..1). */
    TEST_ASSERT_TRUE((int)RCP_REGMAP_SIGNAL_SPI_CLK > 8);
    TEST_ASSERT_TRUE((int)RCP_REGMAP_SIGNAL_I2C_SCL > 1);

    /* The enumeration stops at I2C_SDA: UART_TX, LIN_TXD, PWM phases,
     * ADC_IN, DAC_OUT, CAN RXD/TXD, ISELED ISP_P/N and MDIO MDC/MDIO all
     * fall through to "unknown". */
    TEST_ASSERT_EQUAL_STRING("I2C_SDA", rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_I2C_SDA));
    TEST_ASSERT_EQUAL_STRING("unknown",
                             rcp_regmap_named_signal_string(RCP_REGMAP_SIGNAL_COUNT));
    TEST_ASSERT_EQUAL_STRING("unknown", rcp_regmap_named_signal_string((rcp_regmap_named_signal_t)44));
}

/* ── §12.7.7 Table 22: request-stream configuration ────────────────────────── */

/* TC18 §12.7.7 Table 22 defines rx_secure_channel_index (0x000C, 8 bit,
 * default 0 = MACsec uncontrolled port), rx_ack_stream_index (0x0010,
 * 8 bit, 0 = send no acknowledge) and rx_resp_stream_index (0x0011,
 * 8 bit, POWER-ON DEFAULT 1 so a discovery request can be answered before
 * any configuration is written). Deviation: none of the three is
 * modelled, and rcp_regmap_request_stream_cfg_init() zeroes the struct
 * byte for byte -- asserted here -- so there is no field that could carry
 * the mandated default of 1. */
static void test_request_stream_cfg_lacks_channel_and_stream_indices(void)
{
    rcp_regmap_request_stream_cfg_t cfg;
    const uint8_t                  *raw = (const uint8_t *)&cfg;
    size_t                          i;
    bool                            any_nonzero = false;

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_request_stream_cfg_init(&cfg);

    TEST_ASSERT_FALSE(cfg.configured);
    TEST_ASSERT_EQUAL_HEX64(0u, cfg.rx_stream_id);
    TEST_ASSERT_FALSE(cfg.rx_enforce_e2e);
    TEST_ASSERT_FALSE(cfg.rx_wd_enable);
    TEST_ASSERT_EQUAL_UINT32(0u, cfg.rx_wd_timeout_ms);
    TEST_ASSERT_EQUAL_UINT((size_t)0u, cfg.rx_stream_max_request_size);

    for (i = 0; i < sizeof(cfg); i++) {
        if (raw[i] != 0u) any_nonzero = true;
    }
    /* Not one octet survives at 1: no rx_resp_stream_index default. */
    TEST_ASSERT_FALSE(any_nonzero);
}

/* TC18 §12.7.7 Table 22: rx_wd_timeout_intervall is a 16-bit R/W*
 * register at relative 0x000A expressed in CLOCK TICS. Deviation: c-RCP
 * stores a 32-bit MILLISECOND value, applies no tick/millisecond
 * conversion at the write boundary, and rejects nothing above 0xFFFF --
 * asserted here by 0x10000 ms being accepted and honoured exactly as a
 * 65536 ms threshold, a value the specified register cannot even hold. */
static void test_watchdog_timeout_width_and_unit_deviate(void)
{
    rcp_regmap_request_stream_cfg_t cfg;
    rcp_e2e_wd_result_t             r;

    rcp_regmap_request_stream_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT((size_t)4u, sizeof(cfg.rx_wd_timeout_ms));

    cfg.rx_wd_enable      = true;
    cfg.rx_wd_timeout_ms  = 0x00010000u; /* 65536 -- past a 16-bit register */
    TEST_ASSERT_EQUAL_UINT32(0x00010000u, cfg.rx_wd_timeout_ms);

    r = rcp_e2e_wd_evaluate(cfg.rx_wd_enable, cfg.rx_wd_timeout_ms, false, false, 65535u);
    TEST_ASSERT_FALSE(r.overflowed);
    r = rcp_e2e_wd_evaluate(cfg.rx_wd_enable, cfg.rx_wd_timeout_ms, false, false, 65536u);
    TEST_ASSERT_TRUE(r.overflowed);
}

/* TC18 §12.7.7 Table 22's own legend (TC18.txt:2929-2931): "This
 * configuration table can only be changed in the life-cycle states
 * HW_UNCONFIGURED and HW_CONFIGURED. In RCP_CONFIGURED ... this is
 * read-only. (As indicated by W*)" -- every R/W* field is writable in
 * BOTH HW_UNCONFIGURED and HW_CONFIGURED and read-only only once
 * RCP_CONFIGURED is reached. Fixed: rcp_lifecycle_field_writable() now
 * matches this for all three states -- HW_UNCONFIGURED unconditionally
 * (no association/root-client concept exists that early), HW_CONFIGURED
 * now additionally subject to REQ-LIFECYCLE-030/036's authorization gate
 * (ROOT_WRITER used below rather than PLAIN_WRITER, since this test's own
 * point is the per-state W-star-vs-read-only shape, not writer authorization --
 * that is covered directly by test_functional_w_hw_configured_requires_
 * authorization_or_discovery_stream() in test_lifecycle.c). (No caller
 * yet classifies the request-stream table as FUNCTIONAL_W_STAR, so this
 * primitive being correct doesn't by itself mean Table 22 is wired up
 * end to end -- that's a separate, still-open gap.) */
static void test_table22_w_star_writable_in_both_pre_rcp_configured_states(void)
{
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  PLAIN_WRITER));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  ROOT_WRITER));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                   ROOT_WRITER));
}

/* ── §12.7.8 Table 23: EP_ID_config ────────────────────────────────────────── */

/* TC18 §12.7.8 Table 23 makes one EP_ID_config row a TRIPLE --
 * Request_Stream_Index (row offset 0x0000, 8 bit), EP_Nr (0x0001, 8 bit),
 * BBID (0x0002, 16 bit) -- so the same byte_bus_id may reach different
 * endpoints on different request streams, and a Request_Stream_Index of 0
 * TERMINATES the table. Deviation: c-RCP's row is a (ep_id, byte_bus_id)
 * pair with no stream index, so neither the per-stream mapping nor the
 * sentinel is expressible: a trailing all-zero row is read as a real
 * mapping of BBID 0 to EP0 and, being non-ascending, drags the whole
 * table's diagnostic to false. */
static void test_ep_id_row_lacks_request_stream_index_and_sentinel(void)
{
    rcp_regmap_ep_id_map_entry_t rows[3];

    TEST_ASSERT_EQUAL_UINT((size_t)2u, sizeof(rows[0].ep_id));
    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(rows[0].byte_bus_id));

    rows[0].ep_id = 5u; rows[0].byte_bus_id = 1u;
    rows[1].ep_id = 6u; rows[1].byte_bus_id = 2u;
    /* TC18's end-of-table sentinel row (Request_Stream_Index == 0). */
    rows[2].ep_id = 0u; rows[2].byte_bus_id = 0u;

    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(rows, 2u));
    /* No sentinel convention: the third row counts as a real mapping. */
    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(rows, 3u));
}

/* TC18 §12.7.8 requires the table to be ascending in the COMPOSITE key
 * (Request_Stream_Index, BBID), so a table that restarts its BBID run at
 * each new stream is correctly ordered. Deviation: c-RCP's diagnostic
 * checks strictly increasing BBID alone and is structurally unable to
 * consider a stream index -- so a per-stream-ascending table (BBIDs
 * 1,2 then 1,2 again on the next stream) is reported NON-ascending, the
 * wrong answer. */
static void test_ep_id_ordering_ignores_request_stream_index(void)
{
    rcp_regmap_ep_id_map_entry_t rows[4];

    /* stream 1: BBID 1,2 -- stream 2: BBID 1,2. Ascending per TC18. */
    rows[0].ep_id = 10u; rows[0].byte_bus_id = 1u;
    rows[1].ep_id = 11u; rows[1].byte_bus_id = 2u;
    rows[2].ep_id = 20u; rows[2].byte_bus_id = 1u;
    rows[3].ep_id = 21u; rows[3].byte_bus_id = 2u;

    TEST_ASSERT_FALSE(rcp_regmap_ep_id_map_is_ascending(rows, 4u));
}

/* TC18 §12.7.8 recommends, for safety reasons, that an endpoint be mapped
 * to at most one RC Client at a time, and that endpoints sharing a
 * byte_bus_id within one request stream be of the same ep_type.
 * Deviation: c-RCP has no diagnostic for either condition. The only
 * read-only diagnostic it does have reports both of these tables as
 * perfectly fine: one EP_Nr reached from two rows, and two rows of
 * different endpoints sharing a bus, both come back "ascending". */
static void test_no_diagnostic_for_multi_client_or_heterogeneous_type(void)
{
    rcp_regmap_ep_id_map_entry_t shared_ep[2];
    rcp_regmap_ep_id_map_entry_t shared_bus[2];

    /* One endpoint reachable from two rows (two clients, no warning). */
    shared_ep[0].ep_id = 7u; shared_ep[0].byte_bus_id = 1u;
    shared_ep[1].ep_id = 7u; shared_ep[1].byte_bus_id = 2u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(shared_ep, 2u));

    /* Two different endpoints; nothing here carries an ep_type at all, so
     * a heterogeneous-type multicast group cannot even be detected. */
    shared_bus[0].ep_id = 8u; shared_bus[0].byte_bus_id = 3u;
    shared_bus[1].ep_id = 9u; shared_bus[1].byte_bus_id = 4u;
    TEST_ASSERT_TRUE(rcp_regmap_ep_id_map_is_ascending(shared_bus, 2u));
    TEST_ASSERT_EQUAL_UINT(sizeof(rcp_regmap_ep_id_map_entry_t), sizeof(shared_bus[0]));
}

/* TC18 §12.7.8 Table 23 carries BBID in a 16-bit register holding an
 * 11-bit byte_bus_id, and the ACF byte_message_info header transports
 * byte_bus_id[10:8] in octet 2. Deviation: rcp_byte_bus_id_t is uint8_t,
 * so 0x100..0x7FF are unrepresentable: the value truncates on assignment
 * and a frame legitimately carrying one is REJECTED on decode rather than
 * parsed. Only 256 of the protocol's 2048 endpoints per stream are
 * reachable. */
static void test_byte_bus_id_is_eight_bits_wide(void)
{
    rcp_acf_byte_message_info_t hdr;
    uint8_t                     raw[8];

    TEST_ASSERT_EQUAL_UINT((size_t)1u, sizeof(rcp_byte_bus_id_t));
    TEST_ASSERT_EQUAL_HEX16(0x00FFu, (uint16_t)(rcp_byte_bus_id_t)0x07FFu);

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id = 0xFFu;
    hdr.op          = (uint8_t)RCP_ACF_OP_WRITE;
    rcp_acf_pack_header(raw, RCP_ACF_MSG_TYPE_ABB, 2u, &hdr);
    TEST_ASSERT_EQUAL_HEX8(0x00, (uint8_t)(raw[2] & 0x07u)); /* [10:8] always 0 */
    TEST_ASSERT_EQUAL_HEX8(0xFF, raw[3]);

    /* Hand-craft byte_bus_id 0x7FF and decode it: refused, not parsed. */
    raw[2] = (uint8_t)(raw[2] | 0x07u);
    TEST_ASSERT_EQUAL(RCP_ACF_ERR_BUS_ID_OVERFLOW, rcp_acf_unpack_header(raw, &hdr));
}

/* TC18 §12.7.8/§12.7.9 mark EP_ID_config rows and the Table 24
 * STREAM_UID/flush_on_count/Flush_time registers R/W+ -- explicitly
 * LOCKABLE by the configuring instance, independently of the lifecycle
 * state that governs W and W*. Deviation: rcp_lifecycle_field_kind_t
 * defines exactly three kinds (0 HW_GENERIC, 1 FUNCTIONAL_W,
 * 2 FUNCTIONAL_W_STAR) and rcp_lifecycle_field_writable() takes no lock
 * input; the value 3, where a W+ kind would sit, is unwritable in every
 * state -- an unrecognized kind, not a lockable one. */
static void test_no_lockable_w_plus_field_kind(void)
{
    const rcp_lifecycle_field_kind_t w_plus = (rcp_lifecycle_field_kind_t)3;

    TEST_ASSERT_EQUAL_INT(0, (int)RCP_LIFECYCLE_FIELD_HW_GENERIC);
    TEST_ASSERT_EQUAL_INT(1, (int)RCP_LIFECYCLE_FIELD_FUNCTIONAL_W);
    TEST_ASSERT_EQUAL_INT(2, (int)RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR);

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED, w_plus,
                                                   ROOT_WRITER));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED, w_plus,
                                                   ROOT_WRITER));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED, w_plus,
                                                   ROOT_WRITER));
}

/* ── §12.7.9 Table 24: response/acknowledge queues ─────────────────────────── */

/* REQ-RMAP-060 (TC18 §12.7.9 Table 24, relative address 0x0000, 16 bit,
 * R/W+): STREAM_UID supplies bits [63:48] of the stream_id a response
 * queue transmits on. rcp_regmap_response_queue_cfg_t.stream_uid now
 * carries that register directly, and
 * rcp_regmap_response_queue_stream_id() combines it with the interface's
 * own mac exactly as rcp_stream_id_make() would -- a queue now has an
 * identity rx_ack_stream_index/rx_resp_stream_index (Table 22) can point
 * at. */
static void test_response_queue_stream_id_is_configurable(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_stream_id_t                 sid;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.stream_uid);

    cfg.stream_uid = 0x1234u;
    sid            = rcp_regmap_response_queue_stream_id(&cfg, SERVER_MAC);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, sid.unique_id);
    TEST_ASSERT_EQUAL_MEMORY(SERVER_MAC, sid.mac, sizeof(SERVER_MAC));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(sid, rcp_stream_id_make(SERVER_MAC, 0x1234u)));
}

/* REQ-RMAP-059 (TC18 §12.7.9 Table 24, relative address 0x0004, 16 bit,
 * R/W*): a response/acknowledge queue's transmit-memory reservation, in
 * 32-bit words, and the real storage behind it. regmap.h's
 * rcp_regmap_response_queue_cfg_t now carries the queue_size register
 * itself; respqueue.h's rcp_respqueue_t is the actual queue this
 * codebase was missing entirely before this fix -- distinct from
 * server.h's ep_enable pre-load queue (which holds inbound requests, not
 * outbound responses). rcp_respqueue_init() takes the configured
 * capacity already converted from quadlets to octets (queue_size x 4),
 * matching rcp_acf_reg_write_len()'s own "caller supplies already-
 * classified units" convention. */
static void test_response_queue_size_register_and_storage_now_exist(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    const uint8_t                   frame[4] = {1, 2, 3, 4};

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.queue_size);

    cfg.queue_size = 2u; /* 2 quadlets = 8 octets */
    rcp_respqueue_init(&q, (size_t)cfg.queue_size * 4u, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT(8u, rcp_respqueue_octets(&q));
    /* A third push would exceed the 8-octet reservation: refused. */
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, frame, sizeof(frame)));

    rcp_respqueue_destroy(&q);
}

/* REQ-RMAP-061/062 (TC18 §12.7.9): "The RC Server shall not transmit an
 * AVTPDU longer than the configured Max_AVTPDUsize" and "fragmentation
 * as supported in ACF_ABB and ACF_GBB by the ms-bit will be performed"
 * for a message that would otherwise exceed it. respqueue.h's
 * rcp_respqueue_t now enforces the per-message ceiling directly
 * (REQ-RMAP-061's transmit-enforcement half), and
 * rcp_respqueue_max_fragment_payload() gives a caller the budget to feed
 * fragment.h's existing rcp_fragment_plan()/_plan_count() so an
 * oversized single message is split before it is ever pushed
 * (REQ-RMAP-062, closed). REQ-RMAP-061's other two halves -- rejecting a
 * configured Max_AVTPDUsize inconsistent with the network's own MTU, and
 * exposing the value in the 14-octet discovery slice -- are still open
 * (neither is a per-message queue concern; deferred to a later batch). */
static void test_max_avtpdu_size_is_now_enforced_and_feeds_fragmentation(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    uint8_t                         ok_frame[16]  = {0};
    uint8_t                         over_frame[17] = {0};
    size_t                          budget;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.max_avtpdu_size);

    cfg.max_avtpdu_size = 4u; /* 4 quadlets = 16 octets */
    rcp_respqueue_init(&q, 0, (size_t)cfg.max_avtpdu_size * 4u);
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, ok_frame, sizeof(ok_frame)));
    TEST_ASSERT_FALSE(rcp_respqueue_push(&q, over_frame, sizeof(over_frame)));
    rcp_respqueue_destroy(&q);

    /* A caller with a payload too large for one AVTPDU derives its
     * fragment.h budget from the same max_avtpdu_size, then hands it to
     * rcp_fragment_plan_count()/_plan() -- not modeled again here, since
     * fragment.h's own tests already pin that mechanism's own behavior;
     * this only confirms the budget itself is correctly derived. */
    budget = rcp_respqueue_max_fragment_payload((size_t)cfg.max_avtpdu_size * 4u,
                                                RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT((size_t)5u, budget); /* 16 - 8 - 3(pad) */

    /* REQ-RMAP-061's other two halves -- MTU-consistency rejection and
     * discovery-slice exposure -- remain open. */
    TEST_ASSERT_EQUAL_UINT((size_t)14u, RCP_DISCOVERY_GENERAL_SLICE_LEN);
}

/* REQ-RMAP-063 (TC18 §12.7.9 Table 24, relative address 0x0006, 16 bit,
 * R/W+, default 1, legal range 1..queue_size): "Once a queue is filled
 * with an amount of quadlets that is equal or larger than given by
 * flush_on_count, the transmission of one or multiple AVTPDUs shall be
 * initiated... only as much as fitting to the MAX_AVTPDUsize ACF_types
 * will be included in a generated AVTPDU... packed in a fitting number
 * of AVTPDUs." respqueue.h's rcp_respqueue_should_flush() implements
 * the trigger; rcp_respqueue_plan_batch() implements the packing --
 * both operate on the queue's own state alone, needing no clock. */
static void test_flush_on_count_trigger_and_avtpdu_packing(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    uint8_t                         frame[5] = {0};
    rcp_bytes_t                     out;
    size_t                          i;

    rcp_regmap_response_queue_cfg_init(&cfg);
    cfg.flush_on_count = 2u; /* 2 quadlets = 8 octets */
    rcp_respqueue_init(&q, 0, 0);

    TEST_ASSERT_FALSE(rcp_respqueue_should_flush(&q, (size_t)cfg.flush_on_count * 4u));

    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush(&q, (size_t)cfg.flush_on_count * 4u));

    /* The push that crosses the threshold (5 + 5 = 10 >= 8 octets)
     * itself triggers the flush -- "including the one which was
     * exceeding the Flush_on_Count value." */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush(&q, (size_t)cfg.flush_on_count * 4u));

    /* Packing: a third, small entry plus a Max_AVTPDUsize of 12 octets
     * means the first AVTPDU packs 2 entries (10 octets), the second
     * AVTPDU packs the last one alone. */
    TEST_ASSERT_TRUE(rcp_respqueue_push(&q, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_UINT((size_t)2u, rcp_respqueue_plan_batch(&q, 12u));
    for (i = 0; i < 2u; i++) {
        TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
        rcp_bytes_free(&out);
    }
    TEST_ASSERT_EQUAL_UINT((size_t)1u, rcp_respqueue_plan_batch(&q, 12u));
    TEST_ASSERT_TRUE(rcp_respqueue_pop(&q, &out));
    rcp_bytes_free(&out);
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_plan_batch(&q, 12u));

    rcp_respqueue_destroy(&q);
}

/* REQ-RMAP-064/065 (TC18 §12.7.9): Flush_time (0x0008, microseconds)
 * forces transmission once that interval elapses since the queue's last
 * transmission, independently of flush_on_count, and on expiry an EMPTY
 * queue still emits a heartbeat AVTPDU so a client can see the server is
 * alive. respqueue.h's rcp_respqueue_should_flush_by_time() implements
 * the trigger (REQ-RMAP-064, closed) and deliberately fires the same way
 * whether the queue is empty or not; combined with
 * rcp_respqueue_plan_batch() reporting 0 for an empty queue and
 * avtp.h's rcp_avtp_encode_ntscf() already accepting payload_len == 0,
 * the empty heartbeat AVTPDU is fully constructible (REQ-RMAP-065, the
 * primitive-composition half). What remains open for REQ-RMAP-065 is
 * exactly what REQ-SRV-017 (server.h) already states as this library's
 * own scope boundary: actually SCHEDULING this composition against a
 * real clock and driving a transport with it is an integrator concern,
 * not this protocol library's -- so REQ-RMAP-065 stays catalogued
 * "partial", not "implemented". */
static void test_flush_time_trigger_and_empty_heartbeat_are_composable(void)
{
    rcp_regmap_response_queue_cfg_t cfg;
    rcp_respqueue_t                 q;
    rcp_avtp_ntscf_header_t         hdr;
    rcp_bytes_t                     heartbeat;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(0u, cfg.flush_time_us);

    cfg.flush_time_us = 1000u; /* 1000 us */
    TEST_ASSERT_EQUAL_UINT32(1000u, cfg.flush_time_us);

    /* Below the interval: no trigger. At/past it: triggers, independently
     * of flush_on_count (no push()/should_flush() call anywhere here). */
    TEST_ASSERT_FALSE(rcp_respqueue_should_flush_by_time(999u, (uint64_t)cfg.flush_time_us));
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(1000u, (uint64_t)cfg.flush_time_us));

    /* An EMPTY queue still trips the trigger -- REQ-RMAP-065's own
     * precondition for the heartbeat case. */
    rcp_respqueue_init(&q, 0, 0);
    TEST_ASSERT_TRUE(rcp_respqueue_should_flush_by_time(1000u, (uint64_t)cfg.flush_time_us));
    TEST_ASSERT_EQUAL_UINT(0u, rcp_respqueue_plan_batch(&q, 0u));

    /* Once triggered on an empty queue, the caller-composed heartbeat is
     * exactly a zero-payload NTSCF AVTPDU -- the *only* header format an
     * RC Server itself ever sends (avtp.h) -- proving the composition
     * this codebase's tree makes available end to end. */
    hdr.sv = 1;
    hdr.version = 0;
    hdr.sequence_num = 0;
    hdr.stream_id = rcp_stream_id_make(SERVER_MAC, 0x0001);
    heartbeat = rcp_avtp_encode_ntscf(&hdr, NULL, 0);
    TEST_ASSERT_NOT_NULL(heartbeat.data);
    TEST_ASSERT_EQUAL_UINT(RCP_AVTP_NTSCF_HEADER_LEN, heartbeat.len);
    rcp_bytes_free(&heartbeat);

    rcp_respqueue_destroy(&q);

    /* Liveness on the RECEIVE side (rcp_deadline_config_t) is the mirror
     * image of this obligation and is unaffected by it -- still real,
     * still separate machinery. */
    TEST_ASSERT_EQUAL_UINT64(50u, rcp_deadline_default_config().default_deadline_ms);
}

/* REQ-RMAP-062 (TC18 §12.7.9): a single ACF message that would push an
 * AVTPDU past the queue's Max_AVTPDUsize must be fragmented via the ms
 * bit, i.e. rcp_fragment_plan() must be driven from
 * rcp_regmap_response_queue_cfg_t.max_avtpdu_size. The register is
 * expressed in QUADLETS, not octets -- feeding it into
 * rcp_fragment_plan_count() raw plans against a quarter of the real
 * budget (13 fragments for a 100-octet payload, rather than the correct
 * answer), exactly the mistake this test used to demonstrate as an open
 * deviation. respqueue.h's rcp_respqueue_max_fragment_payload() now
 * closes it: a caller converts quadlets to octets itself
 * (max_avtpdu_size x 4, this codebase's own "caller supplies already-
 * classified units" convention) and gets back a budget that already
 * reserves the fixed ACF header and worst-case pad, ready to hand
 * straight to fragment.h. */
static void test_transmit_fragmentation_now_uses_the_correct_octet_budget(void)
{
    rcp_regmap_response_queue_cfg_t rsp;
    size_t                          budget;

    rcp_regmap_response_queue_cfg_init(&rsp);
    rsp.max_avtpdu_size = 8u; /* 8 quadlets == 32 octets */

    budget = rcp_respqueue_max_fragment_payload((size_t)rsp.max_avtpdu_size * 4u,
                                                RCP_ACF_ABB_HEADER_LEN);
    TEST_ASSERT_EQUAL_UINT((size_t)21u, budget); /* 32 - 8(header) - 3(pad) */
    TEST_ASSERT_EQUAL_UINT((size_t)5u, rcp_fragment_plan_count(100u, budget));

    /* The same register value fed to fragment.h RAW (quadlets, no
     * header/pad accounted for) still plans the wrong, needlessly
     * fragmented result -- the mistake this test originally flagged,
     * kept here as the contrast that shows why the conversion matters. */
    TEST_ASSERT_EQUAL_UINT((size_t)13u, rcp_fragment_plan_count(100u, rsp.max_avtpdu_size));
}

/* ── §13.7.1.2 Table 33: the RC Server endpoint's own functional block ─────── */

/* TC18 Table 33 gives the RC Server endpoint its own functional-config
 * block: svr_ep_len (0x0000, R), svr_ep_enable&clr (0x0002, R/W, with
 * svr_enable permanently 1 AND READ-ONLY), svr_ep_options (0x0003, R/W*),
 * svr_ep_status (0x0004, R/W) and svr_discovery_timeout (0x0004 of the
 * same table, 16 bit, R/W*, microseconds, default 20000 = 20 ms).
 * Deviation: no EP0-specific block exists -- EP0 shares the ordinary
 * five-flag prefix whose init zeroes ep_enable, and Discovery_TimeOut is
 * a caller-supplied constructor argument in MILLISECONDS, not a register.
 */
static void test_ep0_functional_block_and_discovery_timeout_absent(void)
{
    rcp_regmap_ep_functional_cfg_t cfg;
    rcp_discovery_claim_t          claim;

    TEST_ASSERT_TRUE(rcp_regmap_is_ep0(RCP_REGMAP_EP0_INDEX));

    memset(&cfg, 0xAA, sizeof(cfg));
    rcp_regmap_ep_functional_cfg_init(&cfg);
    /* Table 33: svr_enable is always 1. The shared init zeroes it, and
     * there is no EP0 special case to put it back. */
    TEST_ASSERT_FALSE(cfg.ep_enable);
    TEST_ASSERT_FALSE(cfg.ep_clear_req_storage);
    TEST_ASSERT_FALSE(cfg.ep_req_crc_enable);
    TEST_ASSERT_FALSE(cfg.ep_response_ts_enable);
    TEST_ASSERT_FALSE(cfg.ep_suppress_response);

    /* Discovery_TimeOut: a constructor argument in ms, not a 16-bit
     * microsecond register whose default is 20000. */
    rcp_discovery_claim_init(&claim, RCP_DISCOVERY_DEFAULT_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_UINT32(20u, claim.timeout_ms);
    TEST_ASSERT_EQUAL_UINT32(20u, RCP_DISCOVERY_DEFAULT_TIMEOUT_MS);
}

/* TC18 gives TWO distinct concrete write-denial examples, both verified
 * directly against the primary-source PDF (initially only §13.7.1.2's
 * prose was checked, which is necessary but not sufficient -- Figure
 * 16's own diagram carries a second, more specific rule the prose alone
 * does not surface):
 *
 *   - Figure 16's HW_CONFIGURED-box transition: "Request on discovery
 *     stream or known stream/bb_id for configuration to HW_CONFIG or
 *     QUEUE_CFG or EP_GEN_CFG -> send error response LOCKED_CONFIG_ACCESS"
 *     -- an otherwise-authorized request, denied purely because the
 *     target block is state-locked. LOCKED_CONFIG_ACCESS does not
 *     literally match any of the seventeen numbered wire error codes'
 *     own strings (§12.9.6's table lists both UNAUTHORIZED_ACCESS and
 *     LOCKED_MEM_ACCESS by name with no description column for either),
 *     but is unambiguously the same concept as RCP_ERROR_LOCKED_MEM_ACCESS
 *     (4), the only numbered code with a semantically matching name --
 *     the same prose-vs-table naming variance already documented for
 *     POCI_FAILURE.
 *   - §13.7.1.2's own prose: "Writing to a write prohibited register
 *     (e.g. lock bit for map set) creates a response with err=1 and an
 *     error code UNAUTHORIZED_ACCESS" -- a writer/frame-specific denial
 *     on top of an otherwise-permitting state.
 *
 * REQ-WIREERR-004 distinguishes these two. Previously (REQ-LIFECYCLE-024,
 * now closed): rcp_lifecycle_field_writable() reported writability as
 * one plain bool with no wire-error-code mapping at all, and this
 * test's own prior forms (across two revisions) each mislabeled part of
 * this: HW_GENERIC-past-HW_UNCONFIGURED was first called TC18's distinct
 * "read only" case, then (after that was corrected) called
 * UNAUTHORIZED_ACCESS uniformly with every other denial -- both wrong,
 * per Figure 16's own LOCKED_CONFIG_ACCESS transition found on a closer
 * re-reading of the primary source. */
static void test_field_write_error_distinguishes_state_from_writer_denial(void)
{
    /* HW_GENERIC past HW_UNCONFIGURED: state-locked configuration
     * (§12.3.1.2's own text: "access to the HW_config...are locked";
     * Figure 16's own HW_CONFIG/EP_GEN_CFG/QUEUE_CFG grouping,
     * REQ-LIFECYCLE-023) -- LOCKED_MEM_ACCESS, even for the
     * fully-authorized ROOT_WRITER, since HW_GENERIC's own writability
     * rule has no authorization concept at all. */
    TEST_ASSERT_EQUAL(RCP_ERROR_LOCKED_MEM_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_CONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, ROOT_WRITER));

    /* FUNCTIONAL_W in RCP_CONFIGURED from an unauthorized writer:
     * write-prohibited for a different reason (writer, not state) --
     * UNAUTHORIZED_ACCESS, matching §13.7.1.2's own separate example. */
    TEST_ASSERT_EQUAL(RCP_ERROR_UNAUTHORIZED_ACCESS, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_RCP_CONFIGURED, RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, PLAIN_WRITER));

    /* An actually-writable case: RCP_ERROR_NONE. HW_GENERIC's HW_UNCONFIGURED
     * writability requires the discovery claimant specifically
     * (REQ-LIFECYCLE-026/035) -- DISCOVERY_WRITER, not ROOT_WRITER (no
     * root client can exist yet this early in bring-up). */
    TEST_ASSERT_EQUAL(RCP_ERROR_NONE, rcp_lifecycle_field_write_error(
        RCP_LIFECYCLE_HW_UNCONFIGURED, RCP_LIFECYCLE_FIELD_HW_GENERIC, DISCOVERY_WRITER));

    TEST_ASSERT_EQUAL_INT(3, (int)RCP_ERROR_UNAUTHORIZED_ACCESS);
    TEST_ASSERT_EQUAL_INT(4, (int)RCP_ERROR_LOCKED_MEM_ACCESS);
}

/* REQ-RMAP-069 (TC18 §13.7.1.2): the effective number of register-write
 * DATA octets is (acf_msg_length - 3) x 4 - pad -- distinct from, and not
 * interchangeable with, the raw ACF payload_len rcp_acf_decode_abb()
 * reports (which spans the whole payload, address/CRC region included).
 * rcp_acf_reg_write_len() now provides this formula directly; asserted
 * here against a real 5-octet ACF_ABB encoding, confirming pad and
 * acf_msg_length are carried correctly and the helper's own answer
 * matches the spec formula exactly (by construction) while remaining
 * genuinely different from the decoder's own payload_len -- the two
 * numbers answer different questions, and a caller must not conflate
 * them. */
static void test_effective_register_write_length_helper_matches_the_formula(void)
{
    rcp_acf_byte_message_info_t hdr;
    rcp_bytes_t                 msg;
    const uint8_t              *payload;
    size_t                      payload_len;
    const uint8_t               data[5] = {1, 2, 3, 4, 5};

    TEST_ASSERT_EQUAL_UINT8(0u, rcp_acf_pad_len(8u));
    TEST_ASSERT_EQUAL_UINT8(3u, rcp_acf_pad_len(9u));
    TEST_ASSERT_EQUAL_UINT8(2u, rcp_acf_pad_len(10u));
    TEST_ASSERT_EQUAL_UINT8(1u, rcp_acf_pad_len(11u));

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id = 0u;
    hdr.op          = (uint8_t)RCP_ACF_OP_WRITE;
    msg = rcp_acf_encode_abb(&hdr, data, sizeof(data));
    TEST_ASSERT_NOT_NULL(msg.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_decode_abb(msg.data, msg.len, &hdr, &payload,
                                                      &payload_len));
    TEST_ASSERT_EQUAL_UINT((size_t)5u, payload_len);
    TEST_ASSERT_EQUAL_UINT8(3u, hdr.pad);
    TEST_ASSERT_EQUAL_UINT16(4u, hdr.acf_msg_length);

    TEST_ASSERT_EQUAL_UINT((size_t)1u,
                           rcp_acf_reg_write_len(hdr.acf_msg_length, hdr.pad));
    TEST_ASSERT_NOT_EQUAL((int)payload_len,
                          (int)rcp_acf_reg_write_len(hdr.acf_msg_length, hdr.pad));
    rcp_bytes_free(&msg);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_reg_write_len_matches_the_formula);
    RUN_TEST(test_generic_config_request_is_pwm_out_only);
    RUN_TEST(test_ep_len_overrun_rule_exists_only_for_pwm_out);
    RUN_TEST(test_discovery_claim_refusal_is_unreportable);
    RUN_TEST(test_lifecycle_state_register_field_tracks_the_authoritative_state);
    RUN_TEST(test_general_map_wire_reach_stops_after_0x000d);
    RUN_TEST(test_general_static_part_has_no_read_only_class);
    RUN_TEST(test_req_stream_max_and_responder_streams_max_are_now_correctly_sized);
    RUN_TEST(test_responder_and_req_mem_size_are_now_distinctly_addressed);
    RUN_TEST(test_sequencers_max_is_now_correctly_sized_and_synced_from_the_table);
    RUN_TEST(test_configuration_lock_register_now_exists_and_defaults_unlocked);
    RUN_TEST(test_configuration_lock_not_yet_consulted_by_field_writable);
    RUN_TEST(test_implemented_options_now_matches_table_18_exactly);
    RUN_TEST(test_reserved_registers_are_indistinguishable_from_zero_fill);
    RUN_TEST(test_io_pin_count_absent_and_hw_cfg_ptr_mis_shaped);
    RUN_TEST(test_stream_cfg_pointer_capacity_pairs_are_collapsed);
    RUN_TEST(test_ep_cfg_and_bytebus_map_pointers_are_mis_shaped);
    RUN_TEST(test_four_optional_subsystem_pointer_pairs_are_absent);
    RUN_TEST(test_hw_config_table_has_no_server_side_storage);
    RUN_TEST(test_hw_config_row_stride_absent_and_access_class_inverted);
    RUN_TEST(test_pin_type_bit_layout_contradicts_table_20);
    RUN_TEST(test_output_pin_loses_its_input_capability);
    RUN_TEST(test_named_signal_index_is_global_and_covers_three_types);
    RUN_TEST(test_request_stream_cfg_lacks_channel_and_stream_indices);
    RUN_TEST(test_watchdog_timeout_width_and_unit_deviate);
    RUN_TEST(test_table22_w_star_writable_in_both_pre_rcp_configured_states);
    RUN_TEST(test_ep_id_row_lacks_request_stream_index_and_sentinel);
    RUN_TEST(test_ep_id_ordering_ignores_request_stream_index);
    RUN_TEST(test_no_diagnostic_for_multi_client_or_heterogeneous_type);
    RUN_TEST(test_byte_bus_id_is_eight_bits_wide);
    RUN_TEST(test_no_lockable_w_plus_field_kind);
    RUN_TEST(test_response_queue_stream_id_is_configurable);
    RUN_TEST(test_response_queue_size_register_and_storage_now_exist);
    RUN_TEST(test_max_avtpdu_size_is_now_enforced_and_feeds_fragmentation);
    RUN_TEST(test_flush_on_count_trigger_and_avtpdu_packing);
    RUN_TEST(test_flush_time_trigger_and_empty_heartbeat_are_composable);
    RUN_TEST(test_transmit_fragmentation_now_uses_the_correct_octet_budget);
    RUN_TEST(test_ep0_functional_block_and_discovery_timeout_absent);
    RUN_TEST(test_field_write_error_distinguishes_state_from_writer_denial);
    RUN_TEST(test_effective_register_write_length_helper_matches_the_formula);

    return UNITY_END();
}
