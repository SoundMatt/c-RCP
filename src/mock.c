/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/mock.h"

#include "mem_bounded.h"

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/e2e.h"
#include "rcp/ingress.h"
#include "rcp/request.h"
#include "rcp/scheduler.h"
#include "rcp/alloc.h"

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
     * doc comment, mock.h). Run by slot_execute() on every path (issue
     * #618). */
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
     * sets this field). Read only by rx_crc_required() -- the receive
     * pipeline's single CRC-requirement source (issue #618). */
    bool                         req_crc_enable;
    /* REQ-E2E-021 (issue #201): this test double's own stand-in for
     * TC18 §12.7.7 Table 24's own rx_enforce_e2e (a per-REQUEST-STREAM
     * config bit in the real spec, not per-endpoint -- kept here as a
     * per-endpoint stand-in for the same "type-erased slot has no way
     * to read a real config table generically" reason
     * req_crc_enable's own doc comment already gives). Defaults false
     * (RCP_E2E_CRC_ACTION_DROP_REQUEST), matching req_crc_enable's own
     * default disposition. Consulted only by rx_crc_fault_latch() on a
     * CRC mismatch, to decide whether that mismatch also latches the whole
     * stream faulted (see rcp_mock_server_set_stream_fault_tracker()). */
    bool                         rx_enforce_e2e;
} rcp_mock_endpoint_slot_t;

/* ── Reassembly context (issue #618 step 6) ─────────────────────────────────── */

/* One in-flight fragmented request. Bound to (stream_id, byte_bus_id)
 * while in_use; tn_valid is set by the first (ms = 1) fragment, which
 * also records everything the fragmented CRC needs from the FIRST AVTPDU
 * and ACF header (REQ-E2E-038). pinned is set by rcp_mock_server_
 * fragment_reassembler_for(): a pinned context stays bound (with any
 * caller-tuned bound) after its message completes. */
typedef struct {
    bool                       in_use;
    bool                       pinned;
    bool                       tn_valid;
    uint64_t                   stream_id;
    rcp_byte_bus_id_t          byte_bus_id;
    uint8_t                    transaction_num;
    uint8_t                    first_hdr[RCP_ACF_GBB_HEADER_LEN];
    size_t                     first_hdr_len;
    uint8_t                    subtype;
    uint8_t                    header_octet1;
    bool                       tu;
    uint32_t                   avtp_timestamp;
    rcp_fragment_reassembler_t reasm;
} rcp_mock_reasm_ctx_t;

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
    /* Step 6 (issue #618, REQ-INGRESS-014/016): a bounded pool of
     * reassembly contexts, each bound to one (stream_id, byte_bus_id) for
     * the life of one fragmented request -- replacing the old per-request-
     * stream frag_reasm[] table, which collided when two endpoints on one
     * stream were sent fragmented requests interleaved and did not exist
     * at all for a stream with no request-stream row. Every context's
     * rcp_fragment_reassembler_t is initialized in rcp_mock_server_new()
     * with RCP_MOCK_FRAG_REASM_DEFAULT_MAX_TOTAL_LEN (calloc's zero would
     * reject every fragment) and destroyed in rcp_mock_server_destroy(). */
    rcp_mock_reasm_ctx_t            reasm[RCP_MOCK_MAX_CONCURRENT_REASSEMBLIES];
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
     * rcp_mock_server_broadcast_safe_state()'s sole data source.
     * REQ-RMAP-054 (issue #459): NOT left at calloc()'s own all-zero --
     * rcp_mock_server_new() seeds row 0 with rcp_regmap_ep_id_map_row_
     * init_default()'s power-on default (ep_id_map_count = 1) before
     * returning, matching that primitive's own documented contract. */
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
    /* REQ-AVTP-029/030 (issue #431, TC18 §13.3 rule 1): the RC Server's
     * own configuration for a TSCF frame received without time-sync
     * support -- see rcp_mock_server_set_tscf_unsupported_time_sync_
     * policy() (mock.h). RCP_AVTP_TSCF_FALLBACK_DROP is 0, so calloc()
     * gives the drop default. Issue #618: no longer shared with the
     * reserved-bytes rule (tscf_reserved_zero_policy below). */
    rcp_avtp_tscf_fallback_t tscf_unsupported_time_sync_policy;
    /* Issue #618 (REQ-INGRESS-004/005): TC18-13.3-003's own knob, separate
     * from the unsupported-time-sync one above -- see rcp_mock_server_set_
     * tscf_reserved_zero_policy() (mock.h). RCP_AVTP_TSCF_RESERVED_ZERO_
     * TIME_GATE is 0, so calloc() gives every server the default. */
    rcp_avtp_tscf_reserved_zero_policy_t tscf_reserved_zero_policy;
    /* REQ-INGRESS-021: how many runs had output that did not fit their
     * response capacity (rcp_mock_server_response_overflow_count()). */
    size_t                               response_overflow_count;
};

/* Reassembly-pool helpers (defined with the ingress pipeline below),
 * needed earlier by the accessors and purge hooks. */
static rcp_mock_reasm_ctx_t *reasm_find(rcp_mock_server_t *srv, uint64_t stream_id,
                                        rcp_byte_bus_id_t byte_bus_id);
static rcp_mock_reasm_ctx_t *reasm_claim(rcp_mock_server_t *srv, uint64_t stream_id,
                                         rcp_byte_bus_id_t byte_bus_id);
static void reasm_purge(rcp_mock_server_t *srv, bool match_stream, uint64_t stream_id,
                        bool match_bus, rcp_byte_bus_id_t byte_bus_id);

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
//cfusa:req REQ-RMAP-054
rcp_mock_server_t *rcp_mock_server_new(void)
{
    rcp_mock_server_t *srv = (rcp_mock_server_t *)rcp_calloc(1, sizeof(*srv));
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
    /* REQ-INGRESS-014: every pool context needs a real max_total_len,
     * not calloc()'s own zero -- see the pool's declaration comment. */
    {
        size_t i;
        for (i = 0; i < RCP_MOCK_MAX_CONCURRENT_REASSEMBLIES; i++) {
            rcp_fragment_reassembler_init(&srv->reasm[i].reasm,
                                           RCP_MOCK_FRAG_REASM_DEFAULT_MAX_TOTAL_LEN);
        }
    }
    rcp_server_gptp_trigger_state_init(&srv->gptp_trigger_state);
    /* REQ-RMAP-054 (issue #459): TC18 §12.7.8 requires the EP_ID_config
     * table's power-on default to permit EP0 access before any client
     * config is written. rcp_regmap_ep_id_map_row_init_default()'s own
     * contract already says callers owning a fixed-capacity table (srv's
     * own ep_id_map[] above is exactly that) are expected to place its
     * result at row 0 at startup, before any client write -- do that
     * here, rather than leaving ep_id_map[] at calloc()'s own all-zero
     * (request_stream_index == 0, TC18's own end-of-table sentinel,
     * which rcp_regmap_ep_id_map_effective_count() would read as "zero
     * real rows", not a valid EP0 mapping). A later, explicit
     * rcp_mock_server_set_ep_id_map() call still replaces this default
     * row wholesale, exactly like every other "seeded default, freely
     * overwritten by a real client config" field in this constructor. */
    rcp_regmap_ep_id_map_row_init_default(&srv->ep_id_map[0]);
    srv->ep_id_map_count = 1;
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
             * comment below already calls out for the reassembly pool. */
            rcp_bytes_free(&srv->endpoints[i].deferred_response);
        }
    }
    /* REQ-INGRESS-014: pool contexts own heap storage
     * (rcp_fragment_reassembler_t's own buf field) once any fragment has
     * ever been fed to them -- must be released here, not just
     * memset/freed along with srv itself, or every server that ever
     * exercised fragmentation leaks. */
    for (i = 0; i < RCP_MOCK_MAX_CONCURRENT_REASSEMBLIES; i++) {
        rcp_fragment_reassembler_destroy(&srv->reasm[i].reasm);
    }
    rcp_sequencer_table_free(&srv->sequencers);
    rcp_free(srv);
    srv = NULL;
}

//cfusa:req REQ-MOCK-004
rcp_lifecycle_state_t rcp_mock_server_state(const rcp_mock_server_t *srv)
{
    return srv->state;
}

//cfusa:req REQ-MOCK-005
//cfusa:req REQ-RMAP-023
//cfusa:req REQ-INGRESS-017
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
    /* REQ-INGRESS-017: no half-collected fragmented request survives a
     * lifecycle transition. */
    if (rc == RCP_LIFECYCLE_OK) reasm_purge(srv, false, 0u, false, 0u);
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

    if (len > 0) rcp_memcpy_bounded(srv->hw_pin_map, sizeof(srv->hw_pin_map), entries, len * sizeof(*entries));
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

    if (count > 0) rcp_memcpy_bounded(srv->request_stream_cfg, sizeof(srv->request_stream_cfg), entries, count * sizeof(*entries));
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

    if (count > 0) rcp_memcpy_bounded(srv->response_queue_cfg, sizeof(srv->response_queue_cfg), entries, count * sizeof(*entries));
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

    if (count > 0) rcp_memcpy_bounded(srv->ep_id_map, sizeof(srv->ep_id_map), entries, count * sizeof(*entries));
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

    if (len > 0) rcp_memcpy_bounded(cfg->data, sizeof(cfg->data), data, len);
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

/* const counterpart of find_slot_on_stream(), for read-only stream-scoped
 * accessors (rcp_mock_server_pending_count_on_stream(), issue #447) --
 * same relationship find_slot_const() already has to find_slot(). */
static const rcp_mock_endpoint_slot_t *find_slot_on_stream_const(const rcp_mock_server_t *srv,
                                                                   uint64_t           stream_id,
                                                                   rcp_byte_bus_id_t  byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_ENDPOINTS; i++) {
        const rcp_mock_endpoint_slot_t *slot = &srv->endpoints[i];
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

/* REQ-MOCK-032 (issue #447): stream-scoped counterpart of
 * rcp_mock_server_remove_endpoint(), following the same "new function, not
 * a breaking change" pattern rcp_mock_server_add_endpoint_on_stream()
 * (issue #432) already established -- the plain, unscoped
 * rcp_mock_server_remove_endpoint() above is untouched, still resolving by
 * byte_bus_id alone via find_slot() for every one of its existing call
 * sites. Only needed once two slots can legitimately share one
 * byte_bus_id on different stream_ids (rcp_mock_server_add_endpoint_
 * on_stream()); this is the disambiguated remove for that case. */
//cfusa:req REQ-MOCK-032
bool rcp_mock_server_remove_endpoint_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                                rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
    if (!slot) return false;

    rcp_server_endpoint_destroy(&slot->queue);
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

/* Deprecated (issue #618): reassembly is keyed per (stream_id,
 * byte_bus_id) now, so a per-stream accessor can only return one of the
 * stream's contexts -- the first bound one. */
//cfusa:req REQ-INGRESS-028
rcp_fragment_reassembler_t *rcp_mock_server_fragment_reassembler(rcp_mock_server_t *srv,
                                                                  uint64_t stream_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_CONCURRENT_REASSEMBLIES; i++) {
        if (srv->reasm[i].in_use && srv->reasm[i].stream_id == stream_id) {
            return &srv->reasm[i].reasm;
        }
    }
    return NULL;
}

