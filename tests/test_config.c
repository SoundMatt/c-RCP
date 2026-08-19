/* SPDX-License-Identifier: MPL-2.0 */
/* Tests the RC-Server/endpoint manifest loader (ROADMAP.md milestone 77).
 * Replaces the old zone-manifest schema's own test_config.c entirely --
 * see config.h's file header for the new schema.
 *
 * Each REQ-CFG-* requirement's own test-trace marker sits directly
 * above the specific test function that proves it (CONTRIBUTING.md's
 * "Writing a requirement" convention, c-RCP-18/#519/#533) -- this file
 * used to stack all of them once here at the file header instead, the
 * same blind spot #519 documented for REQ-DL-001's own test file: a
 * file-header block satisfies cfusa's coverage gate for every
 * requirement in the file regardless of which test function (if any)
 * actually exercises each one. */
#include "unity.h"

#include <rcp/config.h>
#include <rcp/mock.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>

#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ── parse_json: server fields ─────────────────────────────────────────────── */

//cfusa:test REQ-CFG-016
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

//cfusa:test REQ-CFG-007
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
//cfusa:test REQ-CFG-007
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
//cfusa:test REQ-CFG-007
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

/* MC/DC: parse_server_fields()'s own `k && extract_uint_at(...)` guard for
 * vendor_id needs both conditions shown independent -- every other test
 * either omits the key entirely (k false) or gives it a clean numeric
 * value (both true). This is the missing case: key present, value not a
 * valid number (extract_uint_at's own scan hits a byte that's neither a
 * digit nor '-' and returns false) -- vendor_id must stay at its memset
 * default rather than reading garbage. Also exercises extract_uint_at's
 * own `*from > '9'` disjunct (line 42) with 't' (ASCII 116), a byte
 * neither '-' nor in ['0','9'] -- an independence pair against any of
 * the valid-digit tests above (both hold *from < '0' false; only *from >
 * '9' flips, and the whole condition flips with it). */
//cfusa:test REQ-CFG-007
static void test_parse_server_vendor_id_malformed_value_leaves_default(void)
{
    const char *json = "{ \"server\": { \"vendor_id\": true } }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(0, m.server.vendor_id); /* malformed -> untouched default */

    rcp_config_manifest_free(&m);
}

/* MC/DC: same as above for device_id's own guard, but the malformed byte
 * chosen here ('#', ASCII 35) instead exercises extract_uint_at's
 * `*from < '0'` disjunct (line 42) -- paired against any valid-digit
 * test, where that disjunct is false and the whole condition is false. */
//cfusa:test REQ-CFG-007
static void test_parse_server_device_id_malformed_value_leaves_default(void)
{
    const char *json = "{ \"server\": { \"device_id\": # } }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(0, m.server.device_id); /* malformed -> untouched default */

    rcp_config_manifest_free(&m);
}

/* MC/DC: same as vendor_id/device_id above for magic's own guard (line
 * 236) -- key present, value malformed, so magic stays at its memset
 * default rather than the nonzero value the "negative value" test below
 * proves it CAN take when extract_uint_at succeeds. */
//cfusa:test REQ-CFG-007
static void test_parse_server_magic_malformed_value_leaves_default(void)
{
    const char *json = "{ \"server\": { \"magic\": bad } }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT32(0, m.server.magic); /* malformed -> untouched default */

    rcp_config_manifest_free(&m);
}

/* MC/DC: extract_uint_at's own doc comment says it parses "the first
 * integer (optionally negative)" -- no existing test actually exercises
 * a negative value, which is exactly what's needed to show line 42's
 * `*from != '-'` disjunct independently false (short-circuiting the rest
 * of that condition) against every other test's '-'-free digit, where
 * it's independently true. strtoull's own defined behaviour for a
 * leading '-' (C99 7.20.1.4: negate the unsigned result) means "-1"
 * parses to ULLONG_MAX, truncated by the (uint32_t) cast down to
 * 0xFFFFFFFF -- asserted explicitly here so this isn't just a "does it
 * crash" check. */
//cfusa:test REQ-CFG-007
static void test_parse_server_magic_negative_value_wraps(void)
{
    const char *json = "{ \"server\": { \"magic\": -1 } }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, m.server.magic);

    rcp_config_manifest_free(&m);
}

/* MC/DC: extract_uint_at's leading skip-loop (line 41) ORs five distinct
 * separator checks (space/tab/newline/CR/colon); every existing test's
 * JSON is formatted with plain spaces and a colon, so tab/newline/CR
 * were never independently true in any recorded evaluation. This feeds
 * all three through in one value, each as its own loop iteration (one
 * byte examined per evaluation, so only one of the five can be true at
 * a time) -- independent of the digit-reached iteration where all five
 * are false, already exercised by every passing test above. */
