/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-RMAP-001
//cfusa:req REQ-RMAP-002
//cfusa:req REQ-RMAP-003
//cfusa:req REQ-RMAP-004
//cfusa:req REQ-RMAP-005
//cfusa:req REQ-RMAP-006
//cfusa:req REQ-RMAP-007
//cfusa:req REQ-RMAP-008
//cfusa:req REQ-RMAP-009
//cfusa:req REQ-RMAP-010
//cfusa:req REQ-RMAP-011
//cfusa:req REQ-RMAP-012
//cfusa:req REQ-RMAP-013
//cfusa:req REQ-RMAP-014
//cfusa:req REQ-RMAP-015
//cfusa:req REQ-RMAP-016
//cfusa:req REQ-RMAP-017
//cfusa:req REQ-RMAP-018
//cfusa:req REQ-RMAP-019
//cfusa:req REQ-RMAP-020
//cfusa:req REQ-RMAP-021
//cfusa:req REQ-RMAP-022

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-RMAP-023
//cfusa:req REQ-RMAP-024
//cfusa:req REQ-RMAP-025
//cfusa:req REQ-RMAP-026
//cfusa:req REQ-RMAP-027
//cfusa:req REQ-RMAP-028
//cfusa:req REQ-RMAP-029
//cfusa:req REQ-RMAP-030
//cfusa:req REQ-RMAP-031
//cfusa:req REQ-RMAP-032
//cfusa:req REQ-RMAP-033
//cfusa:req REQ-RMAP-034
//cfusa:req REQ-RMAP-035
//cfusa:req REQ-RMAP-036
//cfusa:req REQ-RMAP-037
//cfusa:req REQ-RMAP-038
//cfusa:req REQ-RMAP-039
//cfusa:req REQ-RMAP-040
//cfusa:req REQ-RMAP-041
//cfusa:req REQ-RMAP-042
//cfusa:req REQ-RMAP-043
//cfusa:req REQ-RMAP-044
//cfusa:req REQ-RMAP-045
//cfusa:req REQ-RMAP-046
//cfusa:req REQ-RMAP-047
//cfusa:req REQ-RMAP-048
//cfusa:req REQ-RMAP-049
//cfusa:req REQ-RMAP-050
//cfusa:req REQ-RMAP-051
//cfusa:req REQ-RMAP-052
//cfusa:req REQ-RMAP-053
//cfusa:req REQ-RMAP-054
//cfusa:req REQ-RMAP-055
//cfusa:req REQ-RMAP-056
//cfusa:req REQ-RMAP-057
//cfusa:req REQ-RMAP-058
//cfusa:req REQ-RMAP-059
//cfusa:req REQ-RMAP-060
//cfusa:req REQ-RMAP-061
//cfusa:req REQ-RMAP-062
//cfusa:req REQ-RMAP-063
//cfusa:req REQ-RMAP-064
//cfusa:req REQ-RMAP-065
//cfusa:req REQ-RMAP-066
//cfusa:req REQ-RMAP-067
//cfusa:req REQ-RMAP-068
//cfusa:req REQ-RMAP-069
/*
 * regmap.h -- Register-map model for the TC18 Remote Control Protocol RC
 * Server (ROADMAP.md Phase 14, "RC Server Lifecycle & Register Map",
 * milestone 62).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), and the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/
 * acf.c, server.h/server.c, or any satellite package is touched here.
 * server.h's own milestone-61 header comment explicitly named this module's
 * job as "the full register-map layout backing hardware pin mapping,
 * request stream, response stream, EP0, and the root-client model" -- that
 * is exactly the surface modeled below.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. Where a name (e.g. "svr_ep_count", "ep_enable",
 * "byte_bus_id") is carried over from the confidential OPEN Alliance TC18
 * Remote Control Protocol Specification v0.5.1_RC, only that identifier
 * and its high-level purpose are taken by reference -- no spec prose, bit
 * layout, or numeric constant is reproduced here. Exact on-wire encodings
 * for the structures below are deliberately left unimplemented (no
 * encode/decode pair, unlike avtp.h/acf.h): per this milestone's roadmap
 * scope, register *contents* are modeled now so every later endpoint type
 * composes them the same way, but wiring them to an actual byte_message_info
 * read/write exchange is later phases' job (Phase 16/19 endpoint types,
 * Phase 18 E2E/watchdog).
 *
 * ── The general (vendor-agnostic, EP0-reachable) register map ─────────────
 *
 * rcp_regmap_general_t is the one register block every RC Server exposes
 * regardless of which endpoint types it implements: a magic number, the
 * server's own protocol version, vendor/device identification,
 * svr_ep_count, stream/sequencer/memory capacity fields, the
 * svr_implemented_options bitmask, svr_root_client_index, and a
 * pointer/capacity pair (rcp_regmap_table_ref_t) to every sub-table listed
 * below. "Pointer" here means an offset into the server's own register
 * address space (the space addressed by ACF_ABB/ACF_GBB byte_bus_id 0 /
 * EP0, per acf.h), not a C pointer -- register maps are read and written
 * over the wire, not dereferenced in-process.
 *
 * ── EP0 and the root-client model ──────────────────────────────────────────
 *
 * EP0 is the pseudo-endpoint exposing this whole register surface; it is
 * always endpoint index RCP_REGMAP_EP0_INDEX (0) -- deliberately the same
 * numeric value as RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID in server.h, since
 * discovery (milestone 63) and the general register map are both reached
 * through the same address. svr_root_client_index names the one stream
 * with full-server write access; every other stream is restricted to the
 * endpoint(s) it owns via rcp_regmap_ep_client_t. rcp_regmap_writer_ctx()
 * derives server.h's rcp_lifecycle_writer_ctx_t from this register data,
 * without duplicating rcp_lifecycle_field_writable()'s already-built
 * authorization logic.
 *
 * ── The generic-vs-functional config split ─────────────────────────────────
 *
 * Every endpoint's configuration is split into two distinct structs,
 * deliberately not merged into one blob: rcp_regmap_ep_generic_cfg_t is
 * server-owned (ep_type, ep_used, ep_delay_time, ep_req_storage_size, ...)
 * and rcp_regmap_ep_functional_cfg_t is the common functional-config
 * prefix every concrete endpoint type (Phase 16/19) composes as its own
 * struct's first member, the same way every ACF variant in acf.h shares
 * one byte_message_info header.
 *
 * ── HW pin mapping and the named-signal index ──────────────────────────────
 *
 * rcp_regmap_hw_pin_map_entry_t associates a hardware endpoint number and
 * pin number with a pin-property byte (this module's own bit layout, see
 * the RCP_REGMAP_PIN_PROP_* macros below). rcp_regmap_named_signal_t is
 * the full per-endpoint-type named-signal index (GPIO 0-31, SPI CLK/PICO/
 * POCI/CS0-5, I2C SCL/SDA) written once here and reused unmodified by
 * every endpoint type added later.
 *
 * ── Request-stream and response/ack queue config ───────────────────────────
 *
 * rcp_regmap_request_stream_cfg_t and rcp_regmap_response_queue_cfg_t model
 * the fields those two tables need. At this module's original milestone
 * (62) the E2E/watchdog-relevant fields (rx_wd_timeout_ms, rx_wd_action,
 * rx_enforce_e2e, rx_safety_measure) existed but were deliberately inert,
 * so Phase 18 would never need a second register-layout pass. Phase 18
 * (e2e.h/e2e.c, milestone 70) has since both wired those four
 * fields to real behavior and rounded out the rest of the rx_wd_* family
 * (rx_wd_enable, rx_wd_safestate_enable, rx_wd_info_enable) plus
 * rx_safestate_sequencer/rx_safe_sequencer_state that milestone 62's own
 * placeholder comment had not yet named -- see
 * rcp_regmap_request_stream_cfg_t's own field comments below and
 * e2e.h's file header for the behavior each one now drives. Phase 20
 * (fragment.h/fragment.c, milestone 76) has since added
 * rx_stream_max_request_size to the same struct, the "request-stream
 * config table reserved since Phase 14" that milestone's own roadmap
 * entry refers to -- see that field's own comment and fragment.h's file
 * header for the fragmentation mechanism it configures.
 *
 * ── TC18 0.5.1_RC5 terminology drift (investigated, NOT restructured) ──────
 *
 * INVESTIGATED 2026-08-11 (spec rebaseline to TC18 0.5.1_RC5,
 * c-RCP-AUDIT-06, task #97): spec revision 0.5.1_RC4 renames and
 * restructures this whole 0x000D octet. The 8 independently-configurable
 * bits this codebase's own rx_enforce_e2e/rx_enforce_seq/
 * rx_seq_safestate_enable/rx_wd_enable/rx_wd_safestate_enable/
 * rx_ovrflw_safestate_enable/rx_safety_measure/rx_wd_info_enable were
 * originally modeled against become, in RC5's own Table 24 (renumbered
 * from Table 24), 4 combined bits -- rx_enforce_crc, rx_enforce_sequence,
 * rx_enforce_watchdog, rx_enforce_request_filing -- plus 3 reserved bits
 * and a new read-only rx_stream_status bit at 0x000D.7.
 *
 * No code change was made here, for three reasons, checked directly
 * against the rendered PDF on both spec revisions before concluding:
 *
 *   1. This struct has NO wire (de)serialization anywhere in this
 *      codebase (confirmed via grep across every .c file under src/) -- only
 *      rcp_regmap_request_stream_cfg_t's own _init() exists. Nothing
 *      decodes a real 0x000D byte from the wire today, so there is no
 *      live conformance defect the old-vs-new bit layout could cause;
 *      this is a pure in-memory, caller-populated API surface.
 *
 *   2. rx_enforce_e2e's own RC5 rename (rx_enforce_crc) is a pure
 *      synonym -- both are already single bits whose 1-value gates BOTH
 *      "block the stream" AND "enter safe state" at once (see e2e.h's
 *      own file header, "rx_enforce_e2e: single-request drop vs.
 *      whole-stream latch-to-fault"); zero semantic change. The other 3
 *      new bits each collapse what were TWO independently-configurable
 *      dimensions in the 0.5.1_RC baseline (an enforce/enable bit plus a
 *      separate safestate-consequence bit) into ONE combined bit. e2e.h's
 *      own rcp_e2e_seq_evaluate()/_wd_evaluate()/
 *      _overflow_should_enter_safe_state() deliberately keep those two
 *      dimensions independently expressible ("deliberately NOT collapsed
 *      into one bool: they answer different questions and either can be
 *      enabled without the other" -- e2e.h's own words). This codebase's
 *      own richer model remains a strict, safe SUPERSET of what RC5's
 *      collapsed wire encoding can express (a real RC5-conformant peer
 *      can only ever request the "coupled" subset -- both dimensions the
 *      same value -- which this model already represents correctly), not
 *      a conformance defect requiring narrowing.
 *
 *   3. rx_safety_measure (the high-impedance-vs-sequencer safe-state
 *      selector) and rx_wd_info_enable (the repetitive-notification-on-
 *      overflow feature) have NO clear 1:1 replacement in RC5's own
 *      4-bit scheme -- genuinely ambiguous, not resolved here. The new
 *      rx_stream_status bit ("will be set automatically as a reaction to
 *      either CRC error, sequence error, watchdog overflow, EP overflow,
 *      when enabled") is a different mechanism entirely (a passive,
 *      client-polled aggregate status covering all four fault classes
 *      uniformly, not an active per-cause notification push). UPDATED
 *      2026-08-13 (issue #201/#336, REQ-E2E-046): the cross-cutting "is
 *      this stream currently blocked by any of its four independent
 *      fault latches" primitive this note originally said the codebase
 *      lacked now exists -- e2e.h's own rcp_e2e_stream_status_t and its
 *      rcp_e2e_stream_status_rx_blocked() aggregate (that file's own
 *      "Aggregate rx_stream_status" section). Still not wired into
 *      mock.c's dispatch (which has no per-endpoint-type dispatch of any
 *      kind), and rx_safety_measure/rx_wd_info_enable's own genuine
 *      ambiguity remains unresolved.
 *      The rest of this octet's own surrounding registers
 *      (rx_safestate_sequencer/rx_safe_sequencer_state, 0x000E/0x000F)
 *      are themselves flagged, in the same RC5 revision, as subject to a
 *      separate, still-draft "trigger request" harmonization proposal
 *      (see ep_spi.h's own file header for that proposal's own
 *      confirmed-still-draft status) -- their own eventual shape is not
 *      yet settled either, reinforcing that a full structural rewrite of
 *      this whole octet would be premature.
 *
 * ── Known spec ambiguity: EP-ID/byte_bus_id ordering is not enforced ───────
 *
 * rcp_regmap_ep_id_map_entry_t models one row of the table associating an
 * endpoint's EP-ID with the byte_bus_id(s) it responds to. That table's
 * required ascending ordering has no server-side enforcement mechanism in
 * the specification itself -- maintaining ascending order when writing
 * this table is a client responsibility with no corrective action defined
 * for a server that receives it out of order. This implementation carries
 * that ambiguity forward exactly as the spec leaves it: no encode/decode
 * or write path in this codebase rejects (or reorders) an out-of-order
 * table. rcp_regmap_ep_id_map_is_ascending() below is provided purely as
 * a read-only diagnostic for tooling that wants to notice the condition;
 * it is not invoked by, and must not be mistaken for, server-side
 * enforcement -- there is deliberately no such thing to call it from.
 *
 * UPDATED 2026-08-11 (spec rebaseline to TC18 0.5.1_RC5): the ascending-
 * order sentence this whole section is built on ("The parameters
 * Request_Stream_Index and BBID shall occur in ascending order...") is
 * entirely DELETED as of spec revision 0.5.1_RC4 (confirmed against the
 * rendered PDF, tracked-change tag 051RC4 "sentence deleted as
 * discussed") -- current TC18 §12.7.8 no longer states or implies any
 * ordering requirement at all. rcp_regmap_ep_id_map_is_ascending() and
 * its own REQ-RMAP-020/021/022/056 remain correct, harmless, purely-
 * diagnostic code (nothing here ever enforced the old rule either), but
 * no longer trace to a live TC18 MUST -- see those requirements' own
 * updated citations for the primary-source evidence.
 */
#ifndef RCP_REGMAP_H
#define RCP_REGMAP_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/lifecycle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── EP0 ────────────────────────────────────────────────────────────────────── */

/* The pseudo-endpoint index exposing the whole register-map surface.
 * Deliberately the same numeric value as RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID
 * (server.h) -- see the file header above. */
#define RCP_REGMAP_EP0_INDEX ((uint16_t)0u)

/* True iff ep_index is EP0. */
bool rcp_regmap_is_ep0(uint16_t ep_index);

/* Sentinel for rcp_regmap_general_t.svr_root_client_index meaning "no
 * stream currently holds the root-client grant" -- the natural state
 * while HW_UNCONFIGURED, before any client has been promoted. */
#define RCP_REGMAP_NO_ROOT_CLIENT ((uint16_t)0xFFFFu)

/* ── Sub-table pointer/capacity pairs ──────────────────────────────────────── */

/* One sub-table's location and size within the server's own register
 * address space. offset is measured in this implementation's own units
 * (register words); capacity is the number of entries the sub-table can
 * hold, not its size in octets. */
typedef struct {
    uint32_t offset;
    uint16_t capacity;
} rcp_regmap_table_ref_t;

/* ── svr_implemented_options: five independent single bits (REQ-RMAP-030) ─── */

/* REQ-RMAP-030 (TC18 §12.7.5 Table 20, absolute address 0x0016, 8 bit,
 * R): five independent bits, one per optional feature, "abcdefgh" with
 * bits f/g/h reserved -- verified directly against the primary-source
 * PDF (Table 20, page 51 of OA_TC18_specification_v_0.5.1_RC.pdf):
 *   a: compound & wait requests
 *   b: trigger requests
 *   c: chained requests
 *   d: time synch and timed requests
 *   e: enhanced request cancellation
 * This replaces a prior design (REQ-RMAP-004..008, now retired -- see
 * their own .fusa-reqs.json entries) that grouped six bits of this
 * project's own invention into three all-or-nothing PAIRS, enforced by
 * a now-removed rcp_regmap_options_group_consistent() function, citing
 * §12.9.1.1 as justification. Primary-source verification (this
 * milestone) found that citation incorrect: §12.9.1.1 ("Handling
 * multiple requests in incoming messages") is entirely about an RC
 * Server processing several ACF-type requests packed into one AVTPDU
 * frame -- it says nothing about svr_implemented_options, feature
 * advertisement, or any pairing rule at all. The prior design also had
 * no bit at all for trigger or chained requests, even though this
 * codebase implements both (request_triggered.c, request_chained.c) --
 * REQ-RMAP-030's own named consequence. */
#define RCP_REGMAP_OPT_COMPOUND_WAIT ((uint8_t)1u << 0) /* a */
#define RCP_REGMAP_OPT_TRIGGER       ((uint8_t)1u << 1) /* b */
#define RCP_REGMAP_OPT_CHAINED       ((uint8_t)1u << 2) /* c */
#define RCP_REGMAP_OPT_TIME_SYNC     ((uint8_t)1u << 3) /* d */
#define RCP_REGMAP_OPT_ENH_CANCEL    ((uint8_t)1u << 4) /* e */

/* ── The general register map ──────────────────────────────────────────────── */

