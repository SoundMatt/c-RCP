/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-LIFECYCLE-022
//cfusa:test REQ-LIFECYCLE-023
//cfusa:test REQ-LIFECYCLE-024
//cfusa:test REQ-LIFECYCLE-025
//cfusa:test REQ-LIFECYCLE-026
//cfusa:test REQ-LIFECYCLE-027
//cfusa:test REQ-LIFECYCLE-028
//cfusa:test REQ-LIFECYCLE-029
//cfusa:test REQ-LIFECYCLE-030
//cfusa:test REQ-LIFECYCLE-031
//cfusa:test REQ-LIFECYCLE-032
//cfusa:test REQ-LIFECYCLE-033
//cfusa:test REQ-LIFECYCLE-034
//cfusa:test REQ-LIFECYCLE-035
//cfusa:test REQ-LIFECYCLE-036
//cfusa:test REQ-LIFECYCLE-037
//cfusa:test REQ-PWRMODE-014
//cfusa:test REQ-PWRMODE-015
//cfusa:test REQ-PWRMODE-016
//cfusa:test REQ-PWRMODE-017
//cfusa:test REQ-PWRMODE-018
//cfusa:test REQ-PWRMODE-019
//cfusa:test REQ-PWRMODE-020
//cfusa:test REQ-PWRMODE-021
//cfusa:test REQ-PWRMODE-022
//cfusa:test REQ-PWRMODE-023
//cfusa:test REQ-PWRMODE-024
//cfusa:test REQ-PWRMODE-025
//cfusa:test REQ-PWRMODE-026
//cfusa:test REQ-PWRMODE-027
//cfusa:test REQ-PWRMODE-028
//cfusa:test REQ-SRV-015
//cfusa:test REQ-SRV-016
//cfusa:test REQ-SRV-017
//cfusa:test REQ-SRV-018
//cfusa:test REQ-SEQ-012
//cfusa:test REQ-SEQ-013
//cfusa:test REQ-SEQ-014
//cfusa:test REQ-TIMED-012
//cfusa:test REQ-TIMED-013
//cfusa:test REQ-WDG-010

/*
 * test_tc18_gaps_server.c -- spec-literal conformance-and-deviation suite
 * for the RC-Server-side TC18 clauses catalogued by the v0.105.0
 * requirements-corpus completeness pass: RC Server lifecycle (§12.3,
 * §12.7), power/operation modes and the Goto Sleep / Goto StandBy
 * exchange (§12.4-§12.5, §13.7.2.3), request handling on enabled and
 * disabled endpoints (§12.3.1.3, §13.7.1), sequencers (§12.7.10 Table 25),
 * the per-stream request watchdog (§12.7.7), and TSCF-carried timed
 * requests (§11.2).
 *
 * Every requirement catalogued by that pass with a status of "partial" or
 * "not-implemented" gets a *deviation-pinning* test here: the assertions
 * state what this library actually does today, and the comment above each
 * one names the TC18 clause that is therefore not met and what a
 * conforming RC Server would do instead. These tests are expected to be
 * rewritten -- not merely re-run -- when the corresponding gap is closed.
 */
#include "unity.h"

#include <rcp/acf.h>
#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/discovery.h>
#include <rcp/ep_wakeup.h>
#include <rcp/errors.h>
#include <rcp/lifecycle.h>
#include <rcp/power.h>
#include <rcp/powerstate.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/request_compound.h>
#include <rcp/request_sequencer.h>
#include <rcp/request_timed.h>
#include <rcp/request_triggered.h>
#include <rcp/server.h>
#include <rcp/watchdog.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── Shared fixtures ───────────────────────────────────────────────────────── */

static const uint8_t MAC_A[6] = {0x02u, 0u, 0u, 0u, 0u, 0x0Au};
static const uint8_t MAC_B[6] = {0x02u, 0u, 0u, 0u, 0u, 0x0Bu};

static rcp_avtp_addr_t addr_of(const uint8_t mac[6], uint16_t uid, rcp_byte_bus_id_t bus)
{
    rcp_avtp_addr_t a;

    a.stream_id   = rcp_stream_id_make(mac, uid);
    a.byte_bus_id = bus;
    return a;
}

/* A plain, unconditional ACF_ABB standard request addressed to bus. */
static rcp_bytes_t standard_abb(rcp_byte_bus_id_t bus, uint8_t transaction_num)
{
    rcp_acf_byte_message_info_t hdr;
    const uint8_t               payload[2] = {0xDEu, 0xADu};

    memset(&hdr, 0, sizeof(hdr));
    hdr.byte_bus_id     = bus;
    hdr.transaction_num = transaction_num;
    return rcp_acf_encode_abb(&hdr, payload, sizeof(payload));
}

/* ── §12.3 lifecycle transitions: no idleness, no writer authorization ─────── */

