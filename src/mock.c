/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/mock.h"

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/e2e.h"
#include "rcp/request_cancel.h"
#include "rcp/request_chained.h"
#include "rcp/request_compound.h"
#include "rcp/scheduler.h"

#include <stdlib.h>
#include <string.h>

/* ── Endpoint slot ─────────────────────────────────────────────────────────── */

typedef struct {
    bool                         in_use;
    rcp_byte_bus_id_t            byte_bus_id;
    /* REQ-MOCK-031 (TC18 §12.9.1, issue #432): stream_scoped == false
     * (every slot's own zero-init default, matching in_use/byte_bus_id's
     * own "calloc()-safe" convention) means this slot was registered via
     * the plain, pre-existing rcp_mock_server_add_endpoint() and matches
     * a byte_bus_id lookup in ANY stream_id context, exactly this
     * module's pre-#432 behavior. stream_scoped == true (set only by
     * rcp_mock_server_add_endpoint_on_stream()) means this slot matches a
     * lookup ONLY when stream_id equals this slot's own stream_id field
     * below -- see find_slot_on_stream()'s own doc comment for the full
     * matching rule, and rcp_mock_server_add_endpoint_on_stream()'s own
     * doc comment (mock.h) for why two slots sharing one byte_bus_id
     * across two different stream_ids is exactly the case TC18 §12.9.1
     * requires this module to support. */
    bool                         stream_scoped;
    uint64_t                     stream_id;
    rcp_regmap_ep_generic_cfg_t  generic;
    rcp_server_endpoint_t        queue;
    rcp_mock_endpoint_handler_fn handler;
    /* REQ-ISELED-025: mutually exclusive with handler above -- a slot
     * registered via rcp_mock_server_add_endpoint_multi_response()
     * sets this instead, leaving handler NULL (see that function's own
     * doc comment, mock.h). Consulted only by rcp_mock_server_dispatch_
     * multi_response(); every other dispatch entry point ignores it. */
    rcp_mock_endpoint_multi_response_handler_fn multi_handler;
    void                        *user_data;
    /* REQ-GPIO-035/036: a real place to hold a response a caller has
     * decided NOT to answer a dispatch() call with yet (e.g. TC18
     * §13.7.4.3's own GPIO debounce-timing rule), until whatever
     * condition it was genuinely waiting for is later met -- see
     * rcp_mock_server_stash_deferred_response()'s own doc comment
     * (mock.h) for the full contract. Zeroed (no stashed response) for
     * every slot by default; .data != NULL means a real, not-yet-taken
     * response is held here. */
    rcp_bytes_t                  deferred_response;
    /* This test double's own stand-in for TC18 §12.7.1's
     * ep_req_crc_enable -- see
     * rcp_mock_server_set_endpoint_req_crc_enable()'s own doc comment
     * for why this can't just be read out of
     * rcp_regmap_ep_functional_cfg_t directly. Defaults false (every
     * slot starts zeroed -- srv itself is calloc()'d in
     * rcp_mock_server_new(), and rcp_mock_server_add_endpoint() never
     * sets this field). Consulted only by
     * rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e(); the plain
     * dispatch()/dispatch_frame() ignore it. */
    bool                         req_crc_enable;
    /* REQ-E2E-021 (issue #201): this test double's own stand-in for
     * TC18 §12.7.7 Table 24's own rx_enforce_e2e (a per-REQUEST-STREAM
     * config bit in the real spec, not per-endpoint -- kept here as a
     * per-endpoint stand-in for the same "type-erased slot has no way
     * to read a real config table generically" reason
     * req_crc_enable's own doc comment already gives). Defaults false
     * (RCP_E2E_CRC_ACTION_DROP_REQUEST), matching req_crc_enable's own
     * default disposition. Consulted only by
     * rcp_mock_server_dispatch_e2e()/_dispatch_frame_e2e() on a CRC
     * mismatch, to decide whether that mismatch also latches the whole
     * stream faulted (see rcp_mock_server_set_stream_fault_tracker()). */
    bool                         rx_enforce_e2e;
} rcp_mock_endpoint_slot_t;

/* ── The server double ─────────────────────────────────────────────────────── */

struct rcp_mock_server {
    rcp_lifecycle_state_t    state;
    rcp_regmap_general_t     regmap;
    rcp_mock_endpoint_slot_t endpoints[RCP_MOCK_MAX_ENDPOINTS];
    size_t                   endpoint_count;
    /* HW_config table (REQ-RMAP-040/041) -- see mock.h's own doc comment
     * on rcp_mock_server_set_hw_pin_map()/_hw_pin_map(). */
    rcp_regmap_hw_pin_map_entry_t hw_pin_map[RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES];
    size_t                        hw_pin_map_len;
    /* request-stream-cfg table (REQ-SEQ-013, issue #335) -- see mock.h's
     * own doc comment on rcp_mock_server_set_request_stream_cfg(). Its
     * own rx_stream_id fields are this server's only way to resolve an
     * incoming request's stream_id into the request_stream_index
     * identity TC18's own access-control-bearing fields (Table 28's
     * Request_stream_index among them) are expressed in terms of. */
    rcp_regmap_request_stream_cfg_t request_stream_cfg[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES];
    size_t                          request_stream_cfg_count;
    /* REQ-E2E-028/029 (issue #338): one caller-owned rcp_e2e_seq_tracker_t
     * per request_stream_cfg[] slot (same indexing, same
     * RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES size) -- the "previously
     * accepted request on this stream" state rcp_e2e_seq_evaluate() (e2e.h)
     * needs, one instance per configured request stream, matching that
     * function's own doc comment. No explicit init loop needed: srv is
     * calloc()'d in rcp_mock_server_new() and rcp_e2e_seq_tracker_init()'s
     * own "zero-initializes" contract is exactly calloc()'s own zero-fill
     * (has_prev = false, prev_seq = 0), the same convention every other
     * table here already relies on. */
    rcp_e2e_seq_tracker_t           seq_tracker[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES];
    /* REQ-E2E-046 (issue #336): one caller-owned rcp_e2e_stream_status_t
     * per request_stream_cfg[] slot -- the identical indexing
     * seq_tracker[] above already establishes. Composition, not
     * duplication, with stream_fault_tracker above (e2e.h's own file
     * header note on rcp_e2e_stream_status_t): stream_status[].crc is a
     * SEPARATE, per-request-stream-indexed rcp_e2e_stream_fault_t of its
     * own -- not aliased to *stream_fault_tracker (a distinct,
     * caller-owned, stream_id-KEYED tracker covering CRC alone) -- since
     * TC18's own rx_stream_status bit (Table 24) is a per-request-stream
     * register this server double has no other home for. No explicit
     * init loop needed: rcp_e2e_stream_status_init()'s own
     * "zero-initializes" contract is exactly calloc()'s own zero-fill,
     * the same convention seq_tracker[] itself already relies on. */
    rcp_e2e_stream_status_t         stream_status[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES];
    /* REQ-E2E-038/039 (issue #336): one caller-owned
     * rcp_fragment_reassembler_t per request_stream_cfg[] slot -- same
     * indexing convention as seq_tracker[]/stream_status[] above.
     * UNLIKE those two, this one is NOT zero-init-safe as-is:
     * rcp_fragment_reassembler_init()'s own max_total_len argument has
     * no zero-valued default that means anything useful (max_total_len
     * == 0 would reject every nonempty fragment as too-large
     * immediately) -- rcp_mock_server_new() calls
     * rcp_fragment_reassembler_init() explicitly for every slot, using
     * RCP_MOCK_FRAG_REASM_DEFAULT_MAX_TOTAL_LEN (this implementation's
     * own bound, deliberately NOT wired to the corresponding
     * request_stream_cfg[]'s own live-changeable
     * rx_stream_max_request_size field -- keeping that cross-cutting
     * sync concern out of this already-large feature's own scope; a
     * caller needing a different, tighter bound calls
     * rcp_fragment_reassembler_init() again directly against the slot
     * returned by rcp_mock_server_fragment_reassembler(), see that
     * accessor's own doc comment, mock.h).
     *
     * frag_first_header[]/frag_first_header_len[] remember the FIRST
     * fragment's own raw encoded header bytes for a sequence currently
     * being collected -- rcp_e2e_compute_fragmented_crc()'s own
     * first_fragment_header parameter needs exactly this, and nothing
     * else in this server double already keeps it once later fragments
     * have overwritten reasm's own state. Zero-init-safe (an all-zero
     * header/0 length is simply never consulted, since it is only ever
     * read after frag_reasm[]'s own is_collecting() is confirmed true,
     * which itself is only possible after this array was first written). */
    rcp_fragment_reassembler_t      frag_reasm[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES];
    uint8_t                         frag_first_header[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES]
                                                       [RCP_ACF_GBB_HEADER_LEN];
    size_t                          frag_first_header_len[RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES];
    /* REQ-SRV-018 (issue #201/tc18-gap post-backlog audit): the RC
     * Server's own edge-detector state for TC18 Table 37's gPTP lock-
     * established/lost trigger signals (server.h's own
     * rcp_server_gptp_trigger_state_t) -- zero-init-safe as-is
     * (has_previous starts false, exactly rcp_server_gptp_trigger_
     * state_init()'s own contract), but initialized explicitly in
     * rcp_mock_server_new() anyway to match this file's own "never rely
     * on calloc() alone for a type with its own _init()" convention. */
    rcp_server_gptp_trigger_state_t gptp_trigger_state;
    /* response-queue-cfg table (REQ-RMAP-034's own response-stream half)
     * -- see mock.h's own doc comment on
     * rcp_mock_server_set_response_queue_cfg(). Mirrors
     * request_stream_cfg's own shape exactly; previously had no backing
     * storage in this server double at all. */
    rcp_regmap_response_queue_cfg_t response_queue_cfg[RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES];
    size_t                          response_queue_cfg_count;
    /* REQ-RMAP-065/SRV-017 (tc18-gap post-backlog audit): per-response-
     * queue Flush_time bookkeeping -- rcp_respqueue_should_flush_by_time()
     * (respqueue.h) takes elapsed_since_last_transmit_us as a caller-
     * supplied value, exactly like e2e.h's own rcp_e2e_wd_evaluate()
     * convention; this module owns no clock of its own (see that
     * function's own file-header note), only the bookkeeping of WHEN a
     * response stream last transmitted, so a caller repeatedly checking
     * "is it time yet" doesn't have to track that itself.
     * response_queue_seeded[]/_last_transmit_us[] together are the same
     * "has_previous" idiom rcp_server_gptp_trigger_state_t (server.h)
     * already establishes: the very first check for a given response
     * stream only seeds last_transmit_us to that call's own now_us and
     * reports no heartbeat due, rather than measuring elapsed time
     * against an arbitrary zero epoch and firing a spurious heartbeat
     * immediately on srv's own first-ever check. Zero-init-safe as
     * arrays (seeded starts false, matching rcp_server_gptp_trigger_
     * state_t's own has_previous default) -- no explicit rcp_mock_
     * server_new() initialization needed. */
    bool                             response_queue_seeded[RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES];
    uint64_t              response_queue_last_transmit_us[RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES];
    /* EP_ID_config table (issue #335) -- see mock.h's own doc comment on
     * rcp_mock_server_set_ep_id_map(). Srv's own only way to know which
     * byte_bus_ids are bound to a given request_stream_index --
     * rcp_mock_server_broadcast_safe_state()'s sole data source. */
    rcp_regmap_ep_id_map_entry_t ep_id_map[RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES];
    size_t                       ep_id_map_count;
    /* The four optional-subsystem sections (REQ-RMAP-039, issue #336) --
     * see mock.h's own doc comment on
     * rcp_mock_server_set_network_interface_cfg() etc. Zero-initialized
     * by rcp_mock_server_new()'s own calloc(), matching every one of
     * these sections' own "len == 0 means not installed" default. */
    rcp_regmap_optional_subsystem_cfg_t network_interface_cfg;
    rcp_regmap_optional_subsystem_cfg_t physical_layer_cfg;
    rcp_regmap_optional_subsystem_cfg_t time_synch_cfg;
    rcp_regmap_optional_subsystem_cfg_t security_cfg;
    /* RC-Server functional-configuration content + discovery-stream
     * claim (REQ-RMAP-066, issue #336) -- see mock.h's own doc comment
     * on rcp_mock_server_svr_ep_cfg()/_discovery_claim()/
     * _set_discovery_timeout_us(). */
    rcp_regmap_svr_ep_cfg_t svr_ep_cfg;
    rcp_discovery_claim_t   discovery_claim;
    /* The sequencer-state registers compound/compound-wait requests read
     * and advance. Server-wide rather than per-endpoint: a sequencer is a
     * server register, and requests on different endpoints routinely
     * drive the same one. */
    rcp_sequencer_table_t    sequencers;
    /* REQ-WDG-010 (issue #201): not owned by srv -- see
     * rcp_mock_server_set_watchdog_keeper()'s own doc comment (mock.h)
     * for the full lifecycle contract. NULL (every slot starts zeroed,
     * srv itself is calloc()'d) disables kicking entirely, the default
     * for every rcp_mock_server_t. */
    rcp_watchdog_keeper_t   *watchdog;
    /* REQ-E2E-021 (issue #201): not owned by srv, same lifecycle
     * contract as watchdog above -- see
     * rcp_mock_server_set_stream_fault_tracker()'s own doc comment
     * (mock.h). NULL disables stream-fault blocking entirely, the
     * default for every rcp_mock_server_t. */
    rcp_e2e_stream_fault_tracker_t *stream_fault_tracker;
    /* REQ-AVTP-021/022 (issue #431, TC18 §13.3): this test double's own
     * in-process stand-in for the RC Server's own configuration of
     * §13.3's two "...or dropped, depending on the configuration"
     * rules -- see rcp_mock_server_set_tscf_unsupported_time_sync_
     * policy()'s own doc comment (mock.h) for the full contract.
     * RCP_AVTP_TSCF_FALLBACK_DROP is 0, so this field's own zero-init
     * (srv is calloc()'d in rcp_mock_server_new()) reproduces this
     * library's pre-issue-#431 unconditional-drop behavior by default,
     * matching every other "defaults false/0, matching the pre-fix
     * disposition" config field already in this struct. */
    rcp_avtp_tscf_fallback_t tscf_unsupported_time_sync_policy;
};

//cfusa:req REQ-MOCK-001
const char *rcp_mock_strerror(rcp_mock_errc_t e)
{
    switch (e) {
        case RCP_MOCK_OK:                   return "ok";
        case RCP_MOCK_ERR_DUPLICATE_BUS_ID: return "duplicate byte_bus_id";
        case RCP_MOCK_ERR_CAPACITY:         return "endpoint table full";
        case RCP_MOCK_ERR_NOT_FOUND:        return "byte_bus_id not registered";
        default:                            return "unknown rcp_mock_errc_t";
    }
}