typedef struct {
    uint32_t magic;      /* vendor/device-defined; this module carries the
                             field only, see the file header */
    uint32_t svr_version; /* 32 bit wide on the wire, not 16 -- see
                              discovery.h's RCP_DISCOVERY_GENERAL_SLICE_LEN
                              for the resulting slice layout */
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t svr_ep_count;
    uint8_t  svr_lifecycle_state; /* REQ-RMAP-023 (TC18 §12.3.1.1/§12.3.1.2):
                                      the server's own rcp_lifecycle_state_t
                                      (regmap.h's own copy, not lifecycle.h's
                                      authoritative one -- a caller such as
                                      mock.c's rcp_mock_server_t keeps the
                                      two in sync after every successful
                                      rcp_lifecycle_transition(), matching
                                      this whole module's "caller composes"
                                      convention). Content modeling only:
                                      making this field actually READABLE
                                      over the wire is REQ-RMAP-024's own
                                      separate, still-open general
                                      register-read-dispatch gap -- this
                                      field's mere existence does not close
                                      that requirement, and REQ-RMAP-023
                                      itself stays "partial" (not
                                      "implemented") until it does. */
    uint8_t  svr_req_stream_max; /* REQ-RMAP-026 (TC18 §12.7.5 Table 20,
                                     absolute address 0x000E, 8 bit, R):
                                     maximum number of request streams
                                     usable to access this RC Server.
                                     8 bit on the wire, not 16 -- renamed
                                     from this field's own former name
                                     (svr_max_request_streams) to match
                                     TC18's own register name, and
                                     retyped so an in-process value this
                                     register's own width could never
                                     represent is impossible to construct
                                     in the first place. Content modeling
                                     only, same REQ-RMAP-024 wire-
                                     reachability boundary as
                                     svr_lifecycle_state above. */
    uint8_t  svr_responder_streams_max; /* REQ-RMAP-026 (TC18 §12.7.5
                                            Table 20, absolute address
                                            0x000F, 8 bit, R): maximum
                                            number of supported responder
                                            queues for response and
                                            acknowledge traffic. Same
                                            content-modeling-only scope
                                            as svr_req_stream_max above. */
    uint8_t  svr_sequencers_max; /* REQ-RMAP-028 (TC18 §12.7.5 Table 20,
                                     absolute address 0x0014, 8 bit, R):
                                     0 means "sequencer operation not
                                     supported"; 1..n gives the number of
                                     available sequencer state registers.
                                     Renamed from this field's own former
                                     name (svr_max_sequencers) and
                                     retyped to match TC18's own register
                                     name and width, same as
                                     svr_req_stream_max (REQ-RMAP-026).
                                     mock.c's rcp_mock_server_set_
                                     sequencer_count() keeps this synced
                                     with the actual
                                     rcp_sequencer_table_t.count it
                                     allocates -- request_sequencer.h's
                                     own rcp_sequencer_table_unsupported()
                                     (table->count == 0) already
                                     implements the "0 means unsupported"
                                     rule this register describes; once
                                     synced, this field correctly
                                     reflects that same rule by
                                     construction, not by a second,
                                     separate check. Content modeling
                                     only, same REQ-RMAP-024 wire-
                                     reachability boundary as every other
                                     Group 1 item. */
    uint8_t  svr_configuration_lock; /* REQ-RMAP-029 (TC18 §12.7.5 Table
                                         20, absolute address 0x0015, 8
                                         bit, R): 0x00 permits write
                                         access to "R/W+" (explicitly
                                         lockable) type parameters; any
                                         other value causes such write
                                         access to be rejected --
                                         independently of the lifecycle-
                                         state-driven gating
                                         rcp_lifecycle_field_writable()
                                         already applies to
                                         RCP_LIFECYCLE_FIELD_HW_GENERIC/
                                         _FUNCTIONAL_W/_FUNCTIONAL_W_STAR.
                                         Content modeling only, same
                                         REQ-RMAP-024 wire-reachability
                                         boundary as every other Group 1
                                         item -- deliberately NOT wired
                                         into rcp_lifecycle_field_kind_t
                                         or rcp_lifecycle_field_writable()
                                         here: REQ-RMAP-055 (issue #200
                                         Group 3) is this codebase's own
                                         already-identified "shared
                                         plumbing, implement once" home
                                         for the W+ lockable-access-type
                                         primitive this register drives
                                         -- Table 23's EP_ID_config rows
                                         and Table 24's STREAM_UID/
                                         flush_on_count/Flush_time
                                         registers are ALSO R/W+ and need
                                         the identical lock check, so
                                         building it once there (not
                                         redundantly here, ahead of that
                                         work) is this codebase's own
                                         established sequencing choice
                                         (issue #200's own suggested
                                         order: Group 1 before Group 3). */
    uint16_t svr_responder_mem_size; /* REQ-RMAP-027 (TC18 §12.7.5 Table
                                         20, absolute address 0x0010, 16
                                         bit, R): maximum responder-queue
                                         memory, in 32-bit words -- same
                                         "caller converts, this field
                                         holds the register's own raw
                                         quadlet count" convention as
                                         respqueue.h's own
                                         capacity_octets/queue_size
                                         (REQ-RMAP-059). Replaces this
                                         struct's former undifferentiated
                                         32-bit svr_memory_capacity,
                                         which conflated two distinct
                                         TC18 registers into one
                                         unaddressed field. Content
                                         modeling only, same REQ-RMAP-024
                                         wire-reachability boundary as
                                         every other Group 1 item. */
    uint16_t svr_req_mem_size; /* REQ-RMAP-027 (TC18 §12.7.5 Table 20,
                                   absolute address 0x0012, 16 bit, R):
                                   maximum memory for EP request queues,
                                   in 32-bit words. Same scope as
                                   svr_responder_mem_size above. */
    uint8_t  svr_implemented_options; /* REQ-RMAP-030: RCP_REGMAP_OPT_*
                                          bitmask, 8 bit on the wire --
                                          see this field's own dedicated
                                          section above for the full
                                          primary-source-verified bit
                                          layout and history. */
    uint8_t  reserved_0x17; /* REQ-RMAP-031 (TC18 §12.7.5 Table 20,
                                absolute address 0x0017, 8 bit): reserved
                                for future use; must read 0x00. Explicitly
                                modeled -- not merely absent -- so intent
                                is documented (this octet is deliberately
                                unused, not forgotten) and so a future
                                wire-dispatch implementation of
                                REQ-RMAP-024 has a concrete field to write
                                zero from, rather than depending on
                                whatever generic buffer zero-fill happens
                                to exist at that point in the encoder.
                                Zero-inits for free via the existing
                                memset in rcp_regmap_general_init(); no
                                setter is provided anywhere in this
                                codebase, so a caller cannot construct a
                                nonzero value here in the first place --
                                the only way this field could ever read
                                nonzero is a direct out-of-convention
                                struct-literal assignment, which
                                populated_map() (tests/
                                test_tc18_gaps_regmap.c) deliberately
                                does NOT do, unlike every other field in
                                that helper. Content modeling only, same
                                REQ-RMAP-024 wire-reachability boundary as
                                every other Group 1 item. */
    uint16_t svr_io_pin_count; /* REQ-RMAP-032 (TC18 §12.7.5 Table 20,
                                   absolute address 0x0018, 16 bit, R):
                                   number of assignable I/O pins --
                                   §12.7.6's own authoritative source for
                                   how many IO-pin entries the HW_config
                                   table (Table 21) contains. Nothing in
                                   this codebase currently allocates or
                                   bounds a real HW_config table against
                                   this count (that is Group 2's own
                                   still-open scope, issue #200 items
                                   -040 through -045); this field is
                                   content modeling only, giving a future
                                   HW_config implementation a place to
                                   read/write the count from, same
                                   REQ-RMAP-024 wire-reachability boundary
                                   as every other Group 1 item. */
    uint16_t svr_root_client_index;   /* RCP_REGMAP_NO_ROOT_CLIENT if unset */

    uint16_t svr_hw_cfg_ptr; /* REQ-RMAP-033 (TC18 §12.7.5 Table 20,
                                 absolute address 0x001A, 16 bit, R): the
                                 address of the HW_config register map
                                 (§12.7.6). Retyped from this field's own
                                 former shape (rcp_regmap_table_ref_t, the
                                 shared pointer/capacity-pair type every
                                 other sub-table ref below still uses) to
                                 a bare 16-bit register-address value,
                                 matching TC18's own definition exactly:
                                 svr_hw_cfg_ptr is a LONE pointer with no
                                 adjacent capacity register of its own --
                                 unlike the sub-table refs below, whose
                                 own deviation (REQ-RMAP-034) is a
                                 different shape mismatch (separate TC18
                                 ptr+capacity REGISTER pairs, collapsed
                                 into one struct), HW_config's extent
                                 comes from a wholly separate, already-
                                 modeled register (svr_io_pin_count,
                                 REQ-RMAP-032) instead of a capacity
                                 field bundled with this pointer at all.
                                 The former rcp_regmap_table_ref_t shape
                                 carried two compounding problems this
                                 retype fixes together: (1) a spurious
                                 capacity member with no TC18 basis for
                                 THIS register, inviting a second,
                                 contradictory source of truth for the
                                 table's length alongside svr_io_pin_
                                 count; (2) an offset expressed in this
                                 project's own "register word" unit
                                 (32 bit) rather than TC18's own 16-bit
                                 register-map address, which could not
                                 be encoded into its real wire slot
                                 without an unwritten unit conversion.
                                 Content modeling only, same REQ-RMAP-024
                                 wire-reachability boundary as every
                                 other Group 1 item -- and, like every
                                 Group 1 item whose real HW_config table
                                 storage does not exist yet (REQ-RMAP-040
                                 through -045, Group 2), this pointer has
                                 nothing real to point AT yet either;
                                 that remains separately open. */
    uint8_t  svr_request_stream_cfg_capacity; /* REQ-RMAP-034 (TC18
                                §12.7.5 Table 20, absolute address
                                0x001C, 8 bit, R): number of usable
                                entries in the request-stream config
                                table (§12.7.7 Table 24). */
    uint8_t  svr_response_stream_cfg_capacity; /* REQ-RMAP-034 (TC18
                                §12.7.5 Table 20, absolute address
                                0x001D, 8 bit, R): number of usable
                                entries in the response/ack-stream
                                config table (§12.7.9 Table 24). */
    uint16_t svr_request_stream_cfg_ptr; /* REQ-RMAP-034 (TC18 §12.7.5
                                Table 20, absolute address 0x001E, 16
                                bit, R): the address of the
                                request-stream config table. */
    uint16_t svr_response_stream_cfg_ptr; /* REQ-RMAP-034 (TC18 §12.7.5
                                Table 20, absolute address 0x0020, 16
                                bit, R): the address of the
                                response/ack-stream config table.

                                These four fields replace this struct's
                                former request_stream_cfg/response_
                                queue_cfg pair (each an
                                rcp_regmap_table_ref_t, the shared
                                pointer/capacity type most sub-table
                                refs below still use), matching the
                                same class of shape mismatch
                                REQ-RMAP-033 fixed for svr_hw_cfg_ptr,
                                but affecting two fields at once rather
                                than one: TC18 defines FOUR separate,
                                non-adjacent 8/16-bit registers here
                                (0x001C/0x001D/0x001E/0x0020), not two
                                structs each bundling a 32-bit
                                "register word" offset with a 16-bit
                                capacity. A 16-bit capacity could
                                represent a value (e.g. 0x0100) TC18's
                                real 8-bit svr_request_stream_cfg_
                                capacity register could never hold, and
                                neither former field's offset could be
                                encoded into its real 16-bit wire slot
                                without an unwritten unit conversion --
                                the same two compounding problems
                                REQ-RMAP-033 already fixed for
                                svr_hw_cfg_ptr, here doubled across two
                                stream directions. Content modeling
                                only, same REQ-RMAP-024 wire-
                                reachability boundary as every other
                                Group 1 item. */
    uint16_t reserved_0x22; /* REQ-RMAP-035 (TC18 §12.7.5 Table 20,
                                absolute address 0x0022, 16 bit):
                                reserved for future use; must read
                                0x00. Explicitly modeled -- not merely
                                absent -- for the same reasons
                                reserved_0x17 (REQ-RMAP-031) is: it
                                documents this span as deliberately
                                unused, not forgotten, and gives a
                                future REQ-RMAP-024 wire-dispatch
                                implementation a concrete field to
                                write zero from. Zero-inits for free
                                via the existing memset in
                                rcp_regmap_general_init(); no setter is
                                provided anywhere in this codebase, so
                                a caller cannot construct a nonzero
                                value here in the first place --
                                populated_map() (tests/
                                test_tc18_gaps_regmap.c) deliberately
                                does NOT set it, same convention as
                                reserved_0x17. Content modeling only,
                                same REQ-RMAP-024 wire-reachability
                                boundary as every other Group 1 item. */
    uint16_t svr_ep_generic_cfg_ptr; /* REQ-RMAP-036 (TC18 §12.7.5 Table
                                20, absolute address 0x0024, 16 bit,
                                R): the address of the EP_config
                                register map (§13.2, generic part of
                                the endpoint register map). Retyped
                                from this field's own former shape
                                (rcp_regmap_table_ref_t) to a bare
                                16-bit register-address value, same
                                class of fix as REQ-RMAP-033/-034. */
    uint16_t svr_ep_generic_cfg_capacity; /* REQ-RMAP-036 (TC18
                                §12.7.5 Table 20, absolute address
                                0x0026, 16 bit, R): the LENGTH OF THE
                                EP CONFIG REGISTER SECTION IN BYTES --
                                verified directly against the primary-
                                source PDF (Table 20, page 52 of
                                OA_TC18_specification_v_0.5.1_RC.pdf).
                                A byte length, NOT an entry count --
                                the exact opposite of what this
                                field's former shared-type home
                                (rcp_regmap_table_ref_t.capacity)
                                documented for every field that used
                                it. Deliberately its own distinct
                                16-bit scalar (not reused from the
                                shared type) so this unit distinction
                                is structurally, not just textually,
                                enforced -- a future caller cannot
                                accidentally treat this value as an
                                entry count the way the former shared
                                field's own documentation would have
                                invited. Content modeling only, same
                                REQ-RMAP-024 wire-reachability boundary
                                as every other Group 1 item. */
    uint16_t svr_ep_functional_cfg_ptr; /* REQ-RMAP-038 (TC18 §12.7.5
                                Table 20, absolute address 0x002C, 16
                                bit, R): the address of the EP_FUNC_
                                config register map (§13.7.1.2 Server)
                                -- the section §12.7.1's configuration
                                request exists to write. A LONE
                                pointer, same as svr_hw_cfg_ptr
                                (REQ-RMAP-033): TC18 defines no
                                adjacent capacity register for this
                                one either -- verified directly against
                                the primary-source PDF (Table 20, page
                                52) during REQ-RMAP-036's own batch.
                                Retyped from this field's own former
                                shape (rcp_regmap_table_ref_t) to a
                                bare 16-bit register-address value,
                                dropping the spurious capacity member
                                the same way REQ-RMAP-033 already did.
                                Content modeling only, same
                                REQ-RMAP-024 wire-reachability boundary
                                as every other Group 1 item. */
    uint16_t svr_ep_bytebus_id_map_ptr; /* REQ-RMAP-037 (TC18 §12.7.5
                                Table 20, absolute address 0x0028, 16
                                bit, R): the address of the EP -
                                byte_bus_id mapping table (§12.7.8) --
                                the table an RC Client needs before it
                                may address an endpoint directly. */
    uint8_t  svr_ep_bytebus_id_map_capacity; /* REQ-RMAP-037 (TC18
                                §12.7.5 Table 20, absolute address
                                0x002A, 8 bit, R): max number of
                                entries in the EP - byte_bus_id map
                                table -- verified directly against the
                                primary-source PDF (Table 20, page 52
                                of OA_TC18_specification_v_0.5.1_RC.pdf)
                                during REQ-RMAP-036's own batch. An
                                entry COUNT, matching this struct's own
                                rcp_regmap_table_ref_t.capacity
                                convention (unlike REQ-RMAP-036's
                                svr_ep_generic_cfg_capacity, which is a
                                byte length) -- this is purely the
                                width/address class of fix
                                REQ-RMAP-033/-034 already established,
                                not a semantic contradiction. Retyped
                                from this field's own former shape
                                (rcp_regmap_table_ref_t) to bare
                                16-bit/8-bit scalars matching TC18's
                                own widths exactly. Content modeling
                                only, same REQ-RMAP-024
                                wire-reachability boundary as every
                                other Group 1 item. */
    uint16_t svr_sequencer_state_ptr; /* REQ-RMAP-038 (TC18 §12.7.5
                                Table 20, absolute address 0x002E, 16
                                bit, R): the address of the Sequencer_
                                config register map (§12.7.10). Also a
                                LONE pointer, same shape as
                                svr_ep_functional_cfg_ptr above and
                                svr_hw_cfg_ptr (REQ-RMAP-033) -- no
                                adjacent capacity register defined by
                                TC18. Retyped from this field's own
                                former shape (rcp_regmap_table_ref_t)
                                the same way. Content modeling only,
                                same REQ-RMAP-024 wire-reachability
                                boundary as every other Group 1 item. */
    uint16_t svr_network_interface_cfg_ptr; /* REQ-RMAP-039 (TC18
                                §12.7.5 Table 20 continued, relative
                                address 0x0030, 16 bit, R): the address
                                of the Network_config register map
                                (§12.7.11). Verified directly against
                                the primary-source PDF (Table 20, page
                                53 of OA_TC18_specification_v_0.5.1_
                                RC.pdf) during REQ-RMAP-036's own batch,
                                re-confirmed by reading page 53 a second
                                time for this batch specifically. TC18's
                                own "Absolute address" column is BLANK
                                for every register on this continuation
                                page -- a genuine gap in the primary
                                source itself, not an extraction
                                failure -- so 0x0030 (this field) through
                                0x003E (svr_security_cfg_capacity below)
                                are INFERRED, not directly read: derived
                                from the visible 0x0030 marker
                                terminating the prior page's table (the
                                natural next address after
                                svr_sequencer_state_ptr's own 16-bit
                                register ending at 0x002F) plus this
                                whole table's consistent, gap-free,
                                sequential-address convention (every
                                other register in Table 20, without
                                exception, sits at offset = previous
                                register's own offset + previous
                                register's own width) -- flagged
                                honestly as inferred, not assumed to be
                                risk-free.

                                These eight fields (four ptr/capacity
                                pairs -- network interface, physical
                                layer, time synch, security) are the
                                LAST Group 1 item (issue #200):
                                rcp_regmap_general_t previously declared
                                none of them at all, so a c-RCP server
                                could not advertise -- and a c-RCP
                                client could not discover -- whether any
                                of these four optional subsystems is
                                present, including the "not supported"
                                answer TC18 requires be expressible as
                                a zero pointer for three of the four
                                (see each pointer's own comment below).
                                Unlike every other Group 1 batch since
                                REQ-RMAP-033, these are genuinely NEW
                                fields, not a retype of an existing one
                                -- rcp_regmap_table_ref_t (already
                                unused by this struct as of
                                REQ-RMAP-038) is deliberately NOT reused
                                here either, since each pair's ptr/
                                capacity are two independently-
                                addressed 16-bit TC18 registers, the
                                same shape every other Group 1 pair this
                                phase has fixed already uses. Content
                                modeling only, same REQ-RMAP-024
                                wire-reachability boundary as every
                                other Group 1 item. */
    uint16_t svr_network_interface_cfg_capacity; /* REQ-RMAP-039 (TC18
                                §12.7.5 Table 20 continued, INFERRED
                                absolute address 0x0032, 16 bit, R):
                                the Network_config register map's own
                                capacity. Unlike the three pairs below,
                                TC18's own table gives no explicit "0
                                means not supported" note for this
                                specific pointer -- flagged honestly
                                rather than assumed to match the other
                                three by analogy. */
    uint16_t svr_physical_layer_cfg_ptr; /* REQ-RMAP-039 (TC18 §12.7.5
                                Table 20 continued, INFERRED relative
                                address 0x0034, 16 bit, R): the address
                                of the physical-layer configuration
                                register map (§12.7.12). A pointer
                                value of 0 is TC18's own defined
                                encoding for "physical layer
                                configuration is not supported". */
    uint16_t svr_physical_layer_cfg_capacity; /* REQ-RMAP-039 (TC18
                                §12.7.5 Table 20 continued, INFERRED
                                absolute address 0x0036, 16 bit, R):
                                the physical-layer configuration
                                register map's own capacity. */
    uint16_t svr_time_synch_cfg_ptr; /* REQ-RMAP-039 (TC18 §12.7.5
                                Table 20 continued, INFERRED relative
                                address 0x0038, 16 bit, R): the address
                                of the PTP_config register map
                                (§12.7.13). A pointer value of 0 is
                                TC18's own defined encoding for "time
                                synch is not supported". */
    uint16_t svr_time_synch_cfg_capacity; /* REQ-RMAP-039 (TC18
                                §12.7.5 Table 20 continued, INFERRED
                                absolute address 0x003A, 16 bit, R):
                                the PTP_config register map's own
                                capacity. */
    uint16_t svr_security_cfg_ptr; /* REQ-RMAP-039 (TC18 §12.7.5 Table
                                20 continued, INFERRED absolute address
                                0x003C, 16 bit, R): the address of the
                                security configuration register map
                                (§12.7.14). A pointer value of 0 is
                                TC18's own defined encoding for
                                "security is not supported". */
    uint16_t svr_security_cfg_capacity; /* REQ-RMAP-039 (TC18 §12.7.5
                                Table 20 continued, INFERRED relative
                                address 0x003E, 16 bit, R): the
                                security configuration register map's
                                own capacity -- see svr_network_
                                interface_cfg_ptr's own comment above
                                for the full explanation of why every
                                address in this group is inferred
                                rather than directly read. */
} rcp_regmap_general_t;

/* Zero-initializes every field of map except svr_root_client_index, which
 * is set to RCP_REGMAP_NO_ROOT_CLIENT (no root client granted yet -- the
 * correct default while the server is still HW_UNCONFIGURED). */
void rcp_regmap_general_init(rcp_regmap_general_t *map);

