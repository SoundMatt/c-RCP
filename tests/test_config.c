/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-CFG-001
//cfusa:test REQ-CFG-002
//cfusa:test REQ-CFG-003
//cfusa:test REQ-CFG-004
//cfusa:test REQ-CFG-005
//cfusa:test REQ-CFG-006
//cfusa:test REQ-CFG-007
//cfusa:test REQ-CFG-008
//cfusa:test REQ-CFG-009
//cfusa:test REQ-CFG-010
//cfusa:test REQ-CFG-013
/* Tests the RC-Server/endpoint manifest loader (ROADMAP.md milestone 77).
 * Replaces the old zone-manifest schema's own test_config.c entirely --
 * see config.h's file header for the new schema. */
#include "unity.h"

#include <rcp/config.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── parse_json: server fields ─────────────────────────────────────────────── */

static void test_parse_empty_object_succeeds(void)
{
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json("{}", &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_EQUAL_UINT(0, m.endpoints_len);
    TEST_ASSERT_EQUAL_UINT(0, m.streams_len);
    TEST_ASSERT_EQUAL_UINT16(0, m.server.vendor_id);

    rcp_config_manifest_free(&m);
}

static void test_parse_server_fields(void)
{
    const char *json =
        "{ \"server\": { \"vendor_id\": 17, \"device_id\": 42, \"magic\": 12345 } }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(17, m.server.vendor_id);
    TEST_ASSERT_EQUAL_UINT16(42, m.server.device_id);
    TEST_ASSERT_EQUAL_UINT32(12345, m.server.magic);

    rcp_config_manifest_free(&m);
}

/* REQ-RMAP-030: five independent single bits, no pairing -- "time_sync"
 * and "compound_bundles" each set exactly their own one bit now,
 * matching TC18 Table 18 exactly (formerly two paired bits each, an
 * invented design REQ-RMAP-004..008 described and this milestone
 * retired -- see test_regmap.c's own retirement note). */
static void test_parse_server_implemented_options(void)
{
    const char *json =
        "{ \"server\": { \"svr_implemented_options\": [\"time_sync\", \"compound_bundles\"] } }";
    rcp_config_manifest_t m;
    uint8_t opts;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    opts = m.server.svr_implemented_options;

    TEST_ASSERT_TRUE((opts & RCP_REGMAP_OPT_TIME_SYNC) != 0);
    TEST_ASSERT_TRUE((opts & RCP_REGMAP_OPT_COMPOUND_WAIT) != 0);
    TEST_ASSERT_FALSE((opts & RCP_REGMAP_OPT_ENH_CANCEL) != 0);
    TEST_ASSERT_FALSE((opts & RCP_REGMAP_OPT_TRIGGER) != 0);
    TEST_ASSERT_FALSE((opts & RCP_REGMAP_OPT_CHAINED) != 0);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(RCP_REGMAP_OPT_TIME_SYNC | RCP_REGMAP_OPT_COMPOUND_WAIT), opts);

    rcp_config_manifest_free(&m);
}

/* REQ-RMAP-030: "trigger" and "chained" are new, previously-unparseable
 * names -- proves the parser now accepts both. */
static void test_parse_server_implemented_options_trigger_and_chained(void)
{
    const char *json =
        "{ \"server\": { \"svr_implemented_options\": [\"trigger\", \"chained\"] } }";
    rcp_config_manifest_t m;
    uint8_t opts;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    opts = m.server.svr_implemented_options;

    TEST_ASSERT_TRUE((opts & RCP_REGMAP_OPT_TRIGGER) != 0);
    TEST_ASSERT_TRUE((opts & RCP_REGMAP_OPT_CHAINED) != 0);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(RCP_REGMAP_OPT_TRIGGER | RCP_REGMAP_OPT_CHAINED), opts);

    rcp_config_manifest_free(&m);
}

/* ── parse_json: hw_pin_map ────────────────────────────────────────────────── */

/* rcp_config_manifest_free()'s own contract (include/rcp/config.h): frees
 * every owned array and zeroes *m in place -- verified here with a
 * non-empty manifest (so there is real heap allocation to free), then a
 * second free of the now-zeroed struct to confirm it is also safe on an
 * already-freed/zero-initialized manifest, per the header's own doc
 * comment. */