//cfusa:req REQ-MOCK-002
rcp_mock_server_t *rcp_mock_server_new(void)
{
    rcp_mock_server_t *srv = (rcp_mock_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;

    srv->state = RCP_LIFECYCLE_HW_UNCONFIGURED;
    rcp_regmap_general_init(&srv->regmap);
    rcp_regmap_svr_ep_cfg_init(&srv->svr_ep_cfg);
    /* REQ-RMAP-066: rcp_regmap_svr_ep_cfg_init() has already set
     * svr_ep_cfg.svr_discovery_timeout to TC18's own stated default
     * (20000 us) -- route it through the same setter every later
     * change to this value uses, rather than duplicating the us->ms
     * conversion here, so srv->discovery_claim is never left holding a
     * timeout_ms out of sync with svr_ep_cfg's own current value. */
    rcp_mock_server_set_discovery_timeout_us(srv, srv->svr_ep_cfg.svr_discovery_timeout);
    /* REQ-E2E-038/039: every frag_reasm[] slot needs a real
     * max_total_len, not calloc()'s own zero -- see this array's own
     * declaration comment for why. */
    {
        size_t i;
        for (i = 0; i < RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES; i++) {
            rcp_fragment_reassembler_init(&srv->frag_reasm[i],
                                           RCP_MOCK_FRAG_REASM_DEFAULT_MAX_TOTAL_LEN);
        }
    }
    rcp_server_gptp_trigger_state_init(&srv->gptp_trigger_state);
    return srv;
}

//cfusa:req REQ-MOCK-003
void rcp_mock_server_destroy(rcp_mock_server_t *srv)
{
    size_t i;

    if (!srv) return;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use) {
            rcp_server_endpoint_destroy(&srv->endpoints[i].queue);
            /* REQ-GPIO-035/036: a stashed-but-never-taken deferred
             * response owns heap storage too -- same leak class the
             * comment below already calls out for frag_reasm[]. */
            rcp_bytes_free(&srv->endpoints[i].deferred_response);
        }
    }
    /* REQ-E2E-038/039: frag_reasm[] slots own heap storage
     * (rcp_fragment_reassembler_t's own buf field) once any fragment has
     * ever been fed to them -- must be released here, not just
     * memset/freed along with srv itself, or every server that ever
     * exercised fragmentation leaks. */
    for (i = 0; i < RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES; i++) {
        rcp_fragment_reassembler_destroy(&srv->frag_reasm[i]);
    }
    rcp_sequencer_table_free(&srv->sequencers);
    free(srv);
}

//cfusa:req REQ-MOCK-004
rcp_lifecycle_state_t rcp_mock_server_state(const rcp_mock_server_t *srv)
{
    return srv->state;
}

//cfusa:req REQ-MOCK-005
//cfusa:req REQ-RMAP-023
rcp_lifecycle_errc_t rcp_mock_server_transition(rcp_mock_server_t *srv,
                                                 rcp_lifecycle_state_t target,
                                                 const rcp_lifecycle_plausibility_snapshot_t *snap,
                                                 rcp_lifecycle_writer_ctx_t writer,
                                                 bool all_other_eps_idle)
{
    rcp_lifecycle_errc_t rc =
        rcp_lifecycle_transition(&srv->state, target, snap, writer, all_other_eps_idle);
    /* srv->state reflects its own post-call value either way (updated on
     * success, left unchanged on failure per rcp_lifecycle_transition()'s
     * own doc comment) -- an unconditional sync here is therefore always
     * correct, not just an on-success special case. Keeps
     * regmap.svr_lifecycle_state (REQ-RMAP-023's own content field) from
     * ever silently drifting out of sync with the authoritative state
     * this same call just updated. */
    srv->regmap.svr_lifecycle_state = (uint8_t)srv->state;
    return rc;
}

//cfusa:req REQ-PWRMODE-019
bool rcp_mock_server_pwrmode_resume(rcp_mock_server_t *srv, rcp_pwrmode_handshake_t *hs)
{
    size_t i;

    if (!rcp_pwrmode_handshake_resume_queues(hs)) return false;

    /* TC18 §12.4.1: "After reception of valid message from the sleep
     * request Client all used endpoints and response queues will be
     * enabled." power.h's own rcp_pwrmode_handshake_resume_queues()
     * deliberately never touches server.h (that module's own file header:
     * "driving the actual re-init sequence... remains a caller's job") --
     * this srv-aware wrapper is that caller, re-enabling every registered
     * endpoint's queue the same way rcp_mock_server_set_endpoint_enable()
     * does one at a time. Response-queue objects and heartbeat-stream
     * re-emission have no implementation anywhere in this codebase yet
     * (see test_flush_triggers_and_heartbeat_are_absent()) -- a separate,
     * already-tracked gap this function cannot close. */
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use) {
            rcp_server_endpoint_set_enable(&srv->endpoints[i].queue, true);
        }
    }
    return true;
}

//cfusa:req REQ-MOCK-006
rcp_regmap_general_t *rcp_mock_server_regmap(rcp_mock_server_t *srv)
{
    return &srv->regmap;
}

//cfusa:req REQ-RMAP-040
//cfusa:req REQ-RMAP-032
bool rcp_mock_server_set_hw_pin_map(rcp_mock_server_t *srv,
                                     const rcp_regmap_hw_pin_map_entry_t *entries, size_t len)
{
    if (len > RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES) return false;

    if (len > 0) memcpy(srv->hw_pin_map, entries, len * sizeof(*entries));
    srv->hw_pin_map_len = len;
    /* REQ-RMAP-032: svr_io_pin_count (Table 20, wire-readable) is this
     * server's own report of how many HW pins it has -- previously never
     * set anywhere, so it silently stayed 0 regardless of how many pins
     * were actually configured here. len is already bounds-checked above
     * against RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES (64), well within
     * uint16_t. */
    srv->regmap.svr_io_pin_count = (uint16_t)len;
    return true;
}

//cfusa:req REQ-RMAP-040
const rcp_regmap_hw_pin_map_entry_t *rcp_mock_server_hw_pin_map(const rcp_mock_server_t *srv,
                                                                  size_t *out_len)
{
    *out_len = srv->hw_pin_map_len;
    return srv->hw_pin_map;
}

//cfusa:req REQ-SEQ-013
//cfusa:req REQ-RMAP-034
bool rcp_mock_server_set_request_stream_cfg(rcp_mock_server_t *srv,
                                             const rcp_regmap_request_stream_cfg_t *entries,
                                             size_t count)
{
    if (count > RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES) return false;

    if (count > 0) memcpy(srv->request_stream_cfg, entries, count * sizeof(*entries));
    srv->request_stream_cfg_count = count;
    /* REQ-RMAP-034 (request-stream half): svr_request_stream_cfg_capacity
     * (Table 20, an entry count not a byte length) previously never set
     * anywhere, staying 0 regardless of how many request streams were
     * actually configured. count is already bounds-checked above against
     * RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES (64), well within
     * uint8_t. */
    srv->regmap.svr_request_stream_cfg_capacity = (uint8_t)count;
    return true;
}

//cfusa:req REQ-RMAP-034
bool rcp_mock_server_set_response_queue_cfg(rcp_mock_server_t *srv,
                                             const rcp_regmap_response_queue_cfg_t *entries,
                                             size_t count)
{
    if (count > RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES) return false;

    if (count > 0) memcpy(srv->response_queue_cfg, entries, count * sizeof(*entries));
    srv->response_queue_cfg_count = count;
    /* REQ-RMAP-034 (response-stream half): svr_response_stream_cfg_
     * capacity (Table 20, an entry count not a byte length) previously
     * had no backing storage in this server double to sync from at all.
     * count is already bounds-checked above against
     * RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES (64), well within
     * uint8_t. */
    srv->regmap.svr_response_stream_cfg_capacity = (uint8_t)count;
    return true;
}

//cfusa:req REQ-RMAP-065
//cfusa:req REQ-SRV-017
bool rcp_mock_server_check_response_queue_heartbeat(rcp_mock_server_t *srv,
                                                      uint8_t response_stream_index,
                                                      const uint8_t mac[6], uint64_t now_us,
                                                      rcp_bytes_t *out_heartbeat)
{
    size_t                            idx;
    rcp_regmap_response_queue_cfg_t *cfg;
    uint64_t                          elapsed_us;
    rcp_avtp_ntscf_header_t           hdr;

    memset(out_heartbeat, 0, sizeof(*out_heartbeat));

    if (response_stream_index == 0u || (size_t)response_stream_index > srv->response_queue_cfg_count) {
        return false;
    }
    idx = (size_t)response_stream_index - 1u;
    cfg = &srv->response_queue_cfg[idx];

    if (!srv->response_queue_seeded[idx]) {
        srv->response_queue_seeded[idx]         = true;
        srv->response_queue_last_transmit_us[idx] = now_us;
        return false; /* nothing to measure elapsed time against yet */
    }

    if (now_us < srv->response_queue_last_transmit_us[idx]) {
        return false; /* non-monotonic caller input -- fail toward no heartbeat
                          rather than an unsigned-underflow false positive */
    }

    elapsed_us = now_us - srv->response_queue_last_transmit_us[idx];
    if (!rcp_respqueue_should_flush_by_time(elapsed_us, cfg->flush_time_us)) {
        return false;
    }

    memset(&hdr, 0, sizeof(hdr));
    hdr.sv        = 1;
    hdr.stream_id = rcp_regmap_response_queue_stream_id(cfg, mac);

    *out_heartbeat = rcp_avtp_encode_ntscf(&hdr, NULL, 0);
    /* Only advance the bookkeeping on a real success -- an allocation
     * failure means nothing was actually transmitted, so the next check
     * should still see this heartbeat as owed and retry, not silently
     * skip a whole Flush_time interval because one attempt happened to
     * fail. */
    if (out_heartbeat->data != NULL) {
        srv->response_queue_last_transmit_us[idx] = now_us;
    }
    return true;
}

//cfusa:req REQ-WAKEUP-018
bool rcp_mock_server_wakeup_repetition_interval_us(const rcp_mock_server_t *srv,
                                                     uint8_t request_stream_index,
                                                     uint32_t *out_interval_us)
{
    size_t  req_idx;
    uint8_t resp_stream_index;
    size_t  resp_idx;

    *out_interval_us = 0;

    if (request_stream_index == 0u ||
        (size_t)request_stream_index > srv->request_stream_cfg_count) {
        return false;
    }
    req_idx = (size_t)request_stream_index - 1u;

    resp_stream_index = srv->request_stream_cfg[req_idx].rx_resp_stream_index;
    if (resp_stream_index == 0u ||
        (size_t)resp_stream_index > srv->response_queue_cfg_count) {
        return false;
    }
    resp_idx = (size_t)resp_stream_index - 1u;

    *out_interval_us = srv->response_queue_cfg[resp_idx].flush_time_us;
    return true;
}

//cfusa:req REQ-E2E-029
//cfusa:req REQ-E2E-030
//cfusa:req REQ-E2E-045
//cfusa:req REQ-RMAP-037
bool rcp_mock_server_set_ep_id_map(rcp_mock_server_t *srv,
                                    const rcp_regmap_ep_id_map_entry_t *entries, size_t count)
{
    if (count > RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES) return false;

    if (count > 0) memcpy(srv->ep_id_map, entries, count * sizeof(*entries));
    srv->ep_id_map_count = count;
    /* REQ-RMAP-037: svr_ep_bytebus_id_map_capacity (Table 20, an entry
     * count not a byte length) previously never set anywhere, staying 0
     * regardless of how many EP_ID_config entries were actually
     * configured. count is already bounds-checked above against
     * RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES, well within uint8_t (confirmed
     * by the field's own 8-bit wire width). */
    srv->regmap.svr_ep_bytebus_id_map_capacity = (uint8_t)count;
    return true;
}

/* Shared body for the four optional-subsystem section setters below --
 * one bounded memcpy plus a capacity-register sync, the identical shape
 * every other _set_*() function above already follows, generalized
 * since all four sections share one storage type
 * (rcp_regmap_optional_subsystem_cfg_t). *capacity_reg is whichever of
 * srv->regmap's own four svr_*_cfg_capacity fields this particular
 * section owns. */
static bool optional_subsystem_cfg_set(rcp_regmap_optional_subsystem_cfg_t *cfg,
                                        uint16_t *capacity_reg, const uint8_t *data, size_t len)
{
    if (len > RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_MAX_OCTETS) return false;

    if (len > 0) memcpy(cfg->data, data, len);
    cfg->len     = len;
    *capacity_reg = (uint16_t)len; /* len is already bounds-checked above against
                                       RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_MAX_OCTETS (256),
                                       well within uint16_t. */
    return true;
}

//cfusa:req REQ-RMAP-039
bool rcp_mock_server_set_network_interface_cfg(rcp_mock_server_t *srv, const uint8_t *data,
                                                size_t len)
{
    return optional_subsystem_cfg_set(&srv->network_interface_cfg,
                                       &srv->regmap.svr_network_interface_cfg_capacity, data, len);
}

//cfusa:req REQ-RMAP-039
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_network_interface_cfg(rcp_mock_server_t *srv)
{
    return &srv->network_interface_cfg;
}

//cfusa:req REQ-RMAP-039
bool rcp_mock_server_set_physical_layer_cfg(rcp_mock_server_t *srv, const uint8_t *data,
                                             size_t len)
{
    return optional_subsystem_cfg_set(&srv->physical_layer_cfg,
                                       &srv->regmap.svr_physical_layer_cfg_capacity, data, len);
}

//cfusa:req REQ-RMAP-039
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_physical_layer_cfg(rcp_mock_server_t *srv)
{
    return &srv->physical_layer_cfg;
}

//cfusa:req REQ-RMAP-039
bool rcp_mock_server_set_time_synch_cfg(rcp_mock_server_t *srv, const uint8_t *data, size_t len)
{
    return optional_subsystem_cfg_set(&srv->time_synch_cfg, &srv->regmap.svr_time_synch_cfg_capacity,
                                       data, len);
}

//cfusa:req REQ-RMAP-039
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_time_synch_cfg(rcp_mock_server_t *srv)
{
    return &srv->time_synch_cfg;
}

//cfusa:req REQ-RMAP-039
bool rcp_mock_server_set_security_cfg(rcp_mock_server_t *srv, const uint8_t *data, size_t len)
{
    return optional_subsystem_cfg_set(&srv->security_cfg, &srv->regmap.svr_security_cfg_capacity,
                                       data, len);
}

//cfusa:req REQ-RMAP-039
rcp_regmap_optional_subsystem_cfg_t *rcp_mock_server_security_cfg(rcp_mock_server_t *srv)
{
    return &srv->security_cfg;
}

//cfusa:req REQ-RMAP-066
rcp_regmap_svr_ep_cfg_t *rcp_mock_server_svr_ep_cfg(rcp_mock_server_t *srv)
{
    return &srv->svr_ep_cfg;
}

//cfusa:req REQ-RMAP-066
rcp_discovery_claim_t *rcp_mock_server_discovery_claim(rcp_mock_server_t *srv)
{
    return &srv->discovery_claim;
}