/* ── Table 20 wire codec (REQ-RMAP-024) ──────────────────────────────────────
 *
 * Every field above is documented against its own TC18 §12.7.5 Table 20
 * absolute address (each field's own comment cites it), but until this
 * section, nothing in this codebase actually serialized that address
 * layout onto the wire -- rcp_discovery_encode_response()/_decode_response()
 * (discovery.h) only ever populate/parse a hardcoded 14-octet, 5-field
 * slice (RCP_DISCOVERY_GENERAL_SLICE_LEN), even when a real read_size asks
 * for more. The functions below close that gap: they are the SAME wire
 * mechanism discovery already uses (a plain ACF_ABB read addressed to
 * byte_bus_id 0 / EP0, response = however many octets of the register map
 * starting at absolute address 0 the requester's read_size asked for --
 * confirmed directly against the primary source, §12.7's own text:
 * "Access to the configuration and status information is per ABB or GBB
 * messages... explained in the endpoint section for... RC Server, since
 * the RC Server exposes itself as endpoint0 (EP0)"), just generalized to
 * serve this struct's real, full Table 20 extent instead of the discovery
 * handshake's own deliberately narrow 5-field identity slice.
 * rcp_discovery_encode_request()/_decode_request() are reused unchanged
 * for the request side -- a "read the general register map" request and a
 * discovery request are, per the primary source, the identical wire
 * message; only how much of the response a caller actually inspects
 * differs. discovery.h's own narrower 5-field API is untouched by this
 * section and remains the right choice for a caller that only needs the
 * discovery handshake's own identity fields.
 *
 * CORRECTED 2026-08-11 (Phase 5d/RMAP addressing investigation,
 * user-directed): every prior citation in this struct's own field
 * comments said "relative address" -- WRONG. Direct verification against
 * the current RC5 baseline PDF (OA_TC18_specification_v_0.5.1_RC_5_3624.pdf,
 * page 61) shows Table 20's own address column is explicitly headed
 * "Absolute address", not "Relative Address". This is not a cosmetic
 * word choice: it is TC18's own confirmation that every register in
 * Table 20 -- including every "_ptr" field (svr_hw_cfg_ptr,
 * svr_ep_bytebus_id_map_ptr, svr_response_stream_cfg_ptr,
 * svr_ep_functional_cfg_ptr, svr_sequencer_state_ptr) -- shares ONE
 * continuous address space scoped to EP0/byte_bus_id=0, not five
 * independent, disconnected addressing domains. A pointer field's own
 * VALUE is therefore itself an absolute address in that SAME space: to
 * reach the table a pointer names, a client issues an ordinary ACF_ABB
 * read/write to byte_bus_id=0 with the request's own start_address set
 * to that pointer's current value, not address 0. This directly answers
 * the "how does a client's request address relate to the pointer's
 * value" question every one of REQ-RMAP-040/041 (HW_config),
 * REQ-RMAP-052/054 (EP_ID_config), REQ-RMAP-061/065 (response-queue
 * config), and REQ-RMAP-023/067 (Table 33/36 -- though that table's own
 * SEPARATE address-collision defect, documented in its own section
 * below, is unaffected by this finding and stays genuinely unresolved)
 * had previously left open. See rcp_regmap_ep0_decode_write_request()
 * below, the generalized, address-routed WRITE dispatcher this finding
 * unblocks -- the read-side counterpart (an
 * rcp_regmap_ep0_encode_read_response(), address-routed the same way)
 * remains a separate, still-open piece of this same finding, not yet
 * built (issue #301). */

/* Total wire length (bytes) of the general register map's TC18 §12.7.5
 * Table 20 extent, absolute address 0x0000 through 0x003F inclusive --
 * see rcp_regmap_general_render()'s own doc comment for exactly which
 * struct fields this covers and which two it deliberately excludes. */
#define RCP_REGMAP_GENERAL_LEN ((size_t)0x0040u)

/* Serializes map's Table 20 fields into out[0..RCP_REGMAP_GENERAL_LEN) at
 * each field's own TC18-documented absolute address (this struct's own
 * field comments above cite each one individually).
 *
 * Deliberately excludes svr_lifecycle_state and svr_root_client_index:
 * neither has a genuine Table 20 address -- both are this struct's own
 * convenience placement of what TC18 §13.7.1.2 Table 33 (the RC Server's
 * own EP_func block, reached via a *different*, pointer-addressed
 * mechanism -- svr_ep_functional_cfg_ptr, §12.7.1's configuration
 * request) actually owns. Table 20's own address sequence has no room for
 * either field at all (0x000C's 16-bit svr_ep_count runs directly into
 * 0x000E's svr_req_stream_max; 0x0018's 16-bit svr_io_pin_count runs
 * directly into 0x001A's svr_hw_cfg_ptr) -- confirmed directly against
 * the rendered primary-source PDF, not assumed. Rendering either field
 * into this Table 20 image at an invented address would be a real
 * conformance defect, not a harmless placeholder, so this function does
 * not attempt it: REQ-RMAP-023's own gap (svr_lifecycle_state) stays open
 * until Table 33's own wire codec (issue #200 Group 5, still
 * unimplemented) exists to give it -- and svr_root_client_index -- a real
 * home.
 *
 * The one-byte gap at absolute address 0x002B (between
 * svr_ep_bytebus_id_map_capacity's own 0x002A and svr_ep_functional_cfg_
 * ptr's own 0x002C) is written as 0x00: TC18's own table has no explicit
 * "reserved" row there the way 0x0017 and 0x0022 both do -- so this is an
 * unconfirmed, inferred single-octet alignment gap, not a directly-cited
 * reserved register, flagged here rather than silently assumed
 * risk-free. */
void rcp_regmap_general_render(const rcp_regmap_general_t *map,
                                uint8_t out[RCP_REGMAP_GENERAL_LEN]);

typedef enum {
    RCP_REGMAP_GENERAL_OK               = 0,
    RCP_REGMAP_GENERAL_ERR_SHORT_FRAME  = 1,
    RCP_REGMAP_GENERAL_ERR_BAD_MSG_TYPE = 2,
    RCP_REGMAP_GENERAL_ERR_WRONG_BUS    = 3,
    RCP_REGMAP_GENERAL_ERR_WRONG_OP     = 4,
} rcp_regmap_general_errc_t;

/* Human-readable message for a rcp_regmap_general_errc_t value. Never
 * returns NULL. */
const char *rcp_regmap_general_strerror(rcp_regmap_general_errc_t e);

/* Encodes an ACF_ABB read RESPONSE addressed to byte_bus_id 0 (EP0),
 * carrying min(read_size, RCP_REGMAP_GENERAL_LEN) octets of map's own
 * rcp_regmap_general_render() image starting at absolute address 0, with
 * any remaining requested octets (up to read_size) zero-filled -- the
 * exact same "response spans exactly read_size octets" convention
 * rcp_discovery_encode_response() already establishes for its own
 * narrower slice. Returns a zeroed rcp_bytes_t (data=NULL) on allocation
 * failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_regmap_general_encode_read_response(const rcp_regmap_general_t *map,
                                                      uint8_t read_size,
                                                      uint8_t transaction_num);

/* Decodes and validates an ACF_ABB general-register-map read RESPONSE
 * from b[0..len). On RCP_REGMAP_GENERAL_OK, *out_map has every Table 20
 * field rcp_regmap_general_render() populates overwritten from whichever
 * prefix of RCP_REGMAP_GENERAL_LEN the response payload actually carries
 * -- a short response (fewer octets than the full extent) leaves the
 * remaining, un-carried fields of *out_map untouched, so a caller must
 * rcp_regmap_general_init() (or otherwise define) *out_map before calling
 * this, the same partial-population convention every register-block
 * apply_reconfig() in this codebase already follows. svr_lifecycle_state
 * and svr_root_client_index are never touched, for the same reason
 * rcp_regmap_general_render() never renders them. Fails with
 * ..._ERR_SHORT_FRAME if b is shorter than the ACF_ABB fixed header or
 * its declared payload length; ..._ERR_BAD_MSG_TYPE if b is not an
 * ACF_ABB message; ..._ERR_WRONG_BUS if its byte_bus_id is not EP0;
 * ..._ERR_WRONG_OP if its op is not RCP_ACF_OP_READ. */
rcp_regmap_general_errc_t rcp_regmap_general_decode_read_response(const uint8_t *b, size_t len,
                                                                    rcp_regmap_general_t *out_map);

/* Decodes an ACF_ABB WRITE request from b[0..len) addressed to byte_bus_id
 * 0 (EP0) targeting the Table 20 general register map. Every one of
 * Table 20's registers is TC18-defined access type R (REQ-RMAP-025), so
 * this never applies a write -- it exists only to recognize a genuine
 * write attempt and report the correct wire error for it. On
 * RCP_REGMAP_GENERAL_OK, *out_error is always RCP_ERROR_LOCKED_MEM_ACCESS
 * (derived via rcp_lifecycle_field_write_error() against
 * RCP_LIFECYCLE_FIELD_READ_ONLY -- reusing, not duplicating, that
 * already-proven primitive) and *out_transaction_num is populated; the
 * caller builds the actual error response via
 * rcp_acf_build_error_response(byte_bus_id, *out_transaction_num,
 * *out_error). Fails with the same ACF-level errc values as
 * rcp_regmap_general_decode_read_response() (short frame / bad msg type /
 * wrong bus) if the frame itself is malformed before authorization is
 * even reached, or ..._ERR_WRONG_OP if op is not RCP_ACF_OP_WRITE. */
rcp_regmap_general_errc_t rcp_regmap_general_decode_write_request(const uint8_t *b, size_t len,
                                                                    rcp_wire_error_t *out_error,
                                                                    uint8_t *out_transaction_num);

/* ── Root-client / per-EP-restricted-client model ──────────────────────────── */

/* One endpoint's write-restriction: the single stream (if any) authorized
 * to write that endpoint's functional config directly through its own
 * registered request stream, per server.h's rcp_lifecycle_writer_ctx_t
 * via_owning_stream member. has_owning_stream distinguishes "no owning
 * stream configured" from stream index 0, which is itself a valid index. */
typedef struct {
    bool     has_owning_stream;
    uint16_t owning_stream_index;
} rcp_regmap_ep_client_t;

/* Derives an rcp_lifecycle_writer_ctx_t (server.h) from this register map's
 * root-client/owning-stream data for a request arriving on
 * requesting_stream_index. via_ep0 must be true iff the request actually
 * arrived through EP0 (RCP_REGMAP_EP0_INDEX); via_unicast must be true
 * iff the request's frame had a unicast destination MAC (see l2.h's
 * rcp_l2_mac_is_unicast() for the primitive that classifies one);
 * via_discovery_stream must be true iff the request arrived via the
 * discovery stream (REQ-LIFECYCLE-030/031/036) -- this function does not
 * re-derive any of the three from an address or stream role itself,
 * matching acf.h/avtp.h's convention of taking already-classified
 * inputs rather than re-parsing a frame. ep_client may be NULL, meaning
 * "this endpoint has no owning stream on record" (via_owning_stream is
 * then always false). REQ-RMAP-009/070: every member of the returned
 * ctx is explicitly assigned by this function -- none are left
 * uninitialized, since this struct's caller passes it straight into
 * ASIL-B write-authorization decisions (rcp_lifecycle_field_writable()/
 * rcp_lifecycle_transition()) that read every member. */
rcp_lifecycle_writer_ctx_t rcp_regmap_writer_ctx(const rcp_regmap_general_t *map,
                                               const rcp_regmap_ep_client_t *ep_client,
                                               uint16_t requesting_stream_index,
                                               bool via_ep0,
                                               bool via_unicast,
                                               bool via_discovery_stream);

/* ── RC Server's own functional-configuration content (TC18 §13.7.1.2) ─────
 *
 * TC18 §13.7.1.2 ("Server functional configuration") describes a table
 * numbered "Table 33: RC Server functional configuration" in the RC1
 * baseline (OA_TC18_specification_v_0.5.1_RC.pdf, p.81) and renumbered
 * "Table 36" in the current RC5 baseline this project treats as
 * authoritative (OA_TC18_specification_v_0.5.1_RC_5_3624.pdf, p.91,
 * table renumbered upward by RC5's own inserted content -- confirmed by
 * direct PDF page-image reads of BOTH revisions, not `pdftotext`
 * extraction, per this codebase's established "verify against the page
 * image" discipline -- see REQ-CANEP-029's own doc comment (ep_can.h)
 * for the earlier, sibling case this reuses the same discipline from).
 *
 * TWO SEPARATE, GENUINE PRIMARY-SOURCE DEFECTS were confirmed present in
 * BOTH revisions (not a transient RC1-only issue later fixed), neither
 * force-resolved here -- documented, not guessed:
 *
 *   1. ADDRESS COLLISION: the table lists 8 register rows sharing only
 *      5 distinct relative addresses. 0x0002 is assigned to BOTH
 *      svr_ep_enable&clr (an EP_FUNC-common-entries-style register,
 *      §12.7.1/Table 32-or-35's own generic header) AND
 *      svr_root_client_index (an RC-Server-specific register); 0x0003
 *      likewise to BOTH svr_ep_options AND svr_lifecycle_state; 0x0004
 *      to BOTH svr_discovery_timeout AND svr_ep_status. Same class of
 *      defect as CAN's Table 53/56 (ep_can.h's own file header) and the
 *      RC-server rx_enforce_* terminology drift (respqueue.h's own
 *      §12.7.7 history) -- a real, uncorrected spec table, not a
 *      rendering artifact of either PDF revision.
 *
 *      WORKING HYPOTHESIS, NOT CONFIRMED, NOT CODED AS FACT: every other
 *      endpoint type's own EP_FUNC block places its type-specific fields
 *      immediately after the shared 4-octet common header (i.e. at
 *      0x0004 onward, never overlapping 0x0000-0x0003 -- see every
 *      concrete ep_*.h's own functional-config layout). The
 *      RC-Server-specific block's own four rows (svr_root_client_index/
 *      svr_lifecycle_state/svr_discovery_timeout/svr_ep_status) read as
 *      though their printed addresses (0x0002-0x0004) should instead
 *      read 0x0004 onward -- an off-by-4, forgot-the-common-header-
 *      offset authoring error consistent with every sibling endpoint's
 *      own layout. This is a plausible reconciliation, not a stated
 *      TC18 fact; a future investigation with a stronger primary-source
 *      basis (e.g. a later spec erratum) should confirm or refute it
 *      before any code treats it as settled.
 *
 *   2. EP_FUNC-BLOCK EXISTENCE CONTRADICTS ITSELF: §13.7.1.1's own prose,
 *      immediately preceding Table 33/36, states plainly: "the RC Server
 *      as endpoint is not included in the EP_FUNC_config register maps"
 *      -- yet Table 33/36 itself lists the generic EP_FUNC common-header
 *      fields (svr_ep_len/reserved/svr_ep_enable&clr/svr_ep_options,
 *      the same four fields rcp_regmap_ep_functional_cfg_t already
 *      models generically for every OTHER endpoint type) for the RC
 *      Server anyway, directly contradicting the sentence that
 *      immediately precedes the table. Given this codebase already has
 *      no evidence the RC Server is dispatched through the generic
 *      per-endpoint EP_FUNC mechanism anywhere (server.c never composes
 *      an rcp_regmap_ep_functional_cfg_t for the server itself), this
 *      codebase does NOT model those four common-header fields for the
 *      RC Server -- modeling them would require silently picking a side
 *      of a stated, unresolved spec self-contradiction. rcp_regmap_svr_ep_cfg_t
 *      below models ONLY the two fields free of both defects above:
 *      svr_discovery_timeout and svr_ep_status -- CORRECTION (was wrong
 *      until this fix, caught investigating REQ-RMAP-023): svr_root_
 *      client_index/svr_lifecycle_state are NOT "already correctly
 *      modeled at their own, uncontested Table 20 addresses" -- see
 *      rcp_regmap_general_render()'s own doc comment above (near
 *      RCP_REGMAP_GENERAL_LEN): Table 20 has no address for either
 *      field at all, and rcp_regmap_general_t only carries them as an
 *      in-process convenience placement, deliberately excluded from
 *      that struct's own Table 20 wire image (REQ-RMAP-023's own gap
 *      is exactly this -- it stays open, not closed elsewhere). This
 *      struct still doesn't duplicate them here, but the real reason is
 *      that doing so would just be a second, no-better-justified guess
 *      at their true Table 33/36 address on top of contradiction #2
 *      above, not that a correct home already exists for them
 *      elsewhere. */

/* REQ-RMAP-066/067 (content-modeling scope only -- see the section note
 * above for what is deliberately excluded and why). svr_discovery_timeout
 * is TC18's own Discovery_TimeOut: how long an unused discovery stream
 * is kept before being dropped (TC18 §12.6); svr_ep_status is the RC
 * Server's own 16-bit status register, TC18 giving no further bit-level
 * breakdown for it at this citation. */
typedef struct {
    uint16_t svr_discovery_timeout; /* REQ-RMAP-066: microseconds; TC18's
                                        own stated default is 20000 (20
                                        ms) -- see
                                        rcp_regmap_svr_ep_cfg_init(). */
    uint16_t svr_ep_status;         /* REQ-RMAP-067 */
} rcp_regmap_svr_ep_cfg_t;

/* Sets svr_discovery_timeout to TC18's own stated power-on default,
 * 20000 (20 ms in microseconds); svr_ep_status to 0. */
void rcp_regmap_svr_ep_cfg_init(rcp_regmap_svr_ep_cfg_t *cfg);

/* ── The generic-vs-functional per-endpoint config split ───────────────────── */

/* Server-owned generic per-endpoint config: fields a client's functional
 * configuration is never allowed to touch, however write-access to the
 * functional block below evolves as the server's lifecycle state changes. */
typedef struct {
    uint8_t  ep_type;             /* concrete meaning assigned by each
                                      endpoint type added in Phase 16/19 */
    bool     ep_used;
    uint32_t ep_delay_time;       /* microseconds. TC18 §13.2 Table 28/31
                                      (RC1 pp.71-72, RC5 Table 31 pp.82-83,
                                      identical content on both revisions)
                                      wires this as a packed 2-bit enum at
                                      relative address 0x0001.4:5 restricted
                                      to exactly {1, 10, 20, 50} microseconds
                                      -- this field's own internal
                                      representation deliberately stays a
                                      free microsecond value rather than the
                                      register's own enum, since it is
                                      consumed as a scheduling tick unit
                                      across request_chained.h/
                                      request_triggered.h/request_compound.h/
                                      server.c, none of which should need to
                                      know about the register's own packed
                                      encoding. A wire codec (issue #311,
                                      not yet built) will need a boundary
                                      conversion pair that REJECTS a value
                                      outside the 4 allowed microsecond
                                      values on write, not silently rounds
                                      it -- this is a real R/W* configuration
                                      input, not a saturating-is-safe case
                                      like rx_wd_timeout_ms/flush_time_us. */
    uint32_t ep_req_storage_size; /* octets of request-payload storage
                                      reserved for this endpoint. TC18's own
                                      register (0x0002, 16 bit, R/W*) is in
                                      32-bit WORDS, not octets -- widened
                                      2026-08-11 (issue #311 batch 2) from
                                      uint16_t: the register's own maximum
                                      representable value, 65535 words, is
                                      262140 octets, which does not fit a
                                      16-bit octet count -- this was a real
                                      correctness gap independent of any
                                      wire codec, not merely a units
                                      mismatch. rcp_regmap_ep_req_storage_
                                      size_words_to_octets()/_octets_to_words()
                                      (regmap.h/.c) perform the boundary
                                      conversion the future wire codec
                                      needs; octets_to_words() rejects an
                                      octet count that is not a multiple of
                                      4 or whose word count would not fit
                                      the register's own 16-bit width. */
    uint32_t ep_description;      /* REQ-RMAP-073 (TC18 §13.2 Table 28/31,
                                      relative address 0x0004, 32 bit,
                                      R/W*): user-defined description, no
                                      further structure given by TC18.
                                      Content-modeling only (issue #311) --
                                      not yet part of any wire codec. */
    uint16_t ep_tx_buffer_size;   /* REQ-RMAP-074 (TC18 §13.2 Table 28/31,
                                      relative address 0x0008, 16 bit,
                                      R/W*): in 32-bit words, matching
                                      ep_req_storage_size's own unit, not
                                      octets. TC18: "If buffer is
                                      configurable in device can assign a
                                      tx buffer here. If not configurable
                                      buffer size in 32-bit words can be
                                      read. If EP does not have a tx buffer
                                      it reads 0x0000." Content-modeling
                                      only (issue #311). */
    uint16_t ep_rx_buffer_size;   /* REQ-RMAP-075 (TC18 §13.2 Table 28/31,
                                      relative address 0x000A, 16 bit,
                                      R/W*): same shape as ep_tx_buffer_size
                                      above, for the endpoint's rx buffer.
                                      Content-modeling only (issue #311). */
} rcp_regmap_ep_generic_cfg_t;

/* Zero-initializes cfg (ep_used = false, everything else 0). */
void rcp_regmap_ep_generic_cfg_init(rcp_regmap_ep_generic_cfg_t *cfg);

/* ── ep_generic_cfg boundary conversions (issue #311 batch 2) ───────────────
 *
 * Two of ep_generic_cfg's fields have a wire representation this struct's
 * own internal representation deliberately does NOT match (see each
 * field's own comment above for why) -- these functions are the
 * conversion boundary a future wire codec (issue #311's own remaining
 * batches) will call, kept separate from render()/apply_reconfig() so
 * they can be unit-tested independently, the same shape already
 * established by rcp_regmap_wd_timeout_ms_to_ticks()/_ticks_to_ms(). */