//cfusa:req REQ-INGRESS-028
rcp_fragment_reassembler_t *rcp_mock_server_fragment_reassembler_for(rcp_mock_server_t *srv,
                                                                      uint64_t stream_id,
                                                                      rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_reasm_ctx_t *c = reasm_find(srv, stream_id, byte_bus_id);

    if (!c) c = reasm_claim(srv, stream_id, byte_bus_id);
    if (!c) return NULL;
    c->pinned = true;
    return &c->reasm;
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

/* REQ-MOCK-032 (issue #447): stream-scoped counterpart of
 * rcp_mock_server_set_endpoint_enable() -- see
 * rcp_mock_server_remove_endpoint_on_stream()'s own doc comment for the
 * shared rationale/pattern every _on_stream() accessor added by this fix
 * follows. */
//cfusa:req REQ-MOCK-032
bool rcp_mock_server_set_endpoint_enable_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                                    rcp_byte_bus_id_t byte_bus_id, bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
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

//cfusa:req REQ-MOCK-032
bool rcp_mock_server_set_endpoint_req_crc_enable_on_stream(rcp_mock_server_t *srv,
                                                             uint64_t stream_id,
                                                             rcp_byte_bus_id_t byte_bus_id,
                                                             bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
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

//cfusa:req REQ-MOCK-032
bool rcp_mock_server_set_endpoint_rx_enforce_e2e_on_stream(rcp_mock_server_t *srv,
                                                             uint64_t stream_id,
                                                             rcp_byte_bus_id_t byte_bus_id,
                                                             bool enable)
{
    rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
    if (!slot) return false;

    slot->rx_enforce_e2e = enable;
    return true;
}

//cfusa:req REQ-AVTP-029
void rcp_mock_server_set_tscf_unsupported_time_sync_policy(rcp_mock_server_t       *srv,
                                                             rcp_avtp_tscf_fallback_t policy)
{
    srv->tscf_unsupported_time_sync_policy = policy;
}

//cfusa:req REQ-INGRESS-005
void rcp_mock_server_set_tscf_reserved_zero_policy(rcp_mock_server_t                   *srv,
                                                     rcp_avtp_tscf_reserved_zero_policy_t policy)
{
    srv->tscf_reserved_zero_policy = policy;
}

//cfusa:req REQ-INGRESS-021
size_t rcp_mock_server_response_overflow_count(const rcp_mock_server_t *srv)
{
    return srv->response_overflow_count;
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

/* ── Ingress pipeline (issue #618) ────────────────────────────────────────────
 *
 * Every receive path in this file -- rcp_mock_server_receive_avtpdu() and
 * the ten legacy rcp_mock_server_dispatch*() wrappers -- goes through the
 * stage functions below, in one fixed order:
 *
 *   frame:  parse (rcp_ingress_parse_avtpdu(), receive_avtpdu() only)
 *        -> timing decision + subtype filter (rx_frame_admit_timing())
 *        -> member pre-pass (frames only, rx_frame_run())
 *        -> sequence gate (frames only, frame_seq_gate_admits())
 *   member: lifecycle filter (rx_lifecycle_filter())
 *        -> stream-fault gate (rx_stream_fault_gate())
 *        -> Table 4 fixed fields (rx_fixed_fields_stage())
 *        -> endpoint lookup
 *        -> reassembly (rx_reassemble_stage())
 *        -> CRC (rx_crc_stage())
 *        -> admission (rx_admit_member()) / execution (slot_execute())
 *
 * The filters run before any gate that has side effects, so a frame or
 * member that lifecycle filtering drops never produces a response, never
 * latches a stream fault, never changes stream status or the sequence
 * tracker and never broadcasts safe state. Responses are collected per
 * member in an rx_out_t instead of a single rcp_bytes_t, so a
 * multi-response endpoint's output survives every path, including stored
 * and scheduled runs. */

/* Per-member response collector: out[0..count) are populated responses
 * (an entry may be zeroed again by suppress_response_per_stream_cfg()).
 * reason records the first stage that decided the member's outcome;
 * truncated records that at least one response did not fit in out[]. */
typedef struct {
    rcp_bytes_t          *out;
    size_t                cap;
    size_t                count;
    rcp_mock_rx_reason_t  reason;
    bool                  truncated;
} rx_out_t;

static void rx_out_init(rx_out_t *o, rcp_bytes_t *buf, size_t cap)
{
    size_t i;

    o->out       = buf;
    o->cap       = cap;
    o->count     = 0;
    o->reason    = RCP_MOCK_RX_REASON_NONE;
    o->truncated = false;
    for (i = 0; i < cap; i++) memset(&buf[i], 0, sizeof(buf[i]));
}

static void rx_out_set_reason(rx_out_t *o, rcp_mock_rx_reason_t reason)
{
    if (o->reason == RCP_MOCK_RX_REASON_NONE) o->reason = reason;
}

/* REQ-INGRESS-021: a response that does not fit is never dropped
 * silently -- the collector is flagged and srv's own overflow counter
 * (rcp_mock_server_response_overflow_count()) advances. */
//cfusa:req REQ-INGRESS-021
static void rx_note_overflow(rcp_mock_server_t *srv, rx_out_t *o)
{
    srv->response_overflow_count++;
    o->truncated = true;
}

/* Takes ownership of r (a zeroed r means "no response" and is ignored). */
static void rx_out_push(rcp_mock_server_t *srv, rx_out_t *o, rcp_bytes_t r)
{
    if (r.data == NULL) return;
    if (o->count < o->cap) {
        o->out[o->count++] = r;
        return;
    }
    rcp_bytes_free(&r);
    rx_note_overflow(srv, o);
}

/* Reads a request's transaction_num back out of its header, for the
 * error responses the gates below build. False for anything too short to
 * carry a byte_message_info header. */
static bool member_transaction_num(const uint8_t *member, size_t member_len, uint8_t *out_tn)
{
    rcp_acf_byte_message_info_t hdr = {0};

    /* rcp_acf_unpack_header() cannot fail once 8 octets are present. */
    if (member_len < 8u) return false;
    (void)rcp_acf_unpack_header(member, &hdr);
    *out_tn = hdr.transaction_num;
    return true;
}

/* Joins buf[0..count)'s populated responses into one rcp_bytes_t for the
 * legacy single-response surfaces (dispatch(), tick(), drain_endpoint(),
 * the frame wrappers' per-member response). Each response is a whole ACF
 * message, so the concatenation is a valid ACF member stream that
 * rcp_sched_split_frame_members() splits back into the originals. One
 * response is moved, not copied. Every entry of buf is left zeroed. */
//cfusa:req REQ-INGRESS-020
static void rx_join_responses(rcp_bytes_t *buf, size_t count, rcp_bytes_t *out)
{
    size_t   i;
    size_t   total   = 0;
    size_t   nonnull = 0;
    size_t   first   = 0;
    size_t   off     = 0;
    uint8_t *joined;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < count; i++) {
        if (buf[i].data == NULL) continue;
        if (nonnull == 0) first = i;
        nonnull++;
        total += buf[i].len;
    }
    if (nonnull == 0) return;
    if (nonnull == 1) {
        *out = buf[first];
        memset(&buf[first], 0, sizeof(buf[first]));
        return;
    }

    joined = (uint8_t *)rcp_malloc(total);
    for (i = 0; i < count; i++) {
        if (buf[i].data == NULL) continue;
        if (joined) {
            rcp_memcpy_bounded(&joined[off], total - off, buf[i].data, buf[i].len);
            off += buf[i].len;
        }
        rcp_bytes_free(&buf[i]);
    }
    if (!joined) return; /* allocation failure: nothing to hand back */
    out->data = joined;
    out->len  = total;
}

/* Step 8 (REQ-INGRESS-019/020): the one execution path for every slot,
 * replacing the old run_handler(). A plain handler contributes at most
 * one response; a multi-response handler writes up to the collector's
 * remaining capacity. A multi-response handler that reports more
 * responses than it was given room for (*out_count > out_cap, the usual
 * "ask first" idiom) has the excess flagged, never silently lost. */
//cfusa:req REQ-INGRESS-019
//cfusa:req REQ-INGRESS-021
static void slot_execute(rcp_mock_server_t *srv, rcp_mock_endpoint_slot_t *slot,
                         const uint8_t *request, size_t request_len, rx_out_t *o)
{
    if (slot->multi_handler != NULL) {
        size_t       avail = o->cap - o->count;
        size_t       n     = 0;
        rcp_bytes_t *dst   = (avail > 0u) ? &o->out[o->count] : NULL;

        slot->multi_handler(request, request_len, dst, avail, &n, slot->user_data);
        if (n > avail) {
            n = avail;
            rx_note_overflow(srv, o);
        }
        o->count += n;
    } else if (slot->handler != NULL) {
        rcp_bytes_t r = {0};

        slot->handler(request, request_len, &r, slot->user_data);
        rx_out_push(srv, o, r);
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

/* issue #454 (this session's own audit finding, following on from #430):
 * RCP_SERVER_ADMIT_REJECTED is NOT one single TC18 response shape --
 * server.c's rcp_server_endpoint_admit() reports several distinct
 * rejection reasons via *out_error, and TC18's own text does not treat
 * them uniformly. The general rule, per §11.3.1's own wording ("err = 1
 * indicates that the request has been rejected" for a request that was
 * never filed into EP request storage at all), is the Acknowledge-
 * rejected shape #430 added (RCP_ADMIT_REJECT_SHAPE_ACKNOWLEDGE below).
 * But §13.5.1 explicitly overrides that default for exactly one
 * admission-rejection reason this codebase currently reports: a
 * compound-wait request's reserved evt[2:0] = 011b, which server.c's
 * admit() reports as RCP_ERROR_UNSUPPORTED_CMD from one single call site
 * (the RCP_SCHED_KIND_COMPOUND_WAIT case) -- "request shall be ignored
 * and an err-response with error code = UNSUPPORTED_CMD shall be sent."
 * "err-response" is TC18's own specific term for §11.3.4's Error
 * Response shape (evt[3:0] < 0x9, err = 1), structurally distinct from
 * §11.3.1's Acknowledge (evt[3:0] = 0xF).
 *
 * This is not a guess: "err-response" appears exactly twice in the whole
 * TC18 0.5.1_RC5 specification text -- here, and in Table 33's unrelated
 * GPIO/PWM_OUT evt[2:0]=100b execution-time row (not reachable through
 * this admission path at all). Every OTHER RCP_ERROR_* value admit() can
 * report for RCP_SERVER_ADMIT_REJECTED -- RCP_ERROR_INVALID_PARAMETER
 * (a response frame received where a request was expected) and
 * RCP_ERROR_REQUEST_STORAGE_OVERFLOW (the request store is full; TC18
 * §12.7's own "newly arriving requests are dropped and responses with
 * the corresponding error code generated" carries no "err-response"
 * wording of its own) -- has no such override anywhere in the spec text,
 * and correctly keeps the §11.3.1 Acknowledge-rejected shape #430
 * established. */
typedef enum {
    RCP_ADMIT_REJECT_SHAPE_ACKNOWLEDGE,    /* TC18 §11.3.1 (evt=0xF, err=1) */
    RCP_ADMIT_REJECT_SHAPE_ERROR_RESPONSE, /* TC18 §11.3.4 (evt<0x9, err=1) */
} rcp_admit_reject_shape_t;

/* Per-rejection-reason lookup (issue #454): which TC18 response shape a
 * given admission-rejection *out_error corresponds to. See the doc
 * comment immediately above for the spec text this is derived from --
 * RCP_ERROR_UNSUPPORTED_CMD is the ONLY value server.c's admit() can
 * report for RCP_SERVER_ADMIT_REJECTED that TC18 itself calls an
 * "err-response"; every other value falls through to the general §11.3.1
 * default. RCP_ERROR_NONE never reaches this function (finish_admission()
 * only calls it once error != RCP_ERROR_NONE has already been checked). */
static rcp_admit_reject_shape_t admission_reject_response_shape(rcp_wire_error_t error)
{
    switch (error) {
    case RCP_ERROR_UNSUPPORTED_CMD:
        /* TC18 §13.5.1 (compound-wait reserved evt[2:0] = 011b),
         * REQ-ACF-024/REQ-SRV-019. */
        return RCP_ADMIT_REJECT_SHAPE_ERROR_RESPONSE;
    default:
        return RCP_ADMIT_REJECT_SHAPE_ACKNOWLEDGE;
    }
}

/* The admission-rejection response for a request that was never filed
 * into its endpoint's request storage -- see finish_admission()'s own doc
 * comment for the two TC18 shapes and why error decides between them.
 * Shared by finish_admission()'s REJECTED case and the receive path's
 * Table 4 fixed-field rejection (rx_fixed_fields_stage(), REQ-INGRESS-
 * 013), which reports the same RCP_ERROR_INVALID_PARAMETER outcome as
 * REQ-ACF-050's rsp check. Zeroed when error is RCP_ERROR_NONE or the
 * request is too short to carry a header. */
static rcp_bytes_t admission_reject_response(rcp_byte_bus_id_t byte_bus_id, const uint8_t *request,
                                             size_t request_len, rcp_wire_error_t error)
{
    rcp_acf_byte_message_info_t hdr  = {0};
    rcp_bytes_t                 resp = {0};

    if (error == RCP_ERROR_NONE || request_len < 8u) return resp;
    (void)rcp_acf_unpack_header(request, &hdr); /* cannot fail on 8 octets */

    if (admission_reject_response_shape(error) == RCP_ADMIT_REJECT_SHAPE_ERROR_RESPONSE) {
        /* TC18 §13.5.1 "err-response": unconditional, no evt[3]
         * qualifier -- matches every other rcp_acf_build_error_response()
         * call site in this file. */
        resp = rcp_acf_build_error_response(byte_bus_id, hdr.transaction_num, error);
    } else if (rcp_acf_evt_requests_acknowledge(hdr.evt)) {
        /* REQ-SRV-016's own evt[3] "if requested" gate, mirrored here for
         * the rejected-Acknowledge sibling (issue #454; #430 omitted this
         * gate). */
        resp = rcp_acf_build_acknowledge_rejected_response(byte_bus_id, hdr.transaction_num, error);
    }
    return resp;
}

/* Maps one server.h admission outcome onto this module's own dispatch
 * result, running the endpoint (slot_execute()) for the execute-now case.
 *
 * ack (issue #463, REQ-SRV-016): the caller's own already-built
 * rcp_server_endpoint_admit_with_ack() out_ack -- a genuine Acknowledge
 * response when the request was successfully filed into ep's request
 * storage (RCP_SERVER_ADMIT_QUEUED or RCP_SERVER_ADMIT_PENDING) and its
 * own evt[3] asked for one, left zeroed otherwise (server.h's own doc
 * comment). Handed to the response collector for exactly those two
 * outcomes, below -- the success-Acknowledge sibling of
 * admission_reject_response()'s own rejection-Acknowledge handling.
 *
 * error is rcp_server_endpoint_admit()'s *out_error: RCP_ERROR_NONE for
 * every outcome except the rejection paths that determined a specific
 * TC18 Table 30 code. When non-RCP_ERROR_NONE, admission_reject_
 * response_shape() decides which of TC18's two rejection response shapes
 * applies (issue #454, extending REQ-ACF-033/REQ-ACF-024):
 *
 *   - RCP_ADMIT_REJECT_SHAPE_ERROR_RESPONSE: TC18's own "err-response"
 *     wording (§13.5.1) for this specific rejection reason overrides the
 *     general rule below -- built via rcp_acf_build_error_response(),
 *     unconditionally.
 *   - RCP_ADMIT_REJECT_SHAPE_ACKNOWLEDGE (the default): TC18 §11.3.1's
 *     Acknowledge-shaped storage-admission-rejection response
 *     (rcp_acf_build_acknowledge_rejected_response(), evt=0xF/err=1) --
 *     NOT the §11.3.4 Error Response, since RCP_SERVER_ADMIT_REJECTED
 *     here specifically means the request was never filed into EP
 *     request storage at all (issue #430, REQ-ACF-033). Built only when
 *     the request's own evt[3] asked for an acknowledge.
 *
 * A side effect worth flagging for a future reader: suppress_response_
 * per_stream_cfg() (REQ-RMAP-048/049) classifies each response via
 * rcp_acf_classify_response() to decide which of Table 24's two routing
 * pointers governs it -- the Acknowledge-shaped branch is governed by
 * rx_ack_stream_index (power-on default 0, "no acknowledge is to be
 * sent"); the Error-Response-shaped branch by rx_resp_stream_index
 * (power-on default 1). A caller/test that wants to actually observe the
 * Acknowledge-shaped branch's response must configure rx_ack_stream_index
 * to a nonzero value for the resolved request stream. */
static rcp_mock_dispatch_result_t finish_admission(rcp_mock_server_t *srv,
                                                    rcp_mock_endpoint_slot_t *slot,
                                                    rcp_server_admit_t admit, uint8_t request_type,
                                                    const uint8_t *request, size_t request_len,
                                                    rcp_wire_error_t error,
                                                    rcp_byte_bus_id_t byte_bus_id,
                                                    rcp_bytes_t *ack, rx_out_t *o)
{
    rcp_bytes_t cancel_resp = {0};

    switch (admit) {
    case RCP_SERVER_ADMIT_EXECUTE_NOW:
        slot_execute(srv, slot, request, request_len, o);
        return RCP_MOCK_DISPATCH_OK;
    case RCP_SERVER_ADMIT_QUEUED:
        /* issue #463, REQ-SRV-016: ack was already built by
         * rcp_server_endpoint_admit_with_ack() iff the request's own
         * evt[3] asked for one; zeroed (nothing pushed) otherwise. */
        rx_out_push(srv, o, *ack);
        return RCP_MOCK_DISPATCH_QUEUED;
    case RCP_SERVER_ADMIT_PENDING:
        /* issue #463, REQ-SRV-016: same transfer as RCP_SERVER_ADMIT_QUEUED
         * immediately above, covering every kind that can reach this
         * outcome (Standard/Cancellation under a TSCF gate, and Compound/
         * Compound Wait/Triggered/Timed/Chained). */
        rx_out_push(srv, o, *ack);
        return RCP_MOCK_DISPATCH_PENDING;
    case RCP_SERVER_ADMIT_CANCELLATION:
        apply_cancellation(slot, request_type, request, request_len, byte_bus_id, &cancel_resp);
        rx_out_push(srv, o, cancel_resp);
        return RCP_MOCK_DISPATCH_CANCELLED;
    case RCP_SERVER_ADMIT_REJECTED:
    default:
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_ADMISSION);
        rx_out_push(srv, o, admission_reject_response(byte_bus_id, request, request_len, error));
        return RCP_MOCK_DISPATCH_REJECTED;
    }
}

/* Per-frame receive context shared by every member of one AVTPDU: the
 * parsed (or, for the legacy wrappers, synthesized) header, the caller's
 * time context, and the frame-level timing decision. */
typedef struct {
    rcp_ingress_avtpdu_t f;
    bool                 time_sync_supported;
    uint64_t             gptp_reference_now;
    uint8_t              eff_subtype; /* rcp_ingress_resolve_timing() */
    bool                 eff_tv;      /* rcp_ingress_resolve_timing() */
} rx_frame_t;

//cfusa:req REQ-MOCK-021
//cfusa:req REQ-MOCK-030
//cfusa:req REQ-AVTP-030
/* Step 4 (issue #618): admission proper -- the store/ack/execute/overflow
 * half of the old dispatch_plain_inner(), now reached only after every
 * filter and gate has passed, with the frame's own effective tv /
 * avtp_timestamp / gptp_reference_now. REQ-AVTP-030 (TC18 §13.3 rule 1):
 * under RCP_AVTP_TSCF_FALLBACK_IGNORE a TSCF frame on a server without
 * time sync arrives here with effective tv already false
 * (rcp_ingress_resolve_timing()), i.e. "executed as if no presentation
 * time were included". */
static rcp_mock_dispatch_result_t rx_admit_member(rcp_mock_server_t *srv, const rx_frame_t *fr,
                                                  rcp_mock_endpoint_slot_t *slot,
                                                  const uint8_t *request, size_t request_len,
                                                  rcp_byte_bus_id_t byte_bus_id, rx_out_t *o)
{
    rcp_server_admit_t admit;
    uint8_t            request_type        = 0;
    rcp_wire_error_t   error               = RCP_ERROR_NONE;
    size_t             admitted_index      = 0;
    rcp_bytes_t        ack                 = {0};
    bool               effective_tv        = fr->eff_tv;
    uint32_t           avtp_timestamp      = fr->f.avtp_timestamp;
    uint64_t           gptp_reference_now  = fr->gptp_reference_now;
    bool               time_sync_supported = fr->time_sync_supported;
    uint64_t           stream_id           = fr->f.stream_id;

    /* The request_type-aware routing decision lives in server.h: a
     * standard request keeps the original submit-or-queue behavior, a
     * conditional one is decoded and stored, a cancellation one is
     * reported back for this module to apply. Admission carries no tick of
     * its own (0): a stored request's exec_delay is measured from the
     * moment its own start condition first holds, which is decided later
     * by rcp_server_endpoint_select_due() against the caller's tick.
     *
     * Issue #618: effective_tv/avtp_timestamp/gptp_reference_now are the
     * frame-level timing decision rcp_ingress_resolve_timing() made once
     * for the whole AVTPDU (TC18-12.9.1.1-003), so every member of a
     * TSCF frame shares one presentation time, whichever entry point the
     * frame came through. */
    /* issue #463, REQ-SRV-016: _with_ack() (not the plain wrapper) so a
     * successfully-stored request's own evt[3] "if requested" acknowledge
     * (TC18 §12.9.5) is actually built -- see finish_admission()'s own doc
     * comment for how ack reaches the member's response. */
    admit = rcp_server_endpoint_admit_with_ack(&slot->queue, request, request_len, 0u, effective_tv,
                                                avtp_timestamp, gptp_reference_now, &request_type,
                                                &admitted_index, &error, &ack);

    /* REQ-WIREERR-006 (issue #163): "In case the time synchronization
     * hasn't been established, timed requests... shall be rejected and
     * an error response shall be sent (error code = GPTP_FAIL)." --
     * unlike PRESENTATION_TIME_TOO_FAR (implementation-defined, "may
     * reject"), this is a spec-mandated "shall", and time_sync_supported
     * is already this function's own real, caller-supplied "has gPTP
     * been established" signal (REQ-AVTP-021's own TSCF rule 1 already
     * treats it that way) -- no new state or configuration concept is
     * needed to detect this condition for real, unlike PRESENTATION_
     * TIME_TOO_FAR's own currently-unconfigurable admission horizon
     * (see rcp_timed_wire_error()'s own doc comment, request_timed.h).
     * A rejected Timed request is cancelled out of the store exactly
     * like an unauthorized sequencer-owned Compound/Compound Wait step
     * just above -- admitted, then immediately un-admitted, since
     * request_type is only known after rcp_server_endpoint_admit() has
     * already run. */
    if (admit == RCP_SERVER_ADMIT_PENDING && request_type == RCP_REQUEST_TYPE_TIMED &&
        !time_sync_supported) {
        uint8_t tn = slot->queue.pending[admitted_index].transaction_num;

        (void)rcp_server_endpoint_cancel_single(&slot->queue, tn, RCP_CANCEL_LIFECYCLE_QUEUED);
        /* issue #463: admit_with_ack() above may already have built a
         * genuine Acknowledge into ack (this request's own evt[3] may
         * have asked for one) before this gate decided to cancel it
         * again right here -- freed, not transferred, since the member's response
         * is about to carry the Error Response this rejection actually
         * gets instead. Same leak guard as the sequencer-access-control
         * gate just below. */
        rcp_bytes_free(&ack);
        rx_out_push(srv, o, rcp_acf_build_error_response(
            byte_bus_id, tn, rcp_timed_wire_error(RCP_TIMED_REJECT_GPTP_FAIL)));
        return RCP_MOCK_DISPATCH_REJECTED;
    }

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
        /* REQ-WIREERR-005 (issue #163): three-way, not the collapsed
         * bool -- an unknown sequencer_index now reports
         * RCP_ERROR_SEQUENCER_NOT_KNOWN, distinct from a real,
         * configured-but-not-owned sequencer's RCP_ERROR_UNAUTHORIZED_
         * ACCESS. See rcp_sequencer_access_check()'s own doc comment
         * (request_sequencer.h). */
        rcp_sequencer_access_errc_t seq_access =
            rcp_sequencer_access_check(&srv->sequencers, sequencer_index, requester);

        if (seq_access != RCP_SEQUENCER_ACCESS_OK) {
            uint8_t tn = slot->queue.pending[admitted_index].transaction_num;

            (void)rcp_server_endpoint_cancel_single(&slot->queue, tn, RCP_CANCEL_LIFECYCLE_QUEUED);
            /* issue #463: admit_with_ack() above may already have built a
             * genuine Acknowledge into ack (this request's own evt[3] may
             * have asked for one) before this access-control gate decided
             * to cancel it again right here -- freed, not transferred,
             * since the member's response is about to carry the Error Response
             * this rejection actually gets instead. */
            rcp_bytes_free(&ack);
            rx_out_push(srv, o, rcp_acf_build_error_response(byte_bus_id, tn,
                                                             rcp_sequencer_wire_error(seq_access)));
            return RCP_MOCK_DISPATCH_REJECTED;
        }
    }

    return finish_admission(srv, slot, admit, request_type, request, request_len, error,
                             byte_bus_id, &ack, o);
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

/* ── Member filters and gates (step 4) ─────────────────────────────────────── */

/* REQ-INGRESS-009: the per-member lifecycle filter, run first for every
 * member on every path. Side-effect free apart from the REJECT response
 * REQ-LIFECYCLE-033 requires (TC18 §12.7's EP0-scoped REQUEST_REJECTED);
 * DROP produces nothing at all (TC18-12.3.1.1-003, TC18-12.7-003). The
 * effective subtype is the frame-level one, so an AS_NTSCF substitution
 * (TC18-13.3-003) is seen here exactly as if the frame had been NTSCF. */
//cfusa:req REQ-INGRESS-009
//cfusa:req REQ-INGRESS-033
//cfusa:req REQ-LIFECYCLE-033
static rcp_lifecycle_accept_t rx_lifecycle_filter(rcp_mock_server_t *srv, const rx_frame_t *fr,
                                                  uint8_t acf_msg_type,
                                                  rcp_byte_bus_id_t byte_bus_id,
                                                  const uint8_t *member, size_t member_len,
                                                  rx_out_t *o)
{
    uint8_t                tn = 0;
    rcp_lifecycle_accept_t accept =
        rcp_lifecycle_should_accept(srv->state, fr->time_sync_supported, fr->eff_subtype,
                                    acf_msg_type, byte_bus_id,
                                    srv->tscf_unsupported_time_sync_policy);

    if (accept == RCP_LIFECYCLE_DROP) {
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_LIFECYCLE_DROP);
    } else if (accept == RCP_LIFECYCLE_REJECT) {
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_LIFECYCLE_REJECT);
        if (member_transaction_num(member, member_len, &tn)) {
            rx_out_push(srv, o,
                        rcp_acf_build_error_response(byte_bus_id, tn, RCP_ERROR_REQUEST_REJECTED));
        }
    }
    return accept;
}

/* REQ-E2E-021 / REQ-INGRESS-029: the whole-stream fault block (TC18
 * §12.7.7, "stream is blocked until released"), lifted out of the old
 * E2E entry points. It now runs per member, after the lifecycle filter
 * (so a member that must be dropped never gets a POCI_FAILURE response),
 * and for plain-command-mode endpoints too -- the block is a property of
 * the stream, not of the addressed endpoint's req_crc_enable. Returns
 * true iff the member is blocked. The single place #619 will switch to
 * a Table 24-driven source. */
//cfusa:req REQ-E2E-021
//cfusa:req REQ-INGRESS-029
static bool rx_stream_fault_gate(rcp_mock_server_t *srv, uint64_t stream_id,
                                 rcp_byte_bus_id_t byte_bus_id, const uint8_t *member,
                                 size_t member_len, rx_out_t *o)
{
    uint8_t tn = 0;

    if (srv->stream_fault_tracker == NULL ||
        !rcp_e2e_stream_fault_tracker_is_faulted(srv->stream_fault_tracker, stream_id)) {
        return false;
    }
    rx_out_set_reason(o, RCP_MOCK_RX_REASON_STREAM_FAULTED);
    if (member_transaction_num(member, member_len, &tn)) {
        rx_out_push(srv, o, rcp_acf_build_error_response(byte_bus_id, tn, RCP_ERROR_POCI_FAILURE));
    }
    return true;
}

/* REQ-INGRESS-013 (TC18-11.2.1-005): a request whose Table 4 fixed fields
 * are wrong is rejected before lookup with RCP_ERROR_INVALID_PARAMETER --
 * the same code and response shape REQ-ACF-050's rsp check uses at
 * admission (admission_reject_response()). A request too short to carry
 * a header is left to the later stages' own short-frame handling. The
 * general error-response policy for this case is tracked by #623.
 * Returns true iff the member may continue. */
//cfusa:req REQ-INGRESS-013
static bool rx_fixed_fields_stage(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                  const uint8_t *member, size_t member_len, rx_out_t *o)
{
    if (member_len < 8u) return true;
    if (rcp_ingress_request_fixed_fields_valid(member,
                                               rcp_ingress_cs_has_meaning(member, member_len))) {
        return true;
    }
    rx_out_set_reason(o, RCP_MOCK_RX_REASON_FIXED_FIELDS);
    rx_out_push(srv, o, admission_reject_response(byte_bus_id, member, member_len,
                                                  RCP_ERROR_INVALID_PARAMETER));
    return false;
}

/* Whether the addressed endpoint is in safe command mode (TC18 §13.6).
 * The one place that decides it, so #619 (Table 24/EP_ID_config as the
 * source) and #631 (EP0 enforcement) each change a single line. */
static bool rx_crc_required(const rcp_mock_endpoint_slot_t *slot)
{
    return slot->req_crc_enable;
}

/* REQ-E2E-021/045/046: the consequences of a CRC mismatch on stream_id --
 * the fault-tracker latch, the stream_status[] CRC cause and the
 * safe-state broadcast -- all keyed to the addressed endpoint's own
 * rx_enforce_e2e stand-in. One copy for every path (single-frame and
 * reassembled). */
//cfusa:req REQ-E2E-045
//cfusa:req REQ-E2E-046
static void rx_crc_fault_latch(rcp_mock_server_t *srv, uint64_t stream_id,
                               const rcp_mock_endpoint_slot_t *slot)
{
    uint8_t stream_index;

    if (srv->stream_fault_tracker != NULL) {
        (void)rcp_e2e_stream_fault_tracker_on_crc_error(srv->stream_fault_tracker, stream_id,
                                                          slot->rx_enforce_e2e);
    }
    stream_index = rcp_regmap_request_stream_cfg_resolve_index(
        srv->request_stream_cfg, srv->request_stream_cfg_count, stream_id);
    if (stream_index == 0u) return;
    (void)rcp_e2e_stream_status_note_crc_error(&srv->stream_status[stream_index - 1u],
                                                slot->rx_enforce_e2e);
    if (rcp_e2e_crc_error_should_enter_safe_state(slot->rx_enforce_e2e)) {
        (void)rcp_mock_server_broadcast_safe_state(srv, stream_index);
    }
}

/* ── Reassembly pool (step 6) ──────────────────────────────────────────────── */

/* Finds the context bound to (stream_id, byte_bus_id), or NULL. */
static rcp_mock_reasm_ctx_t *reasm_find(rcp_mock_server_t *srv, uint64_t stream_id,
                                        rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_CONCURRENT_REASSEMBLIES; i++) {
        rcp_mock_reasm_ctx_t *c = &srv->reasm[i];
        if (c->in_use && c->stream_id == stream_id && c->byte_bus_id == byte_bus_id) return c;
    }
    return NULL;
}

