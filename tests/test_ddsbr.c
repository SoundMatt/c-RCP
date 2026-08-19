/* SPDX-License-Identifier: MPL-2.0 */
/* ddsbr protocol-bridge stub conformance tests.
 *
 * rcp_dds_bridge_send() is a compile-time interface stub: until a concrete
 * DDS backend is linked, it always returns RCP_ERR_NOT_SUPPORTED and never
 * touches *out_response. These tests pin that contract so callers get a
 * well-defined error rather than undefined behaviour, and separately pin
 * rcp_dds_default_config()'s documented defaults.
 */
#include "unity.h"

#include <rcp/ddsbr.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_avtp_addr_t make_addr(void)
{
    uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    rcp_avtp_addr_t a;
    a.stream_id   = rcp_stream_id_make(mac, 1);
    a.byte_bus_id = 3;
    return a;
}

//cfusa:test REQ-DDS-001
static void test_send_returns_not_supported_when_stub(void)
{
    uint8_t payload[] = {0x01, 0x02, 0x03};
    rcp_bytes_t resp;

    memset(&resp, 0, sizeof(resp));
    TEST_ASSERT_EQUAL(RCP_ERR_NOT_SUPPORTED,
                       rcp_dds_bridge_send(rcp_dds_default_config(), make_addr(), 0x00,
                                           payload, sizeof(payload), &resp));

    /* Contract: on failure *out_response is left untouched. */
    TEST_ASSERT_NULL(resp.data);
    TEST_ASSERT_EQUAL(0, resp.len);
}

//cfusa:test REQ-DDS-002
static void test_default_config_returns_documented_defaults(void)
{
    rcp_dds_config_t cfg = rcp_dds_default_config();

    TEST_ASSERT_EQUAL_STRING("rcp", cfg.topic_prefix);
    TEST_ASSERT_EQUAL(0, cfg.domain_id);
    TEST_ASSERT_EQUAL_UINT64(500, cfg.timeout_ms);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_send_returns_not_supported_when_stub);
    RUN_TEST(test_default_config_returns_documented_defaults);

    return UNITY_END();
}