/* ep_delay_time (TC18 §13.2 Table 28/31, relative address 0x0001.4:5, 2
 * bit, R/W*) is restricted to exactly 4 register values: 00b=1us,
 * 01b=10us, 10b=20us, 11b=50us -- Table 28/31's own definitive register
 * definition, unambiguous on both PDF revisions. NOTE: TC18's own
 * separate prose (request_compound/_triggered/_chained's own field
 * descriptions, 4 occurrences, both revisions) instead reads "[1us,
 * 20us, 20us, 50us]" -- a duplicated 20us where the table's own 10us
 * belongs, almost certainly a copy-paste typo in TC18's own text (the
 * duplicated-then-propagated value appears identically at all 4 prose
 * sites, while the one authoritative TYPED register table gets it
 * right) -- the table, not the repeated prose, is treated as
 * authoritative here. rcp_regmap_ep_delay_time_us_to_reg()
 * converts a microsecond value into the matching 2-bit register value;
 * returns false (leaving *out_reg unchanged) for any microsecond value
 * NOT exactly one of the 4 allowed ones -- deliberately REJECTS rather
 * than rounds, since this is a real R/W* configuration input, not a
 * saturating-is-safe case like rx_wd_timeout_ms/flush_time_us (a
 * silently-substituted delay would misconfigure the endpoint's own
 * scheduling timing). */
bool rcp_regmap_ep_delay_time_us_to_reg(uint32_t delay_us, uint8_t *out_reg);

/* The inverse conversion: register value -> microseconds. reg is masked
 * to its own 2 bits internally, so any input is well-defined and this
 * can never fail -- all 4 possible 2-bit register values are valid TC18
 * encodings with no reserved/undefined combination. */
uint32_t rcp_regmap_ep_delay_time_reg_to_us(uint8_t reg);

/* ep_req_storage_size (TC18 §13.2 Table 28/31, relative address 0x0002,
 * 16 bit, R/W*) is expressed in 32-bit words on the wire, octets
 * internally (see this struct's own ep_req_storage_size field comment).
 * rcp_regmap_ep_req_storage_size_words_to_octets() is the read-side
 * conversion: always exact, always fits uint32_t (max register value
 * 65535 words = 262140 octets), cannot fail. */
uint32_t rcp_regmap_ep_req_storage_size_words_to_octets(uint16_t words);

/* The write-side inverse: octets -> words. Returns false (leaving
 * *out_words unchanged) if octets is not an exact multiple of 4 (no
 * lossy rounding of a configuration input) or if the resulting word
 * count would not fit the register's own 16-bit width. */
bool rcp_regmap_ep_req_storage_size_octets_to_words(uint32_t octets,
                                                      uint16_t *out_words);

/* ── ep_generic_cfg wire codec, READ side (issue #311 batch 3) ──────────────
 *
 * The write side (apply_reconfig(), issue #311 batch 4, below) was
 * deliberately NOT built in this same batch: TC18 §13.2 Table 28/31 marks
 * ep_type (relative address 0x0000) plain R -- read-only -- unlike every
 * other field in this row (all R/W*). Every other render()/
 * apply_reconfig() pair in this codebase (HW_config, EP_ID_config,
 * response-queue-config, request-stream-cfg) has no read-only field mixed
 * into an otherwise-writable row; ep_generic_cfg is the first, and needed
 * its own dedicated design pass -- see apply_reconfig()'s own doc comment
 * below for what that pass found (a second, more serious problem beyond
 * ep_type's own read-only status). */

/* RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES is not itself a TC18-derived
 * value (TC18 defines no fixed endpoint count) -- matches every sibling
 * table's own identical, not-spec-derived bound
 * (RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES/RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES/
 * RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES/
 * RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES). */
#define RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES ((size_t)64u)

/* Serializes entries[0..count) into out at each row's own TC18-cited
 * 12-octet stride (relative address 12*N onward per endpoint N) --
 * out must have room for at least 12*count octets. len beyond
 * RCP_REGMAP_EP_GENERIC_CFG_MAX_ENTRIES is the caller's own
 * responsibility to have already bounded, matching every sibling
 * render()'s own convention.
 *
 * ep_type (0x0000, R), ep_description (0x0004-0x0007, R/W*),
 * ep_tx_buffer_size (0x0008-0x0009, R/W*), and ep_rx_buffer_size
 * (0x000A-0x000B, R/W*) are all serialized directly -- ep_type is a
 * plain octet passthrough, and the latter three already store their
 * own wire-native unit internally (see each field's own comment,
 * above). ep_used (0x0001 bit 0) and the two reserved 3-bit/2-bit
 * spans either side of ep_delay_time (0x0001 bits 1:3 and 6:7) are
 * packed into the same octet as ep_delay_time below.
 *
 * ep_delay_time (0x0001 bits 4:5) uses rcp_regmap_ep_delay_time_us_to_reg().
 * DEFENSIVE FALLBACK: if the internal microsecond value is not exactly
 * one of TC18's own 4 allowed values, this function does NOT fail or
 * assert (every render() in this codebase is unconditional, matching
 * the established convention) -- it falls back to register value 0
 * (1us, the shortest/smallest valid delay). This is not a rare edge
 * case: rcp_regmap_ep_generic_cfg_init()'s own memset(0) leaves
 * ep_delay_time at 0us, which is NOT one of the 4 allowed values, so
 * every freshly-initialized, not-yet-configured endpoint hits this
 * fallback until something explicitly sets a valid value -- consistent
 * with the "never grant more delay than configured" bias already
 * established by rcp_regmap_wd_timeout_ms_to_ticks()'s own round-down
 * convention (a too-short realized delay is the conservative direction
 * for an execution-timing field, unlike an too-long one).
 *
 * ep_req_storage_size (0x0002-0x0003) uses
 * rcp_regmap_ep_req_storage_size_octets_to_words() where possible;
 * an internal value that is not an exact multiple of 4, or whose word
 * count exceeds the register's own 16-bit width, is defensively
 * clamped down to the nearest representable word count (never
 * rounded up -- the same "never grant more than configured" bias). */
void rcp_regmap_ep_generic_cfg_render(const rcp_regmap_ep_generic_cfg_t *entries,
                                       size_t count, uint8_t *out);

/* ── ep_generic_cfg wire codec, WRITE side (issue #311 batch 4) ─────────────
 *
 * NOT the render()-then-patch-then-reparse-the-whole-buffer idiom every
 * sibling apply_reconfig() (HW_config/EP_ID_config/response-queue-config/
 * request-stream-cfg) uses -- that idiom is safe ONLY because their own
 * render() is a lossless 1:1 round-trip. rcp_regmap_ep_generic_cfg_render()
 * is NOT lossless (its own defensive ep_delay_time fallback and
 * ep_req_storage_size clamp, see that function's own doc comment above):
 * reparsing a whole rendered-then-patched row would silently "launder"
 * any already-invalid field through its own fallback/clamp on every
 * write, even for a row/field the write never touched at all -- a real
 * corruption with no basis in the actual incoming write. Found and
 * documented (issue #311, comment thread) before implementing, not
 * discovered by a failing test after the fact. */
typedef enum {
    RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK              = 0,
    RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_SHORT        = 1, /* data_len == 0 */
    RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_OUT_OF_RANGE = 2, /* relative_start_address
                                                                  + data_len exceeds
                                                                  count*12 */
} rcp_regmap_ep_generic_cfg_reconfig_errc_t;

/* Human-readable message for an rcp_regmap_ep_generic_cfg_reconfig_errc_t
 * value. Never returns NULL. */
const char *
rcp_regmap_ep_generic_cfg_reconfig_strerror(rcp_regmap_ep_generic_cfg_reconfig_errc_t e);

/* Applies an incoming write of data[0..data_len) at relative_start_address
 * to entries[0..count) -- entries[i] is row i's own 12-octet stride,
 * matching rcp_regmap_ep_generic_cfg_render()'s own layout.
 *
 * PER-FIELD, not per-buffer: for each of the 5 writable fields in each
 * row this write's own byte span overlaps (ep_used+ep_delay_time,
 * ep_req_storage_size, ep_description, ep_tx_buffer_size,
 * ep_rx_buffer_size), the field is updated ONLY if the write's own byte
 * span FULLY covers that field's own octet range within the row -- a
 * write that only partially covers a multi-octet field leaves that
 * field entirely unchanged (the same "do not silently corrupt what
 * wasn't fully specified" principle applied to the render()-lossiness
 * problem above, extended to ordinary partial-field writes).
 *
 * ep_type (relative 0x0000 within each row) is NEVER updated, regardless
 * of whether the write's own byte span covers it: TC18 §13.7.1.2
 * (TC18.txt L4039-4040, identical RC1/RC5) states, in general terms,
 * "Writing data to read only registers has no effect and request is
 * confirmed normally" -- ep_type is R, not R/W*, the one read-only
 * field mixed into this otherwise fully-writable row. A write touching
 * ONLY ep_type's own byte (and no other writable field) still returns
 * RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_OK, matching TC18's own "confirmed
 * normally" language -- this is not an error case.
 *
 * Reserved bits within octet 0x0001 (bits 1:3 and 6:7, either side of
 * ep_delay_time) have no corresponding struct field and are never
 * consulted -- only ep_used (bit 0) and ep_delay_time (bits 4:5) are
 * ever extracted from that octet.
 *
 * Returns RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_SHORT if data_len is 0,
 * or RCP_REGMAP_EP_GENERIC_CFG_RECONFIG_ERR_OUT_OF_RANGE if the write's
 * own span exceeds count*12 -- entries is left entirely unchanged in
 * either error case. */
rcp_regmap_ep_generic_cfg_reconfig_errc_t
rcp_regmap_ep_generic_cfg_apply_reconfig(rcp_regmap_ep_generic_cfg_t *entries, size_t count,
                                          uint16_t relative_start_address,
                                          const uint8_t *data, size_t data_len);

/* The functional-config prefix common to every endpoint type. Every
 * concrete endpoint type built in Phase 16/19 composes this struct as its
 * own first member rather than re-declaring these five fields itself --
 * see the file header. */
typedef struct {
    bool ep_enable;
    bool ep_clear_req_storage;
    bool ep_req_crc_enable;
    bool ep_response_ts_enable;
    bool ep_suppress_response; /* the specification's own register name
                                   for this field renders it with a single
                                   "s"; spelled correctly here as this
                                   project's own C identifier, not a wire
                                   encoding */
} rcp_regmap_ep_functional_cfg_t;

/* Zero-initializes cfg (every flag false). */
void rcp_regmap_ep_functional_cfg_init(rcp_regmap_ep_functional_cfg_t *cfg);

/* ── HW pin mapping ─────────────────────────────────────────────────────────── */

/* RCP_REGMAP_PIN_PROP_* below is NOT this struct's own bit layout --
 * see the RCP_REGMAP_HW_PIN_* family further down for hw_pin_type's
 * real one (REQ-RMAP-042/-043). RCP_REGMAP_PIN_PROP_* is ep_gpio.h's
 * own, separate, runtime-adjustable per-pin bitmask (its own doc
 * comment explains why it exists alongside this table rather than
 * replacing it) -- kept here unchanged, at its original bit positions,
 * so ep_gpio.c/config.c's existing consumption of it is entirely
 * unaffected by this section's own fix. Do not use it for hw_pin_type. */
#define RCP_REGMAP_PIN_PROP_OUTPUT     ((uint8_t)1u << 0)
#define RCP_REGMAP_PIN_PROP_INPUT      ((uint8_t)1u << 1)
#define RCP_REGMAP_PIN_PROP_OPEN_DRAIN ((uint8_t)1u << 2)
#define RCP_REGMAP_PIN_PROP_PULL_UP    ((uint8_t)1u << 3)
#define RCP_REGMAP_PIN_PROP_PULL_DOWN  ((uint8_t)1u << 4)
#define RCP_REGMAP_PIN_PROP_ACTIVE_LOW ((uint8_t)1u << 5)

/* TC18 §12.7.6 Table 20's own hw_pin_type bit layout (REQ-RMAP-042),
 * primary-source verified directly against the TC18 v0.5.1_RC PDF:
 * four packed sub-fields, not the six independent one-hot flags
 * RCP_REGMAP_PIN_PROP_* above uses for its own, different, register.
 * Pull (bits 1:0): float(00b)/pull-down(01b)/pull-up(10b) -- 11b is
 * undefined by the table, left unnamed here rather than guessed.
 * Output stage (bits 3:2): input(00b)/open-drain(01b)/open-source(10b)/
 * push-pull(11b) -- deliberately NOT a separate exclusive INPUT/OUTPUT
 * flag pair the way RCP_REGMAP_PIN_PROP_* models it: TC18's own text
 * states "All outputs are always also an input" (REQ-RMAP-043), so a
 * single 2-bit field selecting one of three OUTPUT drive modes (or
 * plain input) is the only representation an output-is-simultaneously-
 * readable-as-input pin can even have -- there is no "pure output,
 * unreadable" state to invent a separate flag for. Drive strength
 * (bits 5:4): input(00b)/low(01b)/medium(10b)/high(11b). Bit 6 is
 * reserved, reads 0. Schmitt-Trigger (bit 7): a plain single bit. */
#define RCP_REGMAP_HW_PIN_PULL_MASK         ((uint8_t)0x3u)
#define RCP_REGMAP_HW_PIN_PULL_FLOAT        ((uint8_t)0x0u)
#define RCP_REGMAP_HW_PIN_PULL_DOWN         ((uint8_t)0x1u)
#define RCP_REGMAP_HW_PIN_PULL_UP           ((uint8_t)0x2u)

#define RCP_REGMAP_HW_PIN_STAGE_MASK        ((uint8_t)0xCu) /* bits 3:2 */
#define RCP_REGMAP_HW_PIN_STAGE_INPUT       ((uint8_t)0x0u)
#define RCP_REGMAP_HW_PIN_STAGE_OPEN_DRAIN  ((uint8_t)0x4u)
#define RCP_REGMAP_HW_PIN_STAGE_OPEN_SOURCE ((uint8_t)0x8u)
#define RCP_REGMAP_HW_PIN_STAGE_PUSH_PULL   ((uint8_t)0xCu)

#define RCP_REGMAP_HW_PIN_DRIVE_MASK        ((uint8_t)0x30u) /* bits 5:4 */
#define RCP_REGMAP_HW_PIN_DRIVE_INPUT       ((uint8_t)0x00u)
#define RCP_REGMAP_HW_PIN_DRIVE_LOW         ((uint8_t)0x10u)
#define RCP_REGMAP_HW_PIN_DRIVE_MEDIUM      ((uint8_t)0x20u)
#define RCP_REGMAP_HW_PIN_DRIVE_HIGH        ((uint8_t)0x30u)

/* bit 6 reserved, reads 0 -- no macro; never set it. */
#define RCP_REGMAP_HW_PIN_SCHMITT_TRIGGER   ((uint8_t)1u << 7)

typedef struct {
    uint8_t hw_ep_nr;
    uint8_t hw_ep_pin_nr;
    uint8_t hw_pin_type; /* TC18's own register name (Table 21); RCP_
                             REGMAP_HW_PIN_* bitmask above, REQ-RMAP-042.
                             Renamed from this field's earlier name,
                             pin_property, to match the wire register
                             exactly and stop implying kinship with
                             ep_gpio.h's differently-shaped, same-named
                             field. */
} rcp_regmap_hw_pin_map_entry_t;

/* ── HW_config server-side storage + wire codec (REQ-RMAP-040/041) ──────────
 *
 * Row shape (rcp_regmap_hw_pin_map_entry_t) and per-row bit layout
 * (RCP_REGMAP_HW_PIN_* above) were already correct as of REQ-RMAP-042/
 * -043/-044/-045's own earlier batches -- this section closes the two
 * remaining Group 2 items: no server-side STORAGE existed anywhere for a
 * real table of these rows (rcp_config_apply_to_mock() deliberately
 * discarded the parsed manifest data, per its own prior doc comment), and
 * no function serialized a real table into TC18 §12.7.6 Table 21's own
 * 3-octets-per-pin layout (IO_Pin N at relative address 3*N/3*N+1/3*N+2).
 *
 * RESOLVED 2026-08-11 (issue #301, user-directed re-investigation):
 * unlike Table 20 -- reached via a plain read always addressed at
 * absolute address 0, confirmed directly against TC18 §12.7's own text
 * -- HW_config is a SEPARATE table pointed to by Table 20's own
 * svr_hw_cfg_ptr register, with Table 21's own address column headed
 * "Relative Address" (relative to HW_config's own start, i.e. to
 * svr_hw_cfg_ptr's own current value), and R/W* access
 * (write-prohibited outside HW_UNCONFIGURED, TC18 §12.7.6's own opening
 * sentence: "This configuration table can only be changed in the
 * life-cycle state HW_unconfigured"). Precisely how a client's own
 * request address relates to svr_hw_cfg_ptr's value was flagged here as
 * a genuine, unresolved architectural question -- now resolved: Table
 * 20's own address column is itself headed "Absolute address" (verified
 * directly against the current RC5 baseline PDF, page 61, not the
 * "relative address" this codebase's own comments previously
 * mis-cited), confirming every "_ptr" field's own value is an absolute
 * address in the SAME EP0-scoped space Table 20 itself lives in -- a
 * client reaches HW_config by issuing an ordinary evt[2:0]=111b
 * configuration request (TC18 §12.7.1 Figure 19, the same generic
 * mechanism every endpoint type already has its own client-side
 * encoder for) to byte_bus_id=0 (EP0), with the request's own
 * start_address set to svr_hw_cfg_ptr's current value plus whatever
 * relative offset within HW_config the client wants. See
 * rcp_regmap_hw_pin_map_apply_reconfig() and
 * rcp_regmap_ep0_decode_write_request() below, which close the WRITE
 * half of this. The READ half (an address-routed
 * rcp_regmap_ep0_encode_read_response()) remains open, tracked in the
 * same issue. */

/* This module's own chosen upper bound on HW_config table rows -- not a
 * spec-derived number (TC18's own svr_io_pin_count, Table 20, is a
 * 16-bit register with no fixed upper bound at all); matches
 * RCP_MOCK_MAX_ENDPOINTS' own scale (mock.h) as a plausible real-device
 * IO-pin count. */
#define RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES ((size_t)64u)

/* Serializes entries[0..len) into out at each row's own TC18-cited
 * 3-octet stride (hw_ep_nr/hw_ep_pin_nr/hw_pin_type per IO pin, relative
 * address 3*N onward) -- out must have room for at least 3*len octets.
 * len beyond RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES is the caller's own
 * responsibility to have already bounded (this function does not itself
 * clamp len; every real caller in this codebase sources len from a
 * table already bounded at construction, e.g.
 * rcp_mock_server_set_hw_pin_map()). */
void rcp_regmap_hw_pin_map_render(const rcp_regmap_hw_pin_map_entry_t *entries, size_t len,
                                   uint8_t *out);

typedef enum {
    RCP_REGMAP_HW_PIN_MAP_RECONFIG_OK              = 0,
    RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_SHORT       = 1, /* payload has no
                                                             address, or no data
                                                             past the address */
    RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_OUT_OF_RANGE = 2, /* relative_start_address
                                                              + data_len exceeds
                                                              count*3 */
} rcp_regmap_hw_pin_map_reconfig_errc_t;

/* Human-readable message for an rcp_regmap_hw_pin_map_reconfig_errc_t
 * value. Never returns NULL. */
const char *rcp_regmap_hw_pin_map_reconfig_strerror(rcp_regmap_hw_pin_map_reconfig_errc_t e);