//cfusa:req REQ-RMAP-066
void rcp_mock_server_set_discovery_timeout_us(rcp_mock_server_t *srv, uint16_t timeout_us)
{
    srv->svr_ep_cfg.svr_discovery_timeout = timeout_us;
    /* No rcp_discovery_claim_init() call here -- that would reset
     * held/claimant/deadline_ms too, which this function's own doc
     * comment promises NOT to do. srv->discovery_claim's own
     * zero-initial state (held=false, claimant=0, deadline_ms=0, from
     * rcp_mock_server_new()'s own calloc()) already matches exactly
     * what rcp_discovery_claim_init() would produce for a fresh,
     * unheld claim -- the same "calloc's own zero-fill already
     * satisfies this module's own init contract" convention this
     * server double relies on elsewhere (seq_tracker[], etc.).
     * Truncating division, not rounding -- matches every other µs/ms
     * boundary conversion in this codebase's own convention of never
     * silently rounding a caller's own requested bound UP. */
    srv->discovery_claim.timeout_ms = (uint32_t)timeout_us / 1000u;
}

/* Finds the slot addressed at byte_bus_id, or NULL if none is registered. */
static rcp_mock_endpoint_slot_t *find_slot(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use && srv->endpoints[i].byte_bus_id == byte_bus_id) {
            return &srv->endpoints[i];
        }
    }
    return NULL;
}

/* const counterpart of find_slot(), for read-only accessors. */
static const rcp_mock_endpoint_slot_t *find_slot_const(const rcp_mock_server_t *srv,
                                                         rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (srv->endpoints[i].in_use && srv->endpoints[i].byte_bus_id == byte_bus_id) {
            return &srv->endpoints[i];
        }
    }
    return NULL;
}

/* REQ-MOCK-031 (TC18 §12.9.1, issue #432): "In dependence on the
 * stream_id and byte_bus_id the RC Server determines the endpoint that
 * is addressed." Stream-scoped counterpart of find_slot(), used by this
 * module's own real request-dispatch entry points (the ones that already
 * carry a stream_id of their own) rather than find_slot() itself, so the
 * SAME byte_bus_id can correctly resolve to two different endpoints
 * registered on two different stream_ids.
 *
 * Matching rule for a slot addressed at byte_bus_id: a slot with
 * stream_scoped == false (registered via the plain, unscoped
 * rcp_mock_server_add_endpoint()) matches unconditionally, exactly this
 * module's pre-#432 "byte_bus_id alone" behavior; a slot with
 * stream_scoped == true (registered via
 * rcp_mock_server_add_endpoint_on_stream()) matches only when stream_id
 * equals that slot's own recorded stream_id. At most one slot can ever
 * satisfy this for a given (stream_id, byte_bus_id) pair: both add-
 * endpoint entry points' own duplicate-byte_bus_id rejection together
 * rule out every other combination -- see
 * rcp_mock_server_add_endpoint_on_stream()'s own doc comment (mock.h)
 * for why. */
static rcp_mock_endpoint_slot_t *find_slot_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                                       rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        rcp_mock_endpoint_slot_t *slot = &srv->endpoints[i];
        if (slot->in_use && slot->byte_bus_id == byte_bus_id &&
            (!slot->stream_scoped || slot->stream_id == stream_id)) {
            return slot;
        }
    }
    return NULL;
}

//cfusa:req REQ-MOCK-007
//cfusa:req REQ-MOCK-008
rcp_mock_errc_t rcp_mock_server_add_endpoint(rcp_mock_server_t *srv,
                                              rcp_byte_bus_id_t byte_bus_id,
                                              uint8_t ep_type, bool ep_enable,
                                              rcp_mock_endpoint_handler_fn handler,
                                              void *user_data)
{
    size_t i;
    rcp_mock_endpoint_slot_t *slot = NULL;

    if (find_slot(srv, byte_bus_id)) return RCP_MOCK_ERR_DUPLICATE_BUS_ID;
    if (srv->endpoint_count >= RCP_MOCK_MAX_ENDPOINTS) return RCP_MOCK_ERR_CAPACITY;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (!srv->endpoints[i].in_use) {
            slot = &srv->endpoints[i];
            break;
        }
    }
    /* Unreachable given the endpoint_count check above, but guarded rather
     * than trusted -- fail closed instead of dereferencing NULL. */
    if (!slot) return RCP_MOCK_ERR_CAPACITY;

    memset(slot, 0, sizeof(*slot));
    slot->in_use      = true;
    slot->byte_bus_id = byte_bus_id;
    rcp_regmap_ep_generic_cfg_init(&slot->generic);
    slot->generic.ep_type = ep_type;
    slot->generic.ep_used = true;
    rcp_server_endpoint_init(&slot->queue, ep_enable);
    slot->handler   = handler;
    slot->user_data = user_data;

    srv->endpoint_count++;
    srv->regmap.svr_ep_count = (uint16_t)srv->endpoint_count;
    /* REQ-RMAP-036: svr_ep_generic_cfg_capacity (Table 20) is the LENGTH
     * OF THE EP CONFIG REGISTER SECTION IN BYTES, not an entry count
     * (regmap.h's own field doc comment) -- previously never set
     * anywhere, staying 0 regardless of how many endpoints (each
     * carrying its own rcp_regmap_ep_generic_cfg_t, one EP_generic_cfg
     * row) were actually registered. 12 is the same per-entry byte
     * stride rcp_regmap_ep0_decode_write_request()/_encode_read_
     * response() already compute ep_generic_cfg_len from (src/regmap.c,
     * ep_generic_cfg_count * 12u); endpoint_count is well within
     * uint16_t even at RCP_MOCK_MAX_ENDPOINTS (64 * 12 = 768). */
    srv->regmap.svr_ep_generic_cfg_capacity = (uint16_t)(srv->endpoint_count * 12u);
    return RCP_MOCK_OK;
}

//cfusa:req REQ-MOCK-031
rcp_mock_errc_t rcp_mock_server_add_endpoint_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                                        rcp_byte_bus_id_t byte_bus_id,
                                                        uint8_t ep_type, bool ep_enable,
                                                        rcp_mock_endpoint_handler_fn handler,
                                                        void *user_data)
{
    size_t i;
    rcp_mock_endpoint_slot_t *slot = NULL;

    /* Deliberately its own self-contained slot-allocation body, not a
     * thin wrapper around rcp_mock_server_add_endpoint() the way
     * rcp_mock_server_add_endpoint_multi_response() is -- that function's
     * own reuse works only because it registers unscoped, with the exact
     * same duplicate-check semantics add_endpoint() itself already
     * applies. This function's own duplicate check is different on
     * purpose (find_slot_on_stream(), not find_slot()): the entire point
     * of REQ-MOCK-031 is that the SAME byte_bus_id, scoped to two
     * DIFFERENT stream_ids, must be allowed to coexist as two separate
     * slots -- something routing every call through add_endpoint()'s own
     * find_slot()-based rejection could never permit. Following this
     * file's own "new function, not a breaking change" convention
     * (issue #432): rcp_mock_server_add_endpoint() itself is completely
     * untouched by this addition, so its own existing global-uniqueness
     * contract (and every one of its ~100+ existing call sites) keeps its
     * exact prior behavior. */
    if (find_slot_on_stream(srv, stream_id, byte_bus_id)) return RCP_MOCK_ERR_DUPLICATE_BUS_ID;
    if (srv->endpoint_count >= RCP_MOCK_MAX_ENDPOINTS) return RCP_MOCK_ERR_CAPACITY;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (!srv->endpoints[i].in_use) {
            slot = &srv->endpoints[i];
            break;
        }
    }
    /* Unreachable given the endpoint_count check above, but guarded rather
     * than trusted -- matches rcp_mock_server_add_endpoint()'s own
     * identical guard. */
    if (!slot) return RCP_MOCK_ERR_CAPACITY;

    memset(slot, 0, sizeof(*slot));
    slot->in_use        = true;
    slot->byte_bus_id   = byte_bus_id;
    slot->stream_scoped = true;
    slot->stream_id     = stream_id;
    rcp_regmap_ep_generic_cfg_init(&slot->generic);
    slot->generic.ep_type = ep_type;
    slot->generic.ep_used = true;
    rcp_server_endpoint_init(&slot->queue, ep_enable);
    slot->handler   = handler;
    slot->user_data = user_data;

    srv->endpoint_count++;
    srv->regmap.svr_ep_count = (uint16_t)srv->endpoint_count;
    /* Same REQ-RMAP-036 bookkeeping as rcp_mock_server_add_endpoint()'s
     * own identical line -- see that function's own doc comment. */
    srv->regmap.svr_ep_generic_cfg_capacity = (uint16_t)(srv->endpoint_count * 12u);
    return RCP_MOCK_OK;
}

//cfusa:req REQ-ISELED-025
rcp_mock_errc_t rcp_mock_server_add_endpoint_multi_response(
    rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id, uint8_t ep_type, bool ep_enable,
    rcp_mock_endpoint_multi_response_handler_fn handler, void *user_data)
{
    rcp_mock_errc_t            rc;
    rcp_mock_endpoint_slot_t *slot;

    /* Reuses rcp_mock_server_add_endpoint()'s own slot-allocation logic
     * entirely (same capacity/duplicate-bus-id checks, same
     * svr_ep_count/svr_ep_generic_cfg_capacity bookkeeping) rather than
     * duplicating it -- registered with a NULL plain handler, then
     * patched below to carry the real multi-response one instead. */
    rc = rcp_mock_server_add_endpoint(srv, byte_bus_id, ep_type, ep_enable, NULL, NULL);
    if (rc != RCP_MOCK_OK) return rc;

    slot = find_slot(srv, byte_bus_id);
    /* Unreachable -- the call above just successfully created this
     * exact slot; guarded rather than trusted, matching this file's
     * own established "fail closed instead of dereferencing NULL"
     * convention (see rcp_mock_server_add_endpoint()'s own identical
     * guard). */
    if (!slot) return RCP_MOCK_ERR_CAPACITY;

    slot->multi_handler = handler;
    slot->user_data     = user_data;
    return RCP_MOCK_OK;
}

//cfusa:req REQ-MOCK-009
bool rcp_mock_server_remove_endpoint(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    rcp_server_endpoint_destroy(&slot->queue);
    /* REQ-GPIO-035/036: same leak class rcp_mock_server_destroy() must
     * also guard against -- see that function's own identical fix. */
    rcp_bytes_free(&slot->deferred_response);
    memset(slot, 0, sizeof(*slot));

    srv->endpoint_count--;
    srv->regmap.svr_ep_count = (uint16_t)srv->endpoint_count;
    srv->regmap.svr_ep_generic_cfg_capacity = (uint16_t)(srv->endpoint_count * 12u);
    return true;
}

//cfusa:req REQ-RMAP-036
size_t rcp_mock_server_ep_generic_cfg_view(const rcp_mock_server_t *srv,
                                            rcp_regmap_ep_generic_cfg_t *out, size_t out_capacity)
{
    size_t i;
    size_t written = 0;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (!srv->endpoints[i].in_use) continue;
        if (written < out_capacity) out[written] = srv->endpoints[i].generic;
        written++;
    }

    return srv->endpoint_count;
}

//cfusa:req REQ-RMAP-036
bool rcp_mock_server_apply_ep_generic_cfg(rcp_mock_server_t *srv,
                                           const rcp_regmap_ep_generic_cfg_t *entries, size_t count)
{
    size_t i;
    size_t taken = 0;

    if (count != srv->endpoint_count) return false;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS && taken < count; i++) {
        if (!srv->endpoints[i].in_use) continue;
        srv->endpoints[i].generic = entries[taken];
        taken++;
    }

    return true;
}

//cfusa:req REQ-E2E-046
bool rcp_mock_server_stream_status_rx_blocked(const rcp_mock_server_t *srv, uint64_t stream_id)
{
    uint8_t stream_index = rcp_regmap_request_stream_cfg_resolve_index(
        srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);

    if (stream_index == 0u) return false;
    return rcp_e2e_stream_status_rx_blocked(&srv->stream_status[stream_index - 1u]);
}

//cfusa:req REQ-E2E-046
bool rcp_mock_server_check_watchdog(rcp_mock_server_t *srv, uint8_t request_stream_index,
                                     uint64_t elapsed_since_last_kick_ms,
                                     rcp_e2e_wd_result_t *out_result)
{
    size_t                            idx;
    const rcp_regmap_request_stream_cfg_t *cfg;

    memset(out_result, 0, sizeof(*out_result));

    if (request_stream_index == 0u ||
        (size_t)request_stream_index > srv->request_stream_cfg_count) {
        return false;
    }
    idx = (size_t)request_stream_index - 1u;
    cfg = &srv->request_stream_cfg[idx];

    *out_result = rcp_e2e_wd_evaluate(cfg->rx_wd_enable, cfg->rx_wd_timeout_ms,
                                       cfg->rx_wd_safestate_enable, cfg->rx_wd_info_enable,
                                       elapsed_since_last_kick_ms);

    /* Latched every time, not just on overflow -- same "checked every
     * time" reasoning frame_seq_gate_admits() already applies to the
     * sequence cause. */
    rcp_e2e_stream_status_note_wd(&srv->stream_status[idx], *out_result);

    if (out_result->enter_safe_state) {
        (void)rcp_mock_server_broadcast_safe_state(srv, request_stream_index);
    }
    return true;
}

//cfusa:req REQ-E2E-038
//cfusa:req REQ-E2E-039
rcp_fragment_reassembler_t *rcp_mock_server_fragment_reassembler(rcp_mock_server_t *srv,
                                                                  uint64_t stream_id)
{
    uint8_t stream_index = rcp_regmap_request_stream_cfg_resolve_index(
        srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);

    if (stream_index == 0u) return NULL;
    return &srv->frag_reasm[stream_index - 1u];
}

//cfusa:req REQ-MOCK-010
bool rcp_mock_server_set_endpoint_enable(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                          bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    rcp_server_endpoint_set_enable(&slot->queue, enable);
    return true;
}

//cfusa:req REQ-E2E-031
bool rcp_mock_server_set_endpoint_req_crc_enable(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id, bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    slot->req_crc_enable = enable;
    return true;
}

//cfusa:req REQ-E2E-021
bool rcp_mock_server_set_endpoint_rx_enforce_e2e(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id, bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    slot->rx_enforce_e2e = enable;
    return true;
}

//cfusa:req REQ-AVTP-021
//cfusa:req REQ-AVTP-022
void rcp_mock_server_set_tscf_unsupported_time_sync_policy(rcp_mock_server_t       *srv,
                                                             rcp_avtp_tscf_fallback_t policy)
{
    srv->tscf_unsupported_time_sync_policy = policy;
}

//cfusa:req REQ-WDG-010
void rcp_mock_server_set_watchdog_keeper(rcp_mock_server_t *srv,
                                          rcp_watchdog_keeper_t *keeper)
{
    srv->watchdog = keeper;
}

//cfusa:req REQ-E2E-021
void rcp_mock_server_set_stream_fault_tracker(rcp_mock_server_t *srv,
                                               rcp_e2e_stream_fault_tracker_t *tracker)
{
    srv->stream_fault_tracker = tracker;
}

