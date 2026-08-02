/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:test REQ-MDNS-001
//cfusa:test REQ-MDNS-002
//cfusa:test REQ-MDNS-003
//cfusa:test REQ-MDNS-004
//cfusa:test REQ-MDNS-005
//cfusa:test REQ-MDNS-006
//cfusa:test REQ-MDNS-007
//cfusa:test REQ-MDNS-008
//cfusa:test REQ-MDNS-009
//cfusa:test REQ-MDNS-010
//cfusa:test REQ-MDNS-011
#include "unity.h"

#include <rcp/avtp.h>
#include <rcp/mdns.h>
#include <rcp/rcp.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static rcp_stream_id_t make_stream_id(uint8_t last_octet, uint16_t unique_id)
{
    uint8_t mac[6] = {0x00, 0x21, 0xbd, 0x7f, 0x3a, last_octet};
    return rcp_stream_id_make(mac, unique_id);
}

static const rcp_mdns_server_info_t *sample_records(size_t *count)
{
    static rcp_mdns_server_info_t records[3];
    static bool built = false;

    if (!built) {
        records[0].server_stream_id = make_stream_id(0x01, 1);
        records[0].host             = "fl.local";
        records[0].port             = 5000;
        records[0].instance_name    = "0021bd7f3a01-0001.fl.local._rcp-tc18._udp.local";

        records[1].server_stream_id = make_stream_id(0x02, 2);
        records[1].host             = "fr.local";
        records[1].port             = 5001;
        records[1].instance_name    = "0021bd7f3a02-0002.fr.local._rcp-tc18._udp.local";

        records[2].server_stream_id = make_stream_id(0x03, 3);
        records[2].host             = "c.local";
        records[2].port             = 5002;
        records[2].instance_name    = "0021bd7f3a03-0003.c.local._rcp-tc18._udp.local";

        built = true;
    }

    *count = sizeof(records) / sizeof(records[0]);
    return records;
}

/* ── TestAnnouncer: records announce()/withdraw() calls for assertions ────────
 * Mirrors cpp-RCP's TestAnnouncer test double — mdns.h ships no concrete
 * Announcer implementation (platform mDNS responders are out of scope), so
 * this lives entirely in the test file, same as cpp-RCP's test_mdns.cpp. */

typedef struct {
    rcp_stream_id_t        server_stream_id;
    rcp_mdns_server_info_t info;
    char                   host[64];
    char                   instance_name[128];
} announced_entry_t;

typedef struct {
    rcp_mdns_announcer_t base;
    announced_entry_t    entries[8];
    size_t                len;
    bool                  destroyed;
} test_announcer_t;

static int test_announcer_announce(rcp_mdns_announcer_t *self, const rcp_mdns_server_info_t *info)
{
    test_announcer_t *a = (test_announcer_t *)self;
    size_t i;

    for (i = 0; i < a->len; i++) {
        if (rcp_stream_id_equal(a->entries[i].server_stream_id, info->server_stream_id)) {
            a->entries[i].info = *info;
            return RCP_OK;
        }
    }
    a->entries[a->len].server_stream_id = info->server_stream_id;
    a->entries[a->len].info             = *info;
    strncpy(a->entries[a->len].host, info->host ? info->host : "", sizeof(a->entries[a->len].host) - 1);
    strncpy(a->entries[a->len].instance_name, info->instance_name ? info->instance_name : "",
            sizeof(a->entries[a->len].instance_name) - 1);
    a->entries[a->len].info.host          = a->entries[a->len].host;
    a->entries[a->len].info.instance_name = a->entries[a->len].instance_name;
    a->len++;
    return RCP_OK;
}

static void test_announcer_withdraw(rcp_mdns_announcer_t *self, rcp_stream_id_t server_stream_id)
{
    test_announcer_t *a = (test_announcer_t *)self;
    size_t i;
    for (i = 0; i < a->len; i++) {
        if (rcp_stream_id_equal(a->entries[i].server_stream_id, server_stream_id)) {
            a->entries[i] = a->entries[a->len - 1];
            a->len--;
            return;
        }
    }
}