/* Binds a free context to (stream_id, byte_bus_id). NULL when every
 * context is in use (REQ-INGRESS-016: the caller rejects the member). */
//cfusa:req REQ-INGRESS-016
static rcp_mock_reasm_ctx_t *reasm_claim(rcp_mock_server_t *srv, uint64_t stream_id,
                                         rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_CONCURRENT_REASSEMBLIES; i++) {
        rcp_mock_reasm_ctx_t *c = &srv->reasm[i];
        if (!c->in_use) {
            c->in_use      = true;
            c->pinned      = false;
            c->tn_valid    = false;
            c->stream_id   = stream_id;
            c->byte_bus_id = byte_bus_id;
            return c;
        }
    }
    return NULL;
}

/* Ends whatever c was collecting. An unpinned context is unbound and its
 * bound restored to the default; a pinned one (rcp_mock_server_fragment_
 * reassembler_for()) stays bound with the caller's own bound. */
static void reasm_release(rcp_mock_reasm_ctx_t *c)
{
    c->tn_valid = false;
    if (c->pinned) {
        rcp_fragment_reassembler_reset(&c->reasm);
        return;
    }
    rcp_fragment_reassembler_destroy(&c->reasm);
    rcp_fragment_reassembler_init(&c->reasm, RCP_MOCK_FRAG_REASM_DEFAULT_MAX_TOTAL_LEN);
    c->in_use = false;
}