static void test_transition_takes_neither_idle_nor_writer_input(void)
{
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_RCP_CONFIGURED;
    const char           *unknown;

    /* TC18 §12.3 (Figure 16) refuses a lifecycle-state change with an
     * EPs_NOT_IDLE error while any other endpoint still has an in-flight or
     * queued request, and §12.3.1.2 accepts a svr_lifecycle_state write only
     * via the discovery stream, from the configured root client, or -- with
     * no root client configured -- from any valid stream. c-RCP's
     * rcp_lifecycle_transition() takes neither an endpoint-idle input nor a
     * writer context, so the demotion below succeeds unconditionally from an
     * anonymous caller and tears down every in-flight request with it. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_HW_UNCONFIGURED, NULL));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, state);

    /* And there is no EPs_NOT_IDLE outcome to report even if one were
     * detected: rcp_lifecycle_errc_t stops at 3 (INVALID_TRANSITION). */
    unknown = rcp_lifecycle_strerror((rcp_lifecycle_errc_t)4);
    TEST_ASSERT_EQUAL_STRING(rcp_lifecycle_strerror((rcp_lifecycle_errc_t)99), unknown);
    TEST_ASSERT_TRUE(strcmp(unknown, rcp_lifecycle_strerror(RCP_LIFECYCLE_OK)) != 0);
    TEST_ASSERT_TRUE(strcmp(unknown,
                            rcp_lifecycle_strerror(RCP_LIFECYCLE_ERR_INVALID_TRANSITION)) != 0);
}

/* ── §12.3 register locking: missing field kinds, no error response ────────── */

static void test_locked_config_write_has_no_kind_and_no_error_response(void)
{
    rcp_lifecycle_writer_ctx_t root = {true, true};
    /* Stand-in for the endpoint-generic (rcp_regmap_ep_generic_cfg_t) and
     * response-queue (rcp_regmap_response_queue_cfg_t) blocks: TC18 §12.3
     * locks both alongside HW_config once the server leaves HW_UNCONFIGURED,
     * and §12.1 requires every parameter to be writable *while*
     * HW_UNCONFIGURED. rcp_lifecycle_field_kind_t defines no kind for either
     * block, so such a write is never lock-checked; the fail-safe default
     * branch below denies it in all three states -- including the one state
     * where TC18 requires it to be permitted. */
    const rcp_lifecycle_field_kind_t ep_generic_or_queue = (rcp_lifecycle_field_kind_t)3;

    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_UNCONFIGURED,
                                                   ep_generic_or_queue, root));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   ep_generic_or_queue, root));
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   ep_generic_or_queue, root));

    /* TC18 §12.3 answers a write to a locked configuration register with an
     * error response carrying LOCKED_MEM_ACCESS (wire code 4). c-RCP's
     * denial is a bare bool with no response-producing counterpart: the code
     * exists in the table and nothing ever emits it, so the client cannot
     * distinguish a locked register from a lost frame. */
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_HW_GENERIC, root));
    TEST_ASSERT_EQUAL_INT(4, RCP_ERROR_LOCKED_MEM_ACCESS);
    TEST_ASSERT_TRUE(strlen(rcp_wire_error_string(RCP_ERROR_LOCKED_MEM_ACCESS)) > 0);
}

/* ── §12.3.1.2 / §12.7: unknown streams and non-EP0 endpoints ──────────────── */

static void test_hw_configured_admits_any_stream_and_any_endpoint(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)42u, 1u);
    uint8_t               request_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);

    /* TC18 §12.3 (Figure 16) ignores a request arriving on an unknown
     * stream_id/byte_bus_id combination; §12.3.1.2 additionally drops every
     * non-configuration request addressed to an endpoint other than EP0
     * while HW_CONFIGURED; and §12.7 permits a direct-to-endpoint
     * configuration request only where a valid stream/byte_bus_id
     * association exists. rcp_lifecycle_should_accept() receives no
     * stream_id and consults no association table, so byte_bus_id 42 -- an
     * endpoint no configured request stream maps -- is admitted in both
     * configured states. */
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)42u));
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)42u));

    /* ...and the dispatch path behind it takes no lifecycle state either,
     * so the operational request executes immediately rather than being
     * dropped without response. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u,
                                                &request_type, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, request_type);

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* ── §12.3.1.1 / §12.7.2 / §12.7: HW_UNCONFIGURED admission ────────────────── */

static void test_hw_unconfigured_ignores_claimant_and_request_kind(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t       a = rcp_stream_id_make(MAC_A, 1u);
    rcp_stream_id_t       b = rcp_stream_id_make(MAC_B, 2u);

    rcp_discovery_claim_init(&claim, 20u);
    rcp_discovery_claim_note_request(&claim, a, 1000u);
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1000u));
    TEST_ASSERT_FALSE(rcp_discovery_claim_is_claimant(&claim, b, 1000u));

    /* TC18 §12.3.1.1 and §12.7.2 restrict HW_UNCONFIGURED configuration to
     * the stream that claimed discovery. The claim above is modelled and
     * correctly refuses B -- but rcp_lifecycle_should_accept() takes no
     * stream_id and never consults it, so B's configuration request is
     * admitted on exactly the same terms as claimant A's. */
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));

    /* TC18 §12.7 accepts only unconditional STANDARD requests at EP0 in this
     * condition and answers every other otherwise-valid EP0 request with
     * REQUEST_REJECTED (wire code 11). Conditional requests are carried in
     * ACF_GBB messages, which c-RCP drops in silence instead -- and no code
     * path in the library ever produces that error code. */
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));
    TEST_ASSERT_EQUAL_INT(11, RCP_ERROR_REQUEST_REJECTED);
}

/* ── §12.3.1.x / §12.7.3: who may write, and over what framing ─────────────── */

