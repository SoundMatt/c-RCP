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
/*
 * sequencer.h -- Persistent sequencer-state registers for the TC18 Remote
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
 * compound.h's compound/compound-wait requests (this same milestone) are
 * the only things that ever read or advance one -- this header models the
 * register itself as a first-class, independently testable primitive, per
 * the roadmap's explicit instruction, rather than folding it into
 * compound.c as a private implementation detail.
 *
 * regmap.h's rcp_regmap_general_t.svr_max_sequencers (a register field
 * reserved, but inert, since milestone 62) is this module's own
 * "svr_sequencers_max" register value by another name -- the count of
 * sequencers rcp_sequencer_table_new() below is expected to be called
 * with. regmap.h's rcp_regmap_general_t.sequencer_state
 * (rcp_regmap_table_ref_t) is the register map's own wire-facing
 * pointer/capacity pair for exposing this same table over EP0; wiring an
 * actual byte_message_info read/write exchange to it is out of this
 * milestone's scope (regmap.h's own file header already documents that
 * exact deferral for every sub-table it declares) -- this header's table
 * type is this module's in-memory model only, deliberately independent of
 * that wire representation.
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
#ifndef RCP_SEQUENCER_H
#define RCP_SEQUENCER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The power-on default value of every sequencer-state register. */
#define RCP_SEQUENCER_POWER_ON_STATE ((uint8_t)1u)

/* A heap-allocated table of count independent 8-bit sequencer-state
 * registers -- this module's own dynamic-size representation, sized at
 * runtime from a server's own svr_max_sequencers register value rather
 * than a compile-time cap, matching rcp.h's rcp_bytes_t convention for
 * other runtime-sized buffers in this codebase. state is NULL iff
 * count == 0. */
typedef struct {
    uint8_t *state;
    uint16_t count;
} rcp_sequencer_table_t;

/* Allocates a table of count sequencer-state registers, each initialized
 * to RCP_SEQUENCER_POWER_ON_STATE. count == 0 is legal and is this
 * module's own spelling of "compound operations unsupported entirely"
 * (see rcp_sequencer_table_unsupported()) -- the resulting table's state
 * is then NULL, and this is not a failure. On allocation failure for a
 * nonzero count, returns a zeroed table (state=NULL, count=0) --
 * indistinguishable on its own from a legitimately-zero-count table;
 * callers that requested a nonzero count and need to detect allocation
 * failure must compare the returned table's count against the count they
 * requested, the same convention rcp_bytes_dup() (rcp.h) already
 * establishes for a runtime-sized buffer's own allocation-failure case. */
rcp_sequencer_table_t rcp_sequencer_table_new(uint16_t count);

/* True iff table->count == 0 -- this module's own name for the "0
 * sequencers means compound operations unsupported entirely" rule. */
bool rcp_sequencer_table_unsupported(const rcp_sequencer_table_t *table);

/* Resets every sequencer in table back to RCP_SEQUENCER_POWER_ON_STATE.
 * A no-op on an unsupported (count == 0) table. */
void rcp_sequencer_table_reset(rcp_sequencer_table_t *table);

/* Frees table->state (if any) and zeroes *table (state=NULL, count=0).
 * Safe to call on an already-zeroed table. */
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
 * This is the one primitive compound.h's guarded advance step (see
 * rcp_compound_tick()/rcp_compound_wait_tick()) as well as any future
 * direct register-map write to sequencer_state ultimately calls -- this
 * module assigns idx no special meaning of its own beyond addressing one
 * register in table. */
bool rcp_sequencer_set_state(rcp_sequencer_table_t *table, uint16_t idx, uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* RCP_SEQUENCER_H */