/* REQ-INGRESS-017: discards every half-collected message matching the
 * filter (match_stream/match_bus false means "any"), so no stale
 * fragment survives a lifecycle transition, a safe-state broadcast or a
 * watchdog purge. */
//cfusa:req REQ-INGRESS-017
static void reasm_purge(rcp_mock_server_t *srv, bool match_stream, uint64_t stream_id,
                        bool match_bus, rcp_byte_bus_id_t byte_bus_id)
{
    size_t i;
    for (i = 0; i < RCP_MOCK_MAX_CONCURRENT_REASSEMBLIES; i++) {
        rcp_mock_reasm_ctx_t *c = &srv->reasm[i];
        if (!c->in_use) continue;
        if (match_stream && c->stream_id != stream_id) continue;
        if (match_bus && c->byte_bus_id != byte_bus_id) continue;
        reasm_release(c);
    }
}

typedef enum {
    RX_REASM_PASS,     /* not part of a fragmented message: use as-is */
    RX_REASM_PENDING,  /* fragment accepted, message not complete yet */
    RX_REASM_REJECTED, /* fragment refused; context reset */
    RX_REASM_COMPLETE, /* final fragment completed a reassembly */
} rx_reasm_t;

/* The message the CRC and admission stages work on. For RX_REASM_PASS it
 * is the member itself; for RX_REASM_COMPLETE, ctx holds the reassembled
 * payload, got_crc the trailer read from the final fragment (safe command
 * mode only) and final_* the final fragment's own decoded header, from
 * which the complete request is rebuilt once the CRC has passed. owned is
 * any buffer this struct owns (freed by the caller). */
typedef struct {
    const uint8_t               *data;
    size_t                       len;
    rcp_bytes_t                  owned;
    rcp_mock_reasm_ctx_t        *ctx;
    uint32_t                     got_crc;
    uint8_t                      msg_type;
    rcp_acf_byte_message_info_t  final_abb;
    rcp_acf_gbb_header_t         final_gbb;
} rx_msg_t;

/* Decodes member as msg_type and returns its payload slice; also its
 * decoded header for the final-fragment rebuild. */
static bool rx_decode_member(const uint8_t *member, size_t member_len, uint8_t msg_type,
                             rx_msg_t *m, const uint8_t **out_payload, size_t *out_payload_len)
{
    if (msg_type == RCP_ACF_MSG_TYPE_GBB) {
        return rcp_acf_decode_gbb(member, member_len, &m->final_gbb, out_payload,
                                  out_payload_len) == RCP_ACF_OK;
    }
    return rcp_acf_decode_abb(member, member_len, &m->final_abb, out_payload, out_payload_len) ==
           RCP_ACF_OK;
}

/* Intermediate (ms = 1) fragment: no CRC trailer (REQ-E2E-039), fed
 * straight into the (stream_id, byte_bus_id) context. The first fragment
 * of a sequence claims a context and records everything the fragmented
 * CRC later needs from the FIRST AVTPDU (REQ-E2E-038): its raw ACF
 * header, subtype, header_octet1, tu and avtp_timestamp. A different
 * transaction_num mid-sequence resets the context and rejects the member
 * (REQ-INGRESS-015, fail closed). */
//cfusa:req REQ-INGRESS-014
//cfusa:req REQ-INGRESS-015
//cfusa:req REQ-E2E-039
static rx_reasm_t rx_reasm_intermediate(rcp_mock_server_t *srv, const rx_frame_t *fr,
                                        rcp_byte_bus_id_t byte_bus_id, rcp_mock_reasm_ctx_t *ctx,
                                        const rcp_acf_byte_message_info_t *peek,
                                        const uint8_t *member, size_t member_len, rx_msg_t *m,
                                        rx_out_t *o)
{
    const uint8_t *payload     = NULL;
    size_t         payload_len = 0;
    size_t         header_len  = (m->msg_type == RCP_ACF_MSG_TYPE_GBB) ? RCP_ACF_GBB_HEADER_LEN
                                                                       : RCP_ACF_ABB_HEADER_LEN;

    if (!rx_decode_member(member, member_len, m->msg_type, m, &payload, &payload_len)) {
        if (ctx) reasm_release(ctx);
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_FRAGMENT_SEQUENCE);
        return RX_REASM_REJECTED;
    }
    if (ctx && ctx->tn_valid && ctx->transaction_num != peek->transaction_num) {
        reasm_release(ctx);
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_FRAGMENT_TN_MISMATCH);
        return RX_REASM_REJECTED;
    }
    if (!ctx) ctx = reasm_claim(srv, fr->f.stream_id, byte_bus_id);
    if (!ctx) {
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_FRAGMENT_POOL_EXHAUSTED);
        return RX_REASM_REJECTED;
    }
    if (!ctx->tn_valid) {
        rcp_memcpy_bounded(ctx->first_hdr, sizeof(ctx->first_hdr), member, header_len);
        ctx->first_hdr_len   = header_len;
        ctx->subtype         = fr->f.subtype;
        ctx->header_octet1   = fr->f.header_octet1;
        ctx->tu              = fr->f.tu != 0u;
        ctx->avtp_timestamp  = fr->f.avtp_timestamp;
        ctx->transaction_num = peek->transaction_num;
        ctx->tn_valid        = true;
    }
    if (rcp_fragment_reassembler_feed(&ctx->reasm, true, peek->read_size_or_segment_num, payload,
                                      payload_len) != RCP_FRAGMENT_REASM_CONTINUE) {
        reasm_release(ctx);
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_FRAGMENT_SEQUENCE);
        return RX_REASM_REJECTED;
    }
    return RX_REASM_PENDING;
}

/* Final (ms = 0) fragment of a sequence that is being collected. In safe
 * command mode it carries the CRC32 right after its real (unpadded)
 * payload (issue #445), so the trailer is read and stripped first; the
 * fragmented-CRC verdict itself is rx_crc_stage()'s job. */
//cfusa:req REQ-INGRESS-014
//cfusa:req REQ-E2E-039
static rx_reasm_t rx_reasm_final(const rx_frame_t *fr, const rcp_mock_endpoint_slot_t *slot,
                                 rcp_mock_reasm_ctx_t *ctx,
                                 const rcp_acf_byte_message_info_t *peek, const uint8_t *member,
                                 size_t member_len, rx_msg_t *m, rx_out_t *o)
{
    const uint8_t *payload     = NULL;
    size_t         payload_len = 0;
    rcp_bytes_t    unwrapped   = {0};
    const uint8_t *body        = member;
    size_t         body_len    = member_len;
    bool           ok;

    if (ctx->transaction_num != peek->transaction_num) {
        reasm_release(ctx);
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_FRAGMENT_TN_MISMATCH);
        return RX_REASM_REJECTED;
    }

    if (rx_crc_required(slot)) {
        /* member_len >= 8 here (rx_reassemble_stage()), and pad <= 3, so
         * the trailer always fits: real_len >= 1. */
        size_t real_len = member_len - RCP_E2E_CRC_LEN - (size_t)peek->pad;

        m->got_crc = ((uint32_t)member[real_len] << 24) |
                     ((uint32_t)member[real_len + 1] << 16) |
                     ((uint32_t)member[real_len + 2] << 8) | (uint32_t)member[real_len + 3];
        /* Used only to excise the trailer and adapt acf_msg_length back
         * down; its single-frame verdict is not this message's CRC
         * (REQ-E2E-038) and is ignored. A failed unwrap leaves unwrapped
         * zeroed, which the decode just below then refuses. */
        (void)rcp_e2e_unwrap_framed(fr->f.stream_id, fr->f.subtype == RCP_AVTP_SUBTYPE_NTSCF,
                                    fr->f.header_octet1, fr->f.tu != 0u, fr->f.avtp_timestamp,
                                    member, member_len, &unwrapped);
        body     = unwrapped.data;
        body_len = unwrapped.len;
    }

    ok = rx_decode_member(body, body_len, m->msg_type, m, &payload, &payload_len);
    if (ok) {
        ok = rcp_fragment_reassembler_feed(&ctx->reasm, false, 0u, payload, payload_len) ==
             RCP_FRAGMENT_REASM_COMPLETE;
    }
    rcp_bytes_free(&unwrapped);
    if (!ok) {
        reasm_release(ctx);
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_FRAGMENT_SEQUENCE);
        return RX_REASM_REJECTED;
    }
    m->ctx = ctx;
    return RX_REASM_COMPLETE;
}

