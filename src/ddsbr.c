#include "rcp/ddsbr.h"

//cfusa:req REQ-DDS-002
rcp_dds_config_t rcp_dds_default_config(void)
{
    rcp_dds_config_t c;
    c.topic_prefix = "rcp";
    c.domain_id    = 0;
    c.timeout_ms   = 500;
    return c;
}

//cfusa:req REQ-DDS-001
int rcp_dds_bridge_send(rcp_dds_config_t cfg, rcp_avtp_addr_t addr,
                        uint8_t request_type,
                        const uint8_t *payload, size_t payload_len,
                        rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