static void test_hw_configured_write_access_is_unrestricted(void)
{
    rcp_lifecycle_writer_ctx_t stranger = {false, false};
    rcp_lifecycle_writer_ctx_t root     = {true, false};

    /* TC18 §12.3.1.2 permits EP0 write access to another endpoint's
     * configuration only for the configured root client or via the discovery
     * stream, and §12.7.3 permits configuration access in HW_CONFIGURED only
     * over the discovery stream or a known stream_id/byte_bus_id pair.
     * c-RCP grants it to every writer context in this state. */
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, stranger));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_HW_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR,
                                                  stranger));

    /* RCP_CONFIGURED is the one state where the writer is consulted at all,
     * which is what makes the permissiveness above a policy gap rather than
     * an unmodelled parameter. */
    TEST_ASSERT_FALSE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                   RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, stranger));
    TEST_ASSERT_TRUE(rcp_lifecycle_field_writable(RCP_LIFECYCLE_RCP_CONFIGURED,
                                                  RCP_LIFECYCLE_FIELD_FUNCTIONAL_W, root));

    /* TC18 §12.3.1.1, §12.3.1.2 and §12.3.1.3 each restate that a write is
     * accepted only from a unicast frame. Neither writability nor frame
     * acceptance takes any destination-MAC input, so an identical write
     * carried in a multicast or broadcast frame is processed the same way in
     * every state. */
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_RCP_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)9u));
}

/* ── §12.3.1.2: TSCF and ACF_GBB in HW_CONFIGURED ──────────────────────────── */

static void test_hw_configured_admits_tscf_and_gbb(void)
{
    /* TC18 §12.3.1.2 drops TSCF-headed AVTPDUs in HW_CONFIGURED
     * unconditionally -- the same rule c-RCP already implements for
     * HW_UNCONFIGURED. Here it applies only rcp_avtp_should_drop_tscf()'s
     * general time-sync rule, so a time-sync-capable server processes timed
     * requests before its RCP configuration exists. */
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, (rcp_byte_bus_id_t)7u));
    /* Contrast: HW_UNCONFIGURED does apply the unconditional rule. */
    TEST_ASSERT_FALSE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_UNCONFIGURED, true,
        RCP_AVTP_SUBTYPE_TSCF, RCP_ACF_MSG_TYPE_ABB, RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID));

    /* TC18 §12.3.1.2 also drops ACF_GBB-format requests in HW_CONFIGURED
     * without response. acf_msg_type is inspected only in the
     * HW_UNCONFIGURED branch, so a group-byte-bus request is admitted. */
    TEST_ASSERT_TRUE(rcp_lifecycle_should_accept(RCP_LIFECYCLE_HW_CONFIGURED, false,
        RCP_AVTP_SUBTYPE_NTSCF, RCP_ACF_MSG_TYPE_GBB, (rcp_byte_bus_id_t)7u));
}

/* ── §12.7.4: discovery-stream write authority after RCP_CONFIGURED ────────── */

static void test_discovery_write_authority_survives_rcp_configured(void)
{
    rcp_discovery_claim_t claim;
    rcp_stream_id_t       a     = rcp_stream_id_make(MAC_A, 1u);
    rcp_lifecycle_state_t state = RCP_LIFECYCLE_HW_CONFIGURED;
    rcp_lifecycle_endpoint_plausibility_t eps[1]     = {{true, true, true, true}};
    rcp_lifecycle_request_stream_plausibility_t rs[1] = {{true, true}};
    rcp_lifecycle_plausibility_snapshot_t snap;

    snap.endpoints            = eps;
    snap.endpoint_count       = 1u;
    snap.request_streams      = rs;
    snap.request_stream_count = 1u;

    rcp_discovery_claim_init(&claim, 20u);
    rcp_discovery_claim_note_request(&claim, a, 1000u);
    TEST_ASSERT_TRUE(rcp_discovery_claim_note_config_write(&claim, a, 1005u));

    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_OK,
                      rcp_lifecycle_transition(&state, RCP_LIFECYCLE_RCP_CONFIGURED, &snap));
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_RCP_CONFIGURED, state);

    /* TC18 §12.7.4: once RCP_CONFIGURED, a discovery request is still
     * answered but may no longer change configuration. c-RCP's transition
     * releases no claim, and rcp_discovery_claim_note_config_write() takes
     * no lifecycle state, so the claimant's write authorization is still
     * granted -- and refreshed for another Discovery_TimeOut -- in a state
     * where only a configured stream or the root client may configure. */
    TEST_ASSERT_TRUE(rcp_discovery_claim_note_config_write(&claim, a, 1010u));
    TEST_ASSERT_TRUE(rcp_discovery_claim_is_claimant(&claim, a, 1010u));
}

/* ── §12.3 / §12.4: cold start and StandBy retention ───────────────────────── */

