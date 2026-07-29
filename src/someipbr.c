#include "rcp/someipbr.h"

//cfusa:req REQ-SOMEIP-002
rcp_someip_config_t rcp_someip_default_config(void)
{
    rcp_someip_config_t c;
    c.service_id  = 0x0100;
    c.instance_id = 0x0001;
    c.method_id   = 0x0001;
    c.timeout_ms  = 500;
    return c;
}

//cfusa:req REQ-SOMEIP-001
int rcp_someip_bridge_send(rcp_someip_config_t cfg, rcp_avtp_addr_t addr,
                           uint8_t request_type,
                           const uint8_t *payload, size_t payload_len,
                           rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