/* Step 6 (REQ-INGRESS-014, TC18-12.8.2-003): reassembly on every path, for
 * plain and safe-command-mode endpoints alike, keyed per (stream_id,
 * byte_bus_id) so interleaved fragmented requests to two endpoints on
 * one stream never collide. Runs before, and separately from, the CRC
 * decision. A member that is not a fragment (ms = 0 with nothing being
 * collected for its key) passes through untouched. */
//cfusa:req REQ-INGRESS-014
static rx_reasm_t rx_reassemble_stage(rcp_mock_server_t *srv, const rx_frame_t *fr,
                                      const rcp_mock_endpoint_slot_t *slot,
                                      rcp_byte_bus_id_t byte_bus_id, const uint8_t *member,
                                      size_t member_len, rx_msg_t *m, rx_out_t *o)
{
    rcp_acf_byte_message_info_t peek = {0};
    rcp_mock_reasm_ctx_t       *ctx;
    bool                        collecting;

    memset(m, 0, sizeof(*m));
    m->data = member;
    m->len  = member_len;
    if (member_len < 8u) return RX_REASM_PASS;
    (void)rcp_acf_unpack_header(member, &peek); /* cannot fail on 8 octets */
    m->msg_type = peek.acf_msg_type;

    ctx        = reasm_find(srv, fr->f.stream_id, byte_bus_id);
    collecting = ctx != NULL && ctx->tn_valid;
    if (peek.ms) {
        return rx_reasm_intermediate(srv, fr, byte_bus_id, ctx, &peek, member, member_len, m, o);
    }
    if (!collecting) return RX_REASM_PASS;
    return rx_reasm_final(fr, slot, ctx, &peek, member, member_len, m, o);
}

/* ── CRC stage (step 7) ────────────────────────────────────────────────────── */

/* REQ-INGRESS-018 (TC18-13.6-001/-002/-004/-005): safe-command-mode CRC
 * over the REAL received header -- the frame's own subtype, raw
 * header_octet1, tu and avtp_timestamp -- never a placeholder. A
 * reassembled message is checked with rcp_e2e_compute_fragmented_crc()
 * against the FIRST fragment's stored header values (REQ-E2E-038); NTSCF
 * framing contributes a zero timestamp and tu, as rcp_e2e_wrap_framed()
 * does. On a mismatch the request is not executed, a CRC_ERROR
 * (POCI_FAILURE) response is built when a transaction_num can be read,
 * and rx_crc_fault_latch() applies the configured safety measures. On a
 * single-frame success m is redirected to the unwrapped copy. The single
 * CRC call site #619/#631 will change. */
//cfusa:req REQ-INGRESS-018
//cfusa:req REQ-INGRESS-034
//cfusa:req REQ-E2E-031
//cfusa:req REQ-E2E-038
//cfusa:req REQ-E2E-041
static rcp_mock_dispatch_result_t rx_crc_stage(rcp_mock_server_t *srv, const rx_frame_t *fr,
                                               const rcp_mock_endpoint_slot_t *slot, rx_msg_t *m,
                                               rcp_byte_bus_id_t byte_bus_id, rx_out_t *o)
{
    uint8_t tn = 0;

    if (!rx_crc_required(slot)) return RCP_MOCK_DISPATCH_OK;

    if (m->ctx != NULL) {
        const rcp_mock_reasm_ctx_t *c = m->ctx;
        bool                        ntscf = c->subtype == RCP_AVTP_SUBTYPE_NTSCF;
        const uint8_t              *reassembled;
        size_t                      reassembled_len;
        uint32_t                    want;

        rcp_fragment_reassembler_get(&c->reasm, &reassembled, &reassembled_len);
        want = rcp_e2e_compute_fragmented_crc(c->subtype, c->header_octet1, ntscf ? false : c->tu,
                                              fr->f.stream_id, ntscf ? 0u : c->avtp_timestamp,
                                              c->first_hdr, c->first_hdr_len, reassembled,
                                              reassembled_len);
        if (want == m->got_crc) return RCP_MOCK_DISPATCH_OK;
        tn = (uint8_t)((m->msg_type == RCP_ACF_MSG_TYPE_GBB) ? m->final_gbb.info.transaction_num
                                                             : m->final_abb.transaction_num);
        rx_out_push(srv, o, rcp_acf_build_error_response(byte_bus_id, tn, RCP_ERROR_POCI_FAILURE));
    } else {
        rcp_bytes_t    unwrapped = {0};
        rcp_e2e_errc_t rc = rcp_e2e_unwrap_framed(
            fr->f.stream_id, fr->f.subtype == RCP_AVTP_SUBTYPE_NTSCF, fr->f.header_octet1,
            fr->f.tu != 0u, fr->f.avtp_timestamp, m->data, m->len, &unwrapped);
        rcp_wire_error_t werr;

        if (rc == RCP_E2E_OK) {
            m->owned = unwrapped;
            m->data  = unwrapped.data;
            m->len   = unwrapped.len;
            return RCP_MOCK_DISPATCH_OK;
        }
        /* RCP_E2E_ERR_CRC_MISMATCH still populates unwrapped (diagnostic
         * copy); RCP_E2E_ERR_SHORT_FRAME leaves it zeroed -- nothing
         * conformant to answer then (RCP_ERROR_NONE). */
        rcp_bytes_free(&unwrapped);
        werr = rcp_e2e_wire_error(rc);
        if (werr != RCP_ERROR_NONE && member_transaction_num(m->data, m->len, &tn)) {
            rx_out_push(srv, o, rcp_acf_build_error_response(byte_bus_id, tn, werr));
        }
    }
    rx_out_set_reason(o, RCP_MOCK_RX_REASON_CRC);
    rx_crc_fault_latch(srv, fr->f.stream_id, slot);
    return RCP_MOCK_DISPATCH_CRC_ERROR;
}

/* Rebuilds a complete ACF message from a successful reassembly: the
 * final fragment's own header + the full reassembled payload, then
 * releases the context. Issue #614: a reassembled request too large to
 * re-express as one ACF message (acf_msg_length's 9-bit width) is
 * rejected with a real REQUEST_REJECTED response, not silence. */
//cfusa:req REQ-INGRESS-014
static bool rx_rebuild_reassembled(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                   rx_msg_t *m, rx_out_t *o)
{
    const uint8_t *reassembled;
    size_t         reassembled_len;
    uint8_t        tn;

    rcp_fragment_reassembler_get(&m->ctx->reasm, &reassembled, &reassembled_len);
    if (m->msg_type == RCP_ACF_MSG_TYPE_GBB) {
        m->owned = rcp_acf_encode_gbb(&m->final_gbb, reassembled, reassembled_len);
        tn       = m->final_gbb.info.transaction_num;
    } else {
        m->owned = rcp_acf_encode_abb(&m->final_abb, reassembled, reassembled_len);
        tn       = m->final_abb.transaction_num;
    }
    reasm_release(m->ctx);
    m->ctx = NULL;
    if (m->owned.data == NULL) {
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_REASSEMBLED_TOO_LARGE);
        rx_out_push(srv, o, rcp_acf_build_error_response(byte_bus_id, tn,
                                                         RCP_ERROR_REQUEST_REJECTED));
        return false;
    }
    m->data = m->owned.data;
    m->len  = m->owned.len;
    return true;
}

/* Everything after the filters and gates for one member: fixed fields,
 * lookup, reassembly, CRC, admission. */
//cfusa:req REQ-MOCK-030
static rcp_mock_dispatch_result_t rx_member_process(rcp_mock_server_t *srv, const rx_frame_t *fr,
                                                    const uint8_t *member, size_t member_len,
                                                    rcp_byte_bus_id_t byte_bus_id, rx_out_t *o)
{
    rcp_mock_endpoint_slot_t  *slot;
    rx_msg_t                   m;
    rx_reasm_t                 rs;
    rcp_mock_dispatch_result_t result;

    if (!rx_fixed_fields_stage(srv, byte_bus_id, member, member_len, o)) {
        return RCP_MOCK_DISPATCH_REJECTED;
    }

    /* REQ-MOCK-031 (issue #432) / TC18 §12.9.1: stream-scoped lookup; an
     * unknown endpoint is dropped without further notification
     * (REQ-MOCK-030). The unknown-stream policy is #621's. */
    slot = find_slot_on_stream(srv, fr->f.stream_id, byte_bus_id);
    if (!slot) {
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_UNKNOWN_ENDPOINT);
        return RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS;
    }

    rs = rx_reassemble_stage(srv, fr, slot, byte_bus_id, member, member_len, &m, o);
    if (rs == RX_REASM_PENDING) return RCP_MOCK_DISPATCH_FRAGMENT_PENDING;
    if (rs == RX_REASM_REJECTED) return RCP_MOCK_DISPATCH_REJECTED;

    if (rx_crc_stage(srv, fr, slot, &m, byte_bus_id, o) != RCP_MOCK_DISPATCH_OK) {
        if (m.ctx) reasm_release(m.ctx);
        rcp_bytes_free(&m.owned);
        return RCP_MOCK_DISPATCH_CRC_ERROR;
    }
    if (m.ctx != NULL) {
        rcp_bytes_free(&m.owned);
        if (!rx_rebuild_reassembled(srv, byte_bus_id, &m, o)) return RCP_MOCK_DISPATCH_REJECTED;
    }

    result = rx_admit_member(srv, fr, slot, m.data, m.len, byte_bus_id, o);
    rcp_bytes_free(&m.owned);
    return result;
}

/* Frame-level stages (b) and (c): the once-per-AVTPDU timing decision
 * (REQ-INGRESS-003/006) and the subtype-only lifecycle filter
 * (REQ-INGRESS-007). False means the whole AVTPDU is dropped silently,
 * with no side effects. */
//cfusa:req REQ-INGRESS-006
//cfusa:req REQ-INGRESS-032
static bool rx_frame_admit_timing(rcp_mock_server_t *srv, rx_frame_t *fr,
                                  rcp_mock_rx_summary_t *sum)
{
    rcp_ingress_timing_t t = rcp_ingress_resolve_timing(
        &fr->f, fr->time_sync_supported, srv->tscf_unsupported_time_sync_policy,
        srv->tscf_reserved_zero_policy, &fr->eff_subtype, &fr->eff_tv);

    if (sum) sum->timing = t;
    if (t == RCP_INGRESS_TIMING_DROP) {
        if (sum) sum->outcome = RCP_MOCK_RX_FRAME_TIMING_DROP;
        return false;
    }
    if (rcp_lifecycle_should_accept_subtype(srv->state, fr->time_sync_supported, fr->eff_subtype,
                                            srv->tscf_unsupported_time_sync_policy) ==
        RCP_LIFECYCLE_DROP) {
        if (sum) sum->outcome = RCP_MOCK_RX_FRAME_LIFECYCLE_DROP;
        return false;
    }
    return true;
}

/* Builds the synthetic frame a legacy wrapper enters the pipeline with
 * (step 9): header_octet1 is 0x80 | tv -- sv = 1, version 0, mr = 0,
 * exactly what rcp_avtp_encode_ntscf()/_tscf() emit -- and tu is 0. A TSCF
 * wrapper with no reserved-bytes argument passes reserved_all_zero true,
 * the value every IEEE 1722-conformant sender emits, so all four TSCF
 * wrappers agree under every reserved-zero policy. */
static void rx_frame_legacy(rx_frame_t *fr, uint8_t avtp_subtype, uint64_t stream_id, bool tv,
                            uint32_t avtp_timestamp, bool reserved_all_zero,
                            bool time_sync_supported, uint64_t gptp_reference_now)
{
    bool tscf = avtp_subtype == RCP_AVTP_SUBTYPE_TSCF;

    memset(fr, 0, sizeof(*fr));
    fr->f.subtype                = avtp_subtype;
    fr->f.sv                     = 1u;
    fr->f.tv                     = (uint8_t)((tscf && tv) ? 1u : 0u);
    fr->f.header_octet1          = (uint8_t)(0x80u | fr->f.tv);
    fr->f.stream_id              = stream_id;
    fr->f.avtp_timestamp         = tscf ? avtp_timestamp : 0u;
    fr->f.tscf_reserved_all_zero = tscf && reserved_all_zero;
    fr->time_sync_supported      = time_sync_supported;
    fr->gptp_reference_now       = gptp_reference_now;
}

/* Step 9: the shared body of every single-request legacy wrapper. Enters
 * at stage (b); no sequence number, so no sequence gate (as before).
 * After the lifecycle DROP decision, an unpackable request whose header
 * disagrees with the caller-supplied byte_bus_id/acf_msg_type -- or a
 * TSCF frame handed to an untimed wrapper -- is refused with
 * RCP_MOCK_DISPATCH_ERR_HEADER_MISMATCH instead of being trusted
 * (REQ-INGRESS-022/023). A request too short to unpack keeps the
 * caller-argument path. */
