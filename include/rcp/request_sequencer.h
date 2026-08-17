/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-SEQ-001
//cfusa:req REQ-SEQ-002
//cfusa:req REQ-SEQ-003
//cfusa:req REQ-SEQ-004
//cfusa:req REQ-SEQ-005
//cfusa:req REQ-SEQ-006
//cfusa:req REQ-SEQ-007
//cfusa:req REQ-SEQ-008
//cfusa:req REQ-SEQ-009
//cfusa:req REQ-SEQ-010
//cfusa:req REQ-SEQ-011

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-SEQ-012
//cfusa:req REQ-SEQ-013
//cfusa:req REQ-SEQ-014
/*
 * request_sequencer.h -- Persistent sequencer-state registers for the TC18 Remote
 * Control Protocol wire layer (ROADMAP.md Phase 17, "Conditional Requests &
 * Sequencers", milestone 68).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), and the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61). Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/
 * acf.c, server.h/server.c, regmap.h/regmap.c, or any ep_* endpoint module
 * is touched here -- the same layering discipline every module since
 * milestone 64 has followed.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── What a sequencer is, in this codebase's own terms ───────────────────────
 *
 * A sequencer is nothing more than one persistent 8-bit state register.
 * request_compound.h's compound/compound-wait requests (this same milestone) are
 * the only things that ever read or advance one -- this header models the
 * register itself as a first-class, independently testable primitive, per
 * the roadmap's explicit instruction, rather than folding it into
 * request_compound.c as a private implementation detail.
 *
 * regmap.h's rcp_regmap_general_t.svr_sequencers_max (REQ-RMAP-028,
 * a register field reserved, but inert on its own, since milestone 62 --
 * mock.c's rcp_mock_server_set_sequencer_count() is the one caller that
 * keeps it synced with the table below) is the count of
 * sequencers rcp_sequencer_table_new() below is expected to be called
 * with. regmap.h's rcp_regmap_general_t.svr_sequencer_state_ptr
 * (REQ-RMAP-038, a bare pointer -- TC18 defines no adjacent capacity
 * register for this one, unlike most other Group 1 sub-table refs) is
 * the register map's own wire-facing address for exposing this same
 * table over EP0; wiring an actual byte_message_info read/write
 * exchange to it is out of this milestone's scope (regmap.h's own file
 * header already documents that exact deferral for every sub-table it
 * declares) -- this header's table type is this module's in-memory
 * model only, deliberately independent of that wire representation.
 *
 * ── Power-on default and the "0 sequencers" sentinel ────────────────────────
 *
 * Every sequencer's state register powers on to RCP_SEQUENCER_POWER_ON_STATE
 * (1) -- rcp_sequencer_table_new() and rcp_sequencer_table_reset() both
 * apply this uniformly. A table of count == 0 is this module's own
 * spelling of "compound operations unsupported entirely": every index is
 * then invalid (rcp_sequencer_index_valid() is false for any idx), and
 * every accessor below fails safe (returns false) rather than fabricating
 * a register that does not exist.
 */
#ifndef RCP_REQUEST_SEQUENCER_H
#define RCP_REQUEST_SEQUENCER_H

#include "rcp/errors.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The power-on default value of every sequencer-state register. */
#define RCP_SEQUENCER_POWER_ON_STATE ((uint8_t)1u)

/* REQ-SEQ-013 (TC18 §12.7.10 Table 28, relative address 0x0001,
 * "Request_stream_index... refers the Client Nr allowed to access this
 * sequencer"): the reserved "unclaimed" value for owner below. TC18
 * gives no power-on default for this field (unlike Seq_state's own
 * documented "1"), and no register-map mechanism for a client to prove
 * *which* request stream it is other than the stream_id/
 * request_stream_index its own traffic already carries -- this
 * codebase's own design choice (2026-08-13, issue #335, user-approved):
 * a sequencer with no owner yet configured permits no client at all
 * (fail-closed) rather than implicitly granting access to whichever
 * client happens to reach it first. request_stream_index is itself
 * 1-based with 0 reserved as a sentinel throughout this codebase
 * (rcp_regmap_ep_id_map_entry_t's own field, REQ-RMAP-052) -- 0 is
 * therefore never a real client identity this sentinel could collide
 * with. */
#define RCP_SEQUENCER_OWNER_UNCLAIMED ((uint8_t)0u)

