/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/request_sequencer.h"

#include "rcp/alloc.h"

#include <stdlib.h>
#include <string.h>

/* REQ-SEQ-002 (issue #338): allocates via rcp_malloc()/rcp_calloc()
 * (alloc.h) rather than calling malloc()/calloc() directly -- the same
 * failure convention either way (NULL on failure), but this indirection
 * is what lets this function's own allocation-failure branch be proven
 * by a real, portable test (tests/test_request_sequencer.c), not merely
 * asserted correct by code inspection. See alloc.h's own file header for
 * the full rationale; this is that module's first opt-in caller. */
//cfusa:req REQ-SEQ-001
//cfusa:req REQ-SEQ-002
//cfusa:req REQ-SEQ-003
rcp_sequencer_table_t rcp_sequencer_table_new(uint16_t count)
{
    rcp_sequencer_table_t table = {0};
    uint8_t *state;
    uint8_t *owner;

    if (count == 0) return table; /* {NULL,NULL,0} -- unsupported by design, not a failure */

    state = (uint8_t *)rcp_malloc((size_t)count);
    if (!state) return table; /* zeroed -- see the header's failure convention */

    owner = (uint8_t *)rcp_calloc((size_t)count, 1); /* RCP_SEQUENCER_OWNER_UNCLAIMED == 0 */
    if (!owner) {
        rcp_free(state);
        return table; /* zeroed -- same failure convention, all-or-nothing allocation */
    }

    memset(state, RCP_SEQUENCER_POWER_ON_STATE, (size_t)count);

    table.state = state;
    table.owner = owner;
    table.count = count;
    return table;
}

//cfusa:req REQ-SEQ-004
bool rcp_sequencer_table_unsupported(const rcp_sequencer_table_t *table)
{
    return table->count == 0;
}

//cfusa:req REQ-SEQ-005
void rcp_sequencer_table_reset(rcp_sequencer_table_t *table)
{
    if (table->count == 0) return;
    memset(table->state, RCP_SEQUENCER_POWER_ON_STATE, (size_t)table->count);
}

//cfusa:req REQ-SEQ-006
void rcp_sequencer_table_free(rcp_sequencer_table_t *table)
{
    rcp_free(table->state);
    rcp_free(table->owner);
    table->state = NULL;
    table->owner = NULL;
    table->count = 0;
}

//cfusa:req REQ-SEQ-007
bool rcp_sequencer_index_valid(const rcp_sequencer_table_t *table, uint16_t idx)
{
    return idx < table->count;
}

//cfusa:req REQ-SEQ-008
//cfusa:req REQ-SEQ-009
bool rcp_sequencer_get_state(const rcp_sequencer_table_t *table, uint16_t idx,
                              uint8_t *out_state)
{
    if (!rcp_sequencer_index_valid(table, idx)) return false;
    *out_state = table->state[idx];
    return true;
}

//cfusa:req REQ-SEQ-010
//cfusa:req REQ-SEQ-011
bool rcp_sequencer_set_state(rcp_sequencer_table_t *table, uint16_t idx, uint8_t state)
{
    if (!rcp_sequencer_index_valid(table, idx)) return false;
    table->state[idx] = state;
    return true;
}

//cfusa:req REQ-SEQ-013
bool rcp_sequencer_get_owner(const rcp_sequencer_table_t *table, uint16_t idx,
                              uint8_t *out_owner)
{
    if (!rcp_sequencer_index_valid(table, idx)) return false;
    *out_owner = table->owner[idx];
    return true;
}

//cfusa:req REQ-SEQ-013
bool rcp_sequencer_set_owner(rcp_sequencer_table_t *table, uint16_t idx, uint8_t owner)
{
    if (!rcp_sequencer_index_valid(table, idx)) return false;
    table->owner[idx] = owner;
    return true;
}

//cfusa:req REQ-SEQ-013
bool rcp_sequencer_access_permitted(const rcp_sequencer_table_t *table, uint16_t idx,
                                     uint8_t requester_stream_index)
{
    if (!rcp_sequencer_index_valid(table, idx)) return false;
    if (table->owner[idx] == RCP_SEQUENCER_OWNER_UNCLAIMED) return false; /* fail-closed */
    return table->owner[idx] == requester_stream_index;
}