//cfusa:req REQ-INGRESS-022
//cfusa:req REQ-INGRESS-023
static rcp_mock_dispatch_result_t rx_single(rcp_mock_server_t *srv, rx_frame_t *fr,
                                            rcp_byte_bus_id_t byte_bus_id, uint8_t acf_msg_type,
                                            bool untimed, const uint8_t *request,
                                            size_t request_len, rx_out_t *o)
{
    rcp_acf_byte_message_info_t hdr = {0};

    /* REQ-WDG-010: receipt, not validation -- kicked before any check. */
    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, fr->f.stream_id);

    if (!rx_frame_admit_timing(srv, fr, NULL)) return RCP_MOCK_DISPATCH_DROPPED;
    if (rcp_lifecycle_should_accept(srv->state, fr->time_sync_supported, fr->eff_subtype,
                                    acf_msg_type, byte_bus_id,
                                    srv->tscf_unsupported_time_sync_policy) ==
        RCP_LIFECYCLE_DROP) {
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_LIFECYCLE_DROP);
        return RCP_MOCK_DISPATCH_DROPPED;
    }
    if (request_len >= 8u) (void)rcp_acf_unpack_header(request, &hdr); /* cannot fail */
    if ((request_len >= 8u &&
         (hdr.byte_bus_id != byte_bus_id || hdr.acf_msg_type != acf_msg_type)) ||
        (untimed && fr->f.subtype == RCP_AVTP_SUBTYPE_TSCF)) {
        rx_out_set_reason(o, RCP_MOCK_RX_REASON_HEADER_MISMATCH);
        return RCP_MOCK_DISPATCH_ERR_HEADER_MISMATCH;
    }
    if (rx_lifecycle_filter(srv, fr, acf_msg_type, byte_bus_id, request, request_len, o) !=
        RCP_LIFECYCLE_ACCEPT) {
        return RCP_MOCK_DISPATCH_REJECTED;
    }
    if (rx_stream_fault_gate(srv, fr->f.stream_id, byte_bus_id, request, request_len, o)) {
        return RCP_MOCK_DISPATCH_STREAM_FAULTED;
    }
    return rx_member_process(srv, fr, request, request_len, byte_bus_id, o);
}

/* Applies the per-stream response routing (REQ-RMAP-048/049) to every
 * collected response, in place. */
static void rx_out_suppress(rcp_mock_server_t *srv, uint64_t stream_id, rx_out_t *o)
{
    size_t i;
    for (i = 0; i < o->count; i++) suppress_response_per_stream_cfg(srv, stream_id, &o->out[i]);
}

/* Legacy single-response output: suppression, then join. */
static void rx_finish_single(rcp_mock_server_t *srv, uint64_t stream_id, rx_out_t *o,
                             rcp_bytes_t *out_response)
{
    rx_out_suppress(srv, stream_id, o);
    rx_join_responses(o->out, o->count, out_response);
    o->count = 0;
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
         * as a chain member here; admission will reject
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

/* ── Frame composition (step 5) ────────────────────────────────────────────── */

/* Chain sequencing state carried across one frame's members in order --
 * see request_chained.h's rcp_chained_advance(). REQ-CANCEL-012 (issue
 * #334): chain_group == 0 is the "not part of a chain" sentinel; every
 * non-chained member starts a fresh chain_group == index + 1, a chained
 * one keeps its predecessor's group and advances chain_position. */
typedef struct {
    bool     chain_aborted;
    bool     prev_errored;
    uint8_t  cs;
    uint32_t chain_group;
    size_t   chain_position;
} rx_chain_t;

/* One member of a frame through every per-member stage, plus the chain
 * bookkeeping lifted unchanged from the old dispatch_frame(): a chained
 * member's execution condition is its predecessor within this same
 * frame, so the CHAIN_ERROR/CHAIN_ABORTED decision is made here, where
 * frame order is known (REQ-MOCK-029). The lifecycle filter and the
 * stream-fault gate run first, so a member that must be dropped never
 * gets a chain error response either. */
//cfusa:req REQ-MOCK-019
//cfusa:req REQ-MOCK-020
//cfusa:req REQ-MOCK-029
//cfusa:req REQ-E2E-033
static rcp_mock_dispatch_result_t rx_frame_member(rcp_mock_server_t *srv, const rx_frame_t *fr,
                                                  rx_chain_t *ch, size_t index,
                                                  const uint8_t *member, size_t member_len,
                                                  uint8_t acf_msg_type,
                                                  rcp_byte_bus_id_t byte_bus_id, rx_out_t *o)
{
    rcp_mock_dispatch_result_t result;
    rcp_lifecycle_accept_t     accept;
    uint8_t                    member_tn = 0;
    bool                       chained   = is_chained_member(member, member_len, &ch->cs, &member_tn);

    if (!chained) {
        ch->chain_group    = (uint32_t)index + 1u;
        ch->chain_position = 0;
    } else {
        ch->chain_position++;
    }

    accept = rx_lifecycle_filter(srv, fr, acf_msg_type, byte_bus_id, member, member_len, o);
    if (accept == RCP_LIFECYCLE_DROP) {
        result = RCP_MOCK_DISPATCH_DROPPED;
    } else if (accept == RCP_LIFECYCLE_REJECT) {
        result = RCP_MOCK_DISPATCH_REJECTED;
    } else if (rx_stream_fault_gate(srv, fr->f.stream_id, byte_bus_id, member, member_len, o)) {
        result = RCP_MOCK_DISPATCH_STREAM_FAULTED;
    } else {
        if (chained) {
            rcp_chained_member_outcome_t outcome =
                rcp_chained_advance(&ch->chain_aborted, index > 0, ch->prev_errored, ch->cs);

            if (outcome == RCP_CHAINED_MEMBER_CHAIN_ERROR ||
                outcome == RCP_CHAINED_MEMBER_CHAIN_ABORTED) {
                bool err = outcome == RCP_CHAINED_MEMBER_CHAIN_ERROR;

                rx_out_set_reason(o, RCP_MOCK_RX_REASON_CHAIN);
                rx_out_push(srv, o, rcp_acf_build_error_response(
                                        byte_bus_id, member_tn,
                                        err ? RCP_ERROR_CHAIN_ERROR : RCP_ERROR_CHAIN_ABORTED));
                ch->prev_errored = true;
                return err ? RCP_MOCK_DISPATCH_CHAIN_ERROR : RCP_MOCK_DISPATCH_CHAIN_ABORTED;
            }
        }
        result = rx_member_process(srv, fr, member, member_len, byte_bus_id, o);
    }

    /* What "the predecessor errored" means for the next member: one that
     * never reached its endpoint's store at all counts as an error. */
    ch->prev_errored = (result == RCP_MOCK_DISPATCH_ERR_UNKNOWN_BUS ||
                        result == RCP_MOCK_DISPATCH_REJECTED ||
                        result == RCP_MOCK_DISPATCH_DROPPED ||
                        result == RCP_MOCK_DISPATCH_CRC_ERROR ||
                        result == RCP_MOCK_DISPATCH_STREAM_FAULTED);

    /* A chained member accepted into its endpoint's store has its
     * predecessor behind it already, so its chain_exec_delay timer starts
     * now; REQ-CANCEL-012 also tags its chain_group/chain_position. Stream-
     * scoped lookup (REQ-MOCK-031), the same slot admission just used. */
    if (result == RCP_MOCK_DISPATCH_PENDING) {
        rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, fr->f.stream_id, byte_bus_id);
        if (slot) {
            size_t last = last_pending_index(&slot->queue);
            (void)rcp_server_endpoint_chain_predecessor_done(&slot->queue, last, 0u);
            if (last < RCP_SERVER_MAX_PENDING) {
                slot->queue.pending[last].chain_group    = ch->chain_group;
                slot->queue.pending[last].chain_position = (uint8_t)ch->chain_position;
            }
        }
    }
    return result;
}

/* Reads a member's msg type / byte_bus_id / transaction_num. The member
 * iterator has already guaranteed at least a full ABB header. */
static void rx_member_identity(const uint8_t *member, rcp_mock_rx_member_t *m)
{
    rcp_acf_byte_message_info_t hdr = {0};

    (void)rcp_acf_unpack_header(member, &hdr);
    m->acf_msg_type    = hdr.acf_msg_type;
    m->byte_bus_id     = hdr.byte_bus_id;
    m->transaction_num = hdr.transaction_num;
}

/* Hands m to sink (if any), then frees whatever the sink left behind. */
static void rx_emit(rcp_mock_rx_member_t *m, rcp_mock_rx_sink_fn sink, void *user)
{
    size_t i;

    if (sink) sink(m, user);
    for (i = 0; i < m->response_count; i++) rcp_bytes_free(&m->responses[i]);
}

/* Emits every member (up to limit) with one frame-wide result and no
 * responses -- the sequence-gate rejection and the untimed-TSCF header
 * mismatch. */
static size_t rx_emit_all(const rx_frame_t *fr, rcp_mock_dispatch_result_t result,
                          rcp_mock_rx_reason_t reason, size_t limit, rcp_mock_rx_sink_fn sink,
                          void *user)
{
    rcp_ingress_member_iter_t it;
    const uint8_t            *member;
    size_t                    member_len;
    size_t                    n = 0;

    rcp_ingress_member_iter_init(&it, fr->f.payload, fr->f.payload_len);
    while (n < limit && rcp_ingress_member_next(&it, &member, &member_len)) {
        rcp_mock_rx_member_t m;

        memset(&m, 0, sizeof(m));
        rx_member_identity(member, &m);
        m.member_index = n;
        m.result       = result;
        m.reason       = reason;
        rx_emit(&m, sink, user);
        n++;
    }
    return n;
}

/* Stage (d): the allocation-free pre-pass. Counts the members, detects a
 * malformed tail, and asks whether ANY member would get past the
 * per-member lifecycle filter. Pure: no response, no state change. */
static bool rx_frame_prepass(rcp_mock_server_t *srv, const rx_frame_t *fr,
                             rcp_mock_rx_summary_t *sum)
{
    rcp_ingress_member_iter_t it;
    const uint8_t            *member;
    size_t                    member_len;
    bool                      any_passes = false;

    rcp_ingress_member_iter_init(&it, fr->f.payload, fr->f.payload_len);
    while (rcp_ingress_member_next(&it, &member, &member_len)) {
        rcp_mock_rx_member_t m;

        rx_member_identity(member, &m);
        if (rcp_lifecycle_should_accept(srv->state, fr->time_sync_supported, fr->eff_subtype,
                                        m.acf_msg_type, m.byte_bus_id,
                                        srv->tscf_unsupported_time_sync_policy) !=
            RCP_LIFECYCLE_DROP) {
            any_passes = true;
        }
    }
    sum->member_count = it.index;
    if (it.malformed) {
        sum->outcome = RCP_MOCK_RX_FRAME_MALFORMED;
    } else if (it.index == 0u) {
        sum->outcome = RCP_MOCK_RX_FRAME_EMPTY;
    } else if (!any_passes) {
        sum->outcome = RCP_MOCK_RX_FRAME_LIFECYCLE_DROP;
    } else {
        return true;
    }
    return false;
}

/* Stages (b)-(f) for one AVTPDU, in their fixed order (REQ-INGRESS-009):
 * timing and subtype filter, the member pre-pass (a frame none of whose
 * members would pass the per-member filter is dropped before the
 * sequence gate, so it never changes sequence state -- REQ-INGRESS-026),
 * the once-per-AVTPDU sequence gate with the frame's own sequence_num,
 * then every member in order through rx_frame_member(), each handed to
 * sink. limit bounds how many members are processed (the legacy frame
 * wrappers' out_cap); receive_avtpdu() passes no limit. */
//cfusa:req REQ-INGRESS-009
//cfusa:req REQ-INGRESS-026
//cfusa:req REQ-INGRESS-008
static void rx_frame_run(rcp_mock_server_t *srv, rx_frame_t *fr, bool untimed,
                         size_t limit, rcp_mock_rx_sink_fn sink, void *user,
                         rcp_mock_rx_summary_t *sum)
{
    rcp_ingress_member_iter_t it;
    const uint8_t            *member;
    size_t                    member_len;
    rx_chain_t                ch;

    if (!rx_frame_admit_timing(srv, fr, sum)) return;
    if (!rx_frame_prepass(srv, fr, sum)) return;

    if (untimed && fr->f.subtype == RCP_AVTP_SUBTYPE_TSCF) {
        sum->outcome = RCP_MOCK_RX_FRAME_HEADER_MISMATCH;
        sum->members_processed = rx_emit_all(fr, RCP_MOCK_DISPATCH_ERR_HEADER_MISMATCH,
                                             RCP_MOCK_RX_REASON_HEADER_MISMATCH, limit, sink,
                                             user);
        return;
    }
    if (!frame_seq_gate_admits(srv, fr->f.stream_id, fr->f.sequence_num)) {
        sum->outcome = RCP_MOCK_RX_FRAME_SEQ_ERROR;
        sum->members_processed = rx_emit_all(fr, RCP_MOCK_DISPATCH_SEQ_ERROR,
                                             RCP_MOCK_RX_REASON_SEQUENCE, limit, sink, user);
        return;
    }

    memset(&ch, 0, sizeof(ch));
    sum->outcome = RCP_MOCK_RX_FRAME_ACCEPTED;
    rcp_ingress_member_iter_init(&it, fr->f.payload, fr->f.payload_len);
    while (sum->members_processed < limit && rcp_ingress_member_next(&it, &member, &member_len)) {
        rcp_bytes_t          buf[RCP_MOCK_MAX_RESPONSES_PER_EXEC];
        rx_out_t             o;
        rcp_mock_rx_member_t m;

        memset(&m, 0, sizeof(m));
        rx_out_init(&o, buf, RCP_MOCK_MAX_RESPONSES_PER_EXEC);
        rx_member_identity(member, &m);
        m.member_index = sum->members_processed;
        m.result = rx_frame_member(srv, fr, &ch, m.member_index, member, member_len,
                                   m.acf_msg_type, m.byte_bus_id, &o);
        rx_out_suppress(srv, fr->f.stream_id, &o);
        m.reason              = o.reason;
        m.responses_truncated = o.truncated;
        m.responses           = buf;
        m.response_count      = o.count;
        rx_emit(&m, sink, user);
        sum->members_processed++;
    }
}