/* A heap-allocated table of count independent sequencers -- this
 * module's own dynamic-size representation, sized at runtime from a
 * server's own svr_sequencers_max register value rather than a
 * compile-time cap, matching rcp.h's rcp_bytes_t convention for other
 * runtime-sized buffers in this codebase. state/owner are both NULL iff
 * count == 0, and always allocated/freed together -- neither is ever
 * meaningfully present without the other. owner (REQ-SEQ-013, TC18
 * §12.7.10 Table 28's own Request_stream_index column) records each
 * sequencer's own owning client, RCP_SEQUENCER_OWNER_UNCLAIMED by
 * default. */
typedef struct {
    uint8_t *state;
    uint8_t *owner;
    uint16_t count;
} rcp_sequencer_table_t;

/* Allocates a table of count sequencers, each state initialized to
 * RCP_SEQUENCER_POWER_ON_STATE and each owner initialized to
 * RCP_SEQUENCER_OWNER_UNCLAIMED. count == 0 is legal and is this
 * module's own spelling of "compound operations unsupported entirely"
 * (see rcp_sequencer_table_unsupported()) -- the resulting table's
 * state/owner are then both NULL, and this is not a failure. On
 * allocation failure for a nonzero count, returns a zeroed table
 * (state=NULL, owner=NULL, count=0) -- indistinguishable on its own
 * from a legitimately-zero-count table; callers that requested a
 * nonzero count and need to detect allocation failure must compare the
 * returned table's count against the count they requested, the same
 * convention rcp_bytes_dup() (rcp.h) already establishes for a
 * runtime-sized buffer's own allocation-failure case. */
rcp_sequencer_table_t rcp_sequencer_table_new(uint16_t count);

/* True iff table->count == 0 -- this module's own name for the "0
 * sequencers means compound operations unsupported entirely" rule. */
bool rcp_sequencer_table_unsupported(const rcp_sequencer_table_t *table);

/* Resets every sequencer's own state in table back to
 * RCP_SEQUENCER_POWER_ON_STATE. A no-op on an unsupported (count == 0)
 * table. Deliberately does NOT touch owner -- TC18's own "After
 * power-on/reset all sequencers are in state 1" rule names Seq_state
 * only; Request_stream_index is an ordinary configuration register
 * (like every other R/W* config field this codebase already treats as
 * persisting across a lifecycle reset), not something this narrower,
 * literally-scoped reset should silently also clear. */
void rcp_sequencer_table_reset(rcp_sequencer_table_t *table);

/* Frees table->state/table->owner (if any) and zeroes *table
 * (state=NULL, owner=NULL, count=0). Safe to call on an already-zeroed
 * table. */
void rcp_sequencer_table_free(rcp_sequencer_table_t *table);

/* True iff idx < table->count, i.e. idx addresses a real sequencer-state
 * register in table. */
bool rcp_sequencer_index_valid(const rcp_sequencer_table_t *table, uint16_t idx);

/* Reads table's sequencer idx's current state into *out_state. Returns
 * false (*out_state left untouched) if !rcp_sequencer_index_valid(table,
 * idx). */
bool rcp_sequencer_get_state(const rcp_sequencer_table_t *table, uint16_t idx,
                              uint8_t *out_state);

/* Overwrites table's sequencer idx's state with state. Returns whether
 * idx was valid (table left entirely unchanged when it returns false).
 * This is the one primitive request_compound.h's guarded advance step (see
 * rcp_compound_tick()/rcp_compound_wait_tick()) as well as any future
 * direct register-map write to sequencer_state ultimately calls -- this
 * module assigns idx no special meaning of its own beyond addressing one
 * register in table. This function itself performs no ownership check
 * (REQ-SEQ-013) -- it is the mechanism a caller who has already
 * confirmed rcp_sequencer_access_permitted() applies; see that
 * function's own doc comment for why the check is deliberately kept
 * separate from the mechanism, the same "primitive is a pure predicate,
 * caller enforces" split rcp_lifecycle_field_writable() already
 * establishes for every other access-controlled register in this
 * codebase. */
bool rcp_sequencer_set_state(rcp_sequencer_table_t *table, uint16_t idx, uint8_t state);

/* REQ-SEQ-013: reads table's sequencer idx's current owner
 * (Request_stream_index) into *out_owner. Returns false (*out_owner
 * left untouched) if !rcp_sequencer_index_valid(table, idx). */
bool rcp_sequencer_get_owner(const rcp_sequencer_table_t *table, uint16_t idx,
                              uint8_t *out_owner);

/* REQ-SEQ-013: overwrites table's sequencer idx's owner with owner
 * (RCP_SEQUENCER_OWNER_UNCLAIMED to release it back to unclaimed).
 * Returns whether idx was valid (table left entirely unchanged when it
 * returns false). Like rcp_sequencer_set_state(), this is a pure
 * mechanism with no access check of its own -- a caller (the EP0
 * register-map write dispatcher) is responsible for its own
 * authorization decision about who may claim or reassign a sequencer's
 * ownership in the first place; TC18 states no ownership-of-ownership
 * rule beyond the register's own generic R/W* access type. */
