//cfusa:test REQ-MDNS-001
//cfusa:test REQ-MDNS-002
//cfusa:test REQ-MDNS-003
//cfusa:test REQ-MDNS-004
//cfusa:test REQ-MDNS-005
//cfusa:test REQ-MDNS-006
//cfusa:test REQ-MDNS-007
//cfusa:test REQ-MDNS-008
#include "unity.h"

#include <rcp/mdns.h>
#include <rcp/rcp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static const rcp_mdns_zone_info_t *sample_zones(size_t *count)
{
    static const rcp_mdns_zone_info_t zones[] = {
        {RCP_ZONE_FRONT_LEFT,  "fl.local", 5000, "front-left.fl.local._rcp._udp.local"},
        {RCP_ZONE_FRONT_RIGHT, "fr.local", 5001, "front-right.fr.local._rcp._udp.local"},
        {RCP_ZONE_CENTRAL,     "c.local",  5002, "central.c.local._rcp._udp.local"},
    };
    *count = sizeof(zones) / sizeof(zones[0]);
    return zones;
}

/* ── TestAnnouncer: records announce()/withdraw() calls for assertions ────────
 * Mirrors cpp-RCP's TestAnnouncer test double — mdns.h ships no concrete
 * Announcer implementation (platform mDNS responders are out of scope), so
 * this lives entirely in the test file, same as cpp-RCP's test_mdns.cpp. */

typedef struct {
    rcp_zone_t zone;
    rcp_mdns_zone_info_t info;
    char host[64];
    char instance_name[128];
} announced_entry_t;

typedef struct {
    rcp_mdns_announcer_t base;
    announced_entry_t     entries[8];
    size_t                len;
} test_announcer_t;

static int test_announcer_announce(rcp_mdns_announcer_t *self, const rcp_mdns_zone_info_t *info)
{
    test_announcer_t *a = (test_announcer_t *)self;
    size_t i;

    for (i = 0; i < a->len; i++) {
        if (a->entries[i].zone == info->zone) {
            a->entries[i].info = *info;
            return RCP_OK;
        }
    }
    a->entries[a->len].zone = info->zone;
    a->entries[a->len].info = *info;
    strncpy(a->entries[a->len].host, info->host ? info->host : "", sizeof(a->entries[a->len].host) - 1);
    strncpy(a->entries[a->len].instance_name, info->instance_name ? info->instance_name : "",
            sizeof(a->entries[a->len].instance_name) - 1);
    a->entries[a->len].info.host          = a->entries[a->len].host;
    a->entries[a->len].info.instance_name = a->entries[a->len].instance_name;
    a->len++;
    return RCP_OK;
}

static void test_announcer_withdraw(rcp_mdns_announcer_t *self, rcp_zone_t zone)
{
    test_announcer_t *a = (test_announcer_t *)self;
    size_t i;
    for (i = 0; i < a->len; i++) {
        if (a->entries[i].zone == zone) {
            a->entries[i] = a->entries[a->len - 1];
            a->len--;
            return;
        }
    }
}

static void test_announcer_destroy(rcp_mdns_announcer_t *self) { (void)self; }

static const rcp_mdns_announcer_vtable_t test_announcer_vtable = {
    test_announcer_announce,
    test_announcer_withdraw,
    test_announcer_destroy,
};

static bool announcer_has(const test_announcer_t *a, rcp_zone_t zone, uint16_t *port_out)
{
    size_t i;
    for (i = 0; i < a->len; i++) {
        if (a->entries[i].zone == zone) {
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

static void test_static_discoverer_emits_on_start(void)
{
    size_t n;
    const rcp_mdns_zone_info_t *zones = sample_zones(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(zones, n);

    g_count = 0;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, count_cb, NULL));
    TEST_ASSERT_EQUAL_INT(3, g_count);

    rcp_mdns_discoverer_destroy(disc);
}

static rcp_zone_t g_added[8];
static size_t g_added_len;
static bool g_saw_non_added;

static void added_cb(const rcp_mdns_discovery_event_t *ev, void *user_data)
{
    (void)user_data;
    if (ev->event != RCP_MDNS_EVENT_ADDED) g_saw_non_added = true;
    g_added[g_added_len++] = ev->info.zone;
}

static void test_start_fires_added_event_per_zone(void)
{
    size_t n;
    const rcp_mdns_zone_info_t *zones = sample_zones(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(zones, n);

    g_added_len = 0;
    g_saw_non_added = false;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, added_cb, NULL));

    TEST_ASSERT_FALSE(g_saw_non_added);
    TEST_ASSERT_EQUAL_UINT(3, g_added_len);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, g_added[0]);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_RIGHT, g_added[1]);
    TEST_ASSERT_EQUAL(RCP_ZONE_CENTRAL, g_added[2]);

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
    const rcp_mdns_zone_info_t *zones = sample_zones(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(zones, n);

    g_stop_target = disc;
    g_stop_count  = 0;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, stop_after_first_cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_stop_count); /* stopped despite 3 configured zones */

    rcp_mdns_discoverer_destroy(disc);
}