/* The inverse of rcp_regmap_hw_pin_map_render(): patches entries[0..count)
 * (the currently-configured HW_config table, unchanged in row COUNT by
 * this call -- count itself is svr_io_pin_count, a separate, read-only
 * Table 20 register this function never touches) at relative_start_address
 * (relative to HW_config's own start, i.e. to svr_hw_cfg_ptr's own current
 * value -- NOT an EP0-absolute address; a caller routing an incoming
 * request here, e.g. rcp_regmap_ep0_decode_write_request(), has already
 * subtracted svr_hw_cfg_ptr's own value from the request's own absolute
 * address before calling this).
 *
 * Same "render current image, patch the addressed octets, re-parse the
 * whole image back" idiom every other endpoint type's own apply_reconfig()
 * already uses (see e.g. rcp_ep_gpio_apply_reconfig()) -- reused here, not
 * reinvented, for the same TC18 §13.7.1.2-style write mechanics. Every
 * octet of every row is R/W* (Table 21 has no read-only sub-fields within
 * a row the way some endpoints' own EP_func blocks do), so no octet is
 * ever silently skipped the way e.g. GPIO's own reserved octets are.
 *
 * Returns RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_SHORT if data_len is 0;
 * RCP_REGMAP_HW_PIN_MAP_RECONFIG_ERR_OUT_OF_RANGE if
 * relative_start_address + data_len would exceed count*3 octets (the
 * table's own current, fixed extent) -- the whole write is rejected in
 * that case, entries is left entirely unchanged, matching every sibling
 * apply_reconfig()'s own "ignore in its entirety" TC18 §12.7.1 rule. */
rcp_regmap_hw_pin_map_reconfig_errc_t
rcp_regmap_hw_pin_map_apply_reconfig(rcp_regmap_hw_pin_map_entry_t *entries, size_t count,
                                      uint16_t relative_start_address,
                                      const uint8_t *data, size_t data_len);

/* ── Per-endpoint-type named-signal index ──────────────────────────────────── */

/* The full named-signal index shared by every endpoint type, written once
 * here and reused unmodified by every endpoint type added later (see the
 * file header). This is one flat enumeration for human-readable naming
 * (rcp_regmap_named_signal_string()) -- TC18 §12.7.6 Table 23's own
 * EP_Signal_Nr wire value is NOT this enum's own ordinal; it restarts at
 * 0 for every endpoint type (REQ-RMAP-045). rcp_regmap_named_signal_
 * ep_signal_nr() below is the converter between the two: this enum's
 * flat ordinal (for naming/identity) and TC18's own per-type-relative
 * wire value (for hw_ep_pin_nr, TC18 §12.7.6 Table 21). Values are
 * grouped by endpoint type, in the same order and per-type numbering
 * Table 23 itself uses, so that converter can compute an offset from
 * each group's own first member rather than needing a second, parallel
 * lookup table. */
typedef enum {
    RCP_REGMAP_SIGNAL_GPIO0 = 0,
    RCP_REGMAP_SIGNAL_GPIO1,
    RCP_REGMAP_SIGNAL_GPIO2,
    RCP_REGMAP_SIGNAL_GPIO3,
    RCP_REGMAP_SIGNAL_GPIO4,
    RCP_REGMAP_SIGNAL_GPIO5,
    RCP_REGMAP_SIGNAL_GPIO6,
    RCP_REGMAP_SIGNAL_GPIO7,
    RCP_REGMAP_SIGNAL_GPIO8,
    RCP_REGMAP_SIGNAL_GPIO9,
    RCP_REGMAP_SIGNAL_GPIO10,
    RCP_REGMAP_SIGNAL_GPIO11,
    RCP_REGMAP_SIGNAL_GPIO12,
    RCP_REGMAP_SIGNAL_GPIO13,
    RCP_REGMAP_SIGNAL_GPIO14,
    RCP_REGMAP_SIGNAL_GPIO15,
    RCP_REGMAP_SIGNAL_GPIO16,
    RCP_REGMAP_SIGNAL_GPIO17,
    RCP_REGMAP_SIGNAL_GPIO18,
    RCP_REGMAP_SIGNAL_GPIO19,
    RCP_REGMAP_SIGNAL_GPIO20,
    RCP_REGMAP_SIGNAL_GPIO21,
    RCP_REGMAP_SIGNAL_GPIO22,
    RCP_REGMAP_SIGNAL_GPIO23,
    RCP_REGMAP_SIGNAL_GPIO24,
    RCP_REGMAP_SIGNAL_GPIO25,
    RCP_REGMAP_SIGNAL_GPIO26,
    RCP_REGMAP_SIGNAL_GPIO27,
    RCP_REGMAP_SIGNAL_GPIO28,
    RCP_REGMAP_SIGNAL_GPIO29,
    RCP_REGMAP_SIGNAL_GPIO30,
    RCP_REGMAP_SIGNAL_GPIO31,
    RCP_REGMAP_SIGNAL_SPI_CLK,
    RCP_REGMAP_SIGNAL_SPI_PICO,
    RCP_REGMAP_SIGNAL_SPI_POCI,
    RCP_REGMAP_SIGNAL_SPI_CS0,
    RCP_REGMAP_SIGNAL_SPI_CS1,
    RCP_REGMAP_SIGNAL_SPI_CS2,
    RCP_REGMAP_SIGNAL_SPI_CS3,
    RCP_REGMAP_SIGNAL_SPI_CS4,
    RCP_REGMAP_SIGNAL_SPI_CS5,
    RCP_REGMAP_SIGNAL_I2C_SCL,
    RCP_REGMAP_SIGNAL_I2C_SDA,
    /* REQ-RMAP-044: TC18 §12.7.6 Table 23 enumerates EP_Signal_Nr for
     * every endpoint type this codebase implements, not just GPIO/SPI/
     * I2C -- the eight groups below close that coverage gap, each in
     * Table 23's own signal order. */
    RCP_REGMAP_SIGNAL_UART_TX,
    RCP_REGMAP_SIGNAL_UART_RX,
    RCP_REGMAP_SIGNAL_UART_RTS,
    RCP_REGMAP_SIGNAL_UART_CTS,
    RCP_REGMAP_SIGNAL_LIN_TXD,
    RCP_REGMAP_SIGNAL_LIN_RXD,
    RCP_REGMAP_SIGNAL_LIN_NSLP,
    RCP_REGMAP_SIGNAL_PWM_OUT,  /* positive phase -- Table 23's own name */
    RCP_REGMAP_SIGNAL_PWM_OUTN, /* inverted phase -- Table 23's own name */
    RCP_REGMAP_SIGNAL_PWM_IN,
    RCP_REGMAP_SIGNAL_ADC_IN,
    RCP_REGMAP_SIGNAL_DAC_OUT,
    RCP_REGMAP_SIGNAL_CAN_RXD,
    RCP_REGMAP_SIGNAL_CAN_TXD, /* TC18's own counter-intuitive order:
                                   RXD=0, TXD=1 (Table 23) */
    RCP_REGMAP_SIGNAL_ISELED_ISP_P,
    RCP_REGMAP_SIGNAL_ISELED_ISP_N,
    RCP_REGMAP_SIGNAL_MDIO_MDC,
    RCP_REGMAP_SIGNAL_MDIO_DATA, /* Table 23 names this signal "MDIO"
                                     itself, identical to the endpoint
                                     type name -- disambiguated here to
                                     _DATA to avoid an enum-identifier
                                     collision with the type name; not a
                                     departure from the wire meaning */
    RCP_REGMAP_SIGNAL_COUNT /* not itself a valid signal; the number of
                                named signals defined above */
} rcp_regmap_named_signal_t;

/* Human-readable, unique name for sig. Returns "unknown" (never NULL) for
 * a value outside 0..RCP_REGMAP_SIGNAL_COUNT-1. */
const char *rcp_regmap_named_signal_string(rcp_regmap_named_signal_t sig);

/* REQ-RMAP-045: converts sig's own flat enum ordinal into TC18 §12.7.6
 * Table 23's per-endpoint-type EP_Signal_Nr wire value -- the value
 * hw_ep_pin_nr (Table 21) actually carries, which restarts at 0 for
 * every endpoint type rather than continuing this enum's own flat
 * numbering. Returns 0 for RCP_REGMAP_SIGNAL_COUNT or any other value
 * outside 0..RCP_REGMAP_SIGNAL_COUNT-1 (there is no meaningful
 * EP_Signal_Nr for a signal that doesn't exist; 0 is chosen over an
 * out-of-band sentinel to keep the return type a plain uint8_t, matching
 * the wire field's own width -- callers that need to distinguish "not a
 * real signal" from "really is EP_Signal_Nr 0" should validate sig
 * against RCP_REGMAP_SIGNAL_COUNT themselves before calling). */
uint8_t rcp_regmap_named_signal_ep_signal_nr(rcp_regmap_named_signal_t sig);

/* ── Request-stream and response/ack queue config ──────────────────────────── */

/* E2E/watchdog fields are wired to real behavior as of Phase 18
 * (e2e.h/e2e.c, milestone 70): rx_enforce_e2e, rx_wd_enable,
 * rx_wd_timeout_ms, rx_wd_safestate_enable, rx_wd_info_enable,
 * rx_safety_measure, rx_safestate_sequencer, and rx_safe_sequencer_state
 * are all consumed by e2e.h's own pure functions (which take each
 * field as a plain argument rather than this struct itself, matching
 * every request-kind module's "operate on caller-owned data" convention
 * -- see scheduler.h's rcp_sched_compare() for the established
 * precedent). rx_wd_action, present since milestone 62, stays
 * caller-defined/round-tripped: this milestone's roadmap scope names no
 * concrete action enumeration for it, so no e2e.h function
 * interprets its value. */
typedef struct {
    bool     configured;
    uint64_t rx_stream_id;      /* IEEE 1722 StreamID this request stream
                                    listens on; same addressing model as
                                    avtp.h */

    /* ── Secure channel / acknowledge & response routing ────────────── */
    uint8_t  rx_secure_channel_index; /* REQ-RMAP-047 (TC18 §12.7.7 Table
                                          24, relative address 0x000C, 8
                                          bit, R/W*): which secure channel
                                          this request stream is carried
                                          on. 0 (this field's own
                                          zero-init default) is TC18's own
                                          defined "no cyber security,
                                          MACsec uncontrolled port"
                                          encoding -- content modeling
                                          only, no code path in this
                                          codebase yet selects a MACsec
                                          channel from it (this library
                                          has no MACsec layer of its own
                                          to select one in). */
    uint8_t  rx_ack_stream_index;     /* REQ-RMAP-048 (TC18 §12.7.7 Table
                                          24, relative address 0x0010, 8
                                          bit, R/W*): the index of the
                                          response/ack stream (Table 24,
                                          respqueue.h) endpoints bound to
                                          this request stream send their
                                          acknowledges on. 0 (this field's
                                          own zero-init default) is TC18's
                                          own defined "no acknowledge is
                                          to be sent" encoding -- content
                                          modeling only, no code path
                                          routes an acknowledgement to a
                                          selected queue from this field
                                          yet. */
    uint8_t  rx_resp_stream_index;    /* REQ-RMAP-049 (TC18 §12.7.7 Table
                                          24, relative address 0x0011, 8
                                          bit, R/W*): the index of the
                                          response/ack stream endpoints
                                          bound to this request stream
                                          send their responses on. 0 is
                                          TC18's own defined "no response
                                          is to be sent" encoding; the
                                          power-on default is 1 (not 0),
                                          so a freshly reset server can
                                          answer a discovery request
                                          before any configuration has
                                          been written -- see
                                          rcp_regmap_request_stream_cfg_init()'s
                                          own doc comment. Content
                                          modeling only, same scope as
                                          rx_ack_stream_index above. */

    /* ── E2E (e2e.h CRC32 safe points) ──────────────────────────────── */
    bool     rx_enforce_e2e;    /* false: a CRC_ERROR drops only the
                                    single offending request
                                    (RCP_E2E_CRC_ACTION_DROP_REQUEST).
                                    true: the first CRC_ERROR latches the
                                    whole stream to a faulted state
                                    (RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT)
                                    -- see rcp_e2e_crc_error_action().
                                    Wire name as of TC18 0.5.1_RC5:
                                    rx_enforce_crc -- a pure rename, same
                                    single-bit semantics; see the file
                                    header's own "terminology drift"
                                    note. */

    /* ── Per-stream sequence-number enforcement (e2e.h) ─────────────── */
    bool     rx_enforce_seq;          /* true: a request is only filed for
                                          execution if its AVTPDU's
                                          sequence_num has increased
                                          relative to the last accepted
                                          one on this stream -- see
                                          rcp_e2e_seq_evaluate()'s accept
                                          field. Together with
                                          rx_seq_safestate_enable below,
                                          this pair corresponds to TC18
                                          0.5.1_RC5's own single combined
                                          rx_enforce_sequence bit -- see
                                          the file header's own
                                          "terminology drift" note. */
    bool     rx_seq_safestate_enable; /* true: a sequence_num that did not
                                          advance by exactly one increment
                                          drives every endpoint bound to
                                          this stream toward its
                                          configured safe state -- see
                                          rcp_e2e_seq_evaluate()'s
                                          enter_safe_state field, and
                                          rcp_e2e_overflow_should_enter_safe_state()'s
                                          own doc comment for the same
                                          cross-endpoint escalation
                                          boundary this shares. */

    /* ── Per-stream watchdog (e2e.h) ────────────────────────────────── */
    bool     rx_wd_enable;            /* watchdog active on this stream at
                                          all. Together with
                                          rx_wd_safestate_enable below,
                                          this pair corresponds to TC18
                                          0.5.1_RC5's own single combined
                                          rx_enforce_watchdog bit -- see
                                          the file header's own
                                          "terminology drift" note. */
    uint32_t rx_wd_timeout_ms;        /* elapsed-since-last-kick overflow threshold */
    uint8_t  rx_wd_action;            /* caller-defined; round-tripped only */
    bool     rx_wd_safestate_enable;  /* overflow drives the endpoint toward
                                          its configured safe state */
    bool     rx_wd_info_enable;       /* overflow raises an informational
                                          status/event, independent of
                                          rx_wd_safestate_enable. TC18
                                          0.5.1_RC5's own 4-bit scheme has
                                          no clear 1:1 replacement for this
                                          bit -- see the file header's own
                                          "terminology drift" note,
                                          reason 3. */

    /* ── Request-storage overflow (e2e.h) ───────────────────────────── */
    bool     rx_ovrflw_safestate_enable; /* REQ-RMAP-071 (TC18 §12.7.7
                                          Table 24, relative address
                                          0x000D bit 5, 1 bit, R/W*):
                                          true drives every endpoint bound
                                          to this stream toward its
                                          configured safe state if one
                                          endpoint's own request storage
                                          overflows -- see
                                          rcp_e2e_overflow_should_enter_safe_state()
                                          (e2e.h), the pure decision
                                          function this field is the
                                          register-map source for.
                                          Content modeling only, closing a
                                          gap this file's own
                                          "terminology drift" section
                                          (above) already NAMED as one of
                                          this codebase's own eight
                                          independent bits but never
                                          actually added as a struct
                                          field until now -- REQ-E2E-030's
                                          own separate, still-open
                                          cross-endpoint-orchestrator gap
                                          is unaffected by adding it. */

    /* ── Configured safe state (e2e.h) ──────────────────────────────── */
    uint8_t  rx_safety_measure;         /* RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE (0)
                                            or RCP_E2E_MEASURE_SEQUENCER (1).
                                            TC18 0.5.1_RC5's own 4-bit
                                            scheme has no clear 1:1
                                            replacement for this selector
                                            either -- see the file
                                            header's own "terminology
                                            drift" note, reason 3. */
    uint16_t rx_safestate_sequencer;    /* request_sequencer.h table index the
                                            RCP_E2E_MEASURE_SEQUENCER measure
                                            polls; meaningless otherwise */
    uint8_t  rx_safe_sequencer_state;   /* the sequencer state value, at
                                            rx_safestate_sequencer, that means
                                            "endpoint is in its safe state" */

    /* ── Fragmentation (fragment.h) ─────────────────────────────────── */
    size_t   rx_stream_max_request_size; /* the largest single-AVTPDU ACF
                                             payload (header-and-payload,
                                             excluding any e2e.h CRC
                                             trailer) this stream will
                                             assemble or accept in one
                                             fragment, in either direction,
                                             before fragment.h's ms/
                                             segment_num mechanism must
                                             split a larger message across
                                             multiple frames -- a caller
                                             passes this value as
                                             fragment.h's own
                                             max_fragment_payload (encode
                                             side) or max_total_len
                                             (rcp_fragment_reassembler_init(),
                                             decode side; that bounds the
                                             *reassembled* total, not each
                                             individual fragment, but this
                                             is this codebase's one
                                             configured ceiling for the
                                             stream either way). 0 means
                                             fragmentation is unsupported
                                             for this stream: a message
                                             that would not fit in a single
                                             AVTPDU is rejected/capped by
                                             the endpoint's own
                                             single-AVTPDU-only behavior,
                                             unchanged from every milestone
                                             before Phase 20. */
} rcp_regmap_request_stream_cfg_t;

/* Zero-initializes cfg (configured = false, everything else 0), with one
 * deliberate exception: rx_resp_stream_index is set to 1, not 0 -- see
 * that field's own doc comment (REQ-RMAP-049) and REQ-RMAP-018's own
 * corrected text for why "zero everything" is no longer the whole rule
 * as of this field's own addition. */
void rcp_regmap_request_stream_cfg_init(rcp_regmap_request_stream_cfg_t *cfg);

/* REQ-RMAP-050 (TC18 §12.7.7 Table 24, relative address 0x000A, 16 bit,
 * R/W*): rx_wd_timeout_intervall is expressed in clock tics, but TC18
 * names no fixed clock-tick rate for this register anywhere near its own
 * definition (unlike, e.g., PWM's endpoint-local "clock selected for
 * this endpoint" phrasing) -- so, matching the same caller-supplies-
 * already-classified-units convention already established for
 * rcp_acf_reg_write_len()/rcp_respqueue_max_fragment_payload()/
 * rcp_respqueue_max_avtpdu_size_within_mtu(), these two functions accept
 * the tick duration as a caller-supplied parameter rather than this
 * library inventing or hardcoding a rate TC18 never specifies.
 *
 * rcp_regmap_wd_timeout_ms_to_ticks() converts a
 * rcp_regmap_request_stream_cfg_t.rx_wd_timeout_ms value into the
 * register's own clock-tic unit given the caller-supplied duration of
 * one tick, and reports whether the converted value fits the register's
 * 16-bit width (out_ticks is only meaningful when this returns true;
 * REQ-RMAP-050's own "a written value shall be rejected if it does not
 * fit the register's 16-bit width" requirement). ms_per_tick == 0 is
 * rejected (division by zero has no register value): returns false and
 * leaves *out_ticks unchanged. */
bool rcp_regmap_wd_timeout_ms_to_ticks(uint32_t timeout_ms,
                                        uint32_t ms_per_tick,
                                        uint16_t *out_ticks);

/* The inverse conversion: register clock-tic count -> milliseconds, for
 * populating rcp_regmap_request_stream_cfg_t.rx_wd_timeout_ms from a
 * value read off the wire. ms_per_tick == 0 is rejected the same way
 * (returns false, *out_timeout_ms left unchanged) -- there is no
 * meaningful conversion for a zero-length tick. */
bool rcp_regmap_wd_timeout_ticks_to_ms(uint16_t ticks,
                                        uint32_t ms_per_tick,
                                        uint32_t *out_timeout_ms);