//cfusa:req REQ-MOCK-011
size_t rcp_mock_server_endpoint_queue_len(const rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    const rcp_mock_endpoint_slot_t *slot = find_slot_const(srv, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_queue_len(&slot->queue);
}

/* Runs slot's own handler (if any) against one already-dequeued request,
 * writing its result into *out_response (left zeroed if handler is NULL or
 * declines to populate it). Shared by rcp_mock_server_dispatch()'s
 * immediate-execution path and rcp_mock_server_drain_endpoint(). */
static void run_handler(rcp_mock_endpoint_slot_t *slot, const uint8_t *request, size_t request_len,
                         rcp_bytes_t *out_response)
{
    memset(out_response, 0, sizeof(*out_response));
    if (slot->handler) {
        slot->handler(request, request_len, out_response, slot->user_data);
    }
}

//cfusa:req REQ-MOCK-012
//cfusa:req REQ-MOCK-013
//cfusa:req REQ-MOCK-014
//cfusa:req REQ-MOCK-015
//cfusa:req REQ-MOCK-028
/* Applies an already-classified cancellation request against slot's own
 * request store. Which of the three cancellation opcodes it is decides
 * what gets removed; a clear-single additionally needs its target
 * transaction_num decoded out of the message.
 *
 * TC18 §11.2.3.3: "The request initiating the cancellation will create an
 * error response with the error code = REQUEST_NOT_FOUND, when the
 * clear_transaction_num was not found." That response carries the
 * cancellation request's own byte_bus_id/transaction_num (TC18 §12.9.6's
 * general rule), not the not-found target's -- out_response is populated
 * accordingly when rcp_server_endpoint_cancel_single() reports
 * RCP_CANCEL_RESULT_NOT_FOUND. byte_bus_id is this endpoint's own
 * address (see finish_admission()'s own doc comment for why this is
 * already known to the caller rather than re-decoded).
 *
 * Clear-all and clear-non-safestate cancel every request each removes
 * with its own REQUEST_CANCELED error response (TC18 §11.2.3, one
 * response per cancelled request, not one for the cancellation itself)
 * -- a multi-response fanout this function's single out_response cannot
 * represent; not attempted here, tracked separately
 * (github.com/SoundMatt/c-RCP/issues/163). A clear-single's own
 * successful cascade (below, REQ-CANCEL-012) carries the identical
 * fanout gap for the same reason -- each cascaded removal is, by the
 * same TC18 §11.2.3 rule, its own REQUEST_CANCELED response too. */
static void apply_cancellation(rcp_mock_endpoint_slot_t *slot, uint8_t request_type,
                                const uint8_t *request, size_t request_len,
                                rcp_byte_bus_id_t byte_bus_id, rcp_bytes_t *out_response)
{
    rcp_byte_bus_id_t    bus;
    uint8_t              target_tn;
    uint8_t              tn;
    rcp_cancel_result_t  result;

    switch (request_type) {
    case RCP_REQUEST_TYPE_CLEAR_ALL:
        (void)rcp_server_endpoint_cancel_all(&slot->queue);
        break;

    case RCP_REQUEST_TYPE_CLEAR_NON_SAFESTATE:
        (void)rcp_server_endpoint_cancel_non_safestate(&slot->queue);
        break;

    case RCP_REQUEST_TYPE_CLEAR_SINGLE:
        if (rcp_cancel_decode_clear_single(request, request_len, &bus, &target_tn, &tn) ==
            RCP_CANCEL_OK) {
            /* REQ-CANCEL-012: read out BEFORE cancelling -- a successful
             * rcp_server_endpoint_cancel_single() call below already
             * frees and clears the target's own store slot, so its
             * chain_group/chain_position have to be captured first if
             * TC18 §11.2.3's cascade rule (cancelling this request also
             * cancels every chained successor at or after its own
             * position) is to be applied afterward. Left at their
             * zero-valued defaults (chain_group 0, the "not part of a
             * chain" sentinel) if no matching entry is found -- the
             * cascade call below is then a guaranteed no-op, exactly
             * matching a target that reports NOT_FOUND. */
            uint32_t target_chain_group    = 0;
            uint8_t  target_chain_position = 0;
            size_t   j;

            for (j = 0; j < RCP_SERVER_MAX_PENDING; j++) {
                if (slot->queue.pending[j].in_use &&
                    slot->queue.pending[j].transaction_num == target_tn) {
                    target_chain_group    = slot->queue.pending[j].chain_group;
                    target_chain_position = slot->queue.pending[j].chain_position;
                    break;
                }
            }

            /* Every request still sitting in the store is by definition
             * queued rather than under execution -- this dispatcher runs a
             * selected request to completion synchronously inside
             * rcp_mock_server_tick(), so nothing is ever observably
             * mid-execution here. */
            result = rcp_server_endpoint_cancel_single(&slot->queue, target_tn,
                                                        RCP_CANCEL_LIFECYCLE_QUEUED);
            if (result == RCP_CANCEL_RESULT_NOT_FOUND) {
                *out_response =
                    rcp_acf_build_error_response(byte_bus_id, tn, RCP_ERROR_REQUEST_NOT_FOUND);
            } else if (result == RCP_CANCEL_RESULT_CANCELED) {
                (void)rcp_server_endpoint_cancel_chain_from(&slot->queue, target_chain_group,
                                                              target_chain_position);
            }
        }
        break;

    default:
        break;
    }
}

/* Maps one server.h admission outcome onto this module's own dispatch
 * result, running the endpoint's handler for the execute-now case.
 *
 * error is rcp_server_endpoint_admit()'s *out_error: RCP_ERROR_NONE for
 * every outcome except the rejection paths that determined a specific
 * TC18 Table 30 code (see that function's own doc comment for which
 * paths currently do). When non-RCP_ERROR_NONE, out_response is
 * populated (instead of being left zeroed) with TC18 §11.3.1's
 * Acknowledge-shaped storage-admission-rejection response
 * (rcp_acf_build_acknowledge_rejected_response(), evt=0xF/err=1) -- NOT
 * the §11.3.4 Error Response (rcp_acf_build_error_response()) other
 * REJECTED-shaped call sites in this file use, since RCP_SERVER_ADMIT_
 * REJECTED specifically means the request was never filed into EP
 * request storage at all (issue #430, REQ-ACF-033) -- byte_bus_id is
 * this endpoint's own address (already known to the caller, which
 * routed to this slot by it); transaction_num is read back out of the
 * request frame's own header via rcp_acf_unpack_header(), which
 * populates it correctly regardless of mtv/request-type repurposing
 * (transaction_num is not part of the repurposed region).
 *
 * A side effect worth flagging for a future reader: dispatch_plain()'s
 * own suppress_response_per_stream_cfg() (REQ-RMAP-048/049) classifies
 * *out_response via rcp_acf_classify_response() to decide which of
 * Table 24's two routing pointers governs it -- and this function's own
 * response now genuinely classifies as RCP_ACF_RESP_ACKNOWLEDGE (it did
 * not before this fix), so it is now governed by rx_ack_stream_index
 * (TC18-defined power-on default 0, "no acknowledge is to be sent"),
 * not rx_resp_stream_index (power-on default 1) the way the sibling
 * §11.3.4 Error Response path is. A caller/test that wants to actually
 * observe this response on the wire must configure rx_ack_stream_index
 * to a nonzero value for the resolved request stream -- exactly as TC18
 * itself requires for any other Acknowledge-classified response. */
static rcp_mock_dispatch_result_t finish_admission(rcp_mock_endpoint_slot_t *slot,
                                                    rcp_server_admit_t admit, uint8_t request_type,
                                                    const uint8_t *request, size_t request_len,
                                                    rcp_wire_error_t error,
                                                    rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_bytes_t *out_response)
{
    switch (admit) {
    case RCP_SERVER_ADMIT_EXECUTE_NOW:
        run_handler(slot, request, request_len, out_response);
        return RCP_MOCK_DISPATCH_OK;
    case RCP_SERVER_ADMIT_QUEUED:
        return RCP_MOCK_DISPATCH_QUEUED;
    case RCP_SERVER_ADMIT_PENDING:
        return RCP_MOCK_DISPATCH_PENDING;
    case RCP_SERVER_ADMIT_CANCELLATION:
        apply_cancellation(slot, request_type, request, request_len, byte_bus_id, out_response);
        return RCP_MOCK_DISPATCH_CANCELLED;
    case RCP_SERVER_ADMIT_REJECTED:
    default:
        /* TC18 §11.3.1, not §11.3.4: RCP_SERVER_ADMIT_REJECTED means the
         * request was never filed into EP request storage at all ("Nothing
         * was stored and nothing is to be executed" -- see
         * rcp_server_admit_t's own doc comment). §11.3.1's own Acknowledge
         * shape (evt=0xF, err=1) is the response TC18 defines for exactly
         * this case -- distinct from the §11.3.4 Error Response
         * (rcp_acf_build_error_response(), evt=0, err=1) used elsewhere in
         * this file for a request that WAS filed but whose later execution
         * failed (issue #430, REQ-ACF-033). */
        if (error != RCP_ERROR_NONE) {
            rcp_acf_byte_message_info_t hdr = {0};
            if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
                *out_response = rcp_acf_build_acknowledge_rejected_response(
                    byte_bus_id, hdr.transaction_num, error);
            }
        }
        return RCP_MOCK_DISPATCH_REJECTED;
    }
}

//cfusa:req REQ-MOCK-021
//cfusa:req REQ-MOCK-030
//cfusa:req REQ-AVTP-021
/* The actual body of rcp_mock_server_dispatch(), factored out so
 * rcp_mock_server_dispatch_e2e() can reach it directly (dispatch_plain())
 * without going back through the public rcp_mock_server_dispatch() --
 * which, as of REQ-WDG-010, kicks the watchdog itself. dispatch_e2e()
 * already kicks once, unconditionally, at its own top (covering the CRC-
 * mismatch/short-frame paths that return before ever reaching this
 * helper) -- routing its own delegation calls through the public
 * function too would kick a second time for the exact same received
 * request. No behavior other than the watchdog kick's own call site
 * changes here. */
static rcp_mock_dispatch_result_t dispatch_plain_inner(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id,
                                                  uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                  bool time_sync_supported, uint64_t stream_id,
                                                  bool tv, uint32_t avtp_timestamp,
                                                  uint64_t gptp_reference_now,
                                                  const uint8_t *request, size_t request_len,
                                                  rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_server_admit_t        admit;
    uint8_t                   request_type = 0;
    rcp_wire_error_t          error        = RCP_ERROR_NONE;
    rcp_lifecycle_accept_t    accept;
    size_t                    admitted_index = 0;
    /* REQ-AVTP-021, TC18 §13.3 rule 1: "In case the RC Server does not
     * support time synchronization, the presentation time shall be
     * ignored, and the request(s) executed as if no presentation time
     * were included or dropped depending on the configuration of the RC
     * Server." Under RCP_AVTP_TSCF_FALLBACK_IGNORE, rcp_lifecycle_should_
     * accept() below no longer drops this frame (see its own
     * unsupported_time_sync_policy parameter), so this is the second
     * half of that rule this function itself must apply: the request is
     * admitted with tv forced false, exactly the "as if no presentation
     * time were included" wording -- NOT the fuller "as if the header
     * was in NTSCF format" substitution rule 2's own IGNORE side makes
     * (see rcp_mock_server_dispatch_tscf()'s own doc comment for why the
     * two rules differ here despite sharing one config knob). Every
     * other case (time_sync_supported true, subtype NTSCF, or policy
     * still DROP) leaves tv exactly as the caller supplied it. */
    bool effective_tv = tv;

    memset(out_response, 0, sizeof(*out_response));

    if (avtp_subtype == RCP_AVTP_SUBTYPE_TSCF && !time_sync_supported &&
        srv->tscf_unsupported_time_sync_policy == RCP_AVTP_TSCF_FALLBACK_IGNORE) {
        effective_tv = false;
    }

    accept = rcp_lifecycle_should_accept(srv->state, time_sync_supported, avtp_subtype,
                                          acf_msg_type, byte_bus_id,
                                          srv->tscf_unsupported_time_sync_policy);
    if (accept == RCP_LIFECYCLE_DROP) {
        return RCP_MOCK_DISPATCH_DROPPED;
    }
    if (accept == RCP_LIFECYCLE_REJECT) {
        /* REQ-LIFECYCLE-033: admitted far enough to identify (TC18 §12.7's
         * own EP0-scoped rule), but answered with REQUEST_REJECTED rather
         * than processed -- same transaction_num-recovery technique this
         * function used to use for its own (now-removed) EP_NOT_FOUND
         * path; see REQ-MOCK-030's own history below. */
        rcp_acf_byte_message_info_t hdr = {0};
        if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
            *out_response = rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num,
                                                          RCP_ERROR_REQUEST_REJECTED);
        }
        return RCP_MOCK_DISPATCH_REJECTED;
    }

    /* REQ-MOCK-031 (issue #432): stream-scoped, not find_slot() -- this
     * is the real dispatch path TC18 §12.9.1's own "In dependence on the
     * stream_id and byte_bus_id" sentence governs; see
     * find_slot_on_stream()'s own doc comment for the matching rule. */
    slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
    if (!slot) {
        /* TC18 §12.9.1: "If the lookup of the byte_bus_id in the context
         * of the stream_id does not point to an Endpoint, the request is
         * dropped without further notification." No response is sent --
         * out_response stays zeroed, per this function's own entry
         * memset() above (REQ-MOCK-030, corrected 2026-08-10,
         * c-RCP-AUDIT-06 issue #256: this branch previously sent a
         * Table 30 EP_NOT_FOUND response for exactly this case, but
         * Table 30's own EP_NOT_FOUND row is scoped to a different,
         * unimplemented scenario -- "if a Trigger request refers to a
         * nonexisting EP", a Trigger request's own trigger_source_ep
         * sub-field naming a nonexistent EP, not the addressed
         * byte_bus_id of the request itself). */
        return RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
    }

    /* The request_type-aware routing decision lives in server.h: a
     * standard request keeps the original submit-or-queue behavior, a
     * conditional one is decoded and stored, a cancellation one is
     * reported back for this module to apply. Admission carries no tick of
     * its own (0): a stored request's exec_delay is measured from the
     * moment its own start condition first holds, which is decided later
     * by rcp_server_endpoint_select_due() against the caller's tick.
     *
     * tv=false, 0u, 0u: REQ-TIMED-012's own TSCF presentation-time gate
     * (server.h) is now wired to tv/avtp_timestamp/gptp_reference_now,
     * this function's own new parameters (REQ-TIMED-012/013, issue
     * #336 follow-on) -- rcp_mock_server_dispatch()/_dispatch_e2e()
     * still call through with tv=false, 0u, 0u, so every one of their
     * own existing callers keeps its exact prior behavior: every
     * request dispatched through THEM is, and remains, treated as if
     * it arrived under an NTSCF header. Only rcp_mock_server_dispatch_
     * tscf() (mock.h) supplies real values, for a caller that decoded
     * an actual TSCF header one layer up. */
    admit = rcp_server_endpoint_admit(&slot->queue, request, request_len, 0u, effective_tv,
                                       avtp_timestamp, gptp_reference_now, &request_type,
                                       &admitted_index, &error);

    /* REQ-E2E-030 (issue #335): a request-storage overflow on THIS
     * endpoint's own queue is answered locally exactly as before
     * (RCP_SERVER_ADMIT_REJECTED, error == RCP_ERROR_REQUEST_STORAGE_
     * OVERFLOW, handled by finish_admission() below) -- but TC18 §12.7.7
     * Table 24's own rx_ovrflw_safestate_enable names a STREAM-WIDE
     * consequence, not a single-endpoint one: every endpoint bound to
     * the same request stream this overflow occurred on must be driven
     * toward its configured safe state too, not just the one whose own
     * queue happened to fill up. rcp_e2e_overflow_should_enter_safe_state()
     * (e2e.h) is the pure per-cause decision; rcp_mock_server_broadcast_
     * safe_state() (mock.h) is this test double's own actuator for
     * "every endpoint bound to the stream" -- see both functions' own doc
     * comments for the architectural background this closes. A stream_id
     * this srv cannot resolve to a configured request stream (resolve_index()
     * returning 0, e.g. no rcp_mock_server_set_request_stream_cfg() call
     * was ever made) broadcasts nothing -- the same fail-toward-no-action
     * disposition rcp_mock_server_broadcast_safe_state() itself already
     * documents for an unresolvable stream. */
    if (error == RCP_ERROR_REQUEST_STORAGE_OVERFLOW) {
        uint8_t overflow_stream_index = rcp_regmap_request_stream_cfg_resolve_index(
            srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
        if (overflow_stream_index != 0u) {
            bool enter_safe_state = rcp_e2e_overflow_should_enter_safe_state(
                srv->request_stream_cfg[overflow_stream_index - 1u].rx_ovrflw_safestate_enable);
            /* REQ-E2E-046: latches stream_status[]'s own overflow cause --
             * independent of, and in addition to, the broadcast actuator
             * below (this is the readable Table 24 rx_stream_status bit,
             * not itself a safe-state trigger). */
            rcp_e2e_stream_status_note_overflow(&srv->stream_status[overflow_stream_index - 1u],
                                                 enter_safe_state);
            if (enter_safe_state) {
                (void)rcp_mock_server_broadcast_safe_state(srv, overflow_stream_index);
            }
        }
    }

    /* REQ-SEQ-013 (issue #335): a newly-admitted COMPOUND/COMPOUND_WAIT
     * step names a sequencer_index it will read or advance -- TC18
     * §12.7.10 Table 28's own access-control rule ("Request_stream_index
     * refers the Client Nr allowed to access this sequencer") applies to
     * this indirect path exactly as much as to a direct register-map
     * write, since a compound-wait request never touches the register
     * map at all. Checked here, right after admission, rather than
     * inside rcp_server_endpoint_admit() itself -- server.h has no
     * sequencer-table dependency of its own (the same "mechanism lives
     * below, context lives here" layering REQ-CANCEL-012's own
     * chain_group bookkeeping already established in this same
     * function's caller). An unauthorized step is admitted then
     * immediately cancelled rather than never admitted at all, since the
     * decode (and thus sequencer_index) is only known after
     * rcp_server_endpoint_admit() itself has already run.
     *
     * Deliberately skipped when the sequencer table itself is
     * unsupported (rcp_sequencer_table_unsupported(), count == 0) --
     * that is a distinct, already-established "compound operations
     * unsupported entirely" scenario (request_sequencer.h's own file
     * header) orthogonal to REQ-SEQ-013's own ownership concern, and a
     * request admitted before any sequencer table exists is expected to
     * stay validly PENDING (simply never becoming due) rather than being
     * newly rejected here -- conflating the two would move an existing,
     * separately-tested "no table configured yet" behavior onto this
     * access-control gate's own fail-closed default. */
    if (admit == RCP_SERVER_ADMIT_PENDING && !rcp_sequencer_table_unsupported(&srv->sequencers) &&
        (slot->queue.pending[admitted_index].kind == RCP_SCHED_KIND_COMPOUND ||
         slot->queue.pending[admitted_index].kind == RCP_SCHED_KIND_COMPOUND_WAIT)) {
        uint8_t sequencer_index = slot->queue.pending[admitted_index].compound.sequencer_index;
        uint8_t requester = rcp_regmap_request_stream_cfg_resolve_index(
            srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);

        if (!rcp_sequencer_access_permitted(&srv->sequencers, sequencer_index, requester)) {
            uint8_t tn = slot->queue.pending[admitted_index].transaction_num;

            (void)rcp_server_endpoint_cancel_single(&slot->queue, tn, RCP_CANCEL_LIFECYCLE_QUEUED);
            *out_response =
                rcp_acf_build_error_response(byte_bus_id, tn, RCP_ERROR_UNAUTHORIZED_ACCESS);
            return RCP_MOCK_DISPATCH_REJECTED;
        }
    }

    return finish_admission(slot, admit, request_type, request, request_len, error, byte_bus_id,
                             out_response);
}