static void test_cold_start_target_and_standby_retention(void)
{
    rcp_regmap_general_t           map;
    rcp_regmap_general_t           before;
    rcp_ep_wakeup_functional_cfg_t wake;
    rcp_ep_wakeup_functional_cfg_t wake_before;
    rcp_pwrmode_t                  mode = RCP_PWRMODE_NORMAL;
    rcp_pwrmode_start_kind_t       kind = RCP_PWRMODE_START_COLD;

    /* TC18 §12.3 / §12.4.1: after a cold start the RC Server comes up in the
     * lifecycle state it is configured in -- recovered from NVM, or from
     * device defaults which may themselves be an advanced state. c-RCP has
     * no persisted-state input at all and always names HW_UNCONFIGURED, so a
     * server that reached RCP_CONFIGURED loses it on every power cycle. */
    TEST_ASSERT_EQUAL(RCP_LIFECYCLE_HW_UNCONFIGURED, rcp_pwrmode_cold_start_lifecycle_target());

    rcp_regmap_general_init(&map);
    map.svr_ep_count = 4u;
    rcp_ep_wakeup_functional_cfg_init(&wake);
    wake.sources[0].enabled = true;
    memcpy(&before, &map, sizeof(before));
    memcpy(&wake_before, &wake, sizeof(wake_before));

    /* TC18 §12.4: register-map configuration and configured wake sources
     * survive StandBy, so the following hot start needs no reconfiguration.
     * Both are indeed byte-identical afterwards -- but only because
     * rcp_pwrmode_transition() is unaware of either structure: nothing in
     * the library asserts, restores, or re-applies them, so retention rests
     * entirely on the integrator. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_STANDBY, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_NORMAL, &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, mode);
    TEST_ASSERT_EQUAL_MEMORY(&before, &map, sizeof(map));
    TEST_ASSERT_EQUAL_MEMORY(&wake_before, &wake, sizeof(wake));
}

/* ── §12.4.1: the hot-start sequence's missing inputs ──────────────────────── */

static void test_hotstart_has_no_network_check_and_no_responder_stream(void)
{
    rcp_pwrmode_handshake_t   hs;
    rcp_avtp_addr_t           sleeper = addr_of(MAC_A, 1u, (rcp_byte_bus_id_t)3u);
    rcp_avtp_addr_t           other   = addr_of(MAC_B, 2u, (rcp_byte_bus_id_t)4u);
    rcp_avtp_addr_t           eps[2];
    rcp_powerstate_manager_t *m;
    rcp_bytes_t               req;
    rcp_bytes_t               probe;

    /* TC18 §12.4.1 step (b): the server enables its interface, tests whether
     * the network is already available, and initiates a WakeUp only when it
     * is not. c-RCP's handshake has no network-availability input anywhere,
     * so step (b) is entered -- and a WakeUp burned -- unconditionally. */
    rcp_pwrmode_handshake_init(&hs, 3u);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_EQUAL_UINT32(1u, hs.wakeup_attempts);

    /* TC18 §12.4.1 also sends the repetitive wake response on the responder
     * stream configured for the original sleep/standby request. Nothing
     * records that stream: the pending sleep request below belongs to
     * `sleeper`, yet a wake probe is happily produced for an unrelated
     * endpoint that never asked the server to sleep. */
    eps[0] = sleeper;
    eps[1] = other;
    m      = rcp_powerstate_manager_new(eps, 2u);
    TEST_ASSERT_NOT_NULL(m);
    req = rcp_powerstate_manager_encode_entry_request(m, sleeper, RCP_PWRMODE_SLEEP, 7u);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_TRUE(rcp_powerstate_manager_handshake_begin(m, other, 3u));
    probe = rcp_powerstate_manager_encode_wakeup_probe(m, other, 7u);
    TEST_ASSERT_NOT_NULL(probe.data);

    rcp_bytes_free(&req);
    rcp_bytes_free(&probe);
    rcp_powerstate_manager_destroy(m);
}

/* ── §12.4.1: what actually terminates WakeUp repetition ───────────────────── */

static void test_wakeup_repetition_ignores_other_valid_avtpdus(void)
{
    const rcp_byte_bus_id_t bus = (rcp_byte_bus_id_t)3u;
    rcp_pwrmode_handshake_t hs;
    rcp_bytes_t             reply = rcp_ep_wakeup_encode_sleepcmd_response(bus,
                                                                          RCP_PWRMODE_ENTRY_OK,
                                                                          9u);

    TEST_ASSERT_NOT_NULL(reply.data);

    /* TC18 §12.4.1 stops the WakeUp repetition on receipt of ANY valid
     * AVTPDU from the sleep-request client, and defines no repeat limit.
     * c-RCP terminates only on an exact echo of the WakeUp message it sent:
     * the perfectly valid SleepCMD response below is not an echo... */
    TEST_ASSERT_FALSE(rcp_ep_wakeup_is_wakeup_echo(reply.data, reply.len, bus, 9u));

    /* ...so the handshake keeps repeating and then fails outright once the
     * implementation-added repeat limit is exhausted, degrading a hot start
     * that TC18 would have considered complete into a cold one. */
    rcp_pwrmode_handshake_init(&hs, 2u);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_has_failed(&hs));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_wakeup_attempt(&hs, false));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_has_failed(&hs));
    TEST_ASSERT_FALSE(rcp_pwrmode_handshake_is_complete(&hs));

    rcp_bytes_free(&reply);
}

/* ── §12.4.1: completing the handshake, and the network-wake shortcut ──────── */

static void test_wake_completion_reenables_nothing_and_network_skips_all(void)
{
    rcp_server_endpoint_t   ep;
    rcp_pwrmode_handshake_t hs;
    rcp_bytes_t             frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    rcp_bytes_t             drained;
    rcp_pwrmode_t           mode = RCP_PWRMODE_SLEEP;
    rcp_pwrmode_start_kind_t kind = RCP_PWRMODE_START_COLD;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, false);
    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&ep, frame.data, frame.len));

    rcp_pwrmode_handshake_init(&hs, 3u);
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_iface_reenabled(&hs));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_wakeup_attempt(&hs, true));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_resume_queues(&hs));
    TEST_ASSERT_TRUE(rcp_pwrmode_handshake_is_complete(&hs));

    /* TC18 §12.4.1 step (c): completing the handshake re-enables every used
     * endpoint and resumes every response queue. c-RCP only advances a step
     * enum -- the endpoint is still disabled and its pre-load queue is still
     * stalled, while the server reports a completed hot start. */
    TEST_ASSERT_FALSE(ep.ep_enable);
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_queue_len(&ep));
    TEST_ASSERT_FALSE(rcp_server_endpoint_drain_one(&ep, &drained));

    /* TC18 §12.4.1 also has a TC14/TC10-network-woken server skip ONLY the
     * interface-enable step and then run the rest of the sequence. c-RCP
     * skips the whole handshake, so the client that put it to sleep is
     * never told it is awake. */
    TEST_ASSERT_FALSE(rcp_pwrmode_hotstart_required(RCP_PWRMODE_WAKE_VIA_NETWORK));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK,
                      rcp_pwrmode_wake_from_sleep(&mode, RCP_PWRMODE_WAKE_VIA_NETWORK, NULL,
                                                  &kind));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_START_HOT, kind);

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* ── §12.5: how StandBy and Sleep may be entered ───────────────────────────── */