/* ── request-stream-cfg wire codec (issue #306, REQ-RMAP-047/048/049) ──────
 *
 * request-stream-cfg (TC18 §12.7.7 Table 24, pointed to by Table 20's own
 * svr_request_stream_cfg_ptr) is a FOURTH pointed-to table sharing issue
 * #301's own already-resolved addressing finding (svr_request_stream_cfg_ptr's
 * own value is an absolute address in the same EP0-scoped space Table 20
 * itself lives in), filed and closed separately (issue #306) since it was
 * never brought into issue #301's own original scope.
 *
 * Direct primary-source verification, both spec revisions (RC1 PDF pages
 * 57-58; RC5 PDF page 66, "Table 24: Request stream configuration" --
 * renumbered due to RC5's added SPI content, same table): confirms a
 * 24-octet-per-request-stream wire stride, addresses/widths for every
 * field below IDENTICAL across both revisions.
 *
 * Two fields deliberately NOT wire-mapped by this render()/
 * apply_reconfig() pair:
 *
 *   - rx_wd_action: confirmed via direct page-image read of Table 24 on
 *     BOTH revisions that no corresponding register exists anywhere in
 *     this table (or elsewhere in TC18) -- this struct's own field is
 *     round-tripped, caller-defined, with no TC18 wire basis at all.
 *   - configured: a codebase-internal bookkeeping flag (whether this
 *     struct represents a configured request stream), not a TC18 concept
 *     -- no corresponding register.
 *
 * REQ-RMAP-050 (relative address 0x000A, 16 bit, "WatchDog time out for
 * this Stream in clock tics") IS wire-mapped as of this fix, via a new
 * trailing watchdog_ms_per_tick parameter both functions now take --
 * TC18 names no fixed clock-tick rate for this register anywhere near
 * its own definition, so, matching the same caller-supplies-already-
 * classified-units convention already established elsewhere in this
 * file, the caller (ultimately whatever integrates this library with a
 * real clock) supplies it. 0 means "not configured" and is a safe,
 * deliberate default (rcp_mock_server_t is calloc()'d): both
 * rcp_regmap_wd_timeout_ms_to_ticks()/_ticks_to_ms() already reject
 * ms_per_tick == 0 on their own, so passing 0 here makes both
 * conversions uniformly fail-closed until a real rate is configured,
 * with no separate "unconfigured" sentinel needed.
 *
 * On the READ direction (render(), internal ms -> wire ticks): TC18's
 * own REQ-RMAP-050 "a written value shall be rejected if it does not
 * fit the register's 16-bit width" rule is about VALIDATING a value
 * before it is ever accepted into rx_wd_timeout_ms, not about this
 * function -- render() has no error-return mechanism (void) and can
 * only ever be handed an already-valid internal ms value by a
 * conformant caller. Nonetheless, since a value that does not fit is a
 * structurally reachable input here (a non-conformant caller, or simply
 * ms_per_tick == 0), rcp_regmap_wd_timeout_ms_to_ticks() failing falls
 * back to encoding 0x0000 -- the same "reserved / cannot be
 * represented, use 0" treatment this file already established for
 * REQ-RMAP-024's own HW_config alignment octet, not a new pattern.
 *
 * On the WRITE direction (apply_reconfig(), wire ticks -> internal ms):
 * the arriving wire value is always exactly 16 bits by construction (2
 * octets, get_u16()), so it can never itself violate a "16-bit width"
 * constraint -- REQ-RMAP-050's own reject rule is therefore already
 * satisfied structurally for this specific direction and does not need
 * a corresponding runtime check. If rcp_regmap_wd_timeout_ticks_to_ms()
 * still fails (ms_per_tick == 0, or the arriving ticks value combined
 * with an extreme configured rate overflows this library's own uint32_t
 * millisecond representation -- a library-internal representation
 * limit, not a TC18 wire violation), rx_wd_timeout_ms is left
 * unchanged rather than failing the whole apply_reconfig() call --
 * consistent with every other field this function already can't derive
 * anything meaningful for, and strictly more conservative than an
 * all-or-nothing reject that would also discard unrelated fields'
 * legitimate changes in the same write.
 *
 * Two genuine content/wire width mismatches, both resolved the same way
 * REQ-RMAP-061's own flush_time_us mismatch was: saturate (never wrap) on
 * render, since wraparound would silently alias onto ANOTHER valid,
 * meaningfully-different value (0 for a size ceiling meaning "no
 * fragmentation"; some other, unrelated, actually-existing sequencer
 * index for rx_safestate_sequencer -- both worse outcomes than a
 * deterministic, easily-recognized saturated maximum):
 *
 *   - rx_stream_max_request_size is size_t internally (fragment.h's own
 *     byte-count convention) vs. a 16-bit wire register.
 *   - rx_safestate_sequencer is uint16_t internally (a
 *     request_sequencer.h table index) vs. an 8-bit wire register.
 *
 * The 8 independently-configurable bits at relative address 0x000D
 * (rx_enforce_e2e/rx_enforce_seq/rx_seq_safestate_enable/rx_wd_enable/
 * rx_wd_safestate_enable/rx_ovrflw_safestate_enable/rx_safety_measure/
 * rx_wd_info_enable) are serialized using this codebase's OWN existing
 * RC1-baseline 8-independent-bit content model, not RC5's own later
 * 4-combined-bit restructuring -- already investigated and deliberately
 * NOT restructured (task #97, see this file's own "TC18 0.5.1_RC5
 * terminology drift" section above): this codebase's own richer model is
 * a strict, lossless superset of what RC5's collapsed encoding can
 * express, so serializing it directly (one struct field per bit) is both
 * simpler and loses nothing a real RC5-conformant peer could otherwise
 * distinguish. */
void rcp_regmap_request_stream_cfg_render(const rcp_regmap_request_stream_cfg_t *entries,
                                           size_t count, uint8_t *out,
                                           uint32_t watchdog_ms_per_tick);

typedef enum {
    RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_OK = 0,
    RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_SHORT,
    RCP_REGMAP_REQUEST_STREAM_CFG_RECONFIG_ERR_OUT_OF_RANGE
} rcp_regmap_request_stream_cfg_reconfig_errc_t;

const char *
rcp_regmap_request_stream_cfg_reconfig_strerror(rcp_regmap_request_stream_cfg_reconfig_errc_t e);

/* The parse-side inverse of rcp_regmap_request_stream_cfg_render() above --
 * identical patch-then-reparse idiom to every other pointed-to table's own
 * apply_reconfig(). relative_start_address/data are relative to this
 * table's own start (svr_request_stream_cfg_ptr's own current value), not
 * to Table 20. A write landing on the 3 reserved trailing octets
 * (0x0012-0x0017) is accepted (every octet in this table is R/W*, TC18
 * defines no read-only subrange) but has no effect on any struct field --
 * it patches the transient image this function builds internally, which
 * is then discarded rather than re-parsed back into anything, since
 * those octets correspond to no struct field. watchdog_ms_per_tick is
 * this table's own rx_wd_timeout_ms field's caller-supplied tick
 * duration -- see this table's own file-header doc comment above for
 * the full REQ-RMAP-050 rationale, including why a failed ticks-to-ms
 * conversion leaves rx_wd_timeout_ms unchanged rather than failing this
 * whole call. */
rcp_regmap_request_stream_cfg_reconfig_errc_t
rcp_regmap_request_stream_cfg_apply_reconfig(rcp_regmap_request_stream_cfg_t *entries,
                                              size_t count,
                                              uint16_t relative_start_address,
                                              const uint8_t *data, size_t data_len,
                                              uint32_t watchdog_ms_per_tick);

/* REQ-SEQ-013 (issue #335): resolves stream_id to its own 1-based
 * position in entries[0..count) (the same request_stream_index
 * convention rcp_regmap_ep_id_map_entry_t.request_stream_index already
 * establishes, REQ-RMAP-052 -- 1-based, with 0 reserved as a "no match"
 * sentinel rather than a real index). Returns 0 if no entry's own
 * rx_stream_id equals stream_id, or entries is NULL/count is 0. Every
 * request a real transport delivers carries its own AVTP stream_id
 * directly (avtp.h); this is the one place that identity gets turned
 * into "which configured request stream is this," the register-map-
 * relative identity TC18's own access-control-bearing fields (Table 28's
 * Request_stream_index among them) are expressed in terms of. */
uint8_t rcp_regmap_request_stream_cfg_resolve_index(
    const rcp_regmap_request_stream_cfg_t *entries, size_t count, uint64_t stream_id);

/* Not itself TC18-derived -- an implementation ceiling on how many
 * request-stream rows this codebase's own fixed-size wire-codec buffers
 * support, matching the same scale as
 * RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES/RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES/
 * RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES. */
#define RCP_REGMAP_REQUEST_STREAM_CFG_MAX_ENTRIES ((size_t)64u)

typedef struct {
    uint16_t stream_uid;      /* REQ-RMAP-060 (TC18 §12.7.9 Table 24, relative
                                  address 0x0000, 16 bit, R/W+): supplies bits
                                  [63:48] of the stream_id this queue transmits
                                  responses/acknowledges on -- the unique_id
                                  half rcp_stream_id_make() (avtp.h) takes as
                                  its own second argument. This struct owns no
                                  MAC of its own (that is the interface's, not
                                  a per-queue property); see
                                  rcp_regmap_response_queue_stream_id() below
                                  for combining the two. */
    uint16_t max_avtpdu_size; /* REQ-RMAP-061 (TC18 §12.7.9 Table 24, relative
                                  address 0x0002, 16 bit, R/W*): maximum
                                  length, in quadlets, of an AVTPDU this
                                  queue generates -- respqueue.h's
                                  rcp_respqueue_t's own max_avtpdu_size_octets
                                  is this value x 4, same caller-converts
                                  convention as queue_size below. */
    uint16_t queue_size;      /* REQ-RMAP-059 (TC18 §12.7.9 Table 24, relative
                                  address 0x0004, 16 bit, R/W*): this queue's
                                  configured transmit-memory reservation, in
                                  32-bit words -- respqueue.h's
                                  rcp_respqueue_t's own capacity_octets is
                                  this value x 4, the conversion a caller
                                  performs when calling rcp_respqueue_init(). */
    uint16_t flush_on_count;  /* REQ-RMAP-063 (TC18 §12.7.9 Table 24, relative
                                  address 0x0006, 16 bit, R/W+): the queued-
                                  octet threshold that triggers a flush --
                                  respqueue.h's rcp_respqueue_should_flush()
                                  takes the octet-converted form of this
                                  register as its own caller-supplied
                                  parameter. */
    uint32_t flush_time_us;   /* REQ-RMAP-064 (TC18 §12.7.9 Table 24, relative
                                  address 0x0008, 16 bit, R/W+, microseconds):
                                  the elapsed-since-last-transmission
                                  threshold that forces a flush even of an
                                  empty queue (REQ-RMAP-065). Deliberately
                                  wider than the 16-bit wire register --
                                  respqueue.h's own
                                  rcp_respqueue_should_flush_by_time() already
                                  takes an even wider uint64_t
                                  elapsed/threshold pair, so this field's own
                                  width was chosen to match that existing
                                  consumer, not the wire. This is a genuine,
                                  now-documented content/wire width mismatch:
                                  rcp_regmap_response_queue_cfg_render()
                                  (REQ-RMAP-061) saturates (never wraps) a
                                  value exceeding 0xFFFF to 0xFFFF when
                                  serializing this field, since wraparound to
                                  a smaller value -- worst case 0, TC18's own
                                  "flush only by count" encoding -- would
                                  silently invert this field's own meaning;
                                  a value read back off the wire can never
                                  itself exceed 0xFFFF, so no corresponding
                                  clamp exists on the parse side. */
} rcp_regmap_response_queue_cfg_t;

/* Zero-initializes cfg. */
void rcp_regmap_response_queue_cfg_init(rcp_regmap_response_queue_cfg_t *cfg);

/* Builds the full stream_id cfg's queue transmits on, given the
 * interface's own mac -- REQ-RMAP-060: cfg->stream_uid supplies
 * rcp_stream_id_make()'s unique_id argument; this is a thin, directly-
 * testable named wrapper over that existing avtp.h primitive, not new
 * stream_id-construction logic of its own. */
rcp_stream_id_t rcp_regmap_response_queue_stream_id(const rcp_regmap_response_queue_cfg_t *cfg,
                                                     const uint8_t mac[6]);

/* Not itself TC18-derived -- an implementation ceiling on how many
 * response/ack queue rows (TC18 §12.7.9 Table 24) this codebase's own
 * fixed-size wire-codec buffers support, matching the same scale as
 * RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES/RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES. */
#define RCP_REGMAP_RESPONSE_QUEUE_CFG_MAX_ENTRIES ((size_t)64u)

/* RESOLVED 2026-08-11 (issue #301 batch 3, same finding as HW_config's
 * and EP_ID_config's own sections): response-queue-config is a separate
 * table pointed to by Table 20's own svr_response_stream_cfg_ptr
 * register (REQ-RMAP-034), reached the identical way HW_config and
 * EP_ID_config now are -- an ordinary evt[2:0]=111b request to
 * byte_bus_id=0 at an absolute address inside svr_response_stream_cfg_ptr's
 * own extent. Unlike HW_config/EP_ID_config, no render function existed
 * for this table at all before this batch; rcp_regmap_response_queue_cfg_render()
 * is the first one, serializing TC18's own exact 10-octet-per-queue wire
 * stride (STREAM_UID@0x0000, Max_AVTPDUsize@0x0002, queue_size@0x0004,
 * flush_on_count@0x0006, Flush_time@0x0008 -- confirmed via direct
 * TC18.txt read, §12.7.9 Table 24). */
void rcp_regmap_response_queue_cfg_render(const rcp_regmap_response_queue_cfg_t *entries,
                                           size_t count, uint8_t *out);

typedef enum {
    RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_OK = 0,
    RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_SHORT,
    RCP_REGMAP_RESPONSE_QUEUE_CFG_RECONFIG_ERR_OUT_OF_RANGE
} rcp_regmap_response_queue_cfg_reconfig_errc_t;

const char *
rcp_regmap_response_queue_cfg_reconfig_strerror(rcp_regmap_response_queue_cfg_reconfig_errc_t e);

/* The parse-side inverse of rcp_regmap_response_queue_cfg_render() --
 * identical patch-then-reparse idiom to
 * rcp_regmap_hw_pin_map_apply_reconfig()/rcp_regmap_ep_id_map_apply_reconfig().
 * relative_start_address/data are relative to this table's own start
 * (i.e. to svr_response_stream_cfg_ptr's own current value), not to
 * Table 20. A value parsed back for flush_time_us can never itself
 * exceed 0xFFFF (the wire register's own full range), so no saturation
 * is needed on this direction, unlike render()'s own. */
rcp_regmap_response_queue_cfg_reconfig_errc_t
rcp_regmap_response_queue_cfg_apply_reconfig(rcp_regmap_response_queue_cfg_t *entries,
                                              size_t count,
                                              uint16_t relative_start_address,
                                              const uint8_t *data, size_t data_len);

/* ── EP-ID / byte_bus_id map ────────────────────────────────────────────────── */

typedef struct {
    uint16_t          ep_id;
    rcp_byte_bus_id_t byte_bus_id;
    uint8_t           request_stream_index; /* REQ-RMAP-052 (TC18
                                §12.7.8 Table 23, row offset 0x0000, 8
                                bit, R/W+): which request stream this
                                row's mapping applies to -- the same
                                byte_bus_id may legally be mapped to
                                different endpoints on different
                                request streams (avtp.h's own
                                byte_bus_id-uniqueness note already
                                states this is scoped per stream_id,
                                not global). A value of 0 is TC18's own
                                defined end-of-table sentinel (still
                                open, REQ-RMAP-054 -- no consumer in
                                this codebase stops scanning at it
                                yet); the power-on default row set
                                (request_stream_index=1, ep_id=0,
                                byte_bus_id=0, permitting EP0 access
                                before any configuration is written)
                                is provided by
                                rcp_regmap_ep_id_map_row_init_default()
                                below (REQ-RMAP-054, closed as of this
                                field's own second follow-up batch).
                                Placed as this struct's LAST field
                                (TC18's own row puts it first, at
                                offset 0x0000) so every existing
                                positional-initializer test call site
                                in this codebase keeps compiling
                                unchanged -- this struct is this
                                module's in-memory content model, not a
                                wire-order layout dereferenced over the
                                wire (see this file's own header for
                                that standing distinction), so C field
                                order carries no TC18 conformance
                                obligation of its own.
                                rcp_regmap_ep_id_map_is_ascending()
                                below now DOES consider it (REQ-RMAP-056,
                                closed as of this field's own follow-up
                                batch): the ordering this codebase's own
                                diagnostic checks is the COMPOSITE key
                                (request_stream_index, byte_bus_id), not
                                byte_bus_id alone -- TC18 0.5.1_RC's own
                                text required exactly this composite
                                ordering, though that requirement is
                                since removed from the spec entirely
                                (0.5.1_RC4; see the file header's own
                                "UPDATED 2026-08-11" note). */
} rcp_regmap_ep_id_map_entry_t;

/* This module's own chosen upper bound on EP_ID_config table rows -- not
 * a spec-derived number (TC18 gives this table no fixed capacity of its
 * own), matching RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES'/RCP_MOCK_MAX_ENDPOINTS'
 * own scale (mock.h) as a plausible real-device row count -- used only to
 * size rcp_regmap_ep_id_map_apply_reconfig()'s own fixed stack buffer. */
#define RCP_REGMAP_EP_ID_MAP_MAX_ENTRIES ((size_t)64u)

/* Read-only diagnostic: true iff entries[0..count) is strictly ascending
 * in the COMPOSITE key (request_stream_index, byte_bus_id) -- TC18
 * §12.7.8's own required ordering (REQ-RMAP-056): request_stream_index
 * must never decrease, and within one unchanged request_stream_index
 * run, byte_bus_id must strictly increase. A higher request_stream_index
 * always counts as "ascending" regardless of that row's own byte_bus_id
 * value -- TC18 does not require byte_bus_id to reset or relate across
 * different streams, only within one. Vacuously true for count == 0 or
 * count == 1. This is not, and must not be treated as, server-side
 * enforcement -- see the file header's "Known spec ambiguity" note.
 * entries may be NULL iff count == 0. */
bool rcp_regmap_ep_id_map_is_ascending(const rcp_regmap_ep_id_map_entry_t *entries,
                                        size_t count);

/* REQ-RMAP-054: TC18 §12.7.8 (Table 23 / L2976) defines a
 * request_stream_index of 0 as the table's own end-of-table sentinel,
 * not a valid stream index -- every real consumer of the table must
 * stop scanning at the first such row rather than treat it as a live
 * mapping. Returns the number of leading rows in entries[0..capacity)
 * that precede the first sentinel row; if no row in that range is a
 * sentinel, returns capacity unchanged (the whole buffer is real).
 * Read-only: does not modify entries, and (like
 * rcp_regmap_ep_id_map_is_ascending() above) is not, and must not be
 * treated as, server-side enforcement of anything beyond this one
 * scan -- see the file header's "Known spec ambiguity" note. entries
 * may be NULL iff capacity == 0. */
size_t rcp_regmap_ep_id_map_effective_count(const rcp_regmap_ep_id_map_entry_t *entries,
                                             size_t capacity);

/* REQ-RMAP-054's other half: TC18 §12.7.8 requires the table's power-on
 * default contents to permit access to EP0 before any configuration is
 * written. Populates *row with that default: request_stream_index = 1
 * (the smallest value that is a valid stream index rather than the
 * end-of-table sentinel -- 0 itself cannot be used here, since that is
 * exactly the value rcp_regmap_ep_id_map_effective_count() above
 * treats as "no more real rows"), ep_id = RCP_REGMAP_EP0_INDEX,
 * byte_bus_id = 0. Callers that own a fixed-capacity table are
 * expected to place the result at row 0 at startup, before any client
 * write. row must not be NULL. */
void rcp_regmap_ep_id_map_row_init_default(rcp_regmap_ep_id_map_entry_t *row);

/* ── EP_ID_config wire stride (REQ-RMAP-052/054) ─────────────────────────────
 *
 * TC18 §12.7.8 Table 23 lays each row out as four consecutive octets --
 * request_stream_index (8 bit, relative row offset 0x0000),
 * ep_id/EP_Nr (8 bit, 0x0001), byte_bus_id/BBID (16 bit, 0x0002) -- so
 * row N begins at relative address 4*N, confirmed directly against the
 * primary source (the printed row-1/row-2/row-3 examples begin at
 * 0x0000/0x0004/0x0008).
 *
 * RESOLVED 2026-08-11 (issue #301, same finding as HW_config's own
 * section above): EP_ID_config is a separate table pointed to by Table
 * 20's own svr_ep_bytebus_id_map_ptr register (REQ-RMAP-037), reached
 * the identical way HW_config now is -- svr_ep_bytebus_id_map_ptr's own
 * value is an absolute address in the same EP0-scoped space Table 20
 * itself lives in (Table 20's own address column is headed "Absolute
 * address" in the current RC5 baseline PDF, confirmed directly). See
 * rcp_regmap_ep_id_map_apply_reconfig() below and
 * rcp_regmap_ep0_decode_write_request()'s own routing of it. */

/* Serializes entries[0..count) into out at each row's own TC18-cited
 * 4-octet stride -- out must have room for at least 4*count octets.
 * ep_id is this module's own 16-bit in-memory representation (matching
 * every other endpoint-index field in this codebase, e.g.
 * rcp_regmap_is_ep0()'s own uint16_t parameter), truncated to the
 * wire's real 8-bit EP_Nr width on render -- the same honest,
 * documented truncation convention REQ-ADC-035/036's own render path
 * already established for its own wider in-memory fields. count beyond
 * what a real caller has bounded its own table to is the caller's own
 * responsibility, matching rcp_regmap_hw_pin_map_render()'s own
 * convention. */