//cfusa:test REQ-CFG-013
static void test_manifest_free_zeroes_the_struct_and_tolerates_double_free(void)
{
    const char *json =
        "{ \"hw_pin_map\": [ { \"hw_ep_nr\": 0, \"hw_ep_pin_nr\": 3 } ] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, m.hw_pin_map_len);
    TEST_ASSERT_NOT_NULL(m.hw_pin_map);

    rcp_config_manifest_free(&m);
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_EQUAL_UINT(0, m.endpoints_len);
    TEST_ASSERT_EQUAL_UINT(0, m.streams_len);
    TEST_ASSERT_NULL(m.hw_pin_map);
    TEST_ASSERT_NULL(m.endpoints);
    TEST_ASSERT_NULL(m.streams);

    rcp_config_manifest_free(&m); /* double free of the now-zeroed struct: must not crash */
}

static void test_parse_hw_pin_map_entries(void)
{
    const char *json =
        "{ \"hw_pin_map\": ["
        "  { \"hw_ep_nr\": 0, \"hw_ep_pin_nr\": 3, \"hw_pin_type\": [\"push_pull\", \"pull_up\"] },"
        "  { \"hw_ep_nr\": 1, \"hw_ep_pin_nr\": 4 }"
        "] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(2, m.hw_pin_map_len);

    TEST_ASSERT_EQUAL_UINT8(0, m.hw_pin_map[0].hw_ep_nr);
    TEST_ASSERT_EQUAL_UINT8(3, m.hw_pin_map[0].hw_ep_pin_nr);
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_STAGE_PUSH_PULL,
                            (uint8_t)(m.hw_pin_map[0].hw_pin_type & RCP_REGMAP_HW_PIN_STAGE_MASK));
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_PULL_UP,
                            (uint8_t)(m.hw_pin_map[0].hw_pin_type & RCP_REGMAP_HW_PIN_PULL_MASK));
    /* Drive strength and Schmitt-Trigger weren't named -- both default. */
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_DRIVE_INPUT,
                            (uint8_t)(m.hw_pin_map[0].hw_pin_type & RCP_REGMAP_HW_PIN_DRIVE_MASK));
    TEST_ASSERT_FALSE((m.hw_pin_map[0].hw_pin_type & RCP_REGMAP_HW_PIN_SCHMITT_TRIGGER) != 0);

    TEST_ASSERT_EQUAL_UINT8(1, m.hw_pin_map[1].hw_ep_nr);
    TEST_ASSERT_EQUAL_UINT8(0, m.hw_pin_map[1].hw_pin_type); /* omitted -> 0 */

    rcp_config_manifest_free(&m);
}