static void test_sleep_entry_is_request_only_with_no_network_path(void)
{
    const rcp_byte_bus_id_t bus = (rcp_byte_bus_id_t)3u;
    rcp_bytes_t             req = rcp_ep_wakeup_encode_sleepcmd_request(bus,
                                                                       RCP_PWRMODE_STANDBY, 4u);
    rcp_bytes_t             bad;
    rcp_pwrmode_t           target = RCP_PWRMODE_NORMAL;
    uint8_t                 tn     = 0u;

    /* TC18 §12.5: StandBy is entered only in response to an RCP request,
     * never from a network signal. The SleepCMD exchange is indeed the only
     * modelled entry, and a target mode outside {StandBy, Sleep} is refused
     * outright -- so the exclusivity holds, by omission. */
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
                      rcp_ep_wakeup_decode_sleepcmd_request(req.data, req.len, bus, &target, &tn));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_STANDBY, target);
    TEST_ASSERT_EQUAL_UINT8(4u, tn);
    bad = rcp_ep_wakeup_encode_sleepcmd_request(bus, RCP_PWRMODE_NORMAL, 4u);
    TEST_ASSERT_NULL(bad.data);

    /* TC18 §12.5 also allows Sleep to be initiated by a valid TC14/TC10
     * network sleep request. c-RCP has no such decoder: the only
     * network-level message it recognizes at all is the WakeUp echo, which
     * rejects the SleepCMD frame above as a bad opcode. A c-RCP server
     * therefore stays awake when the network is put to sleep. */
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_ERR_BAD_OPCODE,
                      rcp_ep_wakeup_decode_wakeup_message(req.data, req.len, bus, &tn));

    rcp_bytes_free(&req);
}

/* ── §12.5: a sleep request is per-endpoint and unauthenticated ────────────── */

static void test_sleep_request_moves_one_endpoint_only(void)
{
    rcp_avtp_addr_t           sleeper = addr_of(MAC_A, 1u, (rcp_byte_bus_id_t)3u);
    rcp_avtp_addr_t           other   = addr_of(MAC_B, 2u, (rcp_byte_bus_id_t)4u);
    rcp_avtp_addr_t           eps[2];
    rcp_powerstate_manager_t *m;
    rcp_bytes_t               req;
    rcp_bytes_t               resp;

    eps[0] = sleeper;
    eps[1] = other;
    m      = rcp_powerstate_manager_new(eps, 2u);
    TEST_ASSERT_NOT_NULL(m);

    req = rcp_powerstate_manager_encode_entry_request(m, sleeper, RCP_PWRMODE_SLEEP, 7u);
    TEST_ASSERT_NOT_NULL(req.data);
    resp = rcp_ep_wakeup_encode_sleepcmd_response(sleeper.byte_bus_id, RCP_PWRMODE_ENTRY_OK, 7u);
    TEST_ASSERT_NOT_NULL(resp.data);
    TEST_ASSERT_EQUAL(RCP_POWERSTATE_OK,
                      rcp_powerstate_manager_apply_entry_response(m, sleeper, resp.data,
                                                                  resp.len));

    /* TC18 §12.5: an accepted sleep/standby request from an authorized
     * client puts the ENTIRE RC Server implementation into that mode.
     * c-RCP's model is per-endpoint -- the unaddressed endpoint stays in
     * Normal -- and no authorization check guards the wake-up endpoint, so
     * an unauthorized client's request is applied just the same. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, rcp_powerstate_manager_mode(m, sleeper));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_NORMAL, rcp_powerstate_manager_mode(m, other));

    rcp_bytes_free(&req);
    rcp_bytes_free(&resp);
    rcp_powerstate_manager_destroy(m);
}

/* ── §12.5: the entry gate is a one-shot snapshot ──────────────────────────── */

static void test_entry_gate_is_not_rechecked_before_the_mode_change(void)
{
    rcp_pwrmode_entry_gate_t gate = {true, true, true};
    rcp_pwrmode_t            mode = RCP_PWRMODE_NORMAL;

    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_OK, rcp_pwrmode_check_entry(&gate));

    /* A wake source asserts while the sleep request is still being
     * processed -- TC18 §12.5 requires the sleep entry to be aborted. */
    gate.wup_status_clear = false;
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, rcp_pwrmode_check_entry(&gate));

    /* But the gate is a stateless snapshot with no re-check hook: the mode
     * change is an entirely independent call that still succeeds against the
     * now-refusing gate, so the server sleeps through the very wake event
     * that should have kept it awake. This is a lost-wakeup hazard. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_SLEEP, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode);
}

/* ── §12.5: response-before-transition ordering, and the LPS confirmation ──── */