/* REQ-RMAP-048/049 (issue #334-6): TC18 §12.7.7 Table 24's own two
 * per-request-stream routing pointers each carry a "0 means no X is to
 * be sent" default -- rx_ack_stream_index for an Acknowledge-classified
 * response, rx_resp_stream_index for every other response kind (Write/
 * Read/Error). This module owns no real multi-stream transport to
 * actually DELIVER a response on a caller-chosen stream (the same
 * "protocol library, not a scheduler/transport" boundary REQ-SRV-016/
 * 017/018 and REQ-RMAP-065 already established elsewhere in this file)
 * -- but it CAN, and now does, honor the "0 means send nothing at all"
 * half of that rule, which needs no transport concept whatsoever: *out
 * is simply freed and left zeroed, exactly as if this dispatch call had
 * taken one of this module's own pre-existing "no response" paths
 * (RCP_MOCK_DISPATCH_DROPPED, an unresolved byte_bus_id, etc.).
 *
 * An unresolvable stream_id (no rcp_mock_server_set_request_stream_cfg()
 * call for it) suppresses nothing -- the same fail-toward-no-action
 * disposition every other resolve_index() call site in this file already
 * uses; a caller that never configured a request stream sees this
 * module's pre-existing, unaffected response behavior.
 *
 * *out is classified via rcp_acf_classify_response() to pick which of
 * the two pointers governs it -- Acknowledge vs. everything else, TC18's
 * own §11.3 split, already established this file's own response-
 * building convention. This module's own dispatch pipeline never yet
 * builds an Acknowledge-classified response itself (admit(), unlike the
 * lower-level rcp_server_endpoint_submit(), has no evt[3]-triggered
 * out_ack of its own -- a separate, already-known, not-yet-wired gap),
 * so rx_ack_stream_index's own suppression is presently reachable only
 * by a caller-supplied endpoint handler that builds one itself; it is
 * still implemented correctly and tested directly, ready for whenever
 * that separate gap closes. */
static void suppress_response_per_stream_cfg(rcp_mock_server_t *srv, uint64_t stream_id,
                                              rcp_bytes_t *out)
{
    uint8_t                      stream_index;
    rcp_acf_byte_message_info_t hdr = {0};

    if (out->data == NULL) return; /* nothing built, nothing to suppress */

    stream_index = rcp_regmap_request_stream_cfg_resolve_index(
        srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
    if (stream_index == 0u) return;

    if (out->len < 8u || rcp_acf_unpack_header(out->data, &hdr) != RCP_ACF_OK) return;

    if (rcp_acf_classify_response(&hdr) == RCP_ACF_RESP_ACKNOWLEDGE) {
        if (srv->request_stream_cfg[stream_index - 1u].rx_ack_stream_index == 0u) {
            rcp_bytes_free(out);
        }
    } else {
        if (srv->request_stream_cfg[stream_index - 1u].rx_resp_stream_index == 0u) {
            rcp_bytes_free(out);
        }
    }
}

//cfusa:req REQ-RMAP-048
//cfusa:req REQ-RMAP-049
static rcp_mock_dispatch_result_t dispatch_plain(rcp_mock_server_t *srv,
                                                  rcp_byte_bus_id_t byte_bus_id,
                                                  uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                  bool time_sync_supported, uint64_t stream_id,
                                                  bool tv, uint32_t avtp_timestamp,
                                                  uint64_t gptp_reference_now,
                                                  const uint8_t *request, size_t request_len,
                                                  rcp_bytes_t *out_response)
{
    rcp_mock_dispatch_result_t result =
        dispatch_plain_inner(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                              stream_id, tv, avtp_timestamp, gptp_reference_now, request,
                              request_len, out_response);

    suppress_response_per_stream_cfg(srv, stream_id, out_response);
    return result;
}

//cfusa:req REQ-WDG-010
rcp_mock_dispatch_result_t rcp_mock_server_dispatch(rcp_mock_server_t *srv,
                                                     rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                     bool time_sync_supported, uint64_t stream_id,
                                                     const uint8_t *request, size_t request_len,
                                                     rcp_bytes_t *out_response)
{
    /* REQ-WDG-010: kicked unconditionally, before dispatch_plain()'s own
     * lifecycle/admission checks -- same "receipt, not validation" rule
     * and ordering as rcp_mock_server_dispatch_e2e()'s own identical
     * kick (see that function's doc comment for the full rationale). */
    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, stream_id);

    return dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                           stream_id, false, 0u, 0u, request, request_len, out_response);
}

//cfusa:req REQ-TIMED-012
//cfusa:req REQ-TIMED-013
//cfusa:req REQ-AVTP-022
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_tscf(rcp_mock_server_t *srv,
                                                          rcp_byte_bus_id_t byte_bus_id,
                                                          uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                          bool time_sync_supported, uint64_t stream_id,
                                                          bool tv, uint32_t avtp_timestamp,
                                                          bool tscf_reserved_all_zero,
                                                          uint64_t gptp_reference_now,
                                                          const uint8_t *request, size_t request_len,
                                                          rcp_bytes_t *out_response)
{
    /* Same "receipt, not validation" watchdog-kick rule and ordering as
     * rcp_mock_server_dispatch()'s own identical kick. */
    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, stream_id);

    /* REQ-AVTP-022, TC18 §13.3 rule 2: "If the reserved bytes in the
     * header are all zero, then the request shall be queued as if the
     * header was in NTSCF format or dropped, depending on
     * configuration." tscf_reserved_all_zero is the caller's own
     * rcp_avtp_tscf_reserved_all_zero() result from decoding the real
     * TSCF header one layer up (avtp.h) -- this function itself still
     * never touches the outer AVTP/TSCF framing directly, matching every
     * sibling dispatch function's own established convention (see this
     * function's own file-header doc comment, mock.h); it only acts on
     * the caller-supplied boolean, exactly like tv/avtp_timestamp
     * already do.
     *
     * Only meaningful for a genuine TSCF frame -- an NTSCF-headed call
     * through this same entry point (unusual, but this function's own
     * signature does not forbid it) has no reserved-bytes rule to apply
     * in the first place, so tscf_reserved_all_zero is simply ignored
     * whenever avtp_subtype is not RCP_AVTP_SUBTYPE_TSCF. */
    if (avtp_subtype == RCP_AVTP_SUBTYPE_TSCF && tscf_reserved_all_zero) {
        if (srv->tscf_unsupported_time_sync_policy == RCP_AVTP_TSCF_FALLBACK_DROP) {
            memset(out_response, 0, sizeof(*out_response));
            return RCP_MOCK_DISPATCH_DROPPED;
        }
        /* RCP_AVTP_TSCF_FALLBACK_IGNORE: "queued as if the header was in
         * NTSCF format" is a literal, full substitution (unlike rule 1's
         * own narrower "ignore the presentation time" wording) -- both
         * avtp_subtype and tv are overridden together so every downstream
         * decision (rcp_lifecycle_should_accept()'s own state-specific
         * TSCF-drop rules included, not just the presentation-time gate)
         * sees exactly what it would have seen for a frame that had
         * genuinely arrived as NTSCF. */
        avtp_subtype = RCP_AVTP_SUBTYPE_NTSCF;
        tv           = false;
    }

    return dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                           stream_id, tv, avtp_timestamp, gptp_reference_now, request, request_len,
                           out_response);
}

//cfusa:req REQ-ISELED-025
//cfusa:req REQ-WDG-010
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_multi_response(
    rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id, uint8_t avtp_subtype,
    uint8_t acf_msg_type, bool time_sync_supported, uint64_t stream_id, const uint8_t *request,
    size_t request_len, rcp_bytes_t *out_responses, size_t out_cap, size_t *out_response_count)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_lifecycle_accept_t     accept;
    size_t                     i;

    *out_response_count = 0;
    for (i = 0; i < out_cap; i++) memset(&out_responses[i], 0, sizeof(out_responses[i]));

    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, stream_id);

    accept = rcp_lifecycle_should_accept(srv->state, time_sync_supported, avtp_subtype,
                                          acf_msg_type, byte_bus_id,
                                          srv->tscf_unsupported_time_sync_policy);
    if (accept == RCP_LIFECYCLE_DROP) {
        return RCP_MOCK_DISPATCH_DROPPED;
    }
    if (accept == RCP_LIFECYCLE_REJECT) {
        rcp_acf_byte_message_info_t hdr = {0};
        if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK && out_cap > 0u) {
            out_responses[0] = rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num,
                                                             RCP_ERROR_REQUEST_REJECTED);
            suppress_response_per_stream_cfg(srv, stream_id, &out_responses[0]);
            if (out_responses[0].data) *out_response_count = 1u;
        }
        return RCP_MOCK_DISPATCH_REJECTED;
    }

    /* REQ-MOCK-031 (issue #432): stream-scoped lookup -- see
     * find_slot_on_stream()'s own doc comment. */
    slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
    if (!slot) {
        return RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
    }

    if (slot->multi_handler != NULL) {
        slot->multi_handler(request, request_len, out_responses, out_cap, out_response_count,
                             slot->user_data);
        for (i = 0; i < *out_response_count && i < out_cap; i++) {
            suppress_response_per_stream_cfg(srv, stream_id, &out_responses[i]);
        }
    }
    return RCP_MOCK_DISPATCH_OK;
}

