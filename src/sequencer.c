#include "rcp/sequencer.h"

#include <stdlib.h>
#include <string.h>

//cfusa:req REQ-SEQ-001
//cfusa:req REQ-SEQ-002
//cfusa:req REQ-SEQ-003
rcp_sequencer_table_t rcp_sequencer_table_new(uint16_t count)
{
    rcp_sequencer_table_t table = {0};
    uint8_t *state;

    if (count == 0) return table; /* {NULL,0} -- unsupported by design, not a failure */

    state = (uint8_t *)malloc((size_t)count);
    if (!state) return table; /* zeroed -- see the header's failure convention */

    memset(state, RCP_SEQUENCER_POWER_ON_STATE, (size_t)count);

    table.state = state;
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
    free(table->state);
    table->state = NULL;
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
