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

/* REQ-RMAP-030 (TC18 §12.7.5 Table 18, relative address 0x0016, 8 bit,
 * R): five independent bits, one per optional feature, "abcdefgh" with
 * bits f/g/h reserved -- verified directly against the primary-source
 * PDF (Table 18, page 51 of OA_TC18_specification_v_0.5.1_RC.pdf):
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
    uint8_t  svr_req_stream_max; /* REQ-RMAP-026 (TC18 §12.7.5 Table 18,
                                     relative address 0x000E, 8 bit, R):
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
                                            Table 18, relative address
                                            0x000F, 8 bit, R): maximum
                                            number of supported responder
                                            queues for response and
                                            acknowledge traffic. Same
                                            content-modeling-only scope
                                            as svr_req_stream_max above. */
    uint8_t  svr_sequencers_max; /* REQ-RMAP-028 (TC18 §12.7.5 Table 18,
                                     relative address 0x0014, 8 bit, R):
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
                                         18, relative address 0x0015, 8
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
                                         18, relative address 0x0010, 16
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
    uint16_t svr_req_mem_size; /* REQ-RMAP-027 (TC18 §12.7.5 Table 18,
                                   relative address 0x0012, 16 bit, R):
                                   maximum memory for EP request queues,
                                   in 32-bit words. Same scope as
                                   svr_responder_mem_size above. */
    uint8_t  svr_implemented_options; /* REQ-RMAP-030: RCP_REGMAP_OPT_*
                                          bitmask, 8 bit on the wire --
                                          see this field's own dedicated
                                          section above for the full
                                          primary-source-verified bit
                                          layout and history. */
    uint8_t  reserved_0x17; /* REQ-RMAP-031 (TC18 §12.7.5 Table 18,
                                relative address 0x0017, 8 bit): reserved
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
    uint16_t svr_io_pin_count; /* REQ-RMAP-032 (TC18 §12.7.5 Table 18,
                                   relative address 0x0018, 16 bit, R):
                                   number of assignable I/O pins --
                                   §12.7.6's own authoritative source for
                                   how many IO-pin entries the HW_config
                                   table (Table 19) contains. Nothing in
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

    uint16_t svr_hw_cfg_ptr; /* REQ-RMAP-033 (TC18 §12.7.5 Table 18,
                                 relative address 0x001A, 16 bit, R): the
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
                                §12.7.5 Table 18, relative address
                                0x001C, 8 bit, R): number of usable
                                entries in the request-stream config
                                table (§12.7.7 Table 22). */
    uint8_t  svr_response_stream_cfg_capacity; /* REQ-RMAP-034 (TC18
                                §12.7.5 Table 18, relative address
                                0x001D, 8 bit, R): number of usable
                                entries in the response/ack-stream
                                config table (§12.7.9 Table 24). */
    uint16_t svr_request_stream_cfg_ptr; /* REQ-RMAP-034 (TC18 §12.7.5
                                Table 18, relative address 0x001E, 16
                                bit, R): the address of the
                                request-stream config table. */
    uint16_t svr_response_stream_cfg_ptr; /* REQ-RMAP-034 (TC18 §12.7.5
                                Table 18, relative address 0x0020, 16
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
    rcp_regmap_table_ref_t ep_generic_cfg;
    rcp_regmap_table_ref_t ep_functional_cfg;
    rcp_regmap_table_ref_t ep_id_bus_map;
    rcp_regmap_table_ref_t sequencer_state;
} rcp_regmap_general_t;

/* Zero-initializes every field of map except svr_root_client_index, which
 * is set to RCP_REGMAP_NO_ROOT_CLIENT (no root client granted yet -- the
 * correct default while the server is still HW_UNCONFIGURED). */
void rcp_regmap_general_init(rcp_regmap_general_t *map);

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
 * rcp_l2_mac_is_unicast() for the primitive that classifies one) --
 * this function does not re-derive either from an address itself,
 * matching acf.h/avtp.h's convention of taking already-classified
 * inputs rather than re-parsing a frame. ep_client may be NULL, meaning
 * "this endpoint has no owning stream on record" (via_owning_stream is
 * then always false). */
rcp_lifecycle_writer_ctx_t rcp_regmap_writer_ctx(const rcp_regmap_general_t *map,
                                               const rcp_regmap_ep_client_t *ep_client,
                                               uint16_t requesting_stream_index,
                                               bool via_ep0,
                                               bool via_unicast);

/* ── The generic-vs-functional per-endpoint config split ───────────────────── */

/* Server-owned generic per-endpoint config: fields a client's functional
 * configuration is never allowed to touch, however write-access to the
 * functional block below evolves as the server's lifecycle state changes. */
typedef struct {
    uint8_t  ep_type;             /* concrete meaning assigned by each
                                      endpoint type added in Phase 16/19 */
    bool     ep_used;
    uint32_t ep_delay_time;       /* microseconds */
    uint16_t ep_req_storage_size; /* octets of request-payload storage
                                      reserved for this endpoint */
} rcp_regmap_ep_generic_cfg_t;

