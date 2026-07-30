/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/doipbr.h"

//cfusa:req REQ-DOIP-002
rcp_doip_config_t rcp_doip_default_config(void)
{
    rcp_doip_config_t c;
    c.server_ip      = NULL;
    c.server_port    = 13400;
    c.logical_addr   = 0x0001;
    c.tcp_timeout_ms = 2000;
    return c;
}

//cfusa:req REQ-DOIP-001
int rcp_doip_bridge_send(rcp_doip_config_t cfg, rcp_avtp_addr_t addr,
                         uint8_t request_type,
                         const uint8_t *payload, size_t payload_len,
                         rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