//cfusa:req REQ-INGRESS-009
//cfusa:req REQ-INGRESS-027
//cfusa:req REQ-WDG-010
rcp_mock_errc_t rcp_mock_server_receive_avtpdu(rcp_mock_server_t *srv, const uint8_t *avtpdu,
                                               size_t len, const rcp_mock_rx_ctx_t *ctx,
                                               rcp_mock_rx_sink_fn sink, void *user,
                                               rcp_mock_rx_summary_t *out_summary)
{
    rx_frame_t            fr;
    rcp_mock_rx_summary_t sum;
    rcp_ingress_errc_t    pe;

    memset(&fr, 0, sizeof(fr));
    memset(&sum, 0, sizeof(sum));

    /* Stage (a). A buffer that is not an AVTPDU is not a received request:
     * silent drop, no side effects, and no watchdog kick. */
    pe = rcp_ingress_parse_avtpdu(avtpdu, len, &fr.f);
    if (pe != RCP_INGRESS_OK) {
        sum.outcome     = RCP_MOCK_RX_FRAME_PARSE_ERROR;
        sum.parse_error = pe;
    } else {
        /* REQ-WDG-010 refinement (REQ-INGRESS-027): kicked once per
         * successfully parsed AVTPDU, before any filter -- receipt, not
         * validation. */
        if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, fr.f.stream_id);
        if (ctx != NULL) {
            fr.time_sync_supported = ctx->time_sync_supported;
            fr.gptp_reference_now  = ctx->gptp_reference_now;
        }
        rx_frame_run(srv, &fr, false, (size_t)-1, sink, user, &sum);
    }
    if (out_summary) *out_summary = sum;
    return RCP_MOCK_OK;
}

//cfusa:req REQ-INGRESS-025
int rcp_mock_server_pump_transport(rcp_mock_server_t *srv, rcp_avtp_transport_t *t,
                                   const rcp_context_t *ctx, const rcp_mock_rx_ctx_t *rx,
                                   uint8_t *buf, size_t buf_cap, rcp_mock_rx_sink_fn sink,
                                   void *user, rcp_mock_rx_summary_t *out_summary)
{
    size_t n  = 0;
    int    rc;

    if (out_summary) memset(out_summary, 0, sizeof(*out_summary));
    rc = rcp_avtp_transport_recv(t, ctx, buf, buf_cap, &n);
    if (rc != RCP_OK) return rc;
    (void)rcp_mock_server_receive_avtpdu(srv, buf, n, rx, sink, user, out_summary);
    return RCP_OK;
}

/* ── Legacy dispatch entry points (step 9: thin wrappers) ──────────────────── */

//cfusa:req REQ-WDG-010
//cfusa:req REQ-MOCK-012
rcp_mock_dispatch_result_t rcp_mock_server_dispatch(rcp_mock_server_t *srv,
                                                     rcp_byte_bus_id_t byte_bus_id,
                                                     uint8_t avtp_subtype, uint8_t acf_msg_type,
                                                     bool time_sync_supported, uint64_t stream_id,
                                                     const uint8_t *request, size_t request_len,
                                                     rcp_bytes_t *out_response)
{
    rx_frame_t                 fr;
    rcp_bytes_t                buf[RCP_MOCK_MAX_RESPONSES_PER_EXEC];
    rx_out_t                   o;
    rcp_mock_dispatch_result_t result;

    rx_frame_legacy(&fr, avtp_subtype, stream_id, false, 0u, true, time_sync_supported, 0u);
    rx_out_init(&o, buf, RCP_MOCK_MAX_RESPONSES_PER_EXEC);
    result = rx_single(srv, &fr, byte_bus_id, acf_msg_type, true, request, request_len, &o);
    rx_finish_single(srv, stream_id, &o, out_response);
    return result;
}

//cfusa:req REQ-TIMED-012
//cfusa:req REQ-TIMED-013
//cfusa:req REQ-AVTP-032
//cfusa:req REQ-AVTP-033
//cfusa:req REQ-AVTP-034
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
    rx_frame_t                 fr;
    rcp_bytes_t                buf[RCP_MOCK_MAX_RESPONSES_PER_EXEC];
    rx_out_t                   o;
    rcp_mock_dispatch_result_t result;

    /* REQ-AVTP-032/033: the caller's own decoded reserved-bytes flag feeds
     * the same frame-level timing decision every other path uses, under
     * srv's own reserved-zero policy (issue #618). */
    rx_frame_legacy(&fr, avtp_subtype, stream_id, tv, avtp_timestamp, tscf_reserved_all_zero,
                    time_sync_supported, gptp_reference_now);
    rx_out_init(&o, buf, RCP_MOCK_MAX_RESPONSES_PER_EXEC);
    result = rx_single(srv, &fr, byte_bus_id, acf_msg_type, false, request, request_len, &o);
    rx_finish_single(srv, stream_id, &o, out_response);
    return result;
}

//cfusa:req REQ-ISELED-025
//cfusa:req REQ-INGRESS-019
//cfusa:req REQ-WDG-010
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_multi_response(
    rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id, uint8_t avtp_subtype,
    uint8_t acf_msg_type, bool time_sync_supported, uint64_t stream_id, const uint8_t *request,
    size_t request_len, rcp_bytes_t *out_responses, size_t out_cap, size_t *out_response_count)
{
    rx_frame_t                 fr;
    rx_out_t                   o;
    rcp_mock_dispatch_result_t result;

    rx_frame_legacy(&fr, avtp_subtype, stream_id, false, 0u, true, time_sync_supported, 0u);
    /* The caller's own array and capacity, passed straight through: no
     * new cap for existing callers. */
    rx_out_init(&o, out_responses, out_cap);
    result = rx_single(srv, &fr, byte_bus_id, acf_msg_type, false, request, request_len, &o);
    rx_out_suppress(srv, stream_id, &o);
    *out_response_count = o.count;
    return result;
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
    return rcp_mock_server_dispatch_e2e_tscf(srv, byte_bus_id, avtp_subtype, acf_msg_type,
                                              time_sync_supported, stream_id, false, avtp_timestamp,
                                              0u, request, request_len, out_response);
}

//cfusa:req REQ-TIMED-012
//cfusa:req REQ-TIMED-013
//cfusa:req REQ-E2E-021
//cfusa:req REQ-E2E-031
//cfusa:req REQ-E2E-041
//cfusa:req REQ-WDG-010
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_e2e_tscf(rcp_mock_server_t *srv,
                                                               rcp_byte_bus_id_t byte_bus_id,
                                                               uint8_t avtp_subtype,
                                                               uint8_t acf_msg_type,
                                                               bool time_sync_supported,
                                                               uint64_t stream_id, bool tv,
                                                               uint32_t avtp_timestamp,
                                                               uint64_t gptp_reference_now,
                                                               const uint8_t *request,
                                                               size_t request_len,
                                                               rcp_bytes_t *out_response)
{
    rx_frame_t                 fr;
    rcp_bytes_t                buf[RCP_MOCK_MAX_RESPONSES_PER_EXEC];
    rx_out_t                   o;
    rcp_mock_dispatch_result_t result;

    rx_frame_legacy(&fr, avtp_subtype, stream_id, tv, avtp_timestamp, true, time_sync_supported,
                    gptp_reference_now);
    rx_out_init(&o, buf, RCP_MOCK_MAX_RESPONSES_PER_EXEC);
    result = rx_single(srv, &fr, byte_bus_id, acf_msg_type, false, request, request_len, &o);
    rx_finish_single(srv, stream_id, &o, out_response);
    return result;
}

//cfusa:req REQ-E2E-038
//cfusa:req REQ-E2E-039
//cfusa:req REQ-E2E-047
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_e2e_fragment(
    rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id, uint8_t avtp_subtype,
    uint8_t acf_msg_type, bool time_sync_supported, uint64_t stream_id, uint32_t avtp_timestamp,
    const uint8_t *fragment, size_t fragment_len, rcp_bytes_t *out_response)
{
    return rcp_mock_server_dispatch_e2e_tscf(srv, byte_bus_id, avtp_subtype, acf_msg_type,
                                              time_sync_supported, stream_id, false, avtp_timestamp,
                                              0u, fragment, fragment_len, out_response);
}

//cfusa:req REQ-TIMED-012
//cfusa:req REQ-TIMED-013
//cfusa:req REQ-E2E-038
//cfusa:req REQ-E2E-039
//cfusa:req REQ-E2E-047
rcp_mock_dispatch_result_t rcp_mock_server_dispatch_e2e_fragment_tscf(
    rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id, uint8_t avtp_subtype,
    uint8_t acf_msg_type, bool time_sync_supported, uint64_t stream_id, bool tv,
    uint32_t avtp_timestamp, uint64_t gptp_reference_now, const uint8_t *fragment,
    size_t fragment_len, rcp_bytes_t *out_response)
{
    return rcp_mock_server_dispatch_e2e_tscf(srv, byte_bus_id, avtp_subtype, acf_msg_type,
                                              time_sync_supported, stream_id, tv, avtp_timestamp,
                                              gptp_reference_now, fragment, fragment_len,
                                              out_response);
}

/* Sink the legacy frame wrappers use to fill rcp_mock_frame_member_result_t
 * (responses joined into the single per-member response). */
typedef struct {
    rcp_mock_frame_member_result_t *out;
    size_t                          cap;
    size_t                          written;
} rx_legacy_frame_sink_t;

static void rx_legacy_frame_sink(rcp_mock_rx_member_t *m, void *user)
{
    rx_legacy_frame_sink_t         *s = (rx_legacy_frame_sink_t *)user;
    rcp_mock_frame_member_result_t *r;

    if (s->written >= s->cap) return;
    r              = &s->out[s->written++];
    r->result      = m->result;
    /* SEQ_ERROR keeps its historical "byte_bus_id 0, no response" shape. */
    r->byte_bus_id = (m->result == RCP_MOCK_DISPATCH_SEQ_ERROR) ? 0u : m->byte_bus_id;
    rx_join_responses(m->responses, m->response_count, &r->response);
}

/* Shared body of the three legacy frame wrappers. Frame-level drops keep
 * the historical per-member DROPPED report (each member DROPPED, no
 * response, no side effect); a malformed or empty payload reports 0. */
static size_t rx_legacy_frame(rcp_mock_server_t *srv, rx_frame_t *fr, bool untimed,
                              uint8_t sequence_num, const uint8_t *frame, size_t frame_len,
                              rcp_mock_frame_member_result_t *out_results, size_t out_cap)
{
    rx_legacy_frame_sink_t s;
    rcp_mock_rx_summary_t  sum;

    memset(&sum, 0, sizeof(sum));
    s.out     = out_results;
    s.cap     = out_cap;
    s.written = 0;
    fr->f.sequence_num = sequence_num;
    fr->f.payload      = frame;
    fr->f.payload_len  = frame_len;

    if (srv->watchdog != NULL) rcp_watchdog_keeper_kick(srv->watchdog, fr->f.stream_id);
    rx_frame_run(srv, fr, untimed, out_cap, rx_legacy_frame_sink, &s, &sum);
    if (sum.outcome == RCP_MOCK_RX_FRAME_TIMING_DROP ||
        sum.outcome == RCP_MOCK_RX_FRAME_LIFECYCLE_DROP) {
        return rx_emit_all(fr, RCP_MOCK_DISPATCH_DROPPED, RCP_MOCK_RX_REASON_LIFECYCLE_DROP,
                           out_cap, rx_legacy_frame_sink, &s);
    }
    return s.written;
}

//cfusa:req REQ-MOCK-019
//cfusa:req REQ-MOCK-020
//cfusa:req REQ-MOCK-029
//cfusa:req REQ-E2E-028
size_t rcp_mock_server_dispatch_frame(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                       bool time_sync_supported, uint64_t stream_id,
                                       uint8_t sequence_num,
                                       const uint8_t *frame, size_t frame_len,
                                       rcp_mock_frame_member_result_t *out_results,
                                       size_t out_cap)
{
    rx_frame_t fr;

    rx_frame_legacy(&fr, avtp_subtype, stream_id, false, 0u, true, time_sync_supported, 0u);
    return rx_legacy_frame(srv, &fr, true, sequence_num, frame, frame_len, out_results, out_cap);
}