//cfusa:req REQ-E2E-021
//cfusa:req REQ-E2E-031
//cfusa:req REQ-E2E-041
//cfusa:req REQ-WDG-010
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_e2e(rcp_mock_server_t *srv,
                                                          rcp_byte_bus_id_t byte_bus_id,
                                                          uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                          bool time_sync_supported,
                                                          uint64_t stream_id, uint32_t avtp_timestamp,
                                                          const uint8_t *request, size_t request_len,
                                                          rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_bytes_t                unwrapped;
    rcp_e2e_errc_t             unwrap_result;
    rcp_mock_dispatch_result_t result;

    memset(out_response, 0, sizeof(*out_response));

    /* REQ-WDG-010: kicked unconditionally, before any of the checks
     * below -- TC18 §12.7.7's own rule is about RECEIPT ("the watchdog
     * is reset with each request received from this RC Client"), not
     * successful validation or admission. A request this call goes on
     * to reject via plain-command-mode delegation, CRC mismatch, or
     * admission failure still means the RC Client is alive and talking
     * on this stream, which is the watchdog's own entire concern. */
    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, stream_id);

    /* REQ-E2E-021 (TC18 §12.7.7 Table 24, rx_enforce_e2e's "stream is
     * blocked until released" consequence): checked BEFORE
     * plain-command-mode delegation, CRC validation, or admission --
     * the block is a whole-STREAM property (this tracker is keyed by
     * stream_id, not byte_bus_id), so it applies uniformly regardless
     * of whether the addressed endpoint itself has req_crc_enable set.
     * *out_response carries a real Table 30 POCI_FAILURE error response
     * (the same code CRC_ERROR itself uses -- the block's own root
     * cause is a CRC failure) when the frame is at least long enough to
     * read a transaction_num back out of. */
    if (srv->stream_fault_tracker != NULL &&
        rcp_e2e_stream_fault_tracker_is_faulted(srv->stream_fault_tracker, stream_id)) {
        rcp_acf_byte_message_info_t hdr = {0};
        if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
            *out_response =
                rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num, RCP_ERROR_POCI_FAILURE);
        }
        return RCP_MOCK_DISPATCH_STREAM_FAULTED;
    }

    /* "plain command mode" (TC18 §13.6): an endpoint with req_crc_enable
     * not set, or an unknown byte_bus_id, is untouched by this function
     * -- delegate outright, including its own EP_NOT_FOUND handling.
     * Goes to dispatch_plain() directly, not the public dispatch()
     * wrapper -- this call's own watchdog kick already happened above,
     * unconditionally, before this branch was even reached; delegating
     * through the public wrapper would kick a second time for the same
     * received request (see dispatch_plain()'s own doc comment).
     *
     * REQ-MOCK-031 (issue #432): stream-scoped, not find_slot() -- see
     * find_slot_on_stream()'s own doc comment. */
    slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
    if (!slot || !slot->req_crc_enable) {
        return dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                               stream_id, false, 0u, 0u, request, request_len, out_response);
    }

    unwrap_result = rcp_e2e_unwrap_framed(stream_id, avtp_subtype == RCP_AVTP_SUBTYPE_NTSCF,
                                           avtp_timestamp, request, request_len, &unwrapped);
    if (unwrap_result != RCP_E2E_OK) {
        /* Not executed, not even admitted -- TC18 §13.6. RCP_E2E_ERR_CRC_MISMATCH
         * still populates unwrapped (for diagnostic use, per rcp_e2e_unwrap()'s
         * own doc comment) so it must still be freed even though it is not
         * used as a request; RCP_E2E_ERR_SHORT_FRAME leaves it zeroed. */
        rcp_wire_error_t werr = rcp_e2e_wire_error(unwrap_result);
        if (werr != RCP_ERROR_NONE) {
            rcp_acf_byte_message_info_t hdr = {0};
            if (request_len >= 8 && rcp_acf_unpack_header(request, &hdr) == RCP_ACF_OK) {
                *out_response = rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num, werr);
            }
        }
        /* REQ-E2E-021: records this CRC error against stream_id's own
         * tracked fault state -- latches the whole stream faulted iff
         * slot->rx_enforce_e2e (this test double's own per-endpoint
         * stand-in for the real per-request-stream register bit; see
         * its own doc comment). A future request on this same stream_id
         * hits the check above before reaching this point again. */
        if (srv->stream_fault_tracker != NULL) {
            (void)rcp_e2e_stream_fault_tracker_on_crc_error(srv->stream_fault_tracker, stream_id,
                                                              slot->rx_enforce_e2e);
        }
        /* REQ-E2E-046: latches stream_status[]'s own CRC cause too --
         * request_stream_index-indexed (this table's own real address
         * space), a separate instance from stream_fault_tracker's own
         * stream_id-keyed latch immediately above, same reasoning as
         * this array's own declaration comment. An unresolvable
         * stream_id (no rcp_mock_server_set_request_stream_cfg() call
         * for it) latches nothing -- the same fail-toward-no-action
         * disposition every other resolve_index() call site in this
         * file already uses. */
        {
            uint8_t crc_stream_index = rcp_regmap_request_stream_cfg_resolve_index(
                srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
            if (crc_stream_index != 0u) {
                (void)rcp_e2e_stream_status_note_crc_error(
                    &srv->stream_status[crc_stream_index - 1u], slot->rx_enforce_e2e);
            }
        }
        /* REQ-E2E-045 (issue #335): TC18 §12.7.7 Table 24's own
         * rx_enforce_e2e/rx_enforce_crc (0x000D.0) names TWO consequences
         * for a CRC failure at the SAME single bit -- "stream is blocked
         * until released" (the fault-tracker latch immediately above) AND
         * "Safe state will be entered" -- and unlike its wd/overflow/seq
         * siblings, that second consequence has no separate enable bit of
         * its own to gate it: it is rx_enforce_e2e's own value, evaluated
         * by rcp_e2e_crc_error_should_enter_safe_state() (e2e.h). "Safe
         * state" is stream-wide (every endpoint bound to stream_id, not
         * just the one slot this CRC mismatch was addressed to) --
         * rcp_mock_server_broadcast_safe_state() (mock.h) is this test
         * double's own actuator for that, resolved via the same
         * rcp_regmap_request_stream_cfg_resolve_index() call
         * rcp_mock_server_broadcast_safe_state()'s own overflow-cause
         * sibling (dispatch_plain(), above) already uses. slot->rx_enforce_e2e
         * (not the resolved stream's own request_stream_cfg entry) is
         * reused here deliberately, matching the fault-tracker latch call
         * immediately above it: both consequences of the SAME wire bit
         * are read from the SAME per-endpoint stand-in this function
         * already relies on for the CRC-mismatch path specifically, not
         * a second, potentially-inconsistent source. */
        {
            uint8_t crc_stream_index = rcp_regmap_request_stream_cfg_resolve_index(
                srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
            if (crc_stream_index != 0u &&
                rcp_e2e_crc_error_should_enter_safe_state(slot->rx_enforce_e2e)) {
                (void)rcp_mock_server_broadcast_safe_state(srv, crc_stream_index);
            }
        }
        rcp_bytes_free(&unwrapped);
        return RCP_MOCK_DISPATCH_CRC_ERROR;
    }

    /* CRC validated: dispatch the unwrapped header-and-payload region
     * (acf_msg_length already adapted back down) exactly as
     * rcp_mock_server_dispatch() would have dispatched request itself --
     * via dispatch_plain() directly, same already-kicked-once reasoning
     * as the delegation branch above. */
    result = dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                             stream_id, false, 0u, 0u, unwrapped.data, unwrapped.len, out_response);
    rcp_bytes_free(&unwrapped);
    return result;
}

//cfusa:req REQ-E2E-038
//cfusa:req REQ-E2E-039
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_e2e_fragment(
    rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id, uint8_t avtp_subtype,
    uint8_t acf_msg_type, bool time_sync_supported, uint64_t stream_id, uint32_t avtp_timestamp,
    const uint8_t *fragment, size_t fragment_len, rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t   *slot;
    uint8_t                     stream_index;
    rcp_fragment_reassembler_t *reasm;
    rcp_acf_byte_message_info_t peek_hdr = {0};
    size_t                      header_len;
    rcp_fragment_reasm_result_t reasm_result;
    rcp_mock_dispatch_result_t  result;

    memset(out_response, 0, sizeof(*out_response));

    /* Same watchdog-kick / stream-fault-tracker / plain-command-mode
     * delegation checks as rcp_mock_server_dispatch_e2e(), same order,
     * same reasons -- see that function's own doc comment. */
    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, stream_id);

    if (srv->stream_fault_tracker != NULL &&
        rcp_e2e_stream_fault_tracker_is_faulted(srv->stream_fault_tracker, stream_id)) {
        rcp_acf_byte_message_info_t hdr = {0};
        if (fragment_len >= 8 && rcp_acf_unpack_header(fragment, &hdr) == RCP_ACF_OK) {
            *out_response =
                rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num, RCP_ERROR_POCI_FAILURE);
        }
        return RCP_MOCK_DISPATCH_STREAM_FAULTED;
    }

    /* REQ-MOCK-031 (issue #432): stream-scoped, not find_slot() -- see
     * find_slot_on_stream()'s own doc comment. */
    slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
    if (!slot || !slot->req_crc_enable) {
        return dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type, time_sync_supported,
                               stream_id, false, 0u, 0u, fragment, fragment_len, out_response);
    }

    /* A cheap 8-octet peek -- format-identical for ACF_ABB and ACF_GBB,
     * since a GBB header is an ABB header's same 8 octets plus 8 more of
     * timestamp -- to learn ms/read_size_or_segment_num without
     * committing to either variant's own full decode yet. The final
     * fragment's own full decode is CRC-trailer-sensitive (see below)
     * and must not be attempted until ms is known to be false. */
    if (fragment_len < 8 || rcp_acf_unpack_header(fragment, &peek_hdr) != RCP_ACF_OK) {
        return RCP_MOCK_DISPATCH_REJECTED;
    }

    stream_index = rcp_regmap_request_stream_cfg_resolve_index(
        srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
    if (stream_index == 0u) {
        /* No configured request-stream slot to reassemble into --
         * rcp_mock_server_dispatch_e2e() itself has no such dependency,
         * so fall back to it unchanged rather than reject outright. */
        return rcp_mock_server_dispatch_e2e(srv, byte_bus_id, avtp_subtype, acf_msg_type,
                                             time_sync_supported, stream_id, avtp_timestamp,
                                             fragment, fragment_len, out_response);
    }
    reasm      = &srv->frag_reasm[stream_index - 1u];
    header_len = (acf_msg_type == RCP_ACF_MSG_TYPE_GBB) ? RCP_ACF_GBB_HEADER_LEN
                                                          : RCP_ACF_ABB_HEADER_LEN;

    if (peek_hdr.ms) {
        /* Intermediate fragment (REQ-E2E-039): no CRC trailer, safe to
         * decode fully and directly. */
        const uint8_t *payload;
        size_t         payload_len;

        if (acf_msg_type == RCP_ACF_MSG_TYPE_GBB) {
            rcp_acf_gbb_header_t hdr;
            if (rcp_acf_decode_gbb(fragment, fragment_len, &hdr, &payload, &payload_len) !=
                RCP_ACF_OK) {
                return RCP_MOCK_DISPATCH_REJECTED;
            }
        } else {
            rcp_acf_byte_message_info_t hdr;
            if (rcp_acf_decode_abb(fragment, fragment_len, &hdr, &payload, &payload_len) !=
                RCP_ACF_OK) {
                return RCP_MOCK_DISPATCH_REJECTED;
            }
        }

        if (!rcp_fragment_reassembler_is_collecting(reasm)) {
            /* First fragment of a new sequence: remember its own raw
             * encoded header bytes for REQ-E2E-038's eventual fragmented
             * CRC check. header_len <= fragment_len is already
             * guaranteed by the successful decode above. */
            memcpy(srv->frag_first_header[stream_index - 1u], fragment, header_len);
            srv->frag_first_header_len[stream_index - 1u] = header_len;
        }

        reasm_result = rcp_fragment_reassembler_feed(reasm, true, peek_hdr.read_size_or_segment_num,
                                                       payload, payload_len);
        if (reasm_result != RCP_FRAGMENT_REASM_CONTINUE) {
            rcp_fragment_reassembler_reset(reasm);
            return RCP_MOCK_DISPATCH_REJECTED;
        }
        return RCP_MOCK_DISPATCH_FRAGMENT_PENDING;
    }

    /* ms == 0: final fragment. */
    if (!rcp_fragment_reassembler_is_collecting(reasm)) {
        /* Never actually fragmented -- byte-identical to calling
         * rcp_mock_server_dispatch_e2e() directly. */
        return rcp_mock_server_dispatch_e2e(srv, byte_bus_id, avtp_subtype, acf_msg_type,
                                             time_sync_supported, stream_id, avtp_timestamp,
                                             fragment, fragment_len, out_response);
    }

    /* Real multi-fragment message completing: this final fragment's own
     * message carries the CRC32 trailer in its raw last RCP_E2E_CRC_LEN
     * octets, and its own acf_msg_length is already adapted by +1
     * quadlet for it (REQ-E2E-039/e2e.h) -- rcp_e2e_unwrap_framed() is
     * the already-tested tool that strips/adapts that, exactly as
     * rcp_mock_server_dispatch_e2e() itself already relies on for the
     * single-fragment case. Its OWN CRC verdict is wrong for this
     * fragmented case (single-frame formula) and is deliberately
     * ignored here -- rcp_e2e_compute_fragmented_crc() below is the
     * real check (REQ-E2E-038). */
    {
        uint32_t       got;
        rcp_bytes_t    unwrapped;
        rcp_e2e_errc_t unwrap_result;
        /* rcp_e2e_wrap_framed()'s own rule (e2e.h): an NTSCF-framed
         * message carries no avtp_timestamp field of its own on the
         * wire at all, so its CRC's own avtp_timestamp contribution is
         * always 0, regardless of whatever this call's own
         * avtp_timestamp argument happens to be -- rcp_e2e_unwrap_framed()
         * below already applies this same forcing internally for its own
         * (ignored) single-frame verdict; rcp_e2e_compute_fragmented_crc()
         * has no _framed() counterpart of its own to do so on this
         * function's behalf, so it is applied here explicitly. */
        uint32_t effective_ts = (avtp_subtype == RCP_AVTP_SUBTYPE_NTSCF) ? 0u : avtp_timestamp;

        if (fragment_len < RCP_E2E_CRC_LEN) {
            rcp_fragment_reassembler_reset(reasm);
            return RCP_MOCK_DISPATCH_REJECTED;
        }

        /* Pad-aware CRC location (issue #445): TC18 Figures 20/21 place
         * the CRC32 immediately after the real (unpadded) payload, with
         * any quadlet-alignment pad octets re-seated AFTER the trailer --
         * frame = [real_len][CRC32][pad_octets], not "the trailer is
         * always the last RCP_E2E_CRC_LEN octets of the frame" (only ever
         * true when pad_octets == 0). rcp_e2e_wrap()/_unwrap() (e2e.c,
         * issue #420) already locate that boundary this same way, reading
         * it straight out of the header's own wire-format "pad" field
         * (acf.h Figure 7, byte_message_info octet 2 bits 7:6) --
         * peek_hdr.pad above decoded that exact field from this same
         * fragment already, so no fresh read is needed here. */
        if ((size_t)peek_hdr.pad > fragment_len - RCP_E2E_CRC_LEN) {
            rcp_fragment_reassembler_reset(reasm);
            return RCP_MOCK_DISPATCH_REJECTED;
        }
        {
            size_t real_len = fragment_len - RCP_E2E_CRC_LEN - (size_t)peek_hdr.pad;

            got = ((uint32_t)fragment[real_len] << 24) |
                  ((uint32_t)fragment[real_len + 1] << 16) |
                  ((uint32_t)fragment[real_len + 2] << 8) | (uint32_t)fragment[real_len + 3];
        }

        unwrap_result = rcp_e2e_unwrap_framed(stream_id, avtp_subtype == RCP_AVTP_SUBTYPE_NTSCF,
                                               avtp_timestamp, fragment, fragment_len, &unwrapped);
        if (unwrap_result == RCP_E2E_ERR_SHORT_FRAME) {
            rcp_bytes_free(&unwrapped);
            rcp_fragment_reassembler_reset(reasm);
            return RCP_MOCK_DISPATCH_REJECTED;
        }

        if (acf_msg_type == RCP_ACF_MSG_TYPE_GBB) {
            rcp_acf_gbb_header_t final_hdr;
            const uint8_t       *final_payload;
            size_t                final_payload_len;
            const uint8_t        *reassembled;
            size_t                reassembled_len;
            uint32_t              want;

            if (rcp_acf_decode_gbb(unwrapped.data, unwrapped.len, &final_hdr, &final_payload,
                                    &final_payload_len) != RCP_ACF_OK) {
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                return RCP_MOCK_DISPATCH_REJECTED;
            }

            reasm_result =
                rcp_fragment_reassembler_feed(reasm, false, 0u, final_payload, final_payload_len);
            if (reasm_result != RCP_FRAGMENT_REASM_COMPLETE) {
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                return RCP_MOCK_DISPATCH_REJECTED;
            }
            rcp_fragment_reassembler_get(reasm, &reassembled, &reassembled_len);
            want = rcp_e2e_compute_fragmented_crc(stream_id, effective_ts,
                                                   srv->frag_first_header[stream_index - 1u],
                                                   srv->frag_first_header_len[stream_index - 1u],
                                                   reassembled, reassembled_len);

            if (got != want) {
                /* Same three consequences rcp_mock_server_dispatch_e2e()'s
                 * own CRC-mismatch branch already applies (REQ-E2E-021/
                 * REQ-E2E-045/REQ-E2E-046) -- duplicated here deliberately
                 * rather than refactored out of that already-tested
                 * function, to keep this addition from touching any
                 * already-passing behavior. */
                *out_response = rcp_acf_build_error_response(byte_bus_id,
                                                               final_hdr.info.transaction_num,
                                                               RCP_ERROR_POCI_FAILURE);
                if (srv->stream_fault_tracker != NULL) {
                    (void)rcp_e2e_stream_fault_tracker_on_crc_error(srv->stream_fault_tracker,
                                                                      stream_id, slot->rx_enforce_e2e);
                }
                (void)rcp_e2e_stream_status_note_crc_error(&srv->stream_status[stream_index - 1u],
                                                            slot->rx_enforce_e2e);
                if (rcp_e2e_crc_error_should_enter_safe_state(slot->rx_enforce_e2e)) {
                    (void)rcp_mock_server_broadcast_safe_state(srv, stream_index);
                }
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                return RCP_MOCK_DISPATCH_CRC_ERROR;
            }

            {
                rcp_bytes_t encoded = rcp_acf_encode_gbb(&final_hdr, reassembled, reassembled_len);
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                if (!encoded.data && reassembled_len != 0u) return RCP_MOCK_DISPATCH_REJECTED;
                result = dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type,
                                         time_sync_supported, stream_id, false, 0u, 0u, encoded.data,
                                         encoded.len, out_response);
                rcp_bytes_free(&encoded);
                return result;
            }
        } else {
            rcp_acf_byte_message_info_t final_hdr;
            const uint8_t               *final_payload;
            size_t                       final_payload_len;
            const uint8_t               *reassembled;
            size_t                       reassembled_len;
            uint32_t                     want;

            if (rcp_acf_decode_abb(unwrapped.data, unwrapped.len, &final_hdr, &final_payload,
                                    &final_payload_len) != RCP_ACF_OK) {
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                return RCP_MOCK_DISPATCH_REJECTED;
            }

            reasm_result =
                rcp_fragment_reassembler_feed(reasm, false, 0u, final_payload, final_payload_len);
            if (reasm_result != RCP_FRAGMENT_REASM_COMPLETE) {
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                return RCP_MOCK_DISPATCH_REJECTED;
            }
            rcp_fragment_reassembler_get(reasm, &reassembled, &reassembled_len);
            want = rcp_e2e_compute_fragmented_crc(stream_id, effective_ts,
                                                   srv->frag_first_header[stream_index - 1u],
                                                   srv->frag_first_header_len[stream_index - 1u],
                                                   reassembled, reassembled_len);

            if (got != want) {
                *out_response = rcp_acf_build_error_response(byte_bus_id, final_hdr.transaction_num,
                                                               RCP_ERROR_POCI_FAILURE);
                if (srv->stream_fault_tracker != NULL) {
                    (void)rcp_e2e_stream_fault_tracker_on_crc_error(srv->stream_fault_tracker,
                                                                      stream_id, slot->rx_enforce_e2e);
                }
                (void)rcp_e2e_stream_status_note_crc_error(&srv->stream_status[stream_index - 1u],
                                                            slot->rx_enforce_e2e);
                if (rcp_e2e_crc_error_should_enter_safe_state(slot->rx_enforce_e2e)) {
                    (void)rcp_mock_server_broadcast_safe_state(srv, stream_index);
                }
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                return RCP_MOCK_DISPATCH_CRC_ERROR;
            }

            {
                rcp_bytes_t encoded = rcp_acf_encode_abb(&final_hdr, reassembled, reassembled_len);
                rcp_bytes_free(&unwrapped);
                rcp_fragment_reassembler_reset(reasm);
                if (!encoded.data && reassembled_len != 0u) return RCP_MOCK_DISPATCH_REJECTED;
                result = dispatch_plain(srv, byte_bus_id, avtp_subtype, acf_msg_type,
                                         time_sync_supported, stream_id, false, 0u, 0u, encoded.data,
                                         encoded.len, out_response);
                rcp_bytes_free(&encoded);
                return result;
            }
        }
    }
}