static void test_mode_change_is_unordered_against_the_response(void)
{
    rcp_pwrmode_entry_gate_t refusing = {false, true, true};
    rcp_pwrmode_t            mode     = RCP_PWRMODE_NORMAL;
    rcp_bytes_t              resp;

    /* TC18 §12.5: the mode change happens only AFTER the sleep/standby
     * response has been transmitted. rcp_pwrmode_transition() and
     * rcp_ep_wakeup_encode_sleepcmd_response() are unrelated calls with no
     * ordering relationship, so the server can enter Sleep here with the
     * response not merely untransmitted but not yet even encoded. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_OK, rcp_pwrmode_transition(&mode, RCP_PWRMODE_SLEEP, NULL));
    TEST_ASSERT_EQUAL(RCP_PWRMODE_SLEEP, mode);
    resp = rcp_ep_wakeup_encode_sleepcmd_response((rcp_byte_bus_id_t)3u, RCP_PWRMODE_ENTRY_OK, 1u);
    TEST_ASSERT_NOT_NULL(resp.data);
    rcp_bytes_free(&resp);

    /* TC18 §12.5 further requires a refused network sleep request to
     * suppress the TC14/TC10 LPS confirmation. A refusal's entire
     * observable output is this two-valued enum plus the encoded response
     * byte -- there is no PHY/LPS signalling surface to suppress, so an
     * upstream node can never learn this node declined to sleep. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_REFUSED, rcp_pwrmode_check_entry(&refusing));
    TEST_ASSERT_EQUAL_INT(0, RCP_PWRMODE_ENTRY_OK);
    TEST_ASSERT_EQUAL_INT(1, RCP_PWRMODE_ENTRY_REFUSED);
    resp = rcp_ep_wakeup_encode_sleepcmd_response((rcp_byte_bus_id_t)3u,
                                                  RCP_PWRMODE_ENTRY_REFUSED, 1u);
    TEST_ASSERT_NOT_NULL(resp.data);
    rcp_bytes_free(&resp);
}

/* ── §12.5 / §13.7.2.3: gate scope, and the missing admission suspend ──────── */

static void test_entry_gate_is_scoped_to_one_endpoint_and_one_queue(void)
{
    rcp_pwrmode_entry_gate_t gate = {true, true, true};
    rcp_server_endpoint_t    wakeup_ep;
    rcp_server_endpoint_t    busy_ep;
    rcp_bytes_t              frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    uint8_t                  request_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&wakeup_ep, true);
    rcp_server_endpoint_init(&busy_ep, false);

    /* A second endpoint is plainly not idle: it holds an undrained request. */
    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&busy_ep, frame.data, frame.len));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_queue_len(&busy_ep));

    /* TC18 §12.5 refuses entry when AT LEAST ONE endpoint of the whole
     * server is not idle, or AT LEAST ONE responder queue still holds an
     * untransmitted message. c-RCP's gate is three singular bools scoped to
     * the wake-up endpoint and one queue, with no all-endpoints or
     * all-queues aggregate anywhere, so the busy endpoint above is invisible
     * and entry is admitted -- losing its request and any queued response. */
    TEST_ASSERT_EQUAL(RCP_PWRMODE_ENTRY_OK, rcp_pwrmode_check_entry(&gate));

    /* TC18 §13.7.2.3 step 1: on receipt of a sleep request the server stops
     * entering newly arriving requests into endpoint queues while the drain
     * proceeds. There is no admission-suspend state to set, so a request
     * arriving mid-drain is still admitted and executed normally. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&wakeup_ep, frame.data, frame.len, 0u,
                                                &request_type, NULL));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&wakeup_ep);
    rcp_server_endpoint_destroy(&busy_ep);
}

/* ── §12.3.1.3: requests arriving at a disabled endpoint ───────────────────── */

static void test_disabled_endpoint_queues_config_requests_without_ack(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)5u, 0x42u);
    uint8_t               request_type = 0xFFu;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, false);

    /* TC18 §12.3.1.3: a disabled endpoint still executes CONFIGURATION
     * requests immediately and queues only operational ones. c-RCP branches
     * on ep_enable alone and never inspects the request, so a configuration
     * request -- including the write that would set ep_enable itself -- is
     * queued instead of executed, and the endpoint can never be re-enabled
     * over the wire. */
    TEST_ASSERT_FALSE(rcp_server_endpoint_submit(&ep, frame.data, frame.len));
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_queue_len(&ep));

    /* TC18 §12.3.1.3 also emits the requested acknowledge at the moment a
     * request is STORED for a disabled endpoint. The admission path produces
     * no acknowledge of any kind -- only a queued/executed verdict and a
     * request-type byte -- so a client cannot distinguish a stored request
     * (transaction 0x42, above) from a dropped one. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_QUEUED,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u,
                                                &request_type, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, request_type);
    TEST_ASSERT_EQUAL_UINT(2u, rcp_server_endpoint_queue_len(&ep));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* ── §13.7.1.1: the cyclic heartbeat register without an emitter ───────────── */

