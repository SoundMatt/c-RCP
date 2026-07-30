/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/udsbr.h"

//cfusa:req REQ-UDS-002
rcp_uds_config_t rcp_uds_default_config(void)
{
    rcp_uds_config_t c;
    c.routine_id       = 0x0100;
    c.p2_timeout_ms    = 50;
    c.p2ext_timeout_ms = 5000;
    return c;
}

//cfusa:req REQ-UDS-001
int rcp_uds_bridge_send(rcp_uds_config_t cfg, rcp_avtp_addr_t addr,
                        uint8_t request_type,
                        const uint8_t *payload, size_t payload_len,
                        rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