//cfusa:req REQ-E2E-033
//cfusa:req REQ-E2E-028
size_t rcp_mock_server_dispatch_frame_e2e(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                           bool time_sync_supported, uint64_t stream_id,
                                           uint32_t avtp_timestamp, uint8_t sequence_num,
                                           const uint8_t *frame,
                                           size_t frame_len,
                                           rcp_mock_frame_member_result_t *out_results,
                                           size_t out_cap)
{
    rx_frame_t fr;

    rx_frame_legacy(&fr, avtp_subtype, stream_id, false, avtp_timestamp, true,
                    time_sync_supported, 0u);
    return rx_legacy_frame(srv, &fr, false, sequence_num, frame, frame_len, out_results, out_cap);
}

//cfusa:req REQ-TIMED-012
//cfusa:req REQ-TIMED-013
//cfusa:req REQ-E2E-033
size_t rcp_mock_server_dispatch_frame_e2e_tscf(rcp_mock_server_t *srv, uint8_t avtp_subtype,
                                                bool time_sync_supported, uint64_t stream_id,
                                                bool tv, uint32_t avtp_timestamp,
                                                uint64_t gptp_reference_now, uint8_t sequence_num,
                                                const uint8_t *frame, size_t frame_len,
                                                rcp_mock_frame_member_result_t *out_results,
                                                size_t out_cap)
{
    rx_frame_t fr;

    rx_frame_legacy(&fr, avtp_subtype, stream_id, tv, avtp_timestamp, true, time_sync_supported,
                    gptp_reference_now);
    return rx_legacy_frame(srv, &fr, false, sequence_num, frame, frame_len, out_results, out_cap);
}

/* Drains slot's oldest queued request (if any) and runs it through
 * slot_execute(), joining the responses for the single-response surface
 * -- the identical execution path immediate admission uses, so a
 * multi-response slot's output survives a queued run (step 8). */
static bool drain_slot(rcp_mock_server_t *srv, rcp_mock_endpoint_slot_t *slot,
                       rcp_bytes_t *out_response)
{
    rcp_bytes_t frame = {0};
    rcp_bytes_t buf[RCP_MOCK_MAX_RESPONSES_PER_EXEC];
    rx_out_t    o;

    memset(out_response, 0, sizeof(*out_response));
    if (!slot) return false;
    if (!rcp_server_endpoint_drain_one(&slot->queue, &frame)) return false;

    rx_out_init(&o, buf, RCP_MOCK_MAX_RESPONSES_PER_EXEC);
    slot_execute(srv, slot, frame.data, frame.len, &o);
    rcp_bytes_free(&frame);
    rx_join_responses(o.out, o.count, out_response);
    return true;
}

//cfusa:req REQ-MOCK-016
//cfusa:req REQ-MOCK-017
//cfusa:req REQ-MOCK-018
//cfusa:req REQ-INGRESS-020
bool rcp_mock_server_drain_endpoint(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                     rcp_bytes_t *out_response)
{
    return drain_slot(srv, find_slot(srv, byte_bus_id), out_response);
}

//cfusa:req REQ-MOCK-032
//cfusa:req REQ-INGRESS-020
bool rcp_mock_server_drain_endpoint_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                               rcp_byte_bus_id_t byte_bus_id,
                                               rcp_bytes_t *out_response)
{
    return drain_slot(srv, find_slot_on_stream(srv, stream_id, byte_bus_id), out_response);
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

//cfusa:req REQ-MOCK-032
bool rcp_mock_server_stash_deferred_response_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                                         rcp_byte_bus_id_t byte_bus_id,
                                                         rcp_bytes_t response)
{
    rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);
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

//cfusa:req REQ-MOCK-032
bool rcp_mock_server_take_deferred_response_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                                        rcp_byte_bus_id_t byte_bus_id,
                                                        rcp_bytes_t *out_response)
{
    rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);

    memset(out_response, 0, sizeof(*out_response));
    if (!slot || slot->deferred_response.data == NULL) return false;

    *out_response = slot->deferred_response;
    memset(&slot->deferred_response, 0, sizeof(slot->deferred_response));
    return true;
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

/* Runs at most one due stored request on slot into the collector o:
 * rcp_server_endpoint_select_due() picks it, a stored cancellation goes
 * through apply_cancellation() (REQ-TIMED-012/013, issue #422 -- a
 * cancellation admitted under a TSCF header with a future presentation
 * time is stored like any other request and applied once due), every
 * other kind through slot_execute(), the identical execution path
 * immediate admission uses (step 8: a multi-response slot's stored run
 * produces its whole output). Then rcp_server_endpoint_complete()
 * advances a compound request's sequencer and applies the repetition
 * rule. The sequencer table is the server's, never the caller's. */
//cfusa:req REQ-MOCK-023
//cfusa:req REQ-MOCK-024
//cfusa:req REQ-INGRESS-020
static bool tick_slot(rcp_mock_server_t *srv, rcp_mock_endpoint_slot_t *slot,
                      rcp_byte_bus_id_t byte_bus_id, const rcp_server_tick_ctx_t *ctx, rx_out_t *o)
{
    rcp_server_tick_ctx_t local;
    size_t                index = 0;

    if (!slot) return false;

    local            = *ctx;
    local.sequencers = &srv->sequencers;

    if (!rcp_server_endpoint_select_due(&slot->queue, &local, &index)) return false;

    if (slot->queue.pending[index].kind == RCP_SCHED_KIND_CANCELLATION) {
        rcp_bytes_t r = {0};

        apply_cancellation(slot, slot->queue.pending[index].request_type,
                            slot->queue.pending[index].frame.data,
                            slot->queue.pending[index].frame.len, byte_bus_id, &r);
        rx_out_push(srv, o, r);
    } else {
        slot_execute(srv, slot, slot->queue.pending[index].frame.data,
                     slot->queue.pending[index].frame.len, o);
    }

    (void)rcp_server_endpoint_complete(&slot->queue, index, &local);
    return true;
}

/* Legacy single-response tick: every response of the run, joined. */
static bool tick_slot_joined(rcp_mock_server_t *srv, rcp_mock_endpoint_slot_t *slot,
                             rcp_byte_bus_id_t byte_bus_id, const rcp_server_tick_ctx_t *ctx,
                             rcp_bytes_t *out_response)
{
    rcp_bytes_t buf[RCP_MOCK_MAX_RESPONSES_PER_EXEC];
    rx_out_t    o;
    bool        ran;

    memset(out_response, 0, sizeof(*out_response));
    rx_out_init(&o, buf, RCP_MOCK_MAX_RESPONSES_PER_EXEC);
    ran = tick_slot(srv, slot, byte_bus_id, ctx, &o);
    rx_join_responses(o.out, o.count, out_response);
    return ran;
}

//cfusa:req REQ-MOCK-023
//cfusa:req REQ-MOCK-024
bool rcp_mock_server_tick(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                           const rcp_server_tick_ctx_t *ctx, rcp_bytes_t *out_response)
{
    return tick_slot_joined(srv, find_slot(srv, byte_bus_id), byte_bus_id, ctx, out_response);
}

/* REQ-MOCK-032 (issue #447): stream-scoped counterpart of
 * rcp_mock_server_tick() -- tick() takes byte_bus_id as its ONLY endpoint
 * selector and operates on exactly one slot per call, so it cannot
 * disambiguate two slots sharing one byte_bus_id on different
 * stream_ids. */
//cfusa:req REQ-MOCK-032
bool rcp_mock_server_tick_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                     rcp_byte_bus_id_t byte_bus_id,
                                     const rcp_server_tick_ctx_t *ctx, rcp_bytes_t *out_response)
{
    return tick_slot_joined(srv, find_slot_on_stream(srv, stream_id, byte_bus_id), byte_bus_id,
                            ctx, out_response);
}

//cfusa:req REQ-INGRESS-020
bool rcp_mock_server_tick_multi(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id,
                                 const rcp_server_tick_ctx_t *ctx, rcp_bytes_t *out_responses,
                                 size_t out_cap, size_t *out_count)
{
    rx_out_t o;
    bool     ran;

    rx_out_init(&o, out_responses, out_cap);
    ran        = tick_slot(srv, find_slot(srv, byte_bus_id), byte_bus_id, ctx, &o);
    *out_count = o.count;
    return ran;
}

//cfusa:req REQ-INGRESS-020
bool rcp_mock_server_tick_multi_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                           rcp_byte_bus_id_t byte_bus_id,
                                           const rcp_server_tick_ctx_t *ctx,
                                           rcp_bytes_t *out_responses, size_t out_cap,
                                           size_t *out_count)
{
    rx_out_t o;
    bool     ran;

    rx_out_init(&o, out_responses, out_cap);
    ran = tick_slot(srv, find_slot_on_stream(srv, stream_id, byte_bus_id), byte_bus_id, ctx, &o);
    *out_count = o.count;
    return ran;
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

//cfusa:req REQ-MOCK-032
size_t rcp_mock_server_pending_count_on_stream(const rcp_mock_server_t *srv, uint64_t stream_id,
                                                rcp_byte_bus_id_t byte_bus_id)
{
    const rcp_mock_endpoint_slot_t *slot = find_slot_on_stream_const(srv, stream_id, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_pending_count(&slot->queue);
}

//cfusa:req REQ-MOCK-026
//cfusa:req REQ-INGRESS-017
size_t rcp_mock_server_watchdog_purge(rcp_mock_server_t *srv, rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_endpoint_slot_t *slot = find_slot(srv, byte_bus_id);

    reasm_purge(srv, false, 0u, true, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_watchdog_purge(&slot->queue);
}

//cfusa:req REQ-MOCK-032
//cfusa:req REQ-INGRESS-017
size_t rcp_mock_server_watchdog_purge_on_stream(rcp_mock_server_t *srv, uint64_t stream_id,
                                                 rcp_byte_bus_id_t byte_bus_id)
{
    rcp_mock_endpoint_slot_t *slot = find_slot_on_stream(srv, stream_id, byte_bus_id);

    reasm_purge(srv, true, stream_id, true, byte_bus_id);
    if (!slot) return 0;
    return rcp_server_endpoint_watchdog_purge(&slot->queue);
}

/* issue #447: unlike every other function this issue's audit named,
 * rcp_mock_server_broadcast_safe_state() does NOT take byte_bus_id as its
 * only endpoint selector -- request_stream_index already disambiguates
 * which request stream is escalating, so this function does not need a
 * new _on_stream() variant the way rcp_mock_server_tick()/_drain_endpoint()
 * etc. do. It DID have a real, narrower version of the same bug: its own
 * per-byte_bus_id loop below used to resolve each bound slot via the
 * unscoped find_slot(), which -- once two endpoints legitimately share one
 * byte_bus_id on different stream_ids (rcp_mock_server_add_endpoint_
 * on_stream()) -- could purge the wrong one of the two. Fixed in place
 * (not by adding a variant): request_stream_index already carries enough
 * context to resolve the real stream_id this call is escalating (srv's
 * own request_stream_cfg[request_stream_index-1].rx_stream_id, the same
 * table rcp_regmap_request_stream_cfg_resolve_index() -- the inverse
 * lookup -- already keys off), so the loop below now calls
 * find_slot_on_stream() with that resolved stream_id instead. */
//cfusa:req REQ-E2E-029
//cfusa:req REQ-E2E-030
//cfusa:req REQ-E2E-045
//cfusa:req REQ-MOCK-033
//cfusa:req REQ-INGRESS-017
size_t rcp_mock_server_broadcast_safe_state(rcp_mock_server_t *srv, uint8_t request_stream_index)
{
    rcp_byte_bus_id_t bound[RCP_MOCK_MAX_ENDPOINTS];
    size_t             total_bound;
    size_t             purged = 0;
    size_t             i;
    bool               have_stream_id = false;
    uint64_t           resolved_stream_id = 0;

    if (request_stream_index == 0u) return 0;

    /* Resolve the real wire stream_id this request_stream_index names, so
     * the per-slot lookup below can be genuinely stream-scoped rather than
     * byte_bus_id-only -- see this function's own doc comment above. A
     * request_stream_index beyond srv's own currently-configured
     * request_stream_cfg_count has no known stream_id to resolve (an
     * EP_ID_config row referencing an unconfigured request stream); the
     * loop below falls back to the old, unscoped find_slot() only for
     * that misconfigured case, exactly this function's pre-fix behavior
     * -- no worse than before, and no in-range call is affected. */
    if ((size_t)request_stream_index <= srv->request_stream_cfg_count) {
        resolved_stream_id = srv->request_stream_cfg[request_stream_index - 1u].rx_stream_id;
        have_stream_id      = true;
    }

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
        rcp_mock_endpoint_slot_t *slot = have_stream_id
            ? find_slot_on_stream(srv, resolved_stream_id, bound[i])
            : find_slot(srv, bound[i]);
        /* REQ-INGRESS-017: a stream entering its safe state keeps no
         * half-collected fragmented request for a bound endpoint. */
        reasm_purge(srv, have_stream_id, resolved_stream_id, true, bound[i]);
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