bool rcp_sequencer_set_owner(rcp_sequencer_table_t *table, uint16_t idx, uint8_t owner);

/* REQ-SEQ-013 (TC18 §12.7.10 Table 28): true iff idx is a valid
 * sequencer, its own owner is not RCP_SEQUENCER_OWNER_UNCLAIMED, and
 * that owner equals requester_stream_index -- i.e. requester_stream_index
 * is the one client Table 28's own Request_stream_index names as
 * "allowed to access this sequencer". False for an unclaimed sequencer
 * regardless of requester_stream_index (fail-closed: see
 * RCP_SEQUENCER_OWNER_UNCLAIMED's own doc comment for why an unclaimed
 * sequencer is deliberately not open-access). A caller checks this
 * before every Seq_state read/write reaching this sequencer through
 * ANY path -- the register map directly, or indirectly via a
 * compound/compound-wait request naming this sequencer_index -- both
 * are the exact vector TC18's own Table 28 access-control rule exists
 * to close. */
bool rcp_sequencer_access_permitted(const rcp_sequencer_table_t *table, uint16_t idx,
                                     uint8_t requester_stream_index);

/* ── REQ-WIREERR-005 (issue #163): SEQUENCER_NOT_KNOWN vs UNAUTHORIZED_ACCESS ─
 *
 * rcp_sequencer_access_permitted() above answers one caller-facing
 * question ("may requester_stream_index touch this sequencer right
 * now?") by collapsing two DIFFERENT TC18 error-code-table rejection
 * reasons into a single bool: idx not addressing a real sequencer-state
 * register at all (Table 30's own SEQUENCER_NOT_KNOWN, code 2 -- "the
 * referenced sequencer index isn't configured"), versus idx being a
 * real, configured sequencer this requester simply isn't the owner of
 * (Table 30's UNAUTHORIZED_ACCESS, code 3). A caller building a real
 * Error Response frame needs to distinguish these -- an RC Client
 * cannot tell "you don't own this" from "this doesn't exist" from a
 * bool alone, exactly the diagnostic-precision concern errors.h's own
 * file header raises. rcp_sequencer_access_check() is the same
 * predicate, restated as a three-way outcome; rcp_sequencer_access_
 * permitted() itself is unchanged (still `== RCP_SEQUENCER_ACCESS_OK`)
 * for every existing caller. */
typedef enum {
    RCP_SEQUENCER_ACCESS_OK      = 0, /* idx valid, owner == requester_stream_index */
    RCP_SEQUENCER_ACCESS_UNKNOWN = 1, /* !rcp_sequencer_index_valid(table, idx) */
    RCP_SEQUENCER_ACCESS_DENIED  = 2, /* idx valid, but unclaimed or owned by someone else */
} rcp_sequencer_access_errc_t;

/* Human-readable message for an rcp_sequencer_access_errc_t value. Never
 * returns NULL. */
const char *rcp_sequencer_access_strerror(rcp_sequencer_access_errc_t e);

/* The same access decision rcp_sequencer_access_permitted() makes,
 * restated as a three-way rcp_sequencer_access_errc_t rather than a
 * bool -- see this section's own file-header note for why. Priority
 * mirrors rcp_sequencer_access_permitted()'s own short-circuit order:
 * an unknown idx is reported as RCP_SEQUENCER_ACCESS_UNKNOWN before
 * ownership is even consulted (there is no owner to consult), never as
 * RCP_SEQUENCER_ACCESS_DENIED. */
rcp_sequencer_access_errc_t rcp_sequencer_access_check(const rcp_sequencer_table_t *table,
                                                        uint16_t idx,
                                                        uint8_t requester_stream_index);

/* Maps e to its numbered wire error code (errors.h), for a caller
 * populating a Response frame's err field: RCP_ERROR_SEQUENCER_NOT_KNOWN
 * for RCP_SEQUENCER_ACCESS_UNKNOWN, RCP_ERROR_UNAUTHORIZED_ACCESS for
 * RCP_SEQUENCER_ACCESS_DENIED, RCP_ERROR_NONE for RCP_SEQUENCER_ACCESS_OK
 * (access was permitted -- nothing to report). Mirrors rcp_e2e_wire_
 * error()'s own established pattern exactly. */
rcp_wire_error_t rcp_sequencer_wire_error(rcp_sequencer_access_errc_t e);

#ifdef __cplusplus
}
#endif

#endif /* RCP_REQUEST_SEQUENCER_H */