//cfusa:req REQ-MOCK-016
//cfusa:req REQ-MOCK-017
//cfusa:req REQ-MOCK-018
bool rcp_mock_server_drain_endpoint(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                     rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    rcp_bytes_t                frame = {0};

    memset(out_response, 0, sizeof(*out_response));
    if (!slot) return false;

    if (!rcp_server_endpoint_drain_one(&slot->queue, &frame)) return false;

    run_handler(slot, frame.data, frame.len, out_response);
    rcp_bytes_free(&frame);
    return true;
}

//cfusa:req REQ-GPIO-036
bool rcp_mock_server_stash_deferred_response(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                              rcp_bytes_t response)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    rcp_bytes_free(&slot->deferred_response);
    slot->deferred_response = response;
    return true;
}

//cfusa:req REQ-GPIO-036
bool rcp_mock_server_take_deferred_response(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                             rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);

    memset(out_response, 0, sizeof(*out_response));
    if (!slot || slot->deferred_response.data == NULL) return false;

    *out_response = slot->deferred_response;
    memset(&slot->deferred_response, 0, sizeof(slot->deferred_response));
    return true;
}

/* Decodes just member[0..member_len)'s shared byte_message_info header
 * far enough to learn its byte_bus_id, without caring whether it is
 * ACF_ABB or ACF_GBB. Returns true and sets *out_byte_bus_id on success;
 * false (leaving *out_byte_bus_id untouched) if member does not decode as
 * either variant -- see rcp_mock_server_dispatch_frame()'s own doc
 * comment for how that outcome is surfaced. */
static bool peek_member_byte_bus_id(const uint8_t *member, size_t member_len, uint8_t *out_msg_type,
                                     rcp_byte_bus_id_t *out_byte_bus_id)
{
    if (rcp_acf_peek_msg_type(member, member_len, out_msg_type) != RCP_ACF_OK) return false;

    if (*out_msg_type == RCP_ACF_MSG_TYPE_ABB) {
        rcp_acf_byte_message_info_t hdr;
        const uint8_t              *payload;
        size_t                       payload_len;

        if (rcp_acf_decode_abb(member, member_len, &hdr, &payload, &payload_len) != RCP_ACF_OK) {
            return false;
        }
        *out_byte_bus_id = hdr.byte_bus_id;
        return true;
    }

    if (*out_msg_type == RCP_ACF_MSG_TYPE_GBB) {
        rcp_acf_gbb_header_t  hdr;
        const uint8_t        *payload;
        size_t                 payload_len;

        if (rcp_acf_decode_gbb(member, member_len, &hdr, &payload, &payload_len) != RCP_ACF_OK) {
            return false;
        }
        *out_byte_bus_id = hdr.info.byte_bus_id;
        return true;
    }

    return false;
}

/* True iff member[0..member_len) is a chained request; on true,
 * *out_cs receives its own conditional-start selector and *out_tn its own
 * transaction_num -- needed by CHAIN_ERROR/CHAIN_ABORTED error-response
 * construction (TC18 §12.9.6), which this function's own decode call
 * already has on hand and would otherwise have to be re-derived. */
static bool is_chained_member(const uint8_t *member, size_t member_len, uint8_t *out_cs,
                               uint8_t *out_tn)
{
    uint8_t           request_type = 0;
    rcp_byte_bus_id_t bus;
    uint16_t          exec_delay;
    const uint8_t    *payload;
    size_t            payload_len;

    if (rcp_compound_peek_request_type(member, member_len, &request_type) != RCP_COMPOUND_OK) {
        return false;
    }
    if (request_type != RCP_REQUEST_TYPE_CHAINED) return false;

    if (rcp_chained_decode_member(member, member_len, &bus, &exec_delay, out_cs, &payload,
                                   &payload_len, out_tn) != RCP_CHAINED_OK) {
        /* Claims to be chained but does not decode as one -- not treated
         * as a chain member here; rcp_mock_server_dispatch() will reject
         * it on its own for the same reason. */
        return false;
    }
    return true;
}

/* The store index of the most recently admitted request on ep -- the one
 * rcp_server_endpoint_admit() just placed, identified by its highest
 * sequence number. Returns RCP_SERVER_MAX_PENDING if the store is empty. */
static size_t last_pending_index(const rcp_server_endpoint_t *ep)
{
    size_t   i;
    size_t   best  = RCP_SERVER_MAX_PENDING;
    uint64_t max_seq = 0;

    for (i = 0; i < RCP_SERVER_MAX_PENDING; i++) {
        if (!ep->pending[i].in_use) continue;
        if (best == RCP_SERVER_MAX_PENDING || ep->pending[i].sequence >= max_seq) {
            max_seq = ep->pending[i].sequence;
            best    = i;
        }
    }
    return best;
}

/* REQ-E2E-028/029 (issue #338): shared once-per-frame sequence-number
 * gate for rcp_mock_server_dispatch_frame()/_dispatch_frame_e2e() -- see
 * either function's own doc comment (mock.h) for the full rationale.
 * Returns true iff the frame may proceed to per-member dispatch; false
 * means the caller must reject every member with RCP_MOCK_DISPATCH_
 * SEQ_ERROR and return immediately without processing any of them. */
//cfusa:req REQ-E2E-028
//cfusa:req REQ-E2E-029
static bool frame_seq_gate_admits(rcp_mock_server_t *srv, uint64_t stream_id, uint8_t sequence_num)
{
    uint8_t seq_stream_index = rcp_regmap_request_stream_cfg_resolve_index(
        srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
    rcp_regmap_request_stream_cfg_t *cfg;
    rcp_e2e_seq_result_t             result;

    if (seq_stream_index == 0u) return true; /* unresolvable stream -- fail toward no action,
                                                  same disposition as the overflow-safestate check */

    cfg    = &srv->request_stream_cfg[seq_stream_index - 1u];
    result = rcp_e2e_seq_evaluate(&srv->seq_tracker[seq_stream_index - 1u], cfg->rx_enforce_seq,
                                   cfg->rx_seq_safestate_enable, sequence_num);

    /* REQ-E2E-046: latches stream_status[]'s own sequence cause,
     * regardless of result.accept -- same "checked every time, not just
     * on rejection" reasoning as the broadcast actuator right below. */
    rcp_e2e_stream_status_note_seq(&srv->stream_status[seq_stream_index - 1u], result);

    /* Checked regardless of result.accept -- a gap (advanced by more than
     * one) is evidence of a problem even when ordering itself still held. */
    if (result.enter_safe_state) {
        (void)rcp_mock_server_broadcast_safe_state(srv, seq_stream_index);
    }
    return result.accept;
}

//cfusa:req REQ-MOCK-019
//cfusa:req REQ-MOCK-020
//cfusa:req REQ-MOCK-029
size_t rcp_mock_server_dispatch_frame(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                       bool time_sync_supported, uint64_t stream_id,
                                       uint8_t sequence_num,
                                       const uint8_t *frame, size_t frame_len,
                                       rcp_mock_frame_member_result_t *out_results,
                                       size_t out_cap)
{
    size_t offsets[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t real_count;
    size_t stored_count; /* how many of offsets[] rcp_sched_split_frame_members()
                             actually wrote -- may be < real_count */
    size_t process_count;
    size_t i;
    size_t dispatched = 0;
    /* Chain sequencing state, carried across this frame's members in
     * order -- see request_chained.h's rcp_chained_advance(). */
    bool   chain_aborted = false;
    bool   prev_errored  = false;
    uint8_t cs           = 0;
    /* REQ-CANCEL-012 (issue #334): chain-group/position bookkeeping, the
     * same "properties of the enclosing frame, not of any member's own
     * sub-fields" scope server.h's own rcp_server_pending_t.chain_group
     * doc comment describes -- this loop is where frame order is known,
     * so it is where these values are derived. chain_group == 0 is the
     * "not part of a chain" sentinel; every member (chained or not)
     * starts a fresh potential chain_group == i+1 (the +1 avoids
     * colliding with the sentinel when i==0) unless it is itself chained,
     * in which case it keeps its predecessor's own chain_group and
     * advances chain_position by one. */
    uint32_t chain_group    = 0;
    size_t   chain_position = 0;

    real_count = rcp_sched_split_frame_members(frame, frame_len, offsets, RCP_MOCK_MAX_FRAME_MEMBERS);
    if (real_count == 0) return 0;

    stored_count = (real_count < RCP_MOCK_MAX_FRAME_MEMBERS) ? real_count : RCP_MOCK_MAX_FRAME_MEMBERS;
    process_count = (stored_count < out_cap) ? stored_count : out_cap;

    if (!frame_seq_gate_admits(srv, stream_id, sequence_num)) {
        for (i = 0; i < process_count; i++) {
            out_results[i].result      = RCP_MOCK_DISPATCH_SEQ_ERROR;
            out_results[i].byte_bus_id = 0;
            memset(&out_results[i].response, 0, sizeof(out_results[i].response));
        }
        return process_count;
    }

    for (i = 0; i < process_count; i++) {
        size_t                           member_off;
        size_t                           member_end;
        size_t                           member_len;
        const uint8_t                   *member;
        uint8_t                          msg_type    = 0;
        rcp_byte_bus_id_t                byte_bus_id = 0;
        rcp_mock_frame_member_result_t  *out         = &out_results[dispatched];
        bool                              chained_flag;
        uint8_t                           member_tn   = 0;

        member_off = offsets[i];
        if (i + 1 < stored_count) {
            member_end = offsets[i + 1];
        } else if (i + 1 == real_count) {
            /* i is genuinely the last member in the whole frame. */
            member_end = frame_len;
        } else {
            /* real_count exceeds RCP_MOCK_MAX_FRAME_MEMBERS and i is the
             * last offset rcp_sched_split_frame_members() had room to
             * store: there is no reliable way to know where this member
             * ends without its successor's own offset, so stop here
             * rather than guess (and possibly swallow the remainder of
             * the frame into one oversized "member"). */
            break;
        }

        member_len = member_end - member_off;
        member     = &frame[member_off];

        if (!peek_member_byte_bus_id(member, member_len, &msg_type, &byte_bus_id)) {
            out->result      = RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
            out->byte_bus_id = 0;
            memset(&out->response, 0, sizeof(out->response));
            prev_errored     = true;
            dispatched++;
            continue;
        }

        out->byte_bus_id = byte_bus_id;

        /* A chained member's execution condition is its *predecessor*
         * within this same frame -- a chain never spans AVTPDUs, and a
         * member's position in the chain is its position in the frame,
         * not a sub-field of its own. So the chain decision has to be
         * made here, where frame order is known, rather than inside the
         * per-endpoint store. */
        chained_flag = is_chained_member(member, member_len, &cs, &member_tn);

        if (!chained_flag) {
            chain_group    = (uint32_t)i + 1u;
            chain_position = 0;
        } else {
            chain_position++;
        }

        if (chained_flag) {
            rcp_chained_member_outcome_t outcome =
                rcp_chained_advance(&chain_aborted, i > 0, prev_errored, cs);

            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ERROR) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ERROR;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ERROR);
                prev_errored  = true;
                dispatched++;
                continue;
            }
            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ABORTED) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ABORTED;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ABORTED);
                prev_errored  = true;
                dispatched++;
                continue;
            }
        }

        out->result      = rcp_mock_server_dispatch(srv, byte_bus_id, avtp_subtype, msg_type,
                                                      time_sync_supported, stream_id, member,
                                                      member_len, &out->response);

        /* What "the predecessor errored" means for the next member: a
         * member that never reached its endpoint at all (unknown bus,
         * rejected, dropped) counts as an error for chaining purposes;
         * one that ran, queued, or was stored does not. */
        prev_errored = (out->result == RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS ||
                        out->result == RCP_MOCK_DISPATCH_REJECTED ||
                        out->result == RCP_MOCK_DISPATCH_DROPPED);

        /* A chained member accepted into its endpoint's store has its
         * predecessor behind it already, so its chain_exec_delay timer
         * starts now. REQ-CANCEL-012: this same PENDING entry also
         * records its own chain_group/chain_position -- unconditionally,
         * exactly like the chain_predecessor_done() call below, which is
         * itself a no-op for a non-chained entry (see that function's
         * own doc comment); a standalone (non-chained) member that lands
         * here is its own chain's own anchor, tagged chain_position 0,
         * ready to cascade to any actual successors admitted after it.
         *
         * REQ-MOCK-031 (issue #432): stream-scoped, not find_slot() --
         * the dispatch call just above already resolved (and admitted
         * into) the stream-scoped slot; re-finding it with a plain,
         * scope-ignorant byte_bus_id lookup here could land on the WRONG
         * slot's own queue whenever two different stream_ids each hold
         * their own endpoint at this same byte_bus_id. */
        if (out->result == RCP_MOCK_DISPATCH_PENDING) {
            rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
            if (slot) {
                size_t last = last_pending_index(&slot->queue);
                (void)rcp_server_endpoint_chain_predecessor_done(&slot->queue, last, 0u);
                if (last < RCP_SERVER_MAX_PENDING) {
                    slot->queue.pending[last].chain_group    = chain_group;
                    slot->queue.pending[last].chain_position = (uint8_t)chain_position;
                }
            }
        }

        dispatched++;
    }

    return dispatched;
}