static rcp_mdns_zone_info_t g_first_info;
static bool g_got_first;

static void first_info_cb(const rcp_mdns_discovery_event_t *ev, void *user_data)
{
    (void)user_data;
    if (!g_got_first) {
        g_first_info = ev->info;
        g_got_first  = true;
    }
}

static void test_zone_info_carries_host_port_zone(void)
{
    size_t n;
    const rcp_mdns_zone_info_t *zones = sample_zones(&n);
    rcp_mdns_discoverer_t *disc = rcp_mdns_static_discoverer_new(zones, n);

    g_got_first = false;
    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_discoverer_start(disc, first_info_cb, NULL));

    TEST_ASSERT_TRUE(g_got_first);
    TEST_ASSERT_EQUAL(RCP_ZONE_FRONT_LEFT, g_first_info.zone);
    TEST_ASSERT_EQUAL_STRING("fl.local", g_first_info.host);
    TEST_ASSERT_EQUAL_UINT16(5000, g_first_info.port);

    rcp_mdns_discoverer_destroy(disc);
}

static void test_make_instance_name_follows_convention(void)
{
    char buf[128];
    char expected[128];
    size_t n = rcp_mdns_make_instance_name(RCP_ZONE_FRONT_LEFT, "myhost", buf, sizeof(buf));

    TEST_ASSERT_TRUE(n > 0);
    snprintf(expected, sizeof(expected), "%s.myhost._rcp._udp.local", rcp_zone_string(RCP_ZONE_FRONT_LEFT));
    TEST_ASSERT_EQUAL_STRING(expected, buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "._rcp._udp.local"));
}

/* ── Announcer tests ──────────────────────────────────────────────────────── */

static void test_announcer_registers_zone_record(void)
{
    test_announcer_t ann;
    rcp_mdns_zone_info_t info;
    uint16_t port = 0;

    memset(&ann, 0, sizeof(ann));
    ann.base.vt = &test_announcer_vtable;

    info.zone          = RCP_ZONE_REAR_LEFT;
    info.host          = "rl.local";
    info.port          = 6000;
    info.instance_name = "rear-left.rl.local._rcp._udp.local";

    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_announcer_announce(&ann.base, &info));
    TEST_ASSERT_TRUE(announcer_has(&ann, RCP_ZONE_REAR_LEFT, &port));
    TEST_ASSERT_EQUAL_UINT16(6000, port);
}

static void test_withdraw_removes_record(void)
{
    test_announcer_t ann;
    rcp_mdns_zone_info_t info;

    memset(&ann, 0, sizeof(ann));
    ann.base.vt = &test_announcer_vtable;

    info.zone          = RCP_ZONE_REAR_RIGHT;
    info.host          = "rr.local";
    info.port          = 6001;
    info.instance_name = "rr._rcp._udp.local";

    TEST_ASSERT_EQUAL(RCP_OK, rcp_mdns_announcer_announce(&ann.base, &info));
    TEST_ASSERT_TRUE(announcer_has(&ann, RCP_ZONE_REAR_RIGHT, NULL));

    rcp_mdns_announcer_withdraw(&ann.base, RCP_ZONE_REAR_RIGHT);
    TEST_ASSERT_FALSE(announcer_has(&ann, RCP_ZONE_REAR_RIGHT, NULL));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_static_discoverer_emits_on_start);
    RUN_TEST(test_start_fires_added_event_per_zone);
    RUN_TEST(test_stop_terminates_discovery);
    RUN_TEST(test_zone_info_carries_host_port_zone);
    RUN_TEST(test_make_instance_name_follows_convention);
    RUN_TEST(test_announcer_registers_zone_record);
    RUN_TEST(test_withdraw_removes_record);

    return UNITY_END();
}