void rcp_regmap_ep_id_map_render(const rcp_regmap_ep_id_map_entry_t *entries, size_t count,
                                  uint8_t *out);

typedef enum {
    RCP_REGMAP_EP_ID_MAP_RECONFIG_OK               = 0,
    RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_SHORT        = 1,
    RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_OUT_OF_RANGE = 2,
} rcp_regmap_ep_id_map_reconfig_errc_t;

/* Human-readable message for an rcp_regmap_ep_id_map_reconfig_errc_t
 * value. Never returns NULL. */
const char *rcp_regmap_ep_id_map_reconfig_strerror(rcp_regmap_ep_id_map_reconfig_errc_t e);

/* The inverse of rcp_regmap_ep_id_map_render(): patches entries[0..count)
 * at relative_start_address (relative to EP_ID_config's own start, i.e.
 * to svr_ep_bytebus_id_map_ptr's own current value -- NOT an
 * EP0-absolute address; a caller routing an incoming request here has
 * already subtracted svr_ep_bytebus_id_map_ptr's own value from the
 * request's own absolute address). Same "render current image, patch
 * the addressed octets, re-parse the whole image back" idiom
 * rcp_regmap_hw_pin_map_apply_reconfig() and every endpoint type's own
 * apply_reconfig() already use -- reused, not reinvented. Every octet of
 * every row is R/W+ (no read-only sub-fields within a row), so no octet
 * is ever silently skipped. count itself (the table's own current row
 * count) is never changed by this call.
 *
 * Returns RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_SHORT if data_len is 0;
 * RCP_REGMAP_EP_ID_MAP_RECONFIG_ERR_OUT_OF_RANGE if
 * relative_start_address + data_len would exceed count*4 octets --
 * entries is left entirely unchanged in that case, matching every
 * sibling apply_reconfig()'s own "ignore in its entirety" TC18 §12.7.1
 * rule. */
rcp_regmap_ep_id_map_reconfig_errc_t
rcp_regmap_ep_id_map_apply_reconfig(rcp_regmap_ep_id_map_entry_t *entries, size_t count,
                                     uint16_t relative_start_address,
                                     const uint8_t *data, size_t data_len);

/* REQ-RMAP-057: TC18 §12.7.8 recommends, for safety reasons, that an
 * endpoint be mapped to at most one RC Client at a time. In this
 * table's own terms, one RC Client corresponds to one request stream
 * (REQ-RMAP-052's own row shape), so the hazard this diagnostic
 * flags is one ep_id appearing under more than one DISTINCT
 * request_stream_index -- not merely appearing on more than one row.
 * An endpoint legitimately reachable from several rows that all share
 * the same request_stream_index (e.g. via more than one byte_bus_id)
 * is still only one client addressing it, and is not what this
 * recommendation is about. Returns true iff no ep_id in
 * entries[0..count) is associated with two different
 * request_stream_index values. O(count^2); count is expected to stay
 * small (one server's own endpoint set). Read-only diagnostic, not
 * enforcement -- see the file header's "Known spec ambiguity" note.
 * entries may be NULL iff count == 0. */
bool rcp_regmap_ep_id_map_has_single_client_per_ep(const rcp_regmap_ep_id_map_entry_t *entries,
                                                    size_t count);

/* REQ-RMAP-058: TC18 §12.7.8 recommends that endpoints sharing a
 * byte_bus_id within one request stream share the same ep_type -- a
 * shared byte_bus_id is a deliberate multicast-within-a-stream
 * mechanism, and a request broadcast to endpoints of differing
 * ep_type would be decoded differently by each. The EP_ID_config row
 * itself carries no ep_type (TC18's own row layout doesn't have one;
 * ep_type instead lives on rcp_regmap_ep_generic_cfg_t, looked up by
 * ep_id), so this diagnostic takes a caller-supplied, index-parallel
 * ep_types[] array (ep_types[i] is entries[i]'s own endpoint's
 * ep_type) rather than inventing a field this row doesn't have on the
 * wire. Returns true iff, for every group of rows that share one
 * (request_stream_index, byte_bus_id) pair, every ep_types[] value in
 * that group is identical. O(count^2); count is expected to stay
 * small. Read-only diagnostic, not enforcement. entries/ep_types may
 * both be NULL iff count == 0; when non-NULL each must have at least
 * count elements, index-aligned with entries. */
bool rcp_regmap_ep_id_map_shared_bus_homogeneous(const rcp_regmap_ep_id_map_entry_t *entries,
                                                  const uint8_t *ep_types,
                                                  size_t count);

/* REQ-WAKEUP-020: TC18 §13.7.2.1 fixes the WakeUp endpoint's own EP_Nr
 * (this table's own ep_id field, TC18 §12.7.8 Table 23) to 1 -- "The
 * WakeUp endpoint is a special endpoint and as this fixed to the
 * endpoint nr 1, as it is the only endpoint which stays active in
 * Sleep mode." Same shape as REQ-RMAP-057/058 above: a caller-supplied,
 * index-parallel ep_types[] array (this row carries no ep_type of its
 * own, same reason _shared_bus_homogeneous() above takes one), checked
 * against a caller-supplied target_ep_type/required_ep_id pair rather
 * than this generic regmap.c module hardcoding ep_wakeup.h's own
 * RCP_EP_WAKEUP_EP_TYPE/RCP_EP_WAKEUP_ENDPOINT_NUM constants -- regmap.c
 * has no dependency on any concrete endpoint-type header, and this
 * function does not introduce one. Returns true iff every row whose
 * ep_types[i] == target_ep_type has ep_id == required_ep_id; vacuously
 * true if no such row exists (count == 0, or simply no row of that
 * ep_type). O(count). Read-only diagnostic, not enforcement -- same
 * disposition as every other TC18 §12.7.8 recommendation this module
 * already models this way; nothing in this codebase stops a caller
 * from writing an EP_ID_config row that violates this invariant.
 * entries/ep_types may both be NULL iff count == 0; when non-NULL each
 * must have at least count elements, index-aligned with entries. */
bool rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id(const rcp_regmap_ep_id_map_entry_t *entries,
                                                    const uint8_t *ep_types, size_t count,
                                                    uint8_t target_ep_type,
                                                    uint16_t required_ep_id);

/* ── Cross-endpoint orchestrator query (issue #335, REQ-E2E-029/030/045) ────
 *
 * e2e.h's own file header (and rcp_e2e_overflow_should_enter_safe_state()'s/
 * rcp_e2e_crc_error_should_enter_safe_state()'s own doc comments) both name
 * "this library's current data model has no type representing 'all
 * endpoints on a stream'" as the reason a single endpoint's own admit()/
 * dispatch() call could only ever act on itself, never broadcast a
 * stream-wide safe-state escalation to its siblings on the same request
 * stream. That claim was true of this codebase's DISPATCH layer (mock.c),
 * but not of its underlying CONTENT model: TC18 §12.7.8 Table 23
 * (EP_ID_config, rcp_regmap_ep_id_map_entry_t above) already *is* the
 * wire-defined "which endpoints are bound to which request stream" table
 * -- REQ-RMAP-052 already gave it a full render/parse codec -- it simply
 * never had a caller-facing QUERY over it answering "given a stream, which
 * endpoints does it address", only content-shape diagnostics
 * (_is_ascending(), _has_single_client_per_ep(), etc., above). This
 * function is that missing projection, closing the actual architectural
 * gap issue #335 named.
 *
 * Given entries[0..count) (a caller-owned EP_ID_config table -- count is
 * this section's own established "caller has already applied
 * rcp_regmap_ep_id_map_effective_count() first if it cares about the
 * end-of-table sentinel" convention, unchanged from every other
 * diagnostic above), writes every DISTINCT byte_bus_id whose own row
 * names request_stream_index into out_byte_bus_ids[0..out_capacity),
 * skipping a byte_bus_id already written -- TC18 §12.7.8 permits more
 * than one row (different ep_id, or the same ep_id via a different
 * channel) to share one (request_stream_index, byte_bus_id) pair, or to
 * name several distinct byte_bus_id rows under the same stream; either
 * way this function reports each bound byte_bus_id exactly once.
 *
 * byte_bus_id, not ep_id, is this function's own return unit: every
 * existing dispatch path in this codebase (mock.c's own find_slot(),
 * keyed by byte_bus_id) resolves a live endpoint by byte_bus_id, never by
 * raw ep_id alone -- TC18 §12.9.1's own routing rule ("the EPs are mapped
 * by their byte_bus_ids") makes byte_bus_id the actual addressable unit a
 * caller needs to reach one, matching this function's own purpose (a
 * caller that also wants each row's own ep_id/channel can still scan
 * entries[] itself -- this function does not discard that information,
 * it is simply not what a broadcast-by-address caller needs).
 *
 * Returns the TOTAL number of distinct byte_bus_id values found, which
 * may exceed out_capacity -- the same "ask first, then size a buffer"
 * idiom rcp_sched_split_frame_members() (scheduler.h) already established
 * in this codebase, not merely the number actually written.
 * out_byte_bus_ids may be NULL iff out_capacity == 0. entries may be NULL
 * iff count == 0. O(count^2); count is expected to stay small (one
 * server's own endpoint set), the same assumption
 * rcp_regmap_ep_id_map_has_single_client_per_ep() above already makes. */
size_t rcp_regmap_ep_id_map_byte_bus_ids_for_stream(const rcp_regmap_ep_id_map_entry_t *entries,
                                                      size_t count, uint8_t request_stream_index,
                                                      rcp_byte_bus_id_t *out_byte_bus_ids,
                                                      size_t out_capacity);

/* ── Optional-subsystem config sections: Network/PHY/time-synch/security
 * (REQ-RMAP-039, TC18 §12.7.11-.14) ────────────────────────────────────
 *
 * Unlike every other Table 20 pointed-to table this file models
 * (HW_config, EP_ID_config, response-queue-config, request-stream-cfg,
 * ep_generic_cfg, Sequencer_config), TC18 defines NO field-level layout
 * for any of these four sections -- each one's own section text states
 * verbatim "The content is product specific" (§12.7.11: "In case of an
 * Ethernet interface this section comprised the entire MAC
 * configuration"; §12.7.12: "might be empty" for MDIO-managed PHYs;
 * §12.7.13: "Typically, gPTP (IEEE802.1AS)"; §12.7.14: "Typically,
 * MacSec... and specifics of the key agreement"), confirmed by direct
 * primary-source read (TC18.txt) before implementing, not assumed from
 * REQ-RMAP-039's own earlier text. There is therefore no row-typed
 * struct to declare the way HW_config/EP_ID_config/etc. have one: each
 * section is a flat, capacity-bounded, opaque byte buffer, and a
 * conformant implementation's whole job is making that buffer reachable
 * at its own advertised [ptr, ptr+capacity) extent via ordinary
 * ACF_ABB reads/writes -- not interpreting what is inside it.
 *
 * Access type: TC18 gives no table-specific access-type override for
 * any of these four sections in their own surrounding prose (unlike
 * HW_config's own §12.7.6 "HW_unconfigured-only" override) -- so this
 * codebase applies the generic FUNCTIONAL_W_STAR rule, the same
 * documented default request-stream-cfg's own dispatcher routing
 * already uses for the identical "no table-specific override found"
 * reason. Flagged as a codebase-level default choice, not a
 * primary-source-derived fact TC18 itself states. */

/* This implementation's own storage bound per optional-subsystem
 * section -- NOT a TC18-mandated limit (TC18 leaves each section's own
 * real capacity entirely up to the product, via its own
 * svr_*_cfg_capacity register). Matches every sibling table's own
 * MAX_ENTRIES-style bound (RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES etc.):
 * generous enough for real product-specific MAC/PHY/gPTP/MACsec
 * configuration blobs, small enough to keep this dispatcher's own
 * stack-local read-response buffers bounded. A caller with a genuinely
 * larger product-specific section cannot be served by this
 * implementation as-is -- the same honest limitation every other
 * MAX_ENTRIES bound in this file already carries. */
#define RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_MAX_OCTETS ((size_t)256u)

/* One flat byte buffer + its own currently-configured length, for one
 * of the four optional-subsystem sections above -- see this section's
 * own file-header note for why there is no row-typed struct here. len
 * mirrors the corresponding svr_*_cfg_capacity register in
 * rcp_regmap_general_t (kept in sync by whichever call installs this
 * buffer -- rcp_mock_server_set_network_interface_cfg() etc., mock.h --
 * the same capacity-sync convention REQ-RMAP-032/034/036/037 already
 * established for other tables). A zero-initialized instance (len == 0)
 * correctly means "nothing installed" -- matches this table's own zero
 * pointer/capacity register default (rcp_regmap_general_init()), TC18's
 * own defined "not supported" encoding for three of the four
 * (physical-layer/time-synch/security; network-interface has no such
 * explicit note, see svr_network_interface_cfg_ptr's own field
 * comment). */
typedef struct {
    uint8_t data[RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_MAX_OCTETS];
    size_t  len;
} rcp_regmap_optional_subsystem_cfg_t;

/* Bundles all four optional-subsystem sections' own storage into one
 * handle, passed to the EP0 dispatcher pair below as a single optional
 * (NULL-able) parameter -- matches this dispatcher's own established
 * "NULL means this server has none of this table at all"
 * convention (sequencer_state/sequencer_owner's own NULL/NULL/0
 * precedent). A NULL individual field within a non-NULL
 * rcp_regmap_optional_subsystem_cfg_ptrs_t means that ONE section is
 * absent while sibling sections may still be present -- finer-grained
 * than the all-or-nothing NULL convention above, deliberately: a real
 * product may support gPTP time-synch configuration while genuinely
 * having no MACsec security section at all, and TC18's own per-pointer
 * "0 means not supported" encoding already expects exactly this
 * per-section granularity. */
typedef struct {
    rcp_regmap_optional_subsystem_cfg_t *network_interface_cfg;
    rcp_regmap_optional_subsystem_cfg_t *physical_layer_cfg;
    rcp_regmap_optional_subsystem_cfg_t *time_synch_cfg;
    rcp_regmap_optional_subsystem_cfg_t *security_cfg;
} rcp_regmap_optional_subsystem_cfg_ptrs_t;

typedef enum {
    RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_OK = 0,
    RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_SHORT,
    RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_RECONFIG_ERR_OUT_OF_RANGE
} rcp_regmap_optional_subsystem_cfg_reconfig_errc_t;

/* Human-readable message for an
 * rcp_regmap_optional_subsystem_cfg_reconfig_errc_t value. Never
 * returns NULL. */
const char *
rcp_regmap_optional_subsystem_cfg_reconfig_strerror(rcp_regmap_optional_subsystem_cfg_reconfig_errc_t e);

/* Applies a write of data[0..data_len) at relative_start_address within
 * cfg's own current extent ([0, cfg->len)) -- a direct bounded memcpy,
 * not the render-patch-reparse idiom every row-typed table's own
 * apply_reconfig() needs, since an opaque byte buffer has no rows to
 * reparse. Fails ERR_OUT_OF_RANGE if the write would extend past
 * cfg->len (this section's own currently-configured capacity, not
 * RCP_REGMAP_OPTIONAL_SUBSYSTEM_CFG_MAX_OCTETS), ERR_SHORT if
 * data_len == 0 -- matching every sibling apply_reconfig()'s own two
 * failure modes exactly. */
rcp_regmap_optional_subsystem_cfg_reconfig_errc_t
rcp_regmap_optional_subsystem_cfg_apply_reconfig(rcp_regmap_optional_subsystem_cfg_t *cfg,
                                                  uint16_t relative_start_address,
                                                  const uint8_t *data, size_t data_len);

/* ── EP0 address-routed dispatcher (issue #301, issue #306) ────────────────
 *
 * Generalizes rcp_regmap_general_decode_write_request() (which only
 * ever recognized a write landing within Table 20's own
 * [0x0000, RCP_REGMAP_GENERAL_LEN) extent, always rejecting it) to
 * route by absolute address across Table 20's own extent AND every
 * pointed-to table this codebase currently has a wire codec for. As of
 * this milestone that is HW_config (svr_hw_cfg_ptr), EP_ID_config
 * (svr_ep_bytebus_id_map_ptr), response-queue-config
 * (svr_response_stream_cfg_ptr), request-stream-cfg
 * (svr_request_stream_cfg_ptr) -- the fourth pointed-to table, found and
 * closed separately (issue #306) after issue #301's own original four
 * batches -- and the four optional-subsystem sections above
 * (REQ-RMAP-039, issue #336). Table 33/36 (svr_ep_functional_cfg_ptr) is
 * deliberately never routed here: its own address-collision defect
 * (documented in this file's own "RC Server functional-configuration
 * content" section) is a separate, still-unresolved primary-source
 * ambiguity neither issue's own finding resolves.
 *
 * Declared here, after HW_config's, EP_ID_config's, response-queue-
 * config's, request-stream-cfg's, and the optional-subsystem sections'
 * own sections, because its own signature references
 * rcp_regmap_hw_pin_map_entry_t, rcp_regmap_ep_id_map_entry_t,
 * rcp_regmap_response_queue_cfg_t, rcp_regmap_request_stream_cfg_t, and
 * rcp_regmap_optional_subsystem_cfg_ptrs_t -- C requires each to already
 * be declared at this point; this dispatcher cannot live any earlier in
 * this header than the last of the tables it routes to. */

typedef enum {
    RCP_REGMAP_EP0_OK                    = 0,
    RCP_REGMAP_EP0_ERR_SHORT_FRAME       = 1,
    RCP_REGMAP_EP0_ERR_BAD_MSG_TYPE      = 2,
    RCP_REGMAP_EP0_ERR_WRONG_BUS         = 3,
    RCP_REGMAP_EP0_ERR_WRONG_OP          = 4,
    RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD     = 5, /* payload shorter than the
                                                  leading 2-octet address */
} rcp_regmap_ep0_errc_t;

/* Human-readable message for an rcp_regmap_ep0_errc_t value. Never
 * returns NULL. */
const char *rcp_regmap_ep0_strerror(rcp_regmap_ep0_errc_t e);