static void test_announcer_destroy(rcp_mdns_announcer_t *self)
{
    ((test_announcer_t *)self)->destroyed = true;
}

static const rcp_mdns_announcer_vtable_t test_announcer_vtable = {
    test_announcer_announce,
    test_announcer_withdraw,
    test_announcer_destroy,
};

static bool announcer_has(const test_announcer_t *a, rcp_stream_id_t server_stream_id, uint16_t *port_out)
{
    size_t i;
    for (i = 0; i < a->len; i++) {
        if (rcp_stream_id_equal(a->entries[i].server_stream_id, server_stream_id)) {
            if (port_out) *port_out = a->entries[i].info.port;
            return true;
        }
    }
    return false;
}

/* ── Discoverer tests ──────────────────────────────────────────────────────── */

static int g_count;

static void count_cb(const rcp_mdns_discovery_event_t *ev, void *user_data)
{
    (void)ev; (void)user_data;
    g_count++;
}

/* rcp_mdns_static_discoverer_new()/_destroy() are exercised (allocation,
 * the records copied by value, and -- for destroy -- real vtable dispatch
 * through the static discoverer's own destroy, ASan-checked in CI for
 * leaks/double-free) by every test below that constructs and tears down a
 * static discoverer; this is the first and simplest of them. */
//cfusa:test REQ-MDNS-010
//cfusa:test REQ-MDNS-011
static void test_static_discoverer_emits_on_start(void)
{
    size_t n;
    const rcp_mdns_server_info_t *records = sample_records(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(records, n);

    g_count = 0;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, count_cb, NULL));
    TEST_ASSERT_EQUAL_INT(3, g_count);

    rcp_mdns_discoverer_destroy(disc);
}

static rcp_stream_id_t g_added[8];
static size_t g_added_len;
static bool g_saw_non_added;

static void added_cb(const rcp_mdns_discovery_event_t *ev, void *user_data)
{
    (void)user_data;
    if (ev->event != RCP_MDNS_EVENT_ADDED) g_saw_non_added = true;
    g_added[g_added_len++] = ev->info.server_stream_id;
}

static void test_start_fires_added_event_per_record(void)
{
    size_t n;
    const rcp_mdns_server_info_t *records = sample_records(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(records, n);

    g_added_len = 0;
    g_saw_non_added = false;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, added_cb, NULL));

    TEST_ASSERT_FALSE(g_saw_non_added);
    TEST_ASSERT_EQUAL_UINT(3, g_added_len);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(records[0].server_stream_id, g_added[0]));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(records[1].server_stream_id, g_added[1]));
    TEST_ASSERT_TRUE(rcp_stream_id_equal(records[2].server_stream_id, g_added[2]));

    rcp_mdns_discoverer_destroy(disc);
}

static rcp_mdns_discoverer_t *g_stop_target;
static int g_stop_count;

static void stop_after_first_cb(const rcp_mdns_discovery_event_t *ev, void *user_data)
{
    (void)ev; (void)user_data;
    g_stop_count++;
    rcp_mdns_discoverer_stop(g_stop_target);
}

static void test_stop_terminates_discovery(void)
{
    size_t n;
    const rcp_mdns_server_info_t *records = sample_records(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(records, n);

    g_stop_target = disc;
    g_stop_count  = 0;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, stop_after_first_cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_stop_count); /* stopped despite 3 configured records */

    rcp_mdns_discoverer_destroy(disc);
}

static rcp_mdns_server_info_t g_first_info;
static bool g_got_first;

static void first_info_cb(const rcp_mdns_discovery_event_t *ev, void *user_data)
{
    (void)user_data;
    if (!g_got_first) {
        g_first_info = ev->info;
        g_got_first  = true;
    }
}

