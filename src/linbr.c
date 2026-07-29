#include "rcp/linbr.h"

//cfusa:req REQ-LIN-002
rcp_lin_config_t rcp_lin_default_config(void)
{
    rcp_lin_config_t c;
    c.frame_id   = 0x10;
    c.timeout_ms = 50;
    return c;
}

//cfusa:req REQ-LIN-001
int rcp_lin_bridge_send(rcp_lin_config_t cfg, rcp_avtp_addr_t addr,
                         uint8_t request_type,
                         const uint8_t *payload, size_t payload_len,
                         rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