/* Decodes an ACF_ABB WRITE request from b[0..len) addressed to byte_bus_id
 * 0 (EP0), with a payload shaped exactly like every other endpoint type's
 * own evt[2:0]=111b configuration write (TC18 §12.7.1 Figure 19): a
 * leading 2-octet big-endian absolute address, followed by the write's
 * own data. Routes by that address:
 *
 *   - Within [0x0000, RCP_REGMAP_GENERAL_LEN): Table 20 itself -- always
 *     denied, *out_error set to RCP_ERROR_LOCKED_MEM_ACCESS (reusing, not
 *     duplicating, rcp_regmap_general_decode_write_request()'s own
 *     already-proven REQ-RMAP-025 logic).
 *   - Within [map->svr_hw_cfg_ptr, map->svr_hw_cfg_ptr + 3*hw_pin_map_count):
 *     HW_config -- routed to rcp_regmap_hw_pin_map_apply_reconfig()
 *     (relative_start_address = the request's own absolute address minus
 *     map->svr_hw_cfg_ptr).
 *   - Within [map->svr_ep_bytebus_id_map_ptr, map->svr_ep_bytebus_id_map_ptr
 *     + 4*ep_id_map_count): EP_ID_config -- routed to
 *     rcp_regmap_ep_id_map_apply_reconfig() the identical way. REQ-WAKEUP-020
 *     (issue #336): before applying, every row this write's own
 *     [relative, relative+data_len) span touches is checked against
 *     ep_id_map_ep_types[]/fixed_ep_id_target_ep_type/
 *     fixed_ep_id_required_ep_id -- a row whose own ep_type equals
 *     fixed_ep_id_target_ep_type must end up with ep_id equal to
 *     fixed_ep_id_required_ep_id after this write is applied, or the
 *     whole write is denied (RCP_ERROR_INVALID_PARAMETER) with the
 *     table left entirely unchanged (peeked via a scratch render, not
 *     applied then rolled back). ep_id_map_ep_types may be NULL to skip
 *     this check entirely (no enforcement configured) -- same
 *     NULL-means-absent convention every other optional check in this
 *     dispatcher already uses. This is regmap.c's own generic
 *     mechanism: it has no dependency on ep_wakeup.h and does not
 *     hardcode RCP_EP_WAKEUP_EP_TYPE/RCP_EP_WAKEUP_ENDPOINT_NUM --
 *     matching rcp_regmap_ep_id_map_ep_type_has_fixed_ep_id()'s own
 *     established caller-supplied-target convention (same section,
 *     above), now enforced at write time instead of only diagnosed
 *     after the fact.
 *   - Within [map->svr_response_stream_cfg_ptr,
 *     map->svr_response_stream_cfg_ptr + 10*response_queue_cfg_count):
 *     response-queue-config -- routed to
 *     rcp_regmap_response_queue_cfg_apply_reconfig() the identical way.
 *   - Within [map->svr_request_stream_cfg_ptr,
 *     map->svr_request_stream_cfg_ptr + 24*request_stream_cfg_count):
 *     request-stream-cfg -- routed to
 *     rcp_regmap_request_stream_cfg_apply_reconfig() the identical way.
 *   - Within [map->svr_ep_generic_cfg_ptr, map->svr_ep_generic_cfg_ptr
 *     + 12*ep_generic_cfg_count): ep_generic_cfg -- routed to
 *     rcp_regmap_ep_generic_cfg_apply_reconfig() the identical way
 *     (issue #311 batch 5). ep_generic_cfg's own read-only ep_type
 *     octet (relative 0x0000 within each row) is handled entirely
 *     inside that function itself, per its own doc comment -- this
 *     dispatcher's own authorization check below applies to the row
 *     as a whole, not per-field.
 *   - Within [map->svr_network_interface_cfg_ptr, ...+ optional_cfg->
 *     network_interface_cfg->len) and the same shape for
 *     physical_layer_cfg/time_synch_cfg/security_cfg (REQ-RMAP-039,
 *     TC18 §12.7.11-.14): routed to
 *     rcp_regmap_optional_subsystem_cfg_apply_reconfig() -- see that
 *     section's own file-header note (above) for why these four are
 *     opaque byte buffers, not row-typed tables, and why
 *     FUNCTIONAL_W_STAR is this codebase's own documented default
 *     access-type choice for them. optional_cfg (or any one of its own
 *     four fields) may be NULL -- that whole section, or just that one
 *     subsystem, is then skipped by this routing the same way a NULL
 *     sequencer_state/sequencer_owner already skips the sequencer
 *     extent below.
 *   - For any pointed-to table, the write is first subject to lifecycle-
 *     state/writer authorization (issue #308) before being applied.
 *     HW_config is NOT its own "R/W*" column legend despite appearances
 *     -- direct verification of TC18 §12.7.6's own surrounding prose
 *     ("This configuration table can only be changed in the life-cycle
 *     state HW_unconfigured. In other states of the life cycle this is
 *     read-only") finds a narrower, table-specific override matching
 *     RCP_LIFECYCLE_FIELD_HW_GENERIC's own shape, not
 *     RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR's -- checked via
 *     rcp_lifecycle_field_writable(state, RCP_LIFECYCLE_FIELD_HW_GENERIC, writer),
 *     which in turn means via_discovery_stream is the only writer
 *     condition that ever authorizes it (see that kind's own doc
 *     comment, lifecycle.h). request-stream-cfg has no such
 *     table-specific override in its own surrounding prose (confirmed
 *     the same way -- its own TC18 §12.7.7 prose explicitly names BOTH
 *     HW_UNCONFIGURED and HW_CONFIGURED as writable states, matching
 *     FUNCTIONAL_W_STAR exactly) so it genuinely IS entirely TC18 R/W*
 *     (checked via rcp_lifecycle_field_writable(state,
 *     RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer)); EP_ID_config is
 *     entirely R/W+ (checked via
 *     rcp_lifecycle_field_writable_w_plus(state, writer,
 *     map->svr_configuration_lock != 0) -- REQ-RMAP-029's own field IS
 *     TC18's own single, global W+ lock, "0x00: write access to R/W+
 *     type parameters allowed; else: rejected", not a new per-table
 *     lock this codebase invents); response-queue-config is MIXED
 *     per-field (STREAM_UID/flush_on_count/Flush_time are R/W+,
 *     Max_AVTPDUsize/queue_size are R/W*) -- every row-relative
 *     sub-range the write's own [relative, relative+data_len) touches
 *     must pass its own corresponding check, the more specific denial
 *     (LOCKED_MEM_ACCESS over UNAUTHORIZED_ACCESS) reported if more
 *     than one sub-range is touched and denied for different reasons;
 *     ep_generic_cfg is entirely R/W* for authorization purposes
 *     (checked via rcp_lifecycle_field_writable(state,
 *     RCP_LIFECYCLE_FIELD_FUNCTIONAL_W_STAR, writer) against the row
 *     as a whole) -- confirmed via direct primary-source verification
 *     of TC18 §13.2's own surrounding prose, which names no
 *     table-specific lifecycle-state override the way §12.7.6 does
 *     for HW_config; ep_type's own read-only status is a SEPARATE,
 *     per-field concern already enforced inside
 *     rcp_regmap_ep_generic_cfg_apply_reconfig() itself, independent
 *     of whether the row as a whole is authorized.
 *     An authorization denial produces *out_error = the wire error
 *     rcp_lifecycle_field_write_error()/_write_error_w_plus() itself
 *     reports (RCP_ERROR_LOCKED_MEM_ACCESS or
 *     RCP_ERROR_UNAUTHORIZED_ACCESS) WITHOUT applying the write or
 *     consulting that table's own bounds check at all -- authorization
 *     is checked before the out-of-range check below, matching every
 *     other write path in this codebase's own established "authorize
 *     first" ordering (e.g. rcp_regmap_general_decode_write_request()'s
 *     own REQ-RMAP-025 check).
 *   - Once authorized, *out_error is RCP_ERROR_NONE on success,
 *     RCP_ERROR_INVALID_PARAMETER if the write's own address+length
 *     extends past that table's own current extent (the closest of
 *     TC18 Table 30's 17 numbered codes to "address range not entirely
 *     addressable" -- no code with a more specific name exists).
 *   - Any other address: *out_error is RCP_ERROR_EP_NOT_FOUND (the
 *     closest available Table 30 code to "nothing lives at this
 *     address" -- genuinely imprecise for a non-endpoint address, but no
 *     better numbered code exists; flagged here rather than silently
 *     assumed correct).
 *
 * On RCP_REGMAP_EP0_OK, *out_transaction_num is always populated and
 * *out_error reflects one of the outcomes above -- the caller builds the
 * actual ACF response via rcp_acf_build_error_response() (for a denial)
 * or an ordinary positive response (for RCP_ERROR_NONE), the same split
 * every other decode_write_request()-style function in this codebase
 * already uses. Fails with RCP_REGMAP_EP0_ERR_SHORT_FRAME /
 * _BAD_MSG_TYPE / _WRONG_BUS / _WRONG_OP for the same ACF-level reasons
 * rcp_regmap_general_decode_write_request() already fails, or
 * RCP_REGMAP_EP0_ERR_SHORT_PAYLOAD if the payload has no room for its own
 * leading 2-octet address, before authorization/routing is even reached.
 * state/writer are the same already-established types every endpoint
 * type's own writability gate already takes -- a caller already has
 * both to hand for any other write path.
 * hw_pin_map/hw_pin_map_count, ep_id_map/ep_id_map_count,
 * response_queue_cfg/response_queue_cfg_count,
 * request_stream_cfg/request_stream_cfg_count, and
 * ep_generic_cfg/ep_generic_cfg_count describe the currently-configured
 * tables this call may patch in place; any count may be 0 (that table
 * then has no address range at all, and any write targeting it falls
 * through to the "unknown address" case).
 *
 * sequencer_state/sequencer_owner/sequencer_count (REQ-SEQ-013, issue
 * #335) add an eighth extent -- request_sequencer.h's own
 * rcp_sequencer_table_t.state/.owner/.count, same NULL/NULL/0-means-
 * unsupported convention as every other extent. Unlike every table
 * above, this one's authorization is NOT the generic FUNCTIONAL_W_STAR
 * row-level check alone: TC18 §12.7.10 Table 28's own Request_stream_
 * index field names the one client ("Client Nr") allowed to touch each
 * sequencer at all, so on top of the ordinary FUNCTIONAL_W_STAR gate,
 * every octet the write's own span touches is additionally checked
 * per-sequencer-row, per-sub-field, against requester_stream_index (the
 * caller's own already-resolved identity for whichever client sent this
 * write -- rcp_regmap_request_stream_cfg_resolve_index() derives it from
 * a real request's own stream_id): a Seq_state octet (row-relative 0)
 * requires the sequencer's own CURRENT owner to equal
 * requester_stream_index, fail-closed (denied outright) if the
 * sequencer is still unclaimed; a Request_stream_index octet
 * (row-relative 1 -- claiming or reassigning ownership) is permitted if
 * the sequencer is currently unclaimed (first claim, open to whichever
 * client gets there first) OR requester_stream_index already owns it
 * (an owner may reassign or release its own sequencer) -- both rules
 * are this codebase's own design choice, user-approved 2026-08-13,
 * since TC18 states an access-control rule for Seq_state itself but not
 * for who may configure Request_stream_index in the first place. Any
 * denial reports RCP_ERROR_UNAUTHORIZED_ACCESS, checked after the
 * ordinary FUNCTIONAL_W_STAR gate and before the out-of-range check,
 * matching this function's own established "authorize first" ordering. */
rcp_regmap_ep0_errc_t
rcp_regmap_ep0_decode_write_request(const uint8_t *b, size_t len,
                                     const rcp_regmap_general_t *map,
                                     rcp_lifecycle_state_t state,
                                     rcp_lifecycle_writer_ctx_t writer,
                                     rcp_regmap_hw_pin_map_entry_t *hw_pin_map,
                                     size_t hw_pin_map_count,
                                     rcp_regmap_ep_id_map_entry_t *ep_id_map,
                                     size_t ep_id_map_count,
                                     rcp_regmap_response_queue_cfg_t *response_queue_cfg,
                                     size_t response_queue_cfg_count,
                                     rcp_regmap_request_stream_cfg_t *request_stream_cfg,
                                     size_t request_stream_cfg_count,
                                     rcp_regmap_ep_generic_cfg_t *ep_generic_cfg,
                                     size_t ep_generic_cfg_count,
                                     uint8_t *sequencer_state,
                                     uint8_t *sequencer_owner,
                                     size_t sequencer_count,
                                     uint8_t requester_stream_index,
                                     rcp_wire_error_t *out_error,
                                     uint8_t *out_transaction_num,
                                     uint32_t watchdog_ms_per_tick,
                                     const rcp_regmap_optional_subsystem_cfg_ptrs_t *optional_cfg,
                                     const uint8_t *ep_id_map_ep_types,
                                     uint8_t fixed_ep_id_target_ep_type,
                                     uint16_t fixed_ep_id_required_ep_id);

/* ── EP0 address-routed dispatcher, READ side (issue #301 batch 4) ─────────
 *
 * The read-side counterpart to rcp_regmap_ep0_decode_write_request()
 * above -- closes the "no endpoint type has a server-side evt=111b
 * decode/dispatch in either direction" gap this dispatcher's own file
 * header first flagged, for its read half.
 *
 * read_size is deliberately uint8_t, matching
 * rcp_regmap_general_encode_read_response()'s own established
 * precedent (not the ACF header's own wider 12-bit
 * read_size_or_segment_num field) -- every one of this dispatcher's own
 * six routable extents comfortably fits this codebase's real,
 * non-adversarial configurations well under 256 octets; widening this
 * type asymmetrically for just these two new functions, when every
 * sibling read-response function in this codebase already uses uint8_t,
 * would be inconsistent for no real gain. */

/* Decodes an ACF_ABB READ request from b[0..len) addressed to byte_bus_id
 * 0 (EP0), with a payload shaped exactly like the write dispatcher's own
 * request: a leading 2-octet big-endian absolute address, no further
 * payload. The requested read_size is carried in the ACF header's own
 * read_size_or_segment_num field (truncated to this function's own
 * uint8_t output -- see this section's own file-header note), not in
 * the payload. On RCP_REGMAP_EP0_OK, *out_addr, *out_read_size, and
 * *out_transaction_num are populated; pass them to
 * rcp_regmap_ep0_encode_read_response() below to build the actual
 * response. Fails with the same ACF-level errc values as
 * rcp_regmap_ep0_decode_write_request() for the same reasons, or
 * RCP_REGMAP_EP0_ERR_WRONG_OP if op is not RCP_ACF_OP_READ. */
rcp_regmap_ep0_errc_t
rcp_regmap_ep0_decode_read_request(const uint8_t *b, size_t len,
                                    uint16_t *out_addr, uint8_t *out_read_size,
                                    uint8_t *out_transaction_num);

/* REQ-SEQ-014's own upper bound on how many sequencers this dispatcher
 * can serve in one call -- matches svr_sequencers_max's own 8-bit wire
 * width (REQ-RMAP-028: 0..255 sequencers), so every value that register
 * can legally hold fits. request_sequencer.h's own rcp_sequencer_table_t
 * is heap-sized at runtime and carries no such bound itself; this
 * dispatcher's own stack-local copy needs one, the same reason every
 * other extent above (RCP_REGMAP_HW_PIN_MAP_MAX_ENTRIES etc.) already
 * has its own bound. */
#define RCP_REGMAP_SEQUENCER_STATE_MAX_ENTRIES ((size_t)0xFFu)

typedef enum {
    RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_OK = 0,
    RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_ERR_SHORT,
    RCP_REGMAP_SEQUENCER_TABLE_RECONFIG_ERR_OUT_OF_RANGE
} rcp_regmap_sequencer_table_reconfig_errc_t;

/* REQ-SEQ-013/REQ-SEQ-014 (TC18 §12.7.10 Table 25/28's own 2-octet-per-
 * sequencer SEQUENCER_config layout: relative 0x0000 Seq_state (8 bit,
 * R/W), relative 0x0001 Request_stream_index (8 bit, R/W*), repeating
 * per sequencer): renders count sequencers' state/owner pairs into out
 * as out[2*i+0]=state[i], out[2*i+1]=owner[i]. Corrects a real
 * conformance defect this codebase's own earlier REQ-SEQ-014 fix had:
 * that fix's own doc comment claimed request_sequencer.h's
 * rcp_sequencer_table_t.state "IS already exactly TC18's own
 * one-octet-per-sequencer Seq_state layout" and exposed it directly with
 * no render() step at all -- but the real Table 25/28 layout is 2 octets
 * per sequencer, not 1, confirmed via direct primary-source read
 * (TC18.txt) before this fix, not assumed from the prior comment's own
 * unverified claim. A client that had been parsing this extent per
 * TC18's own real stride would have systematically misread every
 * sequencer past the first (e.g. reading what should be sequencer 2's
 * own Seq_state as sequencer 1's own Request_stream_index). state/owner
 * are request_sequencer.h's own rcp_sequencer_table_t.state/.owner
 * arrays, passed as raw pointers (not the struct type) to preserve this
 * header's own no-cross-module-dependency-on-request_sequencer.h
 * layering, the same reason the now-superseded prior comment already
 * gave. Pass NULL/NULL/0 if this server has no sequencer table at all. */
void rcp_regmap_sequencer_table_render(const uint8_t *state, const uint8_t *owner,
                                        size_t count, uint8_t *out);

/* The parse-side inverse of rcp_regmap_sequencer_table_render() above --
 * identical patch-then-reparse idiom to every other pointed-to table's
 * own apply_reconfig(). relative_start_address/data are relative to this
 * table's own start (svr_sequencer_state_ptr's own current value). This
 * function itself performs no access-control check of its own -- see
 * rcp_regmap_ep0_decode_write_request()'s own doc comment for
 * REQ-SEQ-013's ownership-aware authorization, applied by the caller
 * before this function is ever reached, the same "mechanism vs.
 * predicate" split every access-controlled table in this file already
 * follows. */
rcp_regmap_sequencer_table_reconfig_errc_t
rcp_regmap_sequencer_table_apply_reconfig(uint8_t *state, uint8_t *owner, size_t count,
                                           uint16_t relative_start_address,
                                           const uint8_t *data, size_t data_len);

/* Encodes an ACF_ABB READ response for a request decoded by
 * rcp_regmap_ep0_decode_read_request() above. Routes addr across the
 * identical eleven extents rcp_regmap_ep0_decode_write_request() routes
 * (Table 20 itself, HW_config, EP_ID_config, response-queue-config,
 * request-stream-cfg, ep_generic_cfg, sequencer_state, and the four
 * optional-subsystem sections -- REQ-SEQ-014 added sequencer_state,
 * REQ-RMAP-039 added the last four), reusing this dispatcher's own
 * already-proven per-table render() functions where one exists (the
 * four optional-subsystem sections read directly from their own raw
 * buffer instead -- see that section's own file-header note for why),
 * not a second copy of that wire codec.
 *
 * sequencer_state/sequencer_owner/sequencer_count are
 * request_sequencer.h's own rcp_sequencer_table_t.state/.owner/.count,
 * passed as raw pointers (not the struct type) to avoid regmap.h
 * depending on request_sequencer.h -- the same no-cross-module-
 * dependency layering request_sequencer.h's own file header already
 * documents in the other direction. This extent now goes through
 * rcp_regmap_sequencer_table_render() (REQ-SEQ-013, corrects the real
 * 1-octet-vs-2-octet-per-sequencer conformance defect that function's
 * own doc comment describes) rather than being copied through
 * unconverted the way an earlier fix mistakenly did. Pass NULL/NULL/0
 * if this server has no sequencer table at all
 * (rcp_sequencer_table_unsupported()), matching every other optional
 * extent's own NULL/0 convention.
 *
 * On a known extent: *out_error is RCP_ERROR_NONE and the returned
 * rcp_bytes_t carries min(read_size, that extent's own remaining length
 * from addr) real octets followed by zero-fill up to read_size -- the
 * identical "response spans exactly read_size octets, zero-filled past
 * the source's own extent" convention
 * rcp_regmap_general_encode_read_response() already establishes for
 * Table 20 alone, now generalized to whichever of the four extents
 * addr's own starting position falls within.
 *
 * On an address matching none of the five known pointed-to-table
 * extents: *out_error is
 * RCP_ERROR_EP_NOT_FOUND (the identical code the write dispatcher
 * already uses for the same condition) and the returned rcp_bytes_t is
 * zeroed (data=NULL) -- the caller builds the actual error response via
 * rcp_acf_build_error_response(), the same split the write dispatcher
 * already establishes for its own denial cases. Always check *out_error
 * before rcp_bytes_t.data: a zeroed rcp_bytes_t with *out_error ==
 * RCP_ERROR_NONE instead means ordinary allocation failure, the same
 * convention rcp_regmap_general_encode_read_response() already uses.
 *
 * Deliberately does NOT compose data across more than one extent even
 * when addr + read_size would span into a second one -- this function's
 * own zero-fill-past-extent convention above already answers what
 * happens past the FIRST (addr's own) extent's own length, and TC18
 * defines no rule for composing two distinct pointed-to tables into one
 * response; inventing one here would not be primary-source-derived. A
 * caller wanting a second table's own data issues a second,
 * separately-addressed read. */
rcp_bytes_t
rcp_regmap_ep0_encode_read_response(uint16_t addr, uint8_t read_size,
                                     uint8_t transaction_num,
                                     const rcp_regmap_general_t *map,
                                     const rcp_regmap_hw_pin_map_entry_t *hw_pin_map,
                                     size_t hw_pin_map_count,
                                     const rcp_regmap_ep_id_map_entry_t *ep_id_map,
                                     size_t ep_id_map_count,
                                     const rcp_regmap_response_queue_cfg_t *response_queue_cfg,
                                     size_t response_queue_cfg_count,
                                     const rcp_regmap_request_stream_cfg_t *request_stream_cfg,
                                     size_t request_stream_cfg_count,
                                     const rcp_regmap_ep_generic_cfg_t *ep_generic_cfg,
                                     size_t ep_generic_cfg_count,
                                     const uint8_t *sequencer_state,
                                     const uint8_t *sequencer_owner,
                                     size_t sequencer_count,
                                     rcp_wire_error_t *out_error,
                                     uint32_t watchdog_ms_per_tick,
                                     const rcp_regmap_optional_subsystem_cfg_ptrs_t *optional_cfg);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REGMAP_H */
