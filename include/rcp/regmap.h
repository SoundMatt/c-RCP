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
 * e2e.h's file header for the behavior each one now drives.
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

/* ── svr_implemented_options: three all-or-nothing feature groups ─────────── */

/* Time-sync group (TSCF framing plus its presentation-timestamp
 * companion). This module's own bit assignment -- see the file header. */
#define RCP_REGMAP_OPT_TIME_SYNC_TSCF         ((uint32_t)1u << 0)
#define RCP_REGMAP_OPT_TIME_SYNC_PRESENTATION ((uint32_t)1u << 1)

/* Enhanced-cancel group (a cancellation request plus its acknowledgement). */
#define RCP_REGMAP_OPT_ENH_CANCEL_REQUEST ((uint32_t)1u << 2)
#define RCP_REGMAP_OPT_ENH_CANCEL_ACK     ((uint32_t)1u << 3)

/* Compound-bundles group (a bundle header plus per-segment addressing). */
#define RCP_REGMAP_OPT_COMPOUND_HEADER  ((uint32_t)1u << 4)
#define RCP_REGMAP_OPT_COMPOUND_SEGMENT ((uint32_t)1u << 5)

/* True iff, for each of the three groups above, the bits belonging to
 * that group are either all set or all clear in options -- i.e. no group
 * is ever partially implemented. Bits outside all three groups are
 * ignored (forward-compatible with options this milestone does not yet
 * define). */
bool rcp_regmap_options_group_consistent(uint32_t options);

/* ── The general register map ──────────────────────────────────────────────── */

typedef struct {
    uint32_t magic;      /* vendor/device-defined; this module carries the
                             field only, see the file header */
    uint16_t svr_version;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t svr_ep_count;
    uint16_t svr_max_request_streams;
    uint16_t svr_max_sequencers;
    uint32_t svr_memory_capacity;
    uint32_t svr_implemented_options; /* RCP_REGMAP_OPT_* bitmask; see
                                          rcp_regmap_options_group_consistent() */
    uint16_t svr_root_client_index;   /* RCP_REGMAP_NO_ROOT_CLIENT if unset */

    rcp_regmap_table_ref_t hw_pin_map;
    rcp_regmap_table_ref_t request_stream_cfg;
    rcp_regmap_table_ref_t response_queue_cfg;
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
 * arrived through EP0 (RCP_REGMAP_EP0_INDEX); this function does not
 * re-derive that from an address itself, matching acf.h/avtp.h's
 * convention of taking already-classified inputs rather than re-parsing
 * a frame. ep_client may be NULL, meaning "this endpoint has no owning
 * stream on record" (via_owning_stream is then always false). */
rcp_lifecycle_writer_ctx_t rcp_regmap_writer_ctx(const rcp_regmap_general_t *map,
                                               const rcp_regmap_ep_client_t *ep_client,
                                               uint16_t requesting_stream_index,
                                               bool via_ep0);

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
} rcp_regmap_request_stream_cfg_t;

/* Zero-initializes cfg (configured = false, everything else 0). */
void rcp_regmap_request_stream_cfg_init(rcp_regmap_request_stream_cfg_t *cfg);

typedef struct {
    uint16_t max_avtpdu_size;
    uint16_t flush_on_count;
    uint32_t flush_time_us;
} rcp_regmap_response_queue_cfg_t;

/* Zero-initializes cfg. */
void rcp_regmap_response_queue_cfg_init(rcp_regmap_response_queue_cfg_t *cfg);

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