static void test_server_info_carries_host_port_stream_id(void)
{
    size_t n;
    const rcp_mdns_server_info_t *records = sample_records(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(records, n);

    g_got_first = false;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, first_info_cb, NULL));

    TEST_ASSERT_TRUE(g_got_first);
    TEST_ASSERT_TRUE(rcp_stream_id_equal(records[0].server_stream_id, g_first_info.server_stream_id));
    TEST_ASSERT_EQUAL_STRING("fl.local", g_first_info.host);
    TEST_ASSERT_EQUAL_UINT16(5000, g_first_info.port);

    rcp_mdns_discoverer_destroy(disc);
}

static void test_make_instance_name_follows_convention(void)
{
    char buf[128];
    char expected[128];
    rcp_stream_id_t sid = make_stream_id(0x01, 1);
    size_t n = rcp_mdns_make_instance_name(sid, "myhost", buf, sizeof(buf));

    TEST_ASSERT_TRUE(n > 0);
    snprintf(expected, sizeof(expected), "%016" PRIx64 ".myhost._rcp-tc18._udp.local",
              rcp_stream_id_to_u64(sid));
    TEST_ASSERT_EQUAL_STRING(expected, buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "._rcp-tc18._udp.local"));
}

/* ── Announcer tests ──────────────────────────────────────────────────────── */

//cfusa:test REQ-MDNS-007
static void test_announcer_registers_service_record(void)
{
    test_announcer_t ann;
    rcp_mdns_server_info_t info;
    uint16_t port = 0;
    rcp_stream_id_t sid = make_stream_id(0x10, 10);

    memset(&ann, 0, sizeof(ann));
    ann.base.vt = &test_announcer_vtable;

    info.server_stream_id = sid;
    info.host              = "rl.local";
    info.port              = 6000;
    info.instance_name     = "0021bd7f3a10-000a.rl.local._rcp-tc18._udp.local";

    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_announcer_announce(&ann.base, &info));
    TEST_ASSERT_TRUE(announcer_has(&ann, sid, &port));
    TEST_ASSERT_EQUAL_UINT16(6000, port);
}

//cfusa:test REQ-MDNS-008
static void test_withdraw_removes_record(void)
{
    test_announcer_t ann;
    rcp_mdns_server_info_t info;
    rcp_stream_id_t sid = make_stream_id(0x11, 11);

    memset(&ann, 0, sizeof(ann));
    ann.base.vt = &test_announcer_vtable;

    info.server_stream_id = sid;
    info.host              = "rr.local";
    info.port              = 6001;
    info.instance_name     = "0021bd7f3a11-000b.rr.local._rcp-tc18._udp.local";

    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_announcer_announce(&ann.base, &info));
    TEST_ASSERT_TRUE(announcer_has(&ann, sid, NULL));

    rcp_mdns_announcer_withdraw(&ann.base, sid);
    TEST_ASSERT_FALSE(announcer_has(&ann, sid, NULL));
}

static void test_announcer_destroy_dispatches_through_vtable(void)
{
    test_announcer_t ann;

    memset(&ann, 0, sizeof(ann));
    ann.base.vt = &test_announcer_vtable;

    rcp_mdns_announcer_destroy(&ann.base);
    TEST_ASSERT_TRUE(ann.destroyed);

    rcp_mdns_announcer_destroy(NULL); /* must be a no-op, not crash */
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_static_discoverer_emits_on_start);
    RUN_TEST(test_start_fires_added_event_per_record);
    RUN_TEST(test_stop_terminates_discovery);
    RUN_TEST(test_server_info_carries_host_port_stream_id);
    RUN_TEST(test_make_instance_name_follows_convention);
    RUN_TEST(test_announcer_registers_service_record);
    RUN_TEST(test_withdraw_removes_record);
    RUN_TEST(test_announcer_destroy_dispatches_through_vtable);

    return UNITY_END();
}