static void test_response_queue_flush_period_is_carried_but_inert(void)
{
    rcp_regmap_response_queue_cfg_t cfg;

    rcp_regmap_response_queue_cfg_init(&cfg);
    TEST_ASSERT_EQUAL_UINT32(0u, cfg.flush_time_us);
    TEST_ASSERT_EQUAL_UINT16(0u, cfg.flush_on_count);

    /* TC18 §13.7.1.1 requires the server to emit a cyclic heartbeat AVTPDU
     * every flush period, and specifically an EMPTY NTSCF-only PDU -- an
     * NTSCF header carrying no ACF messages at all -- when the period
     * elapses with an empty response queue. c-RCP carries the periodicity
     * register faithfully (it round-trips as plain R/W data below) but ships
     * no heartbeat emitter at all: nothing in the library ever reads
     * flush_time_us, so a client watching for liveness sees nothing. */
    cfg.flush_time_us  = 20000u;
    cfg.flush_on_count = 8u;
    TEST_ASSERT_EQUAL_UINT32(20000u, cfg.flush_time_us);
    TEST_ASSERT_EQUAL_UINT16(8u, cfg.flush_on_count);
}

/* ── §13.7.1.3 Table 34: RC-Server-issued PTP trigger signals ──────────────── */

static void test_gptp_lock_transition_issues_no_trigger_signal(void)
{
    rcp_server_endpoint_t ep;
    rcp_sequencer_table_t seqs = {NULL, 0u};
    rcp_server_tick_ctx_t ctx;
    rcp_triggered_step_t  step;
    rcp_bytes_t           frame;
    uint8_t               request_type = 0xFFu;
    size_t                idx          = 0u;

    memset(&step, 0, sizeof(step));
    step.trigger_source_ep = 0u; /* EP0 -- where Table 34's server signals originate */
    step.trigger_signal_nr = 0u; /* signal 0: PTP time-synch established */
    rcp_server_endpoint_init(&ep, true);
    frame = rcp_triggered_encode_request(RCP_REQUEST_TYPE_TRIGGERED, (rcp_byte_bus_id_t)5u,
                                          &step, 1u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(frame.data);
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_PENDING,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u,
                                                &request_type, &idx));

    memset(&ctx, 0, sizeof(ctx));
    ctx.sequencers    = &seqs;
    ctx.endpoint_idle = true;

    /* TC18 §13.7.1.3 Table 34 has the RC Server issue trigger signal 0 when
     * PTP time-synch is established and signal 1 when it is lost. gPTP lock
     * state is modelled -- but only as a gate on timed requests. Crossing it
     * from unlocked to locked arms nothing: the request above stays not-due
     * on both sides of the transition. */
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));
    ctx.gptp_locked = true;
    TEST_ASSERT_FALSE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    /* Only an explicit, caller-driven notification records an occurrence --
     * there is no PTP-derived trigger source to drive it. */
    TEST_ASSERT_EQUAL_UINT(1u, rcp_server_endpoint_notify_trigger(&ep, 0u, 0u));
    TEST_ASSERT_TRUE(rcp_server_endpoint_select_due(&ep, &ctx, &idx));

    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

/* ── §12.7.10 Table 25: sequencer disable, ownership, and register wiring ──── */

static void test_sequencer_zero_state_ownership_and_regmap_wiring(void)
{
    rcp_sequencer_table_t table = rcp_sequencer_table_new(4u);
    rcp_compound_step_t   step;
    rcp_regmap_general_t  map;
    uint8_t               state = 0xFFu;

    TEST_ASSERT_EQUAL_UINT16(4u, table.count);
    memset(&step, 0, sizeof(step));

    /* TC18 §12.7.10 Table 25: a sequencer whose Seq_state register has been
     * manually written to 0 is DISABLED -- no conditional step bound to it
     * may become executable. c-RCP stores 0 as an ordinary state value, so
     * the step below (start_state 0, i.e. "any state") still starts against
     * the sequencer the client meant to disable. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0u, 0u));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0u, &state));
    TEST_ASSERT_EQUAL_UINT8(0u, state);
    step.start_state     = 0u;
    step.sequencer_index = 0u;
    TEST_ASSERT_TRUE(rcp_compound_start_condition_met(&table, &step));

    /* Table 25 also gives each sequencer a Request_stream_index naming the
     * one RC Client permitted to access it. rcp_sequencer_table_t is a bare
     * state array with no owner, and set_state() takes no requester
     * identity, so any client can overwrite another's sequencer -- including
     * one configured as its rx_safestate_sequencer. */
    TEST_ASSERT_TRUE(rcp_sequencer_set_state(&table, 0u, 9u));
    TEST_ASSERT_TRUE(rcp_sequencer_get_state(&table, 0u, &state));
    TEST_ASSERT_EQUAL_UINT8(9u, state);

    /* And neither register that would expose this table over EP0 is bound to
     * it: svr_max_sequencers and the sequencer_state sub-table reference stay
     * at their initialized zeros while a 4-sequencer table exists. */
    rcp_regmap_general_init(&map);
    TEST_ASSERT_EQUAL_UINT16(0u, map.svr_max_sequencers);
    TEST_ASSERT_EQUAL_UINT16(0u, map.sequencer_state.capacity);

    rcp_sequencer_table_free(&table);
}

/* ── §11.2 / §11.2.1: TSCF-carried timed requests ──────────────────────────── */