//cfusa:test REQ-CFG-007
static void test_parse_server_vendor_id_skips_tab_newline_cr_separators(void)
{
    const char *json = "{ \"server\": { \"vendor_id\":\t\n\r 42 } }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT16(42, m.server.vendor_id);

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

//cfusa:test REQ-CFG-003
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

//cfusa:test REQ-CFG-002
static void test_parse_hw_pin_map_missing_hw_ep_pin_nr_fails(void)
{
    const char *json = "{ \"hw_pin_map\": [{ \"hw_ep_nr\": 0 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* CORRECTED 2026-08-13 (issue #338, REQ-CFG-001): the dispatch loop's own
 * entry-sniffing condition used to route an object into parse_pin_entry()
 * only when "hw_ep_nr" itself was present -- an object missing hw_ep_nr
 * entirely (even with hw_ep_pin_nr present) matched none of the loop's
 * three sniff conditions and was silently skipped as "any other object",
 * returning RCP_OK with hw_pin_map_len == 0 instead of the documented
 * RCP_CFG_ERR_PARSE. Fixed by adding "hw_ep_pin_nr" as a second sniff
 * key (mirroring the pre-existing "configured" precedent for streams,
 * below) so an entry missing hw_ep_nr is still routed to
 * parse_pin_entry(), which correctly rejects it. */
//cfusa:test REQ-CFG-001
static void test_parse_hw_pin_map_missing_hw_ep_nr_fails(void)
{
    const char *json = "{ \"hw_pin_map\": [{ \"hw_ep_pin_nr\": 3 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: parse_pin_entry()'s own `!k || !extract_uint_at(...)` guard for
 * hw_ep_nr (line 262) needs both conditions shown independent. The
 * "missing entirely" test above only ever has k false (short-circuits
 * before extract_uint_at is even called). This is the other half: the
 * key is present (k true) but its value isn't a number, so
 * extract_uint_at itself returns false -- same rejected outcome, reached
 * via the other condition, paired against every valid-entry test above
 * where both are false. */
//cfusa:test REQ-CFG-001
static void test_parse_hw_pin_map_hw_ep_nr_malformed_value_fails(void)
{
    const char *json = "{ \"hw_pin_map\": [{ \"hw_ep_nr\": bad, \"hw_ep_pin_nr\": 3 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: same as above for hw_ep_pin_nr's own guard (line 269) -- valid
 * hw_ep_nr so the entry reaches the hw_ep_pin_nr check at all, then a
 * malformed hw_ep_pin_nr value. */
//cfusa:test REQ-CFG-002
static void test_parse_hw_pin_map_hw_ep_pin_nr_malformed_value_fails(void)
{
    const char *json = "{ \"hw_pin_map\": [{ \"hw_ep_nr\": 0, \"hw_ep_pin_nr\": bad }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: parse_pin_entry()'s hw_pin_type guard (line 278) is
 * `find_bracket_span(...) && arr_end <= close` -- two conditions, and
 * every existing test only ever hits the both-true case (a real
 * "hw_pin_type": [...] array inside the entry). This is the
 * condition-0-false case: the key is present but its value isn't an
 * array at all, and -- because find_bracket_span() itself searches
 * forward from the key with a bare strchr(), unbounded by this entry's
 * own `close` -- there is no '[' anywhere left in the rest of the
 * document either, so find_bracket_span() returns false outright and
 * hw_pin_type is correctly left at its default. */
//cfusa:test REQ-CFG-003
static void test_parse_hw_pin_type_missing_brackets_leaves_default(void)
{
    const char *json =
        "{ \"hw_pin_map\": [ { \"hw_ep_nr\": 0, \"hw_ep_pin_nr\": 1, \"hw_pin_type\": 5 } ] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, m.hw_pin_map_len);
    TEST_ASSERT_EQUAL_UINT8(0, m.hw_pin_map[0].hw_pin_type);

    rcp_config_manifest_free(&m);
}

/* MC/DC: the condition-1-false case for the same guard -- find_bracket_
 * span() *does* find a '[' ... ']' pair (condition 0 true), but only
 * because its unbounded strchr() search walked straight past this
 * entry's own `close` into the NEXT hw_pin_map entry's own array, so the
 * found `arr_end` lands beyond `close` and the guard correctly refuses
 * to treat a later, unrelated entry's array as this entry's own
 * hw_pin_type. The first entry's own (non-array) "hw_pin_type": 5 value
 * is what makes find_bracket_span() skip past entry 1's close in the
 * first place; the second entry's own real array parses normally,
 * proving this isn't accidentally breaking the well-formed case. */
//cfusa:test REQ-CFG-003
static void test_parse_hw_pin_type_array_leaking_past_entry_close_is_ignored(void)
{
    const char *json =
        "{ \"hw_pin_map\": ["
        "  { \"hw_ep_nr\": 0, \"hw_ep_pin_nr\": 1, \"hw_pin_type\": 5 },"
        "  { \"hw_ep_nr\": 2, \"hw_ep_pin_nr\": 3, \"hw_pin_type\": [\"pull_up\"] }"
        "] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(2, m.hw_pin_map_len);

    TEST_ASSERT_EQUAL_UINT8(0, m.hw_pin_map[0].hw_pin_type); /* leaked array correctly ignored */
    TEST_ASSERT_EQUAL_HEX8(RCP_REGMAP_HW_PIN_PULL_UP,
                            (uint8_t)(m.hw_pin_map[1].hw_pin_type & RCP_REGMAP_HW_PIN_PULL_MASK));

    rcp_config_manifest_free(&m);
}

/* ── parse_json: endpoints ─────────────────────────────────────────────────── */

//cfusa:test REQ-CFG-005
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

//cfusa:test REQ-CFG-014
static void test_parse_endpoint_missing_ep_type_fails(void)
{
    const char *json = "{ \"endpoints\": [{ \"byte_bus_id\": 1 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.endpoints_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* CORRECTED 2026-08-13 (issue #338, REQ-CFG-004): same class of bug as
 * REQ-CFG-001 above, for endpoints -- an object missing byte_bus_id
 * entirely (even with ep_type present) matched none of the dispatch
 * loop's own sniff conditions and was silently skipped instead of
 * rejected. Fixed by adding "ep_type" as a second sniff key. */
//cfusa:test REQ-CFG-004
static void test_parse_endpoint_missing_byte_bus_id_fails(void)
{
    const char *json = "{ \"endpoints\": [{ \"ep_type\": 5 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.endpoints_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: parse_endpoint_entry()'s own `!k || !extract_uint_at(...)` guard
 * for byte_bus_id (line 299) -- key present, value malformed, mirroring
 * the hw_pin_map pattern above. */
//cfusa:test REQ-CFG-004
static void test_parse_endpoint_byte_bus_id_malformed_value_fails(void)
{
    const char *json = "{ \"endpoints\": [{ \"byte_bus_id\": bad, \"ep_type\": 1 }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.endpoints_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: same guard for ep_type (line 306) -- valid byte_bus_id so the
 * entry reaches the ep_type check, then a malformed ep_type value. */
//cfusa:test REQ-CFG-014
static void test_parse_endpoint_ep_type_malformed_value_fails(void)
{
    const char *json = "{ \"endpoints\": [{ \"byte_bus_id\": 1, \"ep_type\": bad }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.endpoints_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: parse_endpoint_entry()'s own optional `k && extract_bool_at(...)`
 * guard for ep_enable (line 313) -- key present, but "1" matches neither
 * "true" nor "false" so extract_bool_at() returns false and ep_enable is
 * correctly left at its memset default rather than reading `b`
 * uninitialized. */
//cfusa:test REQ-CFG-005
static void test_parse_endpoint_ep_enable_malformed_value_leaves_default(void)
{
    const char *json = "{ \"endpoints\": [{ \"byte_bus_id\": 1, \"ep_type\": 2, \"ep_enable\": 1 }] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, m.endpoints_len);
    TEST_ASSERT_FALSE(m.endpoints[0].ep_enable); /* malformed -> untouched default */

    rcp_config_manifest_free(&m);
}

/* ── parse_json: streams ───────────────────────────────────────────────────── */

//cfusa:test REQ-CFG-015
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

//cfusa:test REQ-CFG-006
static void test_parse_stream_missing_rx_stream_id_fails(void)
{
    const char *json = "{ \"streams\": [{ \"configured\": true }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.streams_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: parse_stream_entry()'s own `!k || !extract_uint_at(...)` guard
 * for rx_stream_id (line 331) -- key present, value malformed. */
//cfusa:test REQ-CFG-006
static void test_parse_stream_rx_stream_id_malformed_value_fails(void)
{
    const char *json = "{ \"streams\": [{ \"rx_stream_id\": bad }] }";
    rcp_config_manifest_t m;
    char err[128] = {0};

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT(0, m.streams_len);
    TEST_ASSERT_TRUE(strlen(err) > 0);
}

/* MC/DC: parse_stream_entry()'s own optional `k && extract_bool_at(...)`
 * guard for configured (line 338) -- key present but "nope" matches
 * neither "true" nor "false", so extract_bool_at() returns false and
 * `configured` is correctly left at the entry-exists default (true) that
 * was set before parsing began, rather than being clobbered by an
 * uninitialized `b`. Distinguishable from the explicit-false case
 * test_parse_stream_entries already covers. */
//cfusa:test REQ-CFG-015
static void test_parse_stream_configured_malformed_value_leaves_default(void)
{
    const char *json = "{ \"streams\": [{ \"rx_stream_id\": 5, \"configured\": nope }] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, m.streams_len);
    TEST_ASSERT_TRUE(m.streams[0].configured); /* malformed -> untouched "entry exists" default */

    rcp_config_manifest_free(&m);
}

/* MC/DC: extract_bool_at's own leading skip-loop (line 55, textually
 * identical to extract_uint_at's line-41 loop but a distinct source
 * location/decision instance) -- same gap, same fix: tab/newline/CR
 * fed through before a real "false", each its own loop iteration. */
//cfusa:test REQ-CFG-015
static void test_parse_stream_configured_skips_tab_newline_cr_separators(void)
{
    const char *json = "{ \"streams\": [{ \"rx_stream_id\": 5, \"configured\":\t\n\rfalse }] }";
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_OK, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(1, m.streams_len);
    TEST_ASSERT_FALSE(m.streams[0].configured);

    rcp_config_manifest_free(&m);
}

/* MC/DC: set_err()'s own `err_msg && err_msg_cap > 0` guard (line 15) --
 * every existing error-path test passes a real, adequately-sized buffer
 * (both conditions true). These two show each condition independently
 * false: a NULL err_msg (must not deref/crash), and a real pointer with
 * a zero cap (must not touch the buffer at all, not even to check its
 * contents) -- exercised through the same "missing hw_ep_nr" error path
 * the existing REQ-CFG-001 test already uses with a real buffer, so the
 * only thing that differs between these and that baseline is err_msg/
 * err_msg_cap themselves. */
//cfusa:test REQ-CFG-001
static void test_parse_error_with_null_err_msg_does_not_crash(void)
{
    const char *json = "{ \"hw_pin_map\": [{ \"hw_ep_pin_nr\": 3 }] }"; /* missing hw_ep_nr */
    rcp_config_manifest_t m;

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
}

//cfusa:test REQ-CFG-001
static void test_parse_error_with_zero_err_msg_cap_leaves_buffer_untouched(void)
{
    const char *json = "{ \"hw_pin_map\": [{ \"hw_ep_pin_nr\": 3 }] }"; /* missing hw_ep_nr */
    rcp_config_manifest_t m;
    char err[8];

    memset(err, 0x5A, sizeof(err)); /* sentinel: must survive untouched */

    TEST_ASSERT_EQUAL(RCP_CFG_ERR_PARSE, rcp_config_parse_json(json, &m, err, 0));
    TEST_ASSERT_EQUAL_UINT(0, m.hw_pin_map_len);
    TEST_ASSERT_EQUAL_HEX8(0x5A, (uint8_t)err[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5A, (uint8_t)err[sizeof(err) - 1]);
}

/* ── apply_to_mock / load ──────────────────────────────────────────────────── */

//cfusa:test REQ-CFG-008
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

/* REQ-CFG-017 (split 2026-08-18, issue #533, from a prior REQ-CFG-008
 * that also bundled this with the magic-preservation clause below --
 * see REQ-CFG-008's own .fusa-reqs.json text for the split rationale):
 * svr_implemented_options is ORed into whatever the regmap already
 * carried, not overwritten -- a pre-existing bit the manifest doesn't
 * name must survive alongside the newly-named one. */
//cfusa:test REQ-CFG-017
static void test_apply_to_mock_ors_options_into_regmap(void)
{
    const char *json =
        "{ \"server\": { \"svr_implemented_options\": [\"time_sync\"] } }";
    rcp_mock_server_t *srv = rcp_mock_server_new();

    rcp_mock_server_regmap(srv)->svr_implemented_options = RCP_REGMAP_OPT_TRIGGER;

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_load(json, srv, NULL, 0));
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(RCP_REGMAP_OPT_TRIGGER | RCP_REGMAP_OPT_TIME_SYNC),
                            rcp_mock_server_regmap(srv)->svr_implemented_options);

    rcp_mock_server_destroy(srv);
}

/* REQ-CFG-018 (split 2026-08-18, issue #533, from REQ-CFG-008 -- see
 * that id's own text for the split rationale): magic is left unchanged
 * when the manifest's magic is 0 (even over a pre-existing nonzero
 * regmap value), and is set from the manifest otherwise. Both branches
 * of that one if/else are proven here. */
//cfusa:test REQ-CFG-018
static void test_apply_to_mock_magic_preserved_when_zero_else_set(void)
{
    const char *json_zero    = "{ \"server\": { \"vendor_id\": 3 } }";
    const char *json_nonzero = "{ \"server\": { \"magic\": 999 } }";
    rcp_mock_server_t *srv_a = rcp_mock_server_new();
    rcp_mock_server_t *srv_b = rcp_mock_server_new();

    rcp_mock_server_regmap(srv_a)->magic = 0xDEADBEEFu; /* pre-seeded, must survive */

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_load(json_zero, srv_a, NULL, 0));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, rcp_mock_server_regmap(srv_a)->magic);

    TEST_ASSERT_EQUAL(RCP_MOCK_OK, rcp_config_load(json_nonzero, srv_b, NULL, 0));
    TEST_ASSERT_EQUAL_UINT32(999, rcp_mock_server_regmap(srv_b)->magic);

    rcp_mock_server_destroy(srv_a);
    rcp_mock_server_destroy(srv_b);
}

//cfusa:test REQ-CFG-009
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

//cfusa:test REQ-CFG-019
//cfusa:test REQ-CFG-020
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

//cfusa:test REQ-CFG-010
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

//cfusa:test REQ-CFG-016
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
    RUN_TEST(test_parse_server_vendor_id_malformed_value_leaves_default);
    RUN_TEST(test_parse_server_device_id_malformed_value_leaves_default);
    RUN_TEST(test_parse_server_magic_malformed_value_leaves_default);
    RUN_TEST(test_parse_server_magic_negative_value_wraps);
    RUN_TEST(test_parse_server_vendor_id_skips_tab_newline_cr_separators);

    RUN_TEST(test_manifest_free_zeroes_the_struct_and_tolerates_double_free);
    RUN_TEST(test_parse_hw_pin_map_entries);
    RUN_TEST(test_parse_hw_pin_map_missing_hw_ep_pin_nr_fails);
    RUN_TEST(test_parse_hw_pin_map_missing_hw_ep_nr_fails);
    RUN_TEST(test_parse_hw_pin_map_hw_ep_nr_malformed_value_fails);
    RUN_TEST(test_parse_hw_pin_map_hw_ep_pin_nr_malformed_value_fails);
    RUN_TEST(test_parse_hw_pin_type_missing_brackets_leaves_default);
    RUN_TEST(test_parse_hw_pin_type_array_leaking_past_entry_close_is_ignored);

    RUN_TEST(test_parse_endpoint_entries);
    RUN_TEST(test_parse_endpoint_missing_ep_type_fails);
    RUN_TEST(test_parse_endpoint_missing_byte_bus_id_fails);
    RUN_TEST(test_parse_endpoint_byte_bus_id_malformed_value_fails);
    RUN_TEST(test_parse_endpoint_ep_type_malformed_value_fails);
    RUN_TEST(test_parse_endpoint_ep_enable_malformed_value_leaves_default);

    RUN_TEST(test_parse_stream_entries);
    RUN_TEST(test_parse_stream_missing_rx_stream_id_fails);
    RUN_TEST(test_parse_stream_rx_stream_id_malformed_value_fails);
    RUN_TEST(test_parse_stream_configured_malformed_value_leaves_default);
    RUN_TEST(test_parse_stream_configured_skips_tab_newline_cr_separators);

    RUN_TEST(test_parse_error_with_null_err_msg_does_not_crash);
    RUN_TEST(test_parse_error_with_zero_err_msg_cap_leaves_buffer_untouched);

    RUN_TEST(test_apply_to_mock_sets_regmap_fields);
    RUN_TEST(test_apply_to_mock_ors_options_into_regmap);
    RUN_TEST(test_apply_to_mock_magic_preserved_when_zero_else_set);
    RUN_TEST(test_apply_to_mock_registers_endpoints);
    RUN_TEST(test_apply_to_mock_duplicate_byte_bus_id_returns_dup);
    RUN_TEST(test_load_propagates_parse_error);
    RUN_TEST(test_full_manifest_round_trip);

    return UNITY_END();
}