static void test_parse_hw_pin_map_missing_hw_ep_pin_nr_fails(void)
{
    const char *json = "{ \"hw_pin_map\": [{ \"hw_ep_nr\": 0 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* ── parse_json: endpoints ─────────────────────────────────────────────────── */

static void test_parse_endpoint_entries(void)
{
    const char *json =
        "{ \"endpoints\": ["
        "  { \"byte_bus_id\": 1, \"ep_type\": 5, \"ep_enable\": true },"
        "  { \"byte_bus_id\": 2, \"ep_type\": 9 }"
        "] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(2, m.endpoints_len);

    TEST_ASSERT_EQUAL_UINT8(1, m.endpoints[0].byte_bus_id);
    TEST_ASSERT_EQUAL_UINT8(5, m.endpoints[0].ep_type);
    TEST_ASSERT_TRUE(m.endpoints[0].ep_enable);

    TEST_ASSERT_EQUAL_UINT8(2, m.endpoints[1].byte_bus_id);
    TEST_ASSERT_FALSE(m.endpoints[1].ep_enable); /* omitted -> false */

    rcp_config_manifest_free(&m);
}

static void test_parse_endpoint_missing_ep_type_fails(void)
{
    const char *json = "{ \"endpoints\": [{ \"byte_bus_id\": 1 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.endpoints_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* ── parse_json: streams ───────────────────────────────────────────────────── */

static void test_parse_stream_entries(void)
{
    const char *json =
        "{ \"streams\": ["
        "  { \"rx_stream_id\": 1001 },"
        "  { \"rx_stream_id\": 1002, \"configured\": false }"
        "] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(2, m.streams_len);

    TEST_ASSERT_EQUAL_UINT64(1001, m.streams[0].rx_stream_id);
    TEST_ASSERT_TRUE(m.streams[0].configured); /* omitted -> true (entry exists) */

    TEST_ASSERT_EQUAL_UINT64(1002, m.streams[1].rx_stream_id);
    TEST_ASSERT_FALSE(m.streams[1].configured);

    rcp_config_manifest_free(&m);
}

static void test_parse_stream_missing_rx_stream_id_fails(void)
{
    const char *json = "{ \"streams\": [{ \"configured\": true }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.streams_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* ── apply_to_mock / load ──────────────────────────────────────────────────── */

static void test_apply_to_mock_sets_regmap_fields(void)
{
    const char *json = "{ \"server\": { \"vendor_id\": 3, \"device_id\": 4 } }";
    rcp_mock_server_t *srv = rcp_mock_server_new();
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_load(json, srv, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT16(3, rcp_mock_server_regmap(srv)->vendor_id);
    TEST_ASSERT_EQUAL_UINT16(4, rcp_mock_server_regmap(srv)->device_id);

    rcp_mock_server_destroy(srv);
}

static void test_apply_to_mock_registers_endpoints(void)
{
    const char *json =
        "{ \"endpoints\": ["
        "  { \"byte_bus_id\": 1, \"ep_type\": 5, \"ep_enable\": true },"
        "  { \"byte_bus_id\": 2, \"ep_type\": 6 }"
        "] }";
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_load(json, srv, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(2, rcp_mock_server_regmap(srv)->svr_ep_count);
    TEST_ASSERT_EQUAL_UINT(0, rcp_mock_server_endpoint_queue_len(srv, 1));

    rcp_mock_server_destroy(srv);
}

static void test_apply_to_mock_duplicate_byte_bus_id_returns_dup(void)
{
    const char *json =
        "{ \"endpoints\": ["
        "  { \"byte_bus_id\": 1, \"ep_type\": 5 },"
        "  { \"byte_bus_id\": 1, \"ep_type\": 6 }"
        "] }";
    rcp_mock_server_t *srv = rcp_mock_server_new();
    char err[128] = {0};

    TEST_ASSERT_EQUAL((int)RCP_MOCK_ERR_DUPLICATE_BUS_ID, rcp_config_load(json, srv, err, sizeof(err)));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_mock_server_destroy(srv);
}

static void test_load_propagates_parse_error(void)
{
    const char *json = "{ \"endpoints\": [{ \"byte_bus_id\": 1 }] }"; /* missing ep_type */
    rcp_mock_server_t *srv = rcp_mock_server_new();
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_load(json, srv, err, sizeof(err)));
    TEST_ASSERT_TRUE(strlen(err) > 0);
    TEST_ASSERT_EQUAL_UINT16(0, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_mock_server_destroy(srv);
}

static void test_full_manifest_round_trip(void)
{
    const char *json =
        "{"
        "  \"server\": { \"vendor_id\": 1, \"svr_implemented_options\": [\"enhanced_cancel\"] },"
        "  \"hw_pin_map\": [{ \"hw_ep_nr\": 0, \"hw_ep_pin_nr\": 1, \"hw_pin_type\": [\"push_pull\"] }],"
        "  \"endpoints\": [{ \"byte_bus_id\": 9, \"ep_type\": 1, \"ep_enable\": true }],"
        "  \"streams\": [{ \"rx_stream_id\": 55 }]"
        "}";
    rcp_config_manifest_t m;
    rcp_mock_server_t *srv = rcp_mock_server_new();

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, m.hw_pin_map_len);
    TEST_ASSERT_EQUAL_UINT(1, m.endpoints_len);
    TEST_ASSERT_EQUAL_UINT(1, m.streams_len);

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_apply_to_mock(&m, srv));
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->vendor_id);
    TEST_ASSERT_EQUAL_UINT16(1, rcp_mock_server_regmap(srv)->svr_ep_count);

    rcp_config_manifest_free(&m);
    rcp_mock_server_destroy(srv);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_parse_empty_object_succeeds);
    RUN_TEST(test_parse_server_fields);
    RUN_TEST(test_parse_server_implemented_options);
    RUN_TEST(test_parse_server_implemented_options_trigger_and_chained);

    RUN_TEST(test_manifest_free_zeroes_the_struct_and_tolerates_double_free);
    RUN_TEST(test_parse_hw_pin_map_entries);
    RUN_TEST(test_parse_hw_pin_map_missing_hw_ep_pin_nr_fails);

    RUN_TEST(test_parse_endpoint_entries);
    RUN_TEST(test_parse_endpoint_missing_ep_type_fails);

    RUN_TEST(test_parse_stream_entries);
    RUN_TEST(test_parse_stream_missing_rx_stream_id_fails);

    RUN_TEST(test_apply_to_mock_sets_regmap_fields);
    RUN_TEST(test_apply_to_mock_registers_endpoints);
    RUN_TEST(test_apply_to_mock_duplicate_byte_bus_id_returns_dup);
    RUN_TEST(test_load_propagates_parse_error);
    RUN_TEST(test_full_manifest_round_trip);

    return UNITY_END();
}
