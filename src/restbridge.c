#include "rcp/restbridge.h"

//cfusa:req REQ-REST-002
rcp_rest_config_t rcp_rest_default_config(void)
{
    rcp_rest_config_t c;
    c.base_url           = NULL;
    c.max_retries        = 3;
    c.request_timeout_ms = 1000;
    return c;
}

//cfusa:req REQ-REST-001
int rcp_rest_bridge_send(rcp_rest_config_t cfg, rcp_avtp_addr_t addr,
                         uint8_t request_type,
                         const uint8_t *payload, size_t payload_len,
                         rcp_bytes_t *out_response)
{
    (void)cfg; (void)addr; (void)request_type; (void)payload; (void)payload_len;
    (void)out_response;
    return RCP_ERR_NOT_SUPPORTED;
}
