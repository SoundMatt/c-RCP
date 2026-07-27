//cfusa:test REQ-CFG-001
//cfusa:test REQ-CFG-002
//cfusa:test REQ-CFG-003
//cfusa:test REQ-CFG-004
//cfusa:test REQ-CFG-005
//cfusa:test REQ-CFG-006
#include "unity.h"

#include <rcp/config.h>
#include <rcp/proxy.h>
#include <rcp/rcp.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_parse_json_two_zones(void)
{
    const char *json =
        "{"
        "  \"zones\": ["
        "    { \"zone\": \"FrontLeft\",  \"priority\": \"Normal\" },"
        "    { \"zone\": \"FrontRight\", \"priority\": \"High\"   }"
        "  ]"
        "}";
    rcp_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(2, m.zones_len);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, m.zones[0].zone);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_RIGHT, m.zones[1].zone);
    TEST_ASSERT_EQUAL_STRING("High", m.zones[1].priority);

    rcp_manifest_free(&m);
}

static void test_parse_json_unknown_zone_fails(void)
{
    const char *json = "{ \"zones\": [{ \"zone\": \"BadZone\" }] }";
    rcp_manifest_t m;
    char err[128];

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_NULL(m.zones);
    TEST_ASSERT_NOT_NULL(strstr(err, "BadZone"));
}

static void test_load_registers_controllers(void)
{
    const char *json =
        "{"
        "  \"zones\": ["
        "    { \"zone\": \"RearLeft\"  },"
        "    { \"zone\": \"RearRight\" }"
        "  ]"
        "}";
    rcp_registry_t *reg = rcp_proxy_registry_new(); /* starts empty */
    rcp_controller_t *ctrl = NULL;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_load(json, reg, NULL, 0));

    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_REAR_LEFT, &ctrl));
    rcp_controller_release(ctrl);
    TEST_ASSERT_EQUAL(RCP_OK, rcp_registry_lookup(reg, RCP_ZONE_REAR_RIGHT, &ctrl));
    rcp_controller_release(ctrl);

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_load_duplicate_zone_returns_already_exists(void)
{
    const char *json =
        "{"
        "  \"zones\": ["
        "    { \"zone\": \"Central\" },"
        "    { \"zone\": \"Central\" }"
        "  ]"
        "}";
    rcp_registry_t *reg = rcp_proxy_registry_new();

    TEST_ASSERT_EQUAL(RCP_ERR_ALREADY_EXISTS, rcp_config_load(json, reg, NULL, 0));

    rcp_registry_close(reg);
    rcp_registry_destroy(reg);
}

static void test_parse_error_carries_descriptive_message(void)
{
    rcp_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(
        "{ \"zones\": [{ \"zone\": \"Nope\" }] }", &m, err, sizeof(err)));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    TEST_ASSERT_NOT_NULL(strstr(err, "Nope"));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_parse_json_two_zones);
    RUN_TEST(test_parse_json_unknown_zone_fails);
    RUN_TEST(test_load_registers_controllers);
    RUN_TEST(test_load_duplicate_zone_returns_already_exists);
    RUN_TEST(test_parse_error_carries_descriptive_message);

    return UNITY_END();
}