/* Zero-initializes cfg (ep_used = false, everything else 0). */
void rcp_regmap_ep_generic_cfg_init(rcp_regmap_ep_generic_cfg_t *cfg);

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

/* This module's own pin-property bit layout -- see the file header. */
#define RCP_REGMAP_PIN_PROP_OUTPUT     ((uint8_t)1u << 0)
#define RCP_REGMAP_PIN_PROP_INPUT      ((uint8_t)1u << 1)
#define RCP_REGMAP_PIN_PROP_OPEN_DRAIN ((uint8_t)1u << 2)
#define RCP_REGMAP_PIN_PROP_PULL_UP    ((uint8_t)1u << 3)
#define RCP_REGMAP_PIN_PROP_PULL_DOWN  ((uint8_t)1u << 4)
#define RCP_REGMAP_PIN_PROP_ACTIVE_LOW ((uint8_t)1u << 5)

typedef struct {
    uint8_t hw_ep_nr;
    uint8_t hw_ep_pin_nr;
    uint8_t pin_property; /* RCP_REGMAP_PIN_PROP_* bitmask */
} rcp_regmap_hw_pin_map_entry_t;

/* ── Per-endpoint-type named-signal index ──────────────────────────────────── */

/* The full named-signal index shared by every endpoint type, written once
 * here and reused unmodified by every endpoint type added later (see the
 * file header). */
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
    RCP_REGMAP_SIGNAL_COUNT /* not itself a valid signal; the number of
                                named signals defined above */
} rcp_regmap_named_signal_t;

/* Human-readable, unique name for sig. Returns "unknown" (never NULL) for
 * a value outside 0..RCP_REGMAP_SIGNAL_COUNT-1. */
const char *rcp_regmap_named_signal_string(rcp_regmap_named_signal_t sig);

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

    /* ── E2E (e2e.h CRC32 safe points) ──────────────────────────────── */
    bool     rx_enforce_e2e;    /* false: a CRC_ERROR drops only the
                                    single offending request
                                    (RCP_E2E_CRC_ACTION_DROP_REQUEST).
                                    true: the first CRC_ERROR latches the
                                    whole stream to a faulted state
                                    (RCP_E2E_CRC_ACTION_LATCH_STREAM_FAULT)
                                    -- see rcp_e2e_crc_error_action(). */

    /* ── Per-stream sequence-number enforcement (e2e.h) ─────────────── */
    bool     rx_enforce_seq;          /* true: a request is only filed for
                                          execution if its AVTPDU's
                                          sequence_num has increased
                                          relative to the last accepted
                                          one on this stream -- see
                                          rcp_e2e_seq_evaluate()'s accept
                                          field. */
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
    bool     rx_wd_enable;            /* watchdog active on this stream at all */
    uint32_t rx_wd_timeout_ms;        /* elapsed-since-last-kick overflow threshold */
    uint8_t  rx_wd_action;            /* caller-defined; round-tripped only */
    bool     rx_wd_safestate_enable;  /* overflow drives the endpoint toward
                                          its configured safe state */
    bool     rx_wd_info_enable;       /* overflow raises an informational
                                          status/event, independent of
                                          rx_wd_safestate_enable */

    /* ── Configured safe state (e2e.h) ──────────────────────────────── */
    uint8_t  rx_safety_measure;         /* RCP_E2E_MEASURE_FORCE_HIGH_IMPEDANCE (0)
                                            or RCP_E2E_MEASURE_SEQUENCER (1) */
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

/* Zero-initializes cfg (configured = false, everything else 0). */
void rcp_regmap_request_stream_cfg_init(rcp_regmap_request_stream_cfg_t *cfg);

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
    uint16_t max_avtpdu_size;
    uint16_t queue_size;      /* REQ-RMAP-059 (TC18 §12.7.9 Table 24, relative
                                  address 0x0004, 16 bit, R/W*): this queue's
                                  configured transmit-memory reservation, in
                                  32-bit words -- respqueue.h's
                                  rcp_respqueue_t's own capacity_octets is
                                  this value x 4, the conversion a caller
                                  performs when calling rcp_respqueue_init(). */
    uint16_t flush_on_count;
    uint32_t flush_time_us;
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

/* ── EP-ID / byte_bus_id map ────────────────────────────────────────────────── */

typedef struct {
    uint16_t          ep_id;
    rcp_byte_bus_id_t byte_bus_id;
} rcp_regmap_ep_id_map_entry_t;

/* Read-only diagnostic: true iff entries[0..count) is strictly ascending
 * by byte_bus_id (entries[i].byte_bus_id < entries[i+1].byte_bus_id for
 * every consecutive pair). Vacuously true for count == 0 or count == 1.
 * This is not, and must not be treated as, server-side enforcement -- see
 * the file header's "Known spec ambiguity" note. entries may be NULL iff
 * count == 0. */
bool rcp_regmap_ep_id_map_is_ascending(const rcp_regmap_ep_id_map_entry_t *entries,
                                        size_t count);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REGMAP_H */