//cfusa:req REQ-E2E-033
size_t rcp_mock_server_dispatch_frame_e2e(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                           bool time_sync_supported, uint64_t stream_id,
                                           uint32_t avtp_timestamp, uint8_t sequence_num,
                                           const uint8_t *frame,
                                           size_t frame_len,
                                           rcp_mock_frame_member_result_t *out_results,
                                           size_t out_cap)
{
    size_t offsets[RCP_MOCK_MAX_FRAME_MEMBERS];
    size_t real_count;
    size_t stored_count;
    size_t process_count;
    size_t i;
    size_t dispatched = 0;
    bool   chain_aborted = false;
    bool   prev_errored  = false;
    uint8_t cs           = 0;
    /* REQ-CANCEL-012 (issue #334): same chain-group/position bookkeeping
     * as rcp_mock_server_dispatch_frame()'s own identical local state --
     * see that function's own comment for the full rationale. */
    uint32_t chain_group    = 0;
    size_t   chain_position = 0;

    real_count = rcp_sched_split_frame_members(frame, frame_len, offsets, RCP_MOCK_MAX_FRAME_MEMBERS);
    if (real_count == 0) return 0;

    stored_count = (real_count < RCP_MOCK_MAX_FRAME_MEMBERS) ? real_count : RCP_MOCK_MAX_FRAME_MEMBERS;
    process_count = (stored_count < out_cap) ? stored_count : out_cap;

    if (!frame_seq_gate_admits(srv, stream_id, sequence_num)) {
        for (i = 0; i < process_count; i++) {
            out_results[i].result      = RCP_MOCK_DISPATCH_SEQ_ERROR;
            out_results[i].byte_bus_id = 0;
            memset(&out_results[i].response, 0, sizeof(out_results[i].response));
        }
        return process_count;
    }

    for (i = 0; i < process_count; i++) {
        size_t                           member_off;
        size_t                           member_end;
        size_t                           member_len;
        const uint8_t                   *member;
        uint8_t                          msg_type    = 0;
        rcp_byte_bus_id_t                byte_bus_id = 0;
        rcp_mock_frame_member_result_t  *out         = &out_results[dispatched];
        bool                              chained_flag;
        uint8_t                           member_tn   = 0;

        member_off = offsets[i];
        if (i + 1 < stored_count) {
            member_end = offsets[i + 1];
        } else if (i + 1 == real_count) {
            member_end = frame_len;
        } else {
            break;
        }

        member_len = member_end - member_off;
        member     = &frame[member_off];

        if (!peek_member_byte_bus_id(member, member_len, &msg_type, &byte_bus_id)) {
            out->result      = RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
            out->byte_bus_id = 0;
            memset(&out->response, 0, sizeof(out->response));
            prev_errored     = true;
            dispatched++;
            continue;
        }

        out->byte_bus_id = byte_bus_id;

        chained_flag = is_chained_member(member, member_len, &cs, &member_tn);

        if (!chained_flag) {
            chain_group    = (uint32_t)i + 1u;
            chain_position = 0;
        } else {
            chain_position++;
        }

        if (chained_flag) {
            rcp_chained_member_outcome_t outcome =
                rcp_chained_advance(&chain_aborted, i > 0, prev_errored, cs);

            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ERROR) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ERROR;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ERROR);
                prev_errored  = true;
                dispatched++;
                continue;
            }
            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ABORTED) {
                out->result   = RCP_MOCK_DISPATCH_CHAIN_ABORTED;
                out->response = rcp_acf_build_error_response(byte_bus_id, member_tn,
                                                              RCP_ERROR_CHAIN_ABORTED);
                prev_errored  = true;
                dispatched++;
                continue;
            }
        }

        /* The one real difference from rcp_mock_server_dispatch_frame():
         * each member is independently unwrapped-and-verified against
         * its own CRC32 (if the addressed endpoint has req_crc_enable
         * set) via rcp_mock_server_dispatch_e2e() -- TC18 §13.6's "a
         * separate CRC32... for each E2E-protected ACF message"
         * (REQ-E2E-033), never one CRC across the whole frame. */
        out->result      = rcp_mock_server_dispatch_e2e(srv, byte_bus_id, avtp_subtype, msg_type,
                                                          time_sync_supported, stream_id,
                                                          avtp_timestamp, member, member_len,
                                                          &out->response);

        /* A member whose CRC failed never reached its endpoint's store
         * either -- same chaining-error treatment as unknown-bus/
         * rejected/dropped. */
        prev_errored = (out->result == RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS ||
                        out->result == RCP_MOCK_DISPATCH_REJECTED ||
                        out->result == RCP_MOCK_DISPATCH_DROPPED ||
                        out->result == RCP_MOCK_DISPATCH_CRC_ERROR);

        /* REQ-CANCEL-012: see rcp_mock_server_dispatch_frame()'s own
         * identical block for the full rationale. REQ-MOCK-031 (issue
         * #432): stream-scoped, not find_slot() -- same reasoning as
         * that function's own identical block. */
        if (out->result == RCP_MOCK_DISPATCH_PENDING) {
            rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
            if (slot) {
                size_t last = last_pending_index(&slot->queue);
                (void)rcp_server_endpoint_chain_predecessor_done(&slot->queue, last, 0u);
                if (last < RCP_SERVER_MAX_PENDING) {
                    slot->queue.pending[last].chain_group    = chain_group;
                    slot->queue.pending[last].chain_position = (uint8_t)chain_position;
                }
            }
        }

        dispatched++;
    }

    return dispatched;
}

/* ── Conditional-request execution (TC18 §11.2.2) ─────────────────────────── */

//cfusa:req REQ-MOCK-022
//cfusa:req REQ-RMAP-028
bool rcp_mock_server_set_sequencer_count(rcp_mock_server_t *srv, uint16_t count)
{
    bool     ok;
    uint16_t actual;

    rcp_sequencer_table_free(&srv->sequencers);
    srv->sequencers = rcp_sequencer_table_new(count);
    ok = count == 0u || srv->sequencers.state != NULL;
    /* Sync from srv->sequencers.count (the table's own ACTUAL size),
     * never the raw count argument -- on an allocation failure for a
     * nonzero count, rcp_sequencer_table_new() returns a zeroed table
     * (count=0) per its own doc comment, and the register field must
     * reflect that same "unsupported" reality, not the caller's
     * unmet request. Mirrors rcp_mock_server_transition()'s own
     * "sync from the authoritative post-call value" convention
     * (REQ-RMAP-023). svr_sequencers_max (REQ-RMAP-028) is 8 bit on the
     * wire -- an actual count this test double's own uint16_t API could
     * in principle produce but the real register could never hold is
     * capped at the register's own representable maximum (0xFF), never
     * silently truncated/wrapped, so the recorded value is never
     * smaller than the truth. */
    actual = srv->sequencers.count;
    srv->regmap.svr_sequencers_max = (actual > 0xFFu) ? (uint8_t)0xFFu : (uint8_t)actual;
    return ok;
}

//cfusa:req REQ-MOCK-022
rcp_sequencer_table_t *rcp_mock_server_sequencers(rcp_mock_server_t *srv)
{
    return &srv->sequencers;
}

//cfusa:req REQ-MOCK-023
//cfusa:req REQ-MOCK-024
bool rcp_mock_server_tick(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                           const rcp_server_tick_ctx_t *ctx, rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot;
    rcp_server_tick_ctx_t     local;
    size_t                    index = 0;

    memset(out_response, 0, sizeof(*out_response));

    slot = find_slot(srv, byte_bus_id);
    if (!slot) return false;

    /* The sequencer table is the server's, never the caller's. */
    local            = *ctx;
    local.sequencers = &srv->sequencers;

    if (!rcp_server_endpoint_select_due(&slot->queue, &local, &index)) return false;

    /* REQ-TIMED-012/013 (issue #422): a cancellation admitted under a TSCF
     * header with a future presentation time is stored in the request
     * store exactly like a standard/conditional request (see
     * rcp_server_endpoint_admit()'s own TSCF gate, admit_under_tscf_gate())
     * and surfaces here the same way once due -- but its own stored frame
     * is a cancellation request, not something an endpoint handler
     * understands. apply_cancellation() (this file, above) is the same
     * function finish_admission()'s own RCP_SERVER_ADMIT_CANCELLATION case
     * already calls for the tv=false/immediate path; this is that same
     * logic, just reached from the deferred path instead. */
    if (slot->queue.pending[index].kind == RCP_SCHED_KIND_CANCELLATION) {
        apply_cancellation(slot, slot->queue.pending[index].request_type,
                            slot->queue.pending[index].frame.data,
                            slot->queue.pending[index].frame.len, byte_bus_id, out_response);
    } else {
        /* Run the selected request's own stored frame through the endpoint's
         * handler -- the identical execution path a standard request takes. */
        run_handler(slot, slot->queue.pending[index].frame.data,
                     slot->queue.pending[index].frame.len, out_response);
    }

    (void)rcp_server_endpoint_complete(&slot->queue, index, &local);
    return true;
}

//cfusa:req REQ-MOCK-025
size_t rcp_mock_server_notify_trigger(rcp_mock_server_t *srv, uint8_t source_ep,
                                       uint8_t signal_nr)
{
    size_t i;
    size_t matched = 0;

    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        if (!srv->endpoints[i].in_use) continue;
        matched += rcp_server_endpoint_notify_trigger(&srv->endpoints[i].queue, source_ep,
                                                       signal_nr);
    }
    return matched;
}

//cfusa:req REQ-MOCK-027
size_t rcp_mock_server_pending_count(const rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    const rcp_mock_endpoint_slot_t *slot = find_slot_const(srv, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_pending_count(&slot->queue);
}

//cfusa:req REQ-MOCK-026
size_t rcp_mock_server_watchdog_purge(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_watchdog_purge(&slot->queue);
}

//cfusa:req REQ-E2E-029
//cfusa:req REQ-E2E-030
//cfusa:req REQ-E2E-045
size_t rcp_mock_server_broadcast_safe_state(rcp_mock_server_t *srv, uint8_t request_stream_index)
{
    rcp_byte_bus_id_t bound[RCP_MOCK_MAX_ENDPOINTS];
    size_t             total_bound;
    size_t             purged = 0;
    size_t             i;

    if (request_stream_index == 0u) return 0;

    total_bound = rcp_regmap_ep_id_map_byte_bus_ids_for_stream(
        srv->ep_id_map, srv->ep_id_map_count, request_stream_index, bound,
        RCP_MOCK_MAX_ENDPOINTS);
    /* This test double never registers more than RCP_MOCK_MAX_ENDPOINTS
     * live slots (RCP_MOCK_ERR_CAPACITY, rcp_mock_server_add_endpoint()),
     * so a bound byte_bus_id beyond that many distinct values can never
     * name a slot this srv actually holds -- the "ask first" total isn't
     * separately re-scanned here for that reason. */
    if (total_bound > RCP_MOCK_MAX_ENDPOINTS) total_bound = RCP_MOCK_MAX_ENDPOINTS;

    for (i = 0; i < total_bound; i++) {
        rcp_mock_endpoint_slot_t *slot = find_slot(srv, bound[i]);
        if (!slot) continue; /* bound in EP_ID_config, not (or no longer) registered */
        purged += rcp_server_endpoint_watchdog_purge(&slot->queue);
    }

    return purged;
}

//cfusa:req REQ-SRV-018
size_t rcp_mock_server_notify_gptp_lock_state(rcp_mock_server_t *srv, bool locked,
                                               uint8_t source_ep)
{
    uint8_t signal_nr;

    if (!rcp_server_gptp_trigger_evaluate(&srv->gptp_trigger_state, locked, &signal_nr)) {
        return 0; /* no edge -- unchanged locked value, or the very first call */
    }

    /* rcp_mock_server_notify_trigger() (below) is already this test
     * double's own "report one trigger occurrence to every registered
     * endpoint" broadcast primitive -- REQ-SRV-015's own per-endpoint-
     * type triggers already reuse it; the derived Table 37 signal is
     * just another occurrence to report through the same call, not a
     * reason to duplicate its own iterate-every-in_use-slot loop here. */
    return rcp_mock_server_notify_trigger(srv, source_ep, signal_nr);
}