static void test_tscf_presentation_time_and_abb_timed_encoder(void)
{
    rcp_server_endpoint_t ep;
    rcp_bytes_t           frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    rcp_bytes_t           timed;
    uint8_t               request_type = 0xFFu;
    uint8_t               msg_type     = 0u;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);

    /* TC18 §11.2 / §11.2.1: a request carried under a TSCF header is
     * postponed until that header's own avtp_timestamp presentation time.
     * rcp_server_endpoint_admit() receives neither the AVTP subtype nor the
     * timestamp -- only the bare ACF message -- so the request below
     * executes immediately no matter how far in the future the enclosing
     * TSCF header's presentation time lies. */
    TEST_ASSERT_EQUAL(RCP_SERVER_ADMIT_EXECUTE_NOW,
                      rcp_server_endpoint_admit(&ep, frame.data, frame.len, 0u,
                                                &request_type, NULL));
    TEST_ASSERT_EQUAL_UINT8(0u, request_type);

    /* TC18 §11.2 / §11.2.1 also encode a timed request whose presentation
     * time rides in the enclosing TSCF header as an ACF_ABB message. c-RCP's
     * only timed-request encoder emits the §11.2.2.5 ACF_GBB form with the
     * repurposed message_timestamp region -- there is no ACF_ABB variant. */
    timed = rcp_timed_encode_request((rcp_byte_bus_id_t)5u, 1000u, 1u, NULL, 0u);
    TEST_ASSERT_NOT_NULL(timed.data);
    TEST_ASSERT_EQUAL(RCP_ACF_OK, rcp_acf_peek_msg_type(timed.data, timed.len, &msg_type));
    TEST_ASSERT_EQUAL_HEX8(RCP_ACF_MSG_TYPE_GBB, msg_type);
    TEST_ASSERT_NOT_EQUAL(RCP_ACF_MSG_TYPE_ABB, msg_type);

    rcp_bytes_free(&frame);
    rcp_bytes_free(&timed);
    rcp_server_endpoint_destroy(&ep);
}

/* ── §12.7.7: the per-stream request watchdog is never kicked ──────────────── */

static void busy_wait_ms(unsigned ms)
{
    uint64_t start = rcp_monotonic_ms();

    while (rcp_monotonic_ms() - start < ms) {
        /* busy-wait: no sleep primitive is exported by rcp/clock.h */
    }
}

static void test_watchdog_overflows_despite_continuous_requests(void)
{
    rcp_watchdog_stream_cfg_t stream = {7u, true, 40u, true, true};
    rcp_watchdog_config_t     cfg    = rcp_watchdog_default_config();
    rcp_watchdog_keeper_t    *k;
    rcp_server_endpoint_t     ep;
    rcp_bytes_t               frame = standard_abb((rcp_byte_bus_id_t)5u, 1u);
    int                       elapsed_ms = 0;
    bool                      overflowed = false;

    TEST_ASSERT_NOT_NULL(frame.data);
    rcp_server_endpoint_init(&ep, true);
    k = rcp_watchdog_keeper_new(cfg, &stream, 1u);
    TEST_ASSERT_NOT_NULL(k);

    /* TC18 §12.7.7: the per-stream watchdog is reset with EVERY request
     * received from that RC Client, so it measures the gap between
     * consecutive requests. rcp_watchdog_keeper_kick() implements the reset
     * correctly but has no production call site -- no receive path in the
     * library kicks it. Delivering a request every 10 ms on stream 7 for far
     * longer than its 40 ms timeout therefore still overflows the watchdog,
     * driving a live, perfectly responsive client into its safe state. */
    while (elapsed_ms < 1000 && !overflowed) {
        TEST_ASSERT_TRUE(rcp_server_endpoint_submit(&ep, frame.data, frame.len));
        busy_wait_ms(10u);
        elapsed_ms += 10;
        overflowed = rcp_watchdog_keeper_status(k, 7u).overflowed;
    }
    TEST_ASSERT_TRUE(overflowed);
    TEST_ASSERT_TRUE(rcp_watchdog_keeper_status(k, 7u).enter_safe_state);

    rcp_watchdog_keeper_destroy(k);
    rcp_bytes_free(&frame);
    rcp_server_endpoint_destroy(&ep);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_transition_takes_neither_idle_nor_writer_input);
    RUN_TEST(test_locked_config_write_has_no_kind_and_no_error_response);
    RUN_TEST(test_hw_configured_admits_any_stream_and_any_endpoint);
    RUN_TEST(test_hw_unconfigured_ignores_claimant_and_request_kind);
    RUN_TEST(test_hw_configured_write_access_is_unrestricted);
    RUN_TEST(test_hw_configured_admits_tscf_and_gbb);
    RUN_TEST(test_discovery_write_authority_survives_rcp_configured);

    RUN_TEST(test_cold_start_target_and_standby_retention);
    RUN_TEST(test_hotstart_has_no_network_check_and_no_responder_stream);
    RUN_TEST(test_wakeup_repetition_ignores_other_valid_avtpdus);
    RUN_TEST(test_wake_completion_reenables_nothing_and_network_skips_all);
    RUN_TEST(test_sleep_entry_is_request_only_with_no_network_path);
    RUN_TEST(test_sleep_request_moves_one_endpoint_only);
    RUN_TEST(test_entry_gate_is_not_rechecked_before_the_mode_change);
    RUN_TEST(test_mode_change_is_unordered_against_the_response);
    RUN_TEST(test_entry_gate_is_scoped_to_one_endpoint_and_one_queue);

    RUN_TEST(test_disabled_endpoint_queues_config_requests_without_ack);
    RUN_TEST(test_response_queue_flush_period_is_carried_but_inert);
    RUN_TEST(test_gptp_lock_transition_issues_no_trigger_signal);

    RUN_TEST(test_sequencer_zero_state_ownership_and_regmap_wiring);
    RUN_TEST(test_tscf_presentation_time_and_abb_timed_encoder);
    RUN_TEST(test_watchdog_overflows_despite_continuous_requests);

    return UNITY_END();
}
