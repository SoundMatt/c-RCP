/* SPDX-License-Identifier: MPL-2.0 */
/* RELAY conformance tests (RELAY spec §18.2, §5.1, §10.3, §15.7.5, §19.4).
 *
 * Verifies that c-RCP satisfies the mandatory RELAY-conformance
 * requirements this milestone adds: the Message envelope, the Caller
 * adapter (Adapt/send/call/subscribe/close) now bound to an
 * rcp_avtp_transport_t and one TC18 endpoint kind, the per-op
 * rcp_message_to_request()/rcp_response_to_message() field mapping (see
 * adapt.h's file header table), and the RCP_SPEC_VERSION export.
 */
//cfusa:test REQ-RELAY-001
//cfusa:test REQ-RELAY-002
//cfusa:test REQ-RELAY-003
//cfusa:test REQ-RELAY-004
//cfusa:test REQ-RELAY-005
//cfusa:test REQ-RELAY-006
//cfusa:test REQ-RELAY-007
//cfusa:test REQ-RELAY-008
//cfusa:test REQ-RELAY-009
//cfusa:test REQ-RELAY-010
//cfusa:test REQ-RELAY-011
//cfusa:test REQ-RELAY-012
//cfusa:test REQ-RELAY-013
//cfusa:test REQ-RELAY-015
//cfusa:test REQ-RELAY-016
//cfusa:test REQ-RELAY-017
#include "unity.h"

#include <rcp/adapt.h>
#include <rcp/alloc.h>
#include <rcp/avtp.h>
#include <rcp/clock.h>
#include <rcp/discovery.h>
#include <rcp/ep_adc.h>
#include <rcp/ep_can.h>
#include <rcp/ep_gpio.h>
#include <rcp/ep_i2c.h>
#include <rcp/ep_iseled.h>
#include <rcp/ep_lin.h>
#include <rcp/ep_mdio.h>
#include <rcp/ep_pwm.h>
#include <rcp/ep_spi.h>
#include <rcp/ep_uart.h>
#include <rcp/ep_wakeup.h>
#include <rcp/power.h>
#include <rcp/rcp.h>
#include <rcp/regmap.h>
#include <rcp/shmem.h>
#include <relay/relay.h>

#include <stdlib.h>
#include <string.h>

/* Thread helper for the relay_message_channel_recv() blocking-wait tests
 * below -- same pattern as tests/test_admin.c's own test_thread_spawn()/
 * test_thread_join() (plain OS threads, independent of this project's
 * own internal src/platform.h, which is not a public header). */
#if defined(_WIN32)
#include <windows.h>
typedef HANDLE test_thread_t;
static test_thread_t test_thread_spawn(DWORD(WINAPI *fn)(void *), void *arg)
{
    return CreateThread(NULL, 0, fn, arg, 0, NULL);
}
static void test_thread_join(test_thread_t t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
#include <pthread.h>
typedef pthread_t test_thread_t;
static test_thread_t test_thread_spawn(void *(*fn)(void *), void *arg)
{
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void test_thread_join(test_thread_t t) { pthread_join(t, NULL); }
#endif

void setUp(void) {}
void tearDown(void) {}

static rcp_stream_id_t make_stream(uint16_t unique_id)
{
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    return rcp_stream_id_make(mac, unique_id);
}

/* ── §19.4: SpecVersion ────────────────────────────────────────────────────── */

//cfusa:test REQ-RELAY-013
static void test_rcp_spec_version_equals_relay_spec_version(void)
{
    TEST_ASSERT_EQUAL_STRING(RELAY_SPEC_VERSION, RCP_SPEC_VERSION);
    TEST_ASSERT_EQUAL_STRING("2.0", RCP_SPEC_VERSION);
}

/* ── §3: Protocol enum ─────────────────────────────────────────────────────── */

static void test_protocol_enum_values_match_spec(void)
{
    TEST_ASSERT_EQUAL(1, RELAY_PROTOCOL_CAN);
    TEST_ASSERT_EQUAL(2, RELAY_PROTOCOL_DDS);
    TEST_ASSERT_EQUAL(3, RELAY_PROTOCOL_LIN);
    TEST_ASSERT_EQUAL(4, RELAY_PROTOCOL_MQTT);
    TEST_ASSERT_EQUAL(5, RELAY_PROTOCOL_RCP);
    TEST_ASSERT_EQUAL(6, RELAY_PROTOCOL_SOMEIP);
}

static void test_protocol_string_unique_nonempty(void)
{
    TEST_ASSERT_EQUAL_STRING("RCP", relay_protocol_string(RELAY_PROTOCOL_RCP));
    TEST_ASSERT_EQUAL_STRING("CAN", relay_protocol_string(RELAY_PROTOCOL_CAN));
}

/* Every relay_protocol_t enumerator maps to its own distinct name, plus the
 * switch's default arm for a value outside the defined enum range (issue
 * #520 category 1: dispatch-table switch arms previously only exercised for
 * two of the six protocols). */
static void test_protocol_string_covers_every_enumerator(void)
{
    TEST_ASSERT_EQUAL_STRING("CAN",    relay_protocol_string(RELAY_PROTOCOL_CAN));
    TEST_ASSERT_EQUAL_STRING("DDS",    relay_protocol_string(RELAY_PROTOCOL_DDS));
    TEST_ASSERT_EQUAL_STRING("LIN",    relay_protocol_string(RELAY_PROTOCOL_LIN));
    TEST_ASSERT_EQUAL_STRING("MQTT",   relay_protocol_string(RELAY_PROTOCOL_MQTT));
    TEST_ASSERT_EQUAL_STRING("RCP",    relay_protocol_string(RELAY_PROTOCOL_RCP));
    TEST_ASSERT_EQUAL_STRING("SOMEIP", relay_protocol_string(RELAY_PROTOCOL_SOMEIP));
}

static void test_protocol_string_rejects_out_of_range_value(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", relay_protocol_string((relay_protocol_t)0));
    TEST_ASSERT_EQUAL_STRING("unknown", relay_protocol_string((relay_protocol_t)7));
}

/* ── §4/§18.2: relay_message_t lifecycle ───────────────────────────────────── */

static void test_message_init_then_free_is_safe(void)
{
    relay_message_t m;
    relay_message_init(&m);
    TEST_ASSERT_NULL(m.id);
    TEST_ASSERT_EQUAL_UINT(0, m.meta_len);
    relay_message_free(&m); /* must not crash on an already-empty message */
}

static void test_message_set_id_replaces_prior_value(void)
{
    relay_message_t m;
    relay_message_init(&m);

    TEST_ASSERT_TRUE(relay_message_set_id(&m, "ep-gpio-0"));
    TEST_ASSERT_EQUAL_STRING("ep-gpio-0", m.id);

    TEST_ASSERT_TRUE(relay_message_set_id(&m, "ep-pwm-1"));
    TEST_ASSERT_EQUAL_STRING("ep-pwm-1", m.id);

    relay_message_free(&m);
}

static void test_message_meta_set_upserts_and_get_looks_up(void)
{
    relay_message_t m;
    relay_message_init(&m);

    TEST_ASSERT_TRUE(relay_message_set_meta(&m, "rcp.adapt.op", "gpio_read"));
    TEST_ASSERT_EQUAL_UINT(1, m.meta_len);
    TEST_ASSERT_EQUAL_STRING("gpio_read", relay_message_get_meta(&m, "rcp.adapt.op"));

    /* Upsert: same key, new value, must not append a duplicate entry. */
    TEST_ASSERT_TRUE(relay_message_set_meta(&m, "rcp.adapt.op", "gpio_write"));
    TEST_ASSERT_EQUAL_UINT(1, m.meta_len);
    TEST_ASSERT_EQUAL_STRING("gpio_write", relay_message_get_meta(&m, "rcp.adapt.op"));

    TEST_ASSERT_NULL(relay_message_get_meta(&m, "no-such-key"));

    relay_message_free(&m);
}

/* ── §18.2: Channel<T>-equivalent push/recv/close semantics ───────────────── */

static void test_channel_push_returns_false_when_full(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(2);
    relay_message_t a, b, c;

    relay_message_init(&a);
    relay_message_init(&b);
    relay_message_init(&c);

    TEST_ASSERT_TRUE(relay_message_channel_push(ch, &a));
    TEST_ASSERT_TRUE(relay_message_channel_push(ch, &b));
    TEST_ASSERT_FALSE(relay_message_channel_push(ch, &c)); /* full */

    relay_message_channel_release(ch);
}

static void test_channel_is_closed_reflects_close_state(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(8);

    TEST_ASSERT_FALSE(relay_message_channel_is_closed(ch));
    relay_message_channel_close(ch);
    TEST_ASSERT_TRUE(relay_message_channel_is_closed(ch));

    relay_message_channel_release(ch);
}

/* MC/DC: relay_message_channel_push()'s `if (ch->closed || ch->count >=
 * ch->cap)` (src/relay.c) never had the `ch->closed` operand's own
 * independent contribution demonstrated --
 * test_channel_push_returns_false_when_full() above only ever pushes
 * into an open channel (`ch->closed` constant false throughout), so
 * only `ch->count >= ch->cap` has ever driven the decision. This closes
 * a channel that still has room (`count(0) < cap(2)`, the same
 * count<cap state that channel's very first push above returns true
 * for) and confirms push is rejected anyway -- with `ch->count >=
 * ch->cap` held equal (false in both), only `ch->closed` differs
 * between the two vectors and the outcome flips, which is exactly
 * MC/DC's independence requirement for that operand. */
static void test_channel_push_returns_false_when_closed_even_with_room(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(2);
    relay_message_t a;

    relay_message_init(&a);

    relay_message_channel_close(ch);
    TEST_ASSERT_FALSE(relay_message_channel_push(ch, &a)); /* closed, not full */

    relay_message_channel_release(ch);
}

/* relay_message_channel_recv() (src/relay.c) had no test at all in this
 * file before this milestone -- not just an MC/DC gap on its `while
 * (ch->count == 0 && !ch->closed)` wait condition, but a genuinely
 * untested function. The three tests below establish real functional
 * coverage of all three reachable states (message already queued;
 * empty-and-closed; empty-and-open, requiring an actual blocking wait
 * woken by another thread) and, together, close both of that decision's
 * missing MC/DC operands:
 *
 *   - test_recv_returns_true_immediately_when_message_already_queued():
 *     `ch->count == 0` is false, short-circuiting `!ch->closed`
 *     (masked) -- the while loop is never entered.
 *   - test_recv_returns_false_immediately_on_empty_closed_channel():
 *     `ch->count == 0` true, `!ch->closed` false -- loop condition
 *     false, no wait.
 *   - test_recv_blocks_until_another_thread_pushes(): `ch->count == 0`
 *     true, `!ch->closed` true -- the loop is actually entered and
 *     rcp_cond_wait() genuinely blocks this thread until the pusher
 *     thread's rcp_cond_signal() (inside
 *     relay_message_channel_push()) wakes it; the loop then
 *     re-evaluates with `ch->count == 0` now false, exiting.
 *
 * Pairing the second and third tests' first-iteration vectors -- (true,
 * false) -> loop-false vs (true, true) -> loop-true, `ch->count == 0`
 * held equal -- demonstrates `!ch->closed`'s independence. Pairing the
 * third test's own first (true, true) -> true and re-evaluation (false,
 * masked) -> false vectors demonstrates `ch->count == 0`'s independence.
 */
static void test_recv_returns_true_immediately_when_message_already_queued(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(4);
    relay_message_t sent, received;

    relay_message_init(&sent);
    TEST_ASSERT_TRUE(relay_message_set_id(&sent, "already-queued"));
    TEST_ASSERT_TRUE(relay_message_channel_push(ch, &sent));

    TEST_ASSERT_TRUE(relay_message_channel_recv(ch, &received));
    TEST_ASSERT_EQUAL_STRING("already-queued", received.id);

    relay_message_free(&sent);
    relay_message_free(&received);
    relay_message_channel_release(ch);
}

static void test_recv_returns_false_immediately_on_empty_closed_channel(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(4);
    relay_message_t out;

    relay_message_channel_close(ch);
    TEST_ASSERT_FALSE(relay_message_channel_recv(ch, &out));

    relay_message_channel_release(ch);
}

typedef struct {
    relay_message_channel_t *ch;
} recv_thread_pusher_ctx_t;

#if defined(_WIN32)
static DWORD WINAPI recv_thread_pusher(void *arg)
#else
static void *recv_thread_pusher(void *arg)
#endif
{
    recv_thread_pusher_ctx_t *ctx = (recv_thread_pusher_ctx_t *)arg;
    relay_message_t msg;

    /* Give the main thread a real chance to reach rcp_cond_wait() first --
       this is a best-effort ordering aid, not a correctness requirement:
       relay_message_channel_push()'s rcp_cond_signal() would simply be a
       no-op racing ahead of the wait otherwise, and the main thread's own
       `while (count == 0 ...)` loop guards against a missed wakeup either
       way. */
    {
        uint64_t start = rcp_monotonic_ms();
        while (rcp_monotonic_ms() - start < 20) { /* busy-wait */ }
    }

    relay_message_init(&msg);
    (void)relay_message_set_id(&msg, "from-other-thread");
    (void)relay_message_channel_push(ctx->ch, &msg);
    relay_message_free(&msg);

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void test_recv_blocks_until_another_thread_pushes(void)
{
    relay_message_channel_t *ch = relay_message_channel_new(4);
    recv_thread_pusher_ctx_t ctx;
    relay_message_t received;
    test_thread_t t;

    ctx.ch = ch;
    t = test_thread_spawn(recv_thread_pusher, &ctx);

    /* Blocks in rcp_cond_wait() (the channel is empty and open) until the
       spawned thread's push signals it. */
    TEST_ASSERT_TRUE(relay_message_channel_recv(ch, &received));
    TEST_ASSERT_EQUAL_STRING("from-other-thread", received.id);

    test_thread_join(t);
    relay_message_free(&received);
    relay_message_channel_release(ch);
}

/* ── §14: SubscriberOptions defaults ───────────────────────────────────────── */

static void test_subscriber_options_defaults(void)
{
    relay_subscriber_options_t opts = relay_subscriber_options_default();
    TEST_ASSERT_EQUAL_UINT(64, opts.channel_depth);
    TEST_ASSERT_EQUAL(RELAY_BACKPRESSURE_DROP_NEWEST, opts.back_pressure);
    TEST_ASSERT_NULL(opts.topic_name);
}

/* ── rcp_adapt_op_t <-> kind/string ─────────────────────────────────────────── */

static void test_op_kind_families(void)
{
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_GPIO, rcp_adapt_op_kind(RCP_ADAPT_OP_GPIO_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_GPIO, rcp_adapt_op_kind(RCP_ADAPT_OP_GPIO_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_UART, rcp_adapt_op_kind(RCP_ADAPT_OP_UART_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_UART, rcp_adapt_op_kind(RCP_ADAPT_OP_UART_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_WAKEUP, rcp_adapt_op_kind(RCP_ADAPT_OP_WAKEUP_SLEEPCMD));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_WAKEUP, rcp_adapt_op_kind(RCP_ADAPT_OP_WAKEUP_WAKEUP));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_DISCOVERY, rcp_adapt_op_kind(RCP_ADAPT_OP_DISCOVERY));
}

/* Every rcp_adapt_op_t <-> rcp_adapt_ep_kind_t switch arm, one per op (issue
 * #520 category 1: rcp_adapt_op_kind()'s switch was only exercised for 7 of
 * 18 ops -- GPIO/UART/WAKEUP/DISCOVERY -- leaving SPI/I2C/ADC/PWM_OUT/
 * PWM_IN/LIN/CAN/ISELED/MDIO entirely unhit). */
static void test_op_kind_covers_every_op(void)
{
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_GPIO,      rcp_adapt_op_kind(RCP_ADAPT_OP_GPIO_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_GPIO,      rcp_adapt_op_kind(RCP_ADAPT_OP_GPIO_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_SPI,       rcp_adapt_op_kind(RCP_ADAPT_OP_SPI_TRANSFER));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_I2C,       rcp_adapt_op_kind(RCP_ADAPT_OP_I2C_TRANSFER));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_UART,      rcp_adapt_op_kind(RCP_ADAPT_OP_UART_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_UART,      rcp_adapt_op_kind(RCP_ADAPT_OP_UART_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_ADC,       rcp_adapt_op_kind(RCP_ADAPT_OP_ADC_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_PWM_OUT,   rcp_adapt_op_kind(RCP_ADAPT_OP_PWM_OUT_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_PWM_OUT,   rcp_adapt_op_kind(RCP_ADAPT_OP_PWM_OUT_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_PWM_IN,    rcp_adapt_op_kind(RCP_ADAPT_OP_PWM_IN_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_LIN,       rcp_adapt_op_kind(RCP_ADAPT_OP_LIN_COMMAND));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_CAN,       rcp_adapt_op_kind(RCP_ADAPT_OP_CAN_FRAME));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_ISELED,    rcp_adapt_op_kind(RCP_ADAPT_OP_ISELED_COMMAND));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_MDIO,      rcp_adapt_op_kind(RCP_ADAPT_OP_MDIO_READ));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_MDIO,      rcp_adapt_op_kind(RCP_ADAPT_OP_MDIO_WRITE));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_WAKEUP,    rcp_adapt_op_kind(RCP_ADAPT_OP_WAKEUP_SLEEPCMD));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_WAKEUP,    rcp_adapt_op_kind(RCP_ADAPT_OP_WAKEUP_WAKEUP));
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_DISCOVERY, rcp_adapt_op_kind(RCP_ADAPT_OP_DISCOVERY));
}

static void test_op_kind_rejects_out_of_range_op(void)
{
    /* The default arm falls back to RCP_ADAPT_EP_DISCOVERY -- see adapt.c's
     * rcp_adapt_op_kind(). */
    TEST_ASSERT_EQUAL(RCP_ADAPT_EP_DISCOVERY, rcp_adapt_op_kind((rcp_adapt_op_t)999));
}

static void test_op_string_round_trips_every_op(void)
{
    static const rcp_adapt_op_t ALL_OPS[] = {
        RCP_ADAPT_OP_GPIO_READ,       RCP_ADAPT_OP_GPIO_WRITE,   RCP_ADAPT_OP_SPI_TRANSFER,
        RCP_ADAPT_OP_I2C_TRANSFER,    RCP_ADAPT_OP_UART_WRITE,   RCP_ADAPT_OP_UART_READ,
        RCP_ADAPT_OP_ADC_READ,        RCP_ADAPT_OP_PWM_OUT_READ, RCP_ADAPT_OP_PWM_OUT_WRITE,
        RCP_ADAPT_OP_PWM_IN_READ,     RCP_ADAPT_OP_LIN_COMMAND,  RCP_ADAPT_OP_CAN_FRAME,
        RCP_ADAPT_OP_ISELED_COMMAND,  RCP_ADAPT_OP_MDIO_READ,    RCP_ADAPT_OP_MDIO_WRITE,
        RCP_ADAPT_OP_WAKEUP_SLEEPCMD, RCP_ADAPT_OP_WAKEUP_WAKEUP, RCP_ADAPT_OP_DISCOVERY,
    };
    size_t i;
    for (i = 0; i < sizeof(ALL_OPS) / sizeof(ALL_OPS[0]); i++) {
        rcp_adapt_op_t parsed;
        const char *name = rcp_adapt_op_string(ALL_OPS[i]);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(rcp_adapt_op_from_string(name, &parsed));
        TEST_ASSERT_EQUAL(ALL_OPS[i], parsed);
    }
}

static void test_op_string_rejects_out_of_range_op(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown", rcp_adapt_op_string((rcp_adapt_op_t)999));
}

static void test_op_from_string_rejects_unknown_and_null(void)
{
    rcp_adapt_op_t parsed;
    TEST_ASSERT_FALSE(rcp_adapt_op_from_string("no-such-op", &parsed));
    TEST_ASSERT_FALSE(rcp_adapt_op_from_string(NULL, &parsed));
}

static void test_adapt_strerror_never_null(void)
{
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_OK));
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_ERR_ENCODE));
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_ERR_DECODE));
    TEST_ASSERT_NOT_NULL(rcp_adapt_strerror(RCP_ADAPT_ERR_NOT_SUPPORTED));
}

/* Every rcp_adapt_errc_t switch arm, plus the default "unknown" fallback for
 * a value outside the enum (issue #520 category 1: rcp_adapt_strerror()'s
 * switch previously left RCP_ADAPT_ERR_CLOSED/_TIMEOUT/_TRANSPORT and its
 * default arm unhit -- those two error codes are exercised transport-side
 * elsewhere in this file, via rcp_errc_to_relay_errc(), but never fed
 * through strerror() itself). */
static void test_adapt_strerror_covers_every_code(void)
{
    TEST_ASSERT_EQUAL_STRING("ok",      rcp_adapt_strerror(RCP_ADAPT_OK));
    TEST_ASSERT_EQUAL_STRING("closed",  rcp_adapt_strerror(RCP_ADAPT_ERR_CLOSED));
    TEST_ASSERT_EQUAL_STRING("timeout", rcp_adapt_strerror(RCP_ADAPT_ERR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("could not encode message as a wire request",
                              rcp_adapt_strerror(RCP_ADAPT_ERR_ENCODE));
    TEST_ASSERT_EQUAL_STRING("could not decode wire response as a message",
                              rcp_adapt_strerror(RCP_ADAPT_ERR_DECODE));
    TEST_ASSERT_EQUAL_STRING("underlying transport failure",
                              rcp_adapt_strerror(RCP_ADAPT_ERR_TRANSPORT));
    TEST_ASSERT_EQUAL_STRING("not supported", rcp_adapt_strerror(RCP_ADAPT_ERR_NOT_SUPPORTED));
}

static void test_adapt_strerror_rejects_out_of_range_code(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown rcp_adapt_errc_t",
                              rcp_adapt_strerror((rcp_adapt_errc_t)12345));
}

/* ── §15.7.5: rcp_message_to_request()/rcp_response_to_message() field mapping ── */

static void test_gpio_read_request_round_trips(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t out_txn = 0;

    relay_message_init(&msg);

    req = rcp_message_to_request(RCP_ADAPT_OP_GPIO_READ, 7, make_stream(1), &msg, 42, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                       rcp_ep_gpio_decode_read_request(req.data, req.len, 7, &out_txn));
    TEST_ASSERT_EQUAL_UINT8(42, out_txn);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_gpio_write_request_maps_payload_and_evt_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN] = {0x00, 0x00, 0x00, 0x05};
    uint32_t out_bitmask = 0;
    uint8_t out_evt = 0xFF;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(payload, sizeof(payload));
    relay_message_set_meta(&msg, "rcp.gpio.evt", "1"); /* OR */

    req = rcp_message_to_request(RCP_ADAPT_OP_GPIO_WRITE, 7, make_stream(1), &msg, 9, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                       rcp_ep_gpio_decode_write_request(req.data, req.len, 7, &out_bitmask,
                                                         &out_evt, &out_txn));
    TEST_ASSERT_EQUAL_UINT32(5, out_bitmask);
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_GPIO_WRITE_OR, out_evt);
    TEST_ASSERT_EQUAL_UINT8(9, out_txn);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_gpio_write_request_rejects_wrong_payload_length(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;
    uint8_t payload[2] = {0, 0};

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(payload, sizeof(payload));

    req = rcp_message_to_request(RCP_ADAPT_OP_GPIO_WRITE, 7, make_stream(1), &msg, 9, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_gpio_response_maps_bitmask_into_payload(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(7, 0xAABBCCDD, 3, true, 123456);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_GPIO_READ, 7, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, msg.protocol);
    /* RELAY spec v2.0 §15.7.5: a response Message's ID is the responding
     * endpoint's ByteBusID as a decimal string -- see issue #107. */
    TEST_ASSERT_EQUAL_STRING("7", msg.id);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_GPIO_PAYLOAD_LEN, msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0xAA, msg.payload.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xDD, msg.payload.data[3]);
    TEST_ASSERT_EQUAL_STRING("true", relay_message_get_meta(&msg, "rcp.timed"));
    TEST_ASSERT_EQUAL_STRING("123456", relay_message_get_meta(&msg, "rcp.timestamp"));
    TEST_ASSERT_EQUAL_STRING("3", relay_message_get_meta(&msg, "rcp.transaction_num"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_response_to_message_rejects_wrong_bus(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(7, 1, 1, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_GPIO_READ, 8 /* wrong bus */, frame.data, frame.len,
                                   &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);
    TEST_ASSERT_NULL(msg.id);
    TEST_ASSERT_NULL(msg.payload.data);

    rcp_bytes_free(&frame);
}

static void test_spi_transfer_request_maps_channel_meta_and_payload(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0x11, 0x22, 0x33};
    uint8_t out_channel = 0xFF;
    const uint8_t *out_tx = NULL;
    size_t out_tx_len = 0;
    uint16_t out_read_size = 0;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));
    relay_message_set_meta(&msg, "rcp.spi.channel", "3");

    req = rcp_message_to_request(RCP_ADAPT_OP_SPI_TRANSFER, 2, make_stream(1), &msg, 5, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_SPI_OK,
                       rcp_ep_spi_decode_transfer_request(req.data, req.len, 2, &out_channel,
                                                           &out_tx, &out_tx_len, &out_read_size,
                                                           &out_txn));
    TEST_ASSERT_EQUAL_UINT8(3, out_channel);
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));
    /* rcp.spi.read_size wasn't set -- defaults to the payload's own
     * length (see the encode side's own doc comment, src/adapt.c). */
    TEST_ASSERT_EQUAL_UINT16(sizeof(tx), out_read_size);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_i2c_transfer_request_has_no_channel_selector(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0xAA};

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));

    req = rcp_message_to_request(RCP_ADAPT_OP_I2C_TRANSFER, 1, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_uart_read_request_requires_read_size_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg); /* no rcp.uart.read_size set */

    req = rcp_message_to_request(RCP_ADAPT_OP_UART_READ, 1, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_uart_read_request_round_trips_with_read_size_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint16_t out_read_size = 0;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.uart.read_size", "8");

    req = rcp_message_to_request(RCP_ADAPT_OP_UART_READ, 1, make_stream(1), &msg, 3, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_read_request(req.data, req.len, 1, &out_read_size, &out_txn));
    TEST_ASSERT_EQUAL_UINT16(8, out_read_size);
    TEST_ASSERT_EQUAL_UINT8(3, out_txn);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

/* issue #471: RCP_ADAPT_OP_ISELED_COMMAND now selects the read direction
 * the same way RCP_ADAPT_OP_I2C_TRANSFER does -- meta rcp.iseled.read_size
 * absent/0 stays the (unchanged) write direction; non-zero switches to
 * the newly-added read-direction encoder. */
static void test_iseled_command_default_is_write_direction(void)
{
    relay_message_t  msg;
    rcp_bytes_t       req;
    rcp_adapt_errc_t  err = RCP_ADAPT_ERR_ENCODE;
    uint8_t           tx[] = {0x01, 0x02, 0x03};
    const uint8_t    *out_tx;
    size_t            out_tx_len;
    uint8_t           txn;

    relay_message_init(&msg); /* no rcp.iseled.read_size set */
    msg.payload = relay_bytes_dup(tx, sizeof(tx));

    req = rcp_message_to_request(RCP_ADAPT_OP_ISELED_COMMAND, 2, make_stream(1), &msg, 5, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_command_request(req.data, req.len, 2, &out_tx, &out_tx_len, &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_iseled_command_read_size_meta_selects_read_direction(void)
{
    relay_message_t  msg;
    rcp_bytes_t       req;
    rcp_adapt_errc_t  err = RCP_ADAPT_ERR_ENCODE;
    uint8_t           tx[] = {0x03, 0x40}; /* Instruction+Address */
    const uint8_t    *out_tx;
    size_t            out_tx_len;
    uint16_t          read_size = 0;
    uint8_t           txn;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));
    relay_message_set_meta(&msg, "rcp.iseled.read_size", "16");

    req = rcp_message_to_request(RCP_ADAPT_OP_ISELED_COMMAND, 2, make_stream(1), &msg, 5, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_ISELED_OK,
        rcp_ep_iseled_decode_read_request(req.data, req.len, 2, &out_tx, &out_tx_len, &read_size,
                                           &txn));
    TEST_ASSERT_EQUAL_UINT32(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT16(16, read_size);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_iseled_command_read_size_above_max_rejected(void)
{
    relay_message_t  msg;
    rcp_bytes_t       req;
    rcp_adapt_errc_t  err = RCP_ADAPT_OK;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.iseled.read_size", "4096"); /* > 12-bit ceiling */

    req = rcp_message_to_request(RCP_ADAPT_OP_ISELED_COMMAND, 2, make_stream(1), &msg, 5, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_can_frame_request_rejects_xl_formats_as_out_of_scope(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.can.frame_format", "4"); /* RCP_EP_CAN_FRAME_XL_CLASSICAL_PL */
    relay_message_set_meta(&msg, "rcp.can.arbitration_id", "0");

    req = rcp_message_to_request(RCP_ADAPT_OP_CAN_FRAME, 3, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_can_frame_request_accepts_classical_format(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0x01, 0x02};

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));
    relay_message_set_meta(&msg, "rcp.can.frame_format", "0"); /* RCP_EP_CAN_FRAME_CBFF */
    relay_message_set_meta(&msg, "rcp.can.arbitration_id", "100");

    req = rcp_message_to_request(RCP_ADAPT_OP_CAN_FRAME, 3, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_mdio_write_request_maps_addr_meta_and_packed_words(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t words_payload[4];
    rcp_ep_mdio_addr_t out_addr;
    const uint8_t *out_words = NULL;
    size_t out_word_count = 0;
    uint8_t out_txn = 0;

    rcp_ep_mdio_word_encode(0x1234, &words_payload[0]);
    rcp_ep_mdio_word_encode(0x5678, &words_payload[2]);

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(words_payload, sizeof(words_payload));
    relay_message_set_meta(&msg, "rcp.mdio.clause", "0");
    relay_message_set_meta(&msg, "rcp.mdio.prtad", "1");
    relay_message_set_meta(&msg, "rcp.mdio.devad", "0");
    relay_message_set_meta(&msg, "rcp.mdio.regad", "5");

    req = rcp_message_to_request(RCP_ADAPT_OP_MDIO_WRITE, 4, make_stream(1), &msg, 7, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK,
                       rcp_ep_mdio_decode_write_request(req.data, req.len, 4, &out_addr,
                                                         &out_words, &out_word_count, &out_txn));
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_CLAUSE_22, out_addr.clause);
    TEST_ASSERT_EQUAL_UINT8(1, out_addr.prtad);
    TEST_ASSERT_EQUAL_UINT16(5, out_addr.regad);
    TEST_ASSERT_EQUAL_UINT(2, out_word_count);
    TEST_ASSERT_EQUAL_UINT16(0x1234, rcp_ep_mdio_unpack_word_at(out_words, 0));
    TEST_ASSERT_EQUAL_UINT16(0x5678, rcp_ep_mdio_unpack_word_at(out_words, 1));

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

/* RCP_ADAPT_OP_MDIO_WRITE's `if (msg->payload.len == 0 ||
 * (msg->payload.len % 2) != 0) return fail_encode(out_err);` (adapt.c)
 * needs both conditions independently demonstrated. The test above
 * only ever uses a nonzero, even payload.len (4) -- neither the
 * len==0 short-circuit nor an odd (non-word-aligned) nonzero length is
 * exercised anywhere else in this file. */
//cfusa:test REQ-RELAY-005
static void test_mdio_write_request_rejects_empty_payload(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg); /* payload left empty: len == 0 */
    relay_message_set_meta(&msg, "rcp.mdio.clause", "0");
    relay_message_set_meta(&msg, "rcp.mdio.prtad", "1");
    relay_message_set_meta(&msg, "rcp.mdio.devad", "0");
    relay_message_set_meta(&msg, "rcp.mdio.regad", "5");

    req = rcp_message_to_request(RCP_ADAPT_OP_MDIO_WRITE, 4, make_stream(1), &msg, 7, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

//cfusa:test REQ-RELAY-005
static void test_mdio_write_request_rejects_odd_payload_length(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;
    uint8_t odd_payload[3] = {0x12, 0x34, 0x56}; /* not a whole number of
                                                   * 16-bit words */

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(odd_payload, sizeof(odd_payload));
    relay_message_set_meta(&msg, "rcp.mdio.clause", "0");
    relay_message_set_meta(&msg, "rcp.mdio.prtad", "1");
    relay_message_set_meta(&msg, "rcp.mdio.devad", "0");
    relay_message_set_meta(&msg, "rcp.mdio.regad", "5");

    req = rcp_message_to_request(RCP_ADAPT_OP_MDIO_WRITE, 4, make_stream(1), &msg, 7, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_wakeup_sleepcmd_round_trips_without_timed_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    rcp_bytes_t resp_frame;
    relay_message_t resp;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.wakeup.target_mode", "1"); /* RCP_PWRMODE_STANDBY */

    req = rcp_message_to_request(RCP_ADAPT_OP_WAKEUP_SLEEPCMD, 0, make_stream(1), &msg, 6, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    rcp_bytes_free(&req);
    relay_message_free(&msg);

    resp_frame = rcp_ep_wakeup_encode_sleepcmd_response(0, RCP_PWRMODE_ENTRY_OK, 6);
    TEST_ASSERT_NOT_NULL(resp_frame.data);

    err = RCP_ADAPT_ERR_DECODE;
    resp = rcp_response_to_message(RCP_ADAPT_OP_WAKEUP_SLEEPCMD, 0, resp_frame.data,
                                    resp_frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_STRING("0", relay_message_get_meta(&resp, "rcp.wakeup.result"));
    TEST_ASSERT_NULL(relay_message_get_meta(&resp, "rcp.timed")); /* no GBB variant exists */

    relay_message_free(&resp);
    rcp_bytes_free(&resp_frame);
}

static void test_discovery_response_maps_fields_and_server_stream_id(void)
{
    rcp_regmap_general_t map;
    rcp_bytes_t frame;
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;
    char expected_id[17];

    rcp_regmap_general_init(&map);
    map.vendor_id    = 0x1234;
    map.device_id    = 0x5678;
    map.svr_ep_count = 3;

    frame = rcp_discovery_encode_response(&map, (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, 9,
                                           make_stream(0xBEEF));
    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_DISCOVERY, 0, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_STRING("4660", relay_message_get_meta(&msg, "rcp.discovery.vendor_id"));
    TEST_ASSERT_EQUAL_STRING("22136", relay_message_get_meta(&msg, "rcp.discovery.device_id"));
    TEST_ASSERT_EQUAL_STRING("3", relay_message_get_meta(&msg, "rcp.discovery.svr_ep_count"));

    snprintf(expected_id, sizeof(expected_id), "%016llx",
             (unsigned long long)rcp_stream_id_to_u64(make_stream(0xBEEF)));
    TEST_ASSERT_EQUAL_STRING(expected_id, msg.id);

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

/* issue #520 category 1 remainder: rcp_message_to_request()'s and
 * response_to_message_impl()'s per-op switches (src/adapt.c) were only
 * directly exercised for a handful of ops each (GPIO/SPI/I2C/UART_READ/
 * ISELED/CAN/MDIO_WRITE/WAKEUP_SLEEPCMD request side; GPIO/WAKEUP_SLEEPCMD/
 * DISCOVERY response side) even though every op has its own dedicated case
 * arm. The tests below hit every remaining arm on both switches directly,
 * plus the default (out-of-range op) arm on each -- mechanical, table-driven
 * dispatch coverage, the same pattern PR #526 used for the smaller
 * op-kind/strerror/protocol-string tables. */

static void test_discovery_request_round_trips_default_read_size(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    rcp_discovery_request_t out_req;

    relay_message_init(&msg); /* no rcp.discovery.read_size set */

    req = rcp_message_to_request(RCP_ADAPT_OP_DISCOVERY, 0, make_stream(9), &msg, 4, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_DISCOVERY_OK, rcp_discovery_decode_request(req.data, req.len, &out_req));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, out_req.read_size);
    TEST_ASSERT_EQUAL_UINT8(4, out_req.transaction_num);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_uart_write_request_round_trips(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0xAA, 0xBB};
    const uint8_t *out_tx = NULL;
    size_t out_tx_len = 0;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));

    req = rcp_message_to_request(RCP_ADAPT_OP_UART_WRITE, 3, make_stream(1), &msg, 9, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_UART_OK,
        rcp_ep_uart_decode_write_request(req.data, req.len, 3, &out_tx, &out_tx_len, &out_txn));
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_adc_read_request_uses_default_read_size_when_meta_absent(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;

    relay_message_init(&msg); /* no rcp.adc.read_size set */

    req = rcp_message_to_request(RCP_ADAPT_OP_ADC_READ, 2, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_pwm_out_read_request_round_trips(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;

    relay_message_init(&msg);

    req = rcp_message_to_request(RCP_ADAPT_OP_PWM_OUT_READ, 4, make_stream(1), &msg, 2, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_pwm_out_write_request_maps_payload_and_evt_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t payload[RCP_EP_PWM_PAYLOAD_LEN] = {0x00, 0x64, 0x00, 0x32}; /* period=100, active=50 */
    rcp_ep_pwm_value_t out_value;
    uint8_t out_evt = 0xFF;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(payload, sizeof(payload));
    relay_message_set_meta(&msg, "rcp.pwm.evt", "1"); /* RCP_EP_PWM_OUT_WRITE_OR */

    req = rcp_message_to_request(RCP_ADAPT_OP_PWM_OUT_WRITE, 4, make_stream(1), &msg, 3, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_PWM_OUT_OK,
        rcp_ep_pwm_out_decode_write_request(req.data, req.len, 4, &out_value, &out_evt, &out_txn));
    TEST_ASSERT_EQUAL_UINT16(100, out_value.period);
    TEST_ASSERT_EQUAL_UINT16(50, out_value.active_duration);
    TEST_ASSERT_EQUAL_UINT8(RCP_EP_PWM_OUT_WRITE_OR, out_evt);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_pwm_out_write_request_rejects_wrong_payload_length(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;
    uint8_t payload[1] = {0};

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(payload, sizeof(payload));

    req = rcp_message_to_request(RCP_ADAPT_OP_PWM_OUT_WRITE, 4, make_stream(1), &msg, 3, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_pwm_in_read_request_round_trips(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;

    relay_message_init(&msg);

    req = rcp_message_to_request(RCP_ADAPT_OP_PWM_IN_READ, 5, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_lin_command_request_round_trips(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t tx[] = {0x01, 0x02, 0x03};
    const uint8_t *out_tx = NULL;
    size_t out_tx_len = 0;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    msg.payload = relay_bytes_dup(tx, sizeof(tx));

    req = rcp_message_to_request(RCP_ADAPT_OP_LIN_COMMAND, 6, make_stream(1), &msg, 8, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_LIN_OK,
        rcp_ep_lin_decode_command_request(req.data, req.len, 6, &out_tx, &out_tx_len, &out_txn));
    TEST_ASSERT_EQUAL_UINT(sizeof(tx), out_tx_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(tx, out_tx, sizeof(tx));

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_mdio_read_request_maps_addr_and_word_count_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    rcp_ep_mdio_addr_t out_addr;
    size_t out_word_count = 0;
    uint8_t out_txn = 0;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.mdio.clause", "1"); /* RCP_EP_MDIO_CLAUSE_45 */
    relay_message_set_meta(&msg, "rcp.mdio.prtad", "2");
    relay_message_set_meta(&msg, "rcp.mdio.devad", "3");
    relay_message_set_meta(&msg, "rcp.mdio.regad", "256"); /* decimal -- meta_get_u32 is base 10 */
    relay_message_set_meta(&msg, "rcp.mdio.word_count", "5");

    req = rcp_message_to_request(RCP_ADAPT_OP_MDIO_READ, 7, make_stream(1), &msg, 2, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_OK,
        rcp_ep_mdio_decode_read_request(req.data, req.len, 7, &out_addr, &out_word_count,
                                         &out_txn));
    TEST_ASSERT_EQUAL(RCP_EP_MDIO_CLAUSE_45, out_addr.clause);
    TEST_ASSERT_EQUAL_UINT8(2, out_addr.prtad);
    TEST_ASSERT_EQUAL_UINT8(3, out_addr.devad);
    TEST_ASSERT_EQUAL_UINT16(256, out_addr.regad);
    TEST_ASSERT_EQUAL_UINT(5, out_word_count);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_wakeup_wakeup_request_round_trips(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_ENCODE;
    uint8_t out_txn = 0;

    relay_message_init(&msg);

    req = rcp_message_to_request(RCP_ADAPT_OP_WAKEUP_WAKEUP, 0, make_stream(1), &msg, 12, &err);
    TEST_ASSERT_NOT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL(RCP_EP_WAKEUP_OK,
        rcp_ep_wakeup_decode_wakeup_message(req.data, req.len, 0, &out_txn));
    TEST_ASSERT_EQUAL_UINT8(12, out_txn);

    rcp_bytes_free(&req);
    relay_message_free(&msg);
}

static void test_message_to_request_rejects_out_of_range_op(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg);

    req = rcp_message_to_request((rcp_adapt_op_t)999, 0, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

/* out_err is documented as an optional out-parameter on both
 * rcp_message_to_request() and rcp_response_to_message() -- every `if
 * (out_err) ...` null-check in src/adapt.c (fail_encode()/fail_decode()/
 * the two entry points themselves) was previously only ever called with a
 * non-NULL out_err across the whole suite, leaving that guard's NULL arm
 * completely unexercised on both the success and failure paths. */
static void test_message_to_request_tolerates_null_out_err(void)
{
    relay_message_t msg;
    rcp_bytes_t req;

    relay_message_init(&msg);

    req = rcp_message_to_request(RCP_ADAPT_OP_GPIO_READ, 1, make_stream(1), &msg, 1, NULL);
    TEST_ASSERT_NOT_NULL(req.data);
    rcp_bytes_free(&req);

    req = rcp_message_to_request((rcp_adapt_op_t)999, 0, make_stream(1), &msg, 1, NULL);
    TEST_ASSERT_NULL(req.data);

    relay_message_free(&msg);
}

static void test_response_to_message_tolerates_null_out_err(void)
{
    rcp_bytes_t frame = rcp_ep_gpio_encode_response(7, 1, 1, false, 0);
    relay_message_t msg;

    msg = rcp_response_to_message(RCP_ADAPT_OP_GPIO_READ, 7, frame.data, frame.len, NULL);
    TEST_ASSERT_NOT_NULL(msg.payload.data);
    relay_message_free(&msg);

    msg = rcp_response_to_message(RCP_ADAPT_OP_GPIO_READ, 8 /* wrong bus */, frame.data,
                                   frame.len, NULL);
    TEST_ASSERT_NULL(msg.payload.data);

    rcp_bytes_free(&frame);
}

/* meta_get_u32()'s own `!v || !*v` short-circuit (src/adapt.c): the `!v`
 * (key entirely absent) arm was already exercised via e.g.
 * test_uart_read_request_requires_read_size_meta, but the `!*v` arm (key
 * present with an explicit empty-string value) was not. */
static void test_uart_read_request_rejects_empty_read_size_meta(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.uart.read_size", "");

    req = rcp_message_to_request(RCP_ADAPT_OP_UART_READ, 1, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

/* meta_get_u32()'s SECOND decision, `if (!end || *end != '\0') return
 * false;` (src/adapt.c, right after the strtoul() call): needs
 * `*end != '\0'` independently demonstrated -- every existing meta
 * value in this file is either absent, empty, or a clean all-digit
 * string, so strtoul() always consumes the whole value and leaves
 * *end == '\0'. A value with trailing non-digit garbage after real
 * digits (distinct from the all-garbage/empty cases above, which
 * never reach strtoul() at all) leaves *end pointing at that garbage,
 * demonstrating this condition true for the first time in this file.
 *
 * The OTHER condition on this same line, `!end`, is NOT independently
 * demonstrable: `end` is the address of a local passed as strtoul()'s
 * endptr argument, and C11 7.22.1.4p6 guarantees strtoul() always
 * stores a value through a non-NULL endptr -- success, partial parse,
 * or total failure alike -- so `end` is never NULL here regardless of
 * *v's content. Since line 126's own `!v || !*v` check has already
 * returned before this line whenever v could have been NULL, `end`
 * cannot be NULL here in any real execution; this is a genuine,
 * structural MC/DC gap guaranteed unreachable by the C standard's own
 * strtoul() contract, not a missing test. */
//cfusa:test REQ-RELAY-005
static void test_uart_read_request_rejects_meta_with_trailing_garbage(void)
{
    relay_message_t msg;
    rcp_bytes_t req;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.uart.read_size", "8x"); /* digits followed
                                                                * by non-digit
                                                                * trailing
                                                                * garbage */

    req = rcp_message_to_request(RCP_ADAPT_OP_UART_READ, 1, make_stream(1), &msg, 1, &err);
    TEST_ASSERT_NULL(req.data);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, err);

    relay_message_free(&msg);
}

static void test_spi_transfer_response_maps_channel_and_payload(void)
{
    uint8_t rx[] = {0x10, 0x20, 0x30};
    rcp_bytes_t frame = rcp_ep_spi_encode_response(2, 5, rx, sizeof(rx), 11, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_SPI_TRANSFER, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(rx), msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, msg.payload.data, sizeof(rx));
    TEST_ASSERT_EQUAL_STRING("5", relay_message_get_meta(&msg, "rcp.spi.channel"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_i2c_transfer_response_reports_read_size_for_read_direction(void)
{
    uint8_t rx[] = {0x01, 0x02};
    rcp_bytes_t frame =
        rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_READ, rx, sizeof(rx), 4, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_I2C_TRANSFER, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(rx), msg.payload.len);
    TEST_ASSERT_EQUAL_STRING("2", relay_message_get_meta(&msg, "rcp.i2c.read_size"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_i2c_transfer_response_reports_zero_read_size_for_write_direction(void)
{
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_WRITE, NULL, 0, 4, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_I2C_TRANSFER, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_STRING("0", relay_message_get_meta(&msg, "rcp.i2c.read_size"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_uart_write_response_maps_accepted_payload(void)
{
    uint8_t accepted[] = {0x55, 0x66};
    rcp_bytes_t frame =
        rcp_ep_uart_encode_write_response(3, accepted, sizeof(accepted), 4, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_UART_WRITE, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(accepted), msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(accepted, msg.payload.data, sizeof(accepted));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_uart_read_response_maps_rx_payload(void)
{
    uint8_t rx[] = {0x01, 0x02, 0x03};
    rcp_bytes_t frame = rcp_ep_uart_encode_read_response(2, rx, sizeof(rx), 11, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_UART_READ, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(rx), msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, msg.payload.data, sizeof(rx));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_adc_read_response_maps_values_big_endian_and_count_meta(void)
{
    uint16_t values[] = {0x0102, 0x0304, 0x0506};
    rcp_bytes_t frame = rcp_ep_adc_encode_response(2, values, 3, 7, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_ADC_READ, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(6, msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0x01, msg.payload.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, msg.payload.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0x05, msg.payload.data[4]);
    TEST_ASSERT_EQUAL_UINT8(0x06, msg.payload.data[5]);
    TEST_ASSERT_EQUAL_STRING("3", relay_message_get_meta(&msg, "rcp.adc.value_count"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_pwm_out_response_maps_value_payload_for_read_and_write_ops(void)
{
    rcp_ep_pwm_value_t value;
    rcp_bytes_t frame;
    relay_message_t msg;
    rcp_adapt_errc_t err;

    value.period          = 100;
    value.active_duration = 25;
    frame = rcp_ep_pwm_out_encode_response(9, value, 11, false, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    err = RCP_ADAPT_ERR_DECODE;
    msg = rcp_response_to_message(RCP_ADAPT_OP_PWM_OUT_READ, 9, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_PWM_PAYLOAD_LEN, msg.payload.len);
    relay_message_free(&msg);

    /* Same underlying frame/case block, entered via the WRITE label this
     * time -- both case labels fall into the identical body. */
    err = RCP_ADAPT_ERR_DECODE;
    msg = rcp_response_to_message(RCP_ADAPT_OP_PWM_OUT_WRITE, 9, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_PWM_PAYLOAD_LEN, msg.payload.len);

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_pwm_in_read_response_maps_value_payload(void)
{
    rcp_ep_pwm_value_t value;
    rcp_bytes_t frame;
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    value.period          = 40;
    value.active_duration = 10;
    frame = rcp_ep_pwm_in_encode_response(1, value, 3, false, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_PWM_IN_READ, 1, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_PWM_PAYLOAD_LEN, msg.payload.len);

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_lin_command_response_maps_rx_payload(void)
{
    uint8_t rx[] = {0xAA, 0xBB};
    rcp_bytes_t frame = rcp_ep_lin_encode_response(2, rx, sizeof(rx), 11, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_LIN_COMMAND, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(rx), msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, msg.payload.data, sizeof(rx));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_can_frame_response_maps_frame_fields_and_payload(void)
{
    uint8_t rx[] = {0xDE, 0xAD};
    rcp_bytes_t frame = rcp_ep_can_encode_frame_response(2, RCP_EP_CAN_FRAME_FBFF, 0x55, NULL, rx,
                                                          sizeof(rx), 11, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_CAN_FRAME, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(rx), msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, msg.payload.data, sizeof(rx));
    {
        char expected_format[8];
        snprintf(expected_format, sizeof(expected_format), "%u",
                 (unsigned)RCP_EP_CAN_FRAME_FBFF);
        TEST_ASSERT_EQUAL_STRING(expected_format,
                                  relay_message_get_meta(&msg, "rcp.can.frame_format"));
    }
    TEST_ASSERT_EQUAL_STRING("85", relay_message_get_meta(&msg, "rcp.can.arbitration_id"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_iseled_command_response_maps_rx_payload(void)
{
    uint8_t rx[] = {0x01, 0x02, 0x03};
    rcp_bytes_t frame = rcp_ep_iseled_encode_response(2, rx, sizeof(rx), 11, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_ISELED_COMMAND, 2, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(sizeof(rx), msg.payload.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rx, msg.payload.data, sizeof(rx));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_mdio_read_response_maps_words_payload(void)
{
    uint16_t words[2] = {0x1234, 0x5678};
    rcp_bytes_t frame;
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    frame = rcp_ep_mdio_encode_read_response(3, words, 2, 5, false, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_MDIO_READ, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(4, msg.payload.len);
    TEST_ASSERT_EQUAL_UINT16(0x1234, rcp_ep_mdio_unpack_word_at(msg.payload.data, 0));
    TEST_ASSERT_EQUAL_UINT16(0x5678, rcp_ep_mdio_unpack_word_at(msg.payload.data, 1));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_mdio_write_response_maps_words_payload(void)
{
    uint16_t accepted[1] = {0xBEEF};
    rcp_bytes_t frame;
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    frame = rcp_ep_mdio_encode_write_response(3, accepted, 1, 6, false, 0);
    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_MDIO_WRITE, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_UINT(2, msg.payload.len);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, rcp_ep_mdio_unpack_word_at(msg.payload.data, 0));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_wakeup_wakeup_response_round_trips(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_wakeup_message(0, 12);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_ERR_DECODE;

    TEST_ASSERT_NOT_NULL(frame.data);

    msg = rcp_response_to_message(RCP_ADAPT_OP_WAKEUP_WAKEUP, 0, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, err);
    TEST_ASSERT_EQUAL_STRING("12", relay_message_get_meta(&msg, "rcp.transaction_num"));

    relay_message_free(&msg);
    rcp_bytes_free(&frame);
}

static void test_response_to_message_rejects_out_of_range_op(void)
{
    uint8_t junk[4] = {0};
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message((rcp_adapt_op_t)999, 0, junk, sizeof(junk), &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);
    TEST_ASSERT_NULL(msg.id);
    TEST_ASSERT_NULL(msg.payload.data);
}

/* Every remaining response_to_message_impl() `return fail_decode(out_err);`
 * call site (issue #520 category 1 remainder): each op's own decode
 * failure was previously only exercised for GPIO. A response frame built
 * for byte_bus_id 2 decoded against a mismatched requested bus (3) fails
 * that op's own rcp_ep_*_decode_*() call the same way
 * test_response_to_message_rejects_wrong_bus does for GPIO, hitting this
 * op's own fail_decode() line rather than GPIO's already-covered one. */

static void test_spi_transfer_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_spi_encode_response(2, 0, NULL, 0, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_SPI_TRANSFER, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);
    TEST_ASSERT_NULL(msg.payload.data);

    rcp_bytes_free(&frame);
}

static void test_i2c_transfer_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_i2c_encode_response(2, RCP_EP_I2C_DIR_WRITE, NULL, 0, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_I2C_TRANSFER, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_uart_write_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_uart_encode_write_response(2, NULL, 0, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_UART_WRITE, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_uart_read_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_uart_encode_read_response(2, NULL, 0, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_UART_READ, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_adc_read_response_decode_failure_is_reported(void)
{
    uint16_t values[1] = {1};
    rcp_bytes_t frame = rcp_ep_adc_encode_response(2, values, 1, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_ADC_READ, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_pwm_out_response_decode_failure_is_reported(void)
{
    rcp_ep_pwm_value_t value = {0, 0};
    rcp_bytes_t frame = rcp_ep_pwm_out_encode_response(2, value, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_PWM_OUT_READ, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_pwm_in_response_decode_failure_is_reported(void)
{
    rcp_ep_pwm_value_t value = {0, 0};
    rcp_bytes_t frame = rcp_ep_pwm_in_encode_response(2, value, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_PWM_IN_READ, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_lin_command_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_lin_encode_response(2, NULL, 0, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_LIN_COMMAND, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_can_frame_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_can_encode_frame_response(2, RCP_EP_CAN_FRAME_CBFF, 0, NULL, NULL,
                                                          0, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_CAN_FRAME, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_iseled_command_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_iseled_encode_response(2, NULL, 0, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_ISELED_COMMAND, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_mdio_read_response_decode_failure_is_reported(void)
{
    uint16_t words[1] = {0x1234};
    rcp_bytes_t frame = rcp_ep_mdio_encode_read_response(2, words, 1, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_MDIO_READ, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_mdio_write_response_decode_failure_is_reported(void)
{
    uint16_t words[1] = {0x1234};
    rcp_bytes_t frame = rcp_ep_mdio_encode_write_response(2, words, 1, 0, false, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_MDIO_WRITE, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_wakeup_sleepcmd_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_sleepcmd_response(2, RCP_PWRMODE_ENTRY_OK, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_WAKEUP_SLEEPCMD, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_wakeup_wakeup_response_decode_failure_is_reported(void)
{
    rcp_bytes_t frame = rcp_ep_wakeup_encode_wakeup_message(2, 0);
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_WAKEUP_WAKEUP, 3, frame.data, frame.len, &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);

    rcp_bytes_free(&frame);
}

static void test_discovery_response_decode_failure_is_reported(void)
{
    uint8_t junk[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    relay_message_t msg;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;

    msg = rcp_response_to_message(RCP_ADAPT_OP_DISCOVERY, 0, junk, sizeof(junk), &err);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, err);
    TEST_ASSERT_NULL(msg.id);
}

/* ── §10.3: rcp_adapt() wraps an rcp_avtp_transport_t as a relay Caller ─────── */

static void test_adapt_returns_non_null(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_NOT_NULL(caller);

    rcp_relay_caller_release(caller);
    /* rcp_adapt() retains its own reference to t (adapt.c's
     * rcp_relay_caller_release() only drops that one); the reference
     * this test obtained from rcp_avtp_loopback_transport_new() is
     * still this test's own to release. */
    rcp_avtp_transport_release(t);
}

static void test_adapt_protocol_returns_rcp(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, rcp_relay_caller_protocol(caller));

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_adapt_rejects_op_outside_bound_kind(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    relay_context_t ctx = relay_context_with_timeout_ms(200);
    relay_message_t msg;

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "i2c_transfer"); /* wrong kind */

    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, rcp_relay_caller_send(caller, &ctx, &msg));

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_adapt_send_transmits_a_valid_gpio_write_request(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    rcp_context_t sctx = rcp_context_with_timeout_ms(1000);
    relay_message_t msg;
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN] = {0, 0, 0, 0x2A};
    uint8_t recv_buf[512];
    size_t recv_len = 0;
    rcp_avtp_ntscf_header_t hdr;
    const uint8_t *acf = NULL;
    size_t acf_len = 0;
    uint32_t out_bitmask = 0;
    uint8_t out_evt = 0xFF;
    uint8_t out_txn = 0;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "gpio_write");
    msg.payload = relay_bytes_dup(payload, sizeof(payload));

    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, rcp_relay_caller_send(caller, &ctx, &msg));

    TEST_ASSERT_EQUAL(RCP_OK,
                       rcp_avtp_transport_recv(server_t, &sctx, recv_buf, sizeof(recv_buf),
                                                &recv_len));
    TEST_ASSERT_EQUAL(RCP_AVTP_OK,
                       rcp_avtp_decode_ntscf(recv_buf, recv_len, &hdr, &acf, &acf_len));
    TEST_ASSERT_EQUAL(RCP_EP_GPIO_OK,
                       rcp_ep_gpio_decode_write_request(acf, acf_len, 9, &out_bitmask, &out_evt,
                                                         &out_txn));
    TEST_ASSERT_EQUAL_UINT32(0x2A, out_bitmask);

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    /* caller only holds its own retained reference to client_t (see
     * test_adapt_returns_non_null()'s comment); this test's own
     * reference from rcp_shmem_avtp_pair_new() is separate. */
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

/* build_frame()'s `if (!frame.data && out_err) *out_err =
 * RCP_ADAPT_ERR_ENCODE;` (adapt.c) needs `!frame.data` independently
 * demonstrated: every other send/call test in this file reaches a
 * successful rcp_avtp_encode_ntscf(), so frame.data is always non-NULL
 * there. A call-counting malloc hook (same idiom as ep_can.c's
 * counting_malloc / shmem.c's counting_calloc) lets the GPIO-write
 * request's own body encode (rcp_acf_encode_abb()'s one rcp_malloc()
 * call, #1) succeed while encode_ntscf()'s own rcp_malloc() call (#2)
 * fails -- the only way to reach this line with frame.data NULL
 * through this adapter's real call graph, since build_frame() returns
 * early (before ever reaching this line) whenever body.data itself is
 * NULL.
 *
 * The OTHER condition on this line -- `out_err` itself -- can NOT be
 * independently demonstrated: build_frame() is `static` and has
 * exactly two call sites in this file (adapter_send() and
 * adapter_call()), both of which unconditionally pass `&err`, the
 * address of a real local variable, never NULL. There is no reachable
 * call path through this adapter's public API (rcp_relay_caller_send()/
 * _call(), the only entry points) that can make out_err NULL here --
 * this is a genuine, structural MC/DC gap, not a missing test. */
static int g_adapt_malloc_call_count;
static int g_adapt_malloc_fail_at_call;

static void *counting_malloc(size_t size)
{
    g_adapt_malloc_call_count++;
    if (g_adapt_malloc_call_count == g_adapt_malloc_fail_at_call) return NULL;
    return malloc(size);
}

//cfusa:test REQ-RELAY-008
static void test_adapt_send_reports_encode_error_when_ntscf_wrap_allocation_fails(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t msg;
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN] = {0, 0, 0, 0x2A};
    rcp_alloc_hooks_t hooks = {0};
    int ec;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "gpio_write");
    msg.payload = relay_bytes_dup(payload, sizeof(payload));

    g_adapt_malloc_call_count   = 0;
    g_adapt_malloc_fail_at_call = 2; /* call #1 = body's rcp_acf_encode_abb(),
                                       * call #2 = encode_ntscf()'s own frame
                                       * allocation -- the one this test
                                       * targets. */
    hooks.malloc_fn = counting_malloc;
    rcp_alloc_set_hooks(&hooks);

    ec = rcp_relay_caller_send(caller, &ctx, &msg);

    rcp_alloc_reset_hooks();

    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_ENCODE, ec);

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

/* adapter_send()'s own final transport-error fallthrough (ec not OK/
 * CLOSED): shmem_side_send() (src/shmem.c) reports RCP_ERR_BUSY once the
 * client->server queue is at its configured capacity -- a real, reachable
 * transport condition distinct from the CLOSED case
 * test_send_closed_error_is_relay_equivalent() already covers. */
static void test_adapt_send_returns_transport_error_when_queue_is_full(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t msg;
    uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN] = {0, 0, 0, 0x2A};
    int ec;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 1, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "gpio_write");
    msg.payload = relay_bytes_dup(payload, sizeof(payload));

    /* Queue capacity is 1 and nothing ever drains it (server_t never
     * recv()s) -- the first send() fills the queue, the second overflows
     * it. */
    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, rcp_relay_caller_send(caller, &ctx, &msg));
    ec = rcp_relay_caller_send(caller, &ctx, &msg);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_TRANSPORT, ec);

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

static void test_adapt_call_gpio_read_returns_mapped_response(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t req, resp = {0};
    rcp_bytes_t resp_acf, resp_frame;
    rcp_avtp_ntscf_header_t hdr;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    /* Pre-queue the server's own reply on the server->client leg -- see
     * this file's own note on why the shmem pair's two independent FIFOs
     * make this safe to do before the client's call() even sends its
     * request. */
    resp_acf = rcp_ep_gpio_encode_response(9, 0x000000FF, 1, false, 0);
    TEST_ASSERT_NOT_NULL(resp_acf.data);
    hdr.sv = 1;
    hdr.version = 0;
    hdr.ntscf_data_length = 0;
    hdr.sequence_num = 1;
    hdr.stream_id = make_stream(2); /* the server's own stream_id */
    resp_frame = rcp_avtp_encode_ntscf(&hdr, resp_acf.data, resp_acf.len);
    TEST_ASSERT_NOT_NULL(resp_frame.data);
    rcp_bytes_free(&resp_acf);
    TEST_ASSERT_EQUAL(RCP_OK,
                       rcp_avtp_transport_send(server_t, resp_frame.data, resp_frame.len));
    rcp_bytes_free(&resp_frame);

    relay_message_init(&req);
    relay_message_set_meta(&req, "rcp.adapt.op", "gpio_read");

    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, rcp_relay_caller_call(caller, &ctx, &req, &resp));
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, resp.protocol);
    TEST_ASSERT_EQUAL_UINT(RCP_EP_GPIO_PAYLOAD_LEN, resp.payload.len);
    TEST_ASSERT_EQUAL_UINT8(0xFF, resp.payload.data[3]);

    relay_message_free(&req);
    relay_message_free(&resp);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

/* adapter_call()'s RCP_ADAPT_OP_DISCOVERY branch (src/adapt.c): unlike
 * every other op, a discovery reply is read straight off the wire with no
 * NTSCF unwrap -- rcp_discovery_encode_response() already returns a
 * complete standalone frame, mirroring the request side's own
 * already-NTSCF-framed discovery request (build_frame()'s own doc
 * comment). Previously only exercised via the GPIO op, which always takes
 * the NTSCF-unwrap branch instead. */
static void test_adapt_call_discovery_returns_mapped_response(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t req, resp = {0};
    rcp_regmap_general_t map;
    rcp_bytes_t resp_frame;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 0, RCP_ADAPT_EP_DISCOVERY);

    rcp_regmap_general_init(&map);
    map.vendor_id = 0xAAAA;
    resp_frame = rcp_discovery_encode_response(&map, (uint8_t)RCP_DISCOVERY_GENERAL_SLICE_LEN, 1,
                                                make_stream(2));
    TEST_ASSERT_NOT_NULL(resp_frame.data);
    TEST_ASSERT_EQUAL(RCP_OK,
                       rcp_avtp_transport_send(server_t, resp_frame.data, resp_frame.len));
    rcp_bytes_free(&resp_frame);

    relay_message_init(&req);
    relay_message_set_meta(&req, "rcp.adapt.op", "discovery");

    TEST_ASSERT_EQUAL(RCP_ADAPT_OK, rcp_relay_caller_call(caller, &ctx, &req, &resp));
    TEST_ASSERT_EQUAL_STRING("43690", relay_message_get_meta(&resp, "rcp.discovery.vendor_id"));

    relay_message_free(&req);
    relay_message_free(&resp);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

/* adapter_call()'s rcp_avtp_decode_ntscf() failure branch: a non-discovery
 * op whose reply frame doesn't parse as NTSCF at all (not merely a
 * downstream ep_*.h decode failure, which the response_to_message_impl()
 * tests above already cover). */
static void test_adapt_call_returns_decode_error_on_malformed_ntscf_reply(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t req, resp = {0};
    uint8_t junk[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    int ec;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(server_t, junk, sizeof(junk)));

    relay_message_init(&req);
    relay_message_set_meta(&req, "rcp.adapt.op", "gpio_read");

    ec = rcp_relay_caller_call(caller, &ctx, &req, &resp);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_DECODE, ec);

    relay_message_free(&req);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

/* adapter_call()'s final transport-error fallthrough (ec not OK/TIMEOUT/
 * CLOSED): shmem_side_recv() (src/shmem.c) reports RCP_ERR_BUSY when a
 * queued frame exceeds the caller's receive buffer -- ADAPT_RECV_BUF_LEN
 * (src/adapt.c) -- which is exactly such a non-OK/TIMEOUT/CLOSED code. */
static void test_adapt_call_returns_transport_error_on_oversized_reply(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t req, resp = {0};
    static uint8_t oversized[2200]; /* > ADAPT_RECV_BUF_LEN (2047 + 64) */
    int ec;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 9, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_avtp_transport_send(server_t, oversized, sizeof(oversized)));

    relay_message_init(&req);
    relay_message_set_meta(&req, "rcp.adapt.op", "gpio_read");

    ec = rcp_relay_caller_call(caller, &ctx, &req, &resp);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_TRANSPORT, ec);

    relay_message_free(&req);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

static void test_adapt_subscribe_is_not_supported(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    relay_subscriber_options_t opts = relay_subscriber_options_default();
    relay_message_channel_t *ch = NULL;

    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_NOT_SUPPORTED,
                       rcp_relay_caller_subscribe(caller, &opts, &ch));
    TEST_ASSERT_NULL(ch);

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_adapt_close_is_idempotent(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_close(caller));
    TEST_ASSERT_EQUAL(RCP_OK, rcp_relay_caller_close(caller));

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_caller_retain_returns_same_pointer_and_keeps_it_alive(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    rcp_relay_caller_t *retained = rcp_relay_caller_retain(caller);

    TEST_ASSERT_TRUE(retained == caller);

    /* One release just drops the retain()'d share; the caller must still
     * be usable afterwards. */
    rcp_relay_caller_release(retained);
    TEST_ASSERT_EQUAL(RELAY_PROTOCOL_RCP, rcp_relay_caller_protocol(caller));

    TEST_ASSERT_NULL(rcp_relay_caller_retain(NULL));

    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

/* ── §5.2 error wrapping ───────────────────────────────────────────────────── */

static void test_send_closed_error_is_relay_equivalent(void)
{
    rcp_avtp_transport_t *t = rcp_avtp_loopback_transport_new(false, 4);
    rcp_relay_caller_t *caller = rcp_adapt(t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);
    relay_context_t ctx = relay_context_with_timeout_ms(1000);
    relay_message_t msg;
    relay_errc_t relay_ec;
    int ec;

    rcp_avtp_transport_close(t);

    relay_message_init(&msg);
    relay_message_set_meta(&msg, "rcp.adapt.op", "gpio_read");

    ec = rcp_relay_caller_send(caller, &ctx, &msg);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_CLOSED, ec);
    TEST_ASSERT_TRUE(rcp_errc_to_relay_errc(ec, &relay_ec));
    TEST_ASSERT_EQUAL(RELAY_ERRC_CLOSED, relay_ec);

    relay_message_free(&msg);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(t);
}

static void test_call_timeout_error_is_relay_equivalent(void)
{
    rcp_avtp_transport_t *client_t = NULL;
    rcp_avtp_transport_t *server_t = NULL;
    rcp_relay_caller_t *caller;
    relay_context_t ctx = relay_context_with_deadline_ms(1); /* already expired */
    relay_message_t req, resp = {0};
    relay_errc_t relay_ec;
    int ec;

    TEST_ASSERT_EQUAL(RCP_SHMEM_OK, rcp_shmem_avtp_pair_new(false, 4, &client_t, &server_t));
    caller = rcp_adapt(client_t, make_stream(1), 7, RCP_ADAPT_EP_GPIO);

    relay_message_init(&req);
    relay_message_set_meta(&req, "rcp.adapt.op", "gpio_read");

    ec = rcp_relay_caller_call(caller, &ctx, &req, &resp);
    TEST_ASSERT_EQUAL(RCP_ADAPT_ERR_TIMEOUT, ec);
    TEST_ASSERT_TRUE(rcp_errc_to_relay_errc(ec, &relay_ec));
    TEST_ASSERT_EQUAL(RELAY_ERRC_TIMEOUT, relay_ec);

    relay_message_free(&req);
    rcp_relay_caller_release(caller);
    rcp_avtp_transport_release(client_t);
    rcp_avtp_transport_release(server_t);
}

static void test_ok_and_adapt_specific_errors_have_no_relay_equivalent(void)
{
    relay_errc_t relay_ec;

    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_OK, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_ERR_ENCODE, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_ERR_DECODE, &relay_ec));
    TEST_ASSERT_FALSE(rcp_errc_to_relay_errc(RCP_ADAPT_ERR_NOT_SUPPORTED, &relay_ec));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rcp_spec_version_equals_relay_spec_version);
    RUN_TEST(test_protocol_enum_values_match_spec);
    RUN_TEST(test_protocol_string_unique_nonempty);
    RUN_TEST(test_protocol_string_covers_every_enumerator);
    RUN_TEST(test_protocol_string_rejects_out_of_range_value);

    RUN_TEST(test_message_init_then_free_is_safe);
    RUN_TEST(test_message_set_id_replaces_prior_value);
    RUN_TEST(test_message_meta_set_upserts_and_get_looks_up);

    RUN_TEST(test_channel_push_returns_false_when_full);
    RUN_TEST(test_channel_push_returns_false_when_closed_even_with_room);
    RUN_TEST(test_channel_is_closed_reflects_close_state);
    RUN_TEST(test_recv_returns_true_immediately_when_message_already_queued);
    RUN_TEST(test_recv_returns_false_immediately_on_empty_closed_channel);
    RUN_TEST(test_recv_blocks_until_another_thread_pushes);

    RUN_TEST(test_subscriber_options_defaults);

    RUN_TEST(test_op_kind_families);
    RUN_TEST(test_op_kind_covers_every_op);
    RUN_TEST(test_op_kind_rejects_out_of_range_op);
    RUN_TEST(test_op_string_round_trips_every_op);
    RUN_TEST(test_op_string_rejects_out_of_range_op);
    RUN_TEST(test_op_from_string_rejects_unknown_and_null);
    RUN_TEST(test_adapt_strerror_never_null);
    RUN_TEST(test_adapt_strerror_covers_every_code);
    RUN_TEST(test_adapt_strerror_rejects_out_of_range_code);

    RUN_TEST(test_gpio_read_request_round_trips);
    RUN_TEST(test_gpio_write_request_maps_payload_and_evt_meta);
    RUN_TEST(test_gpio_write_request_rejects_wrong_payload_length);
    RUN_TEST(test_gpio_response_maps_bitmask_into_payload);
    RUN_TEST(test_response_to_message_rejects_wrong_bus);
    RUN_TEST(test_spi_transfer_request_maps_channel_meta_and_payload);
    RUN_TEST(test_i2c_transfer_request_has_no_channel_selector);
    RUN_TEST(test_uart_read_request_requires_read_size_meta);
    RUN_TEST(test_uart_read_request_round_trips_with_read_size_meta);
    RUN_TEST(test_uart_read_request_rejects_empty_read_size_meta);
    RUN_TEST(test_uart_read_request_rejects_meta_with_trailing_garbage);
    RUN_TEST(test_iseled_command_default_is_write_direction);
    RUN_TEST(test_iseled_command_read_size_meta_selects_read_direction);
    RUN_TEST(test_iseled_command_read_size_above_max_rejected);
    RUN_TEST(test_can_frame_request_rejects_xl_formats_as_out_of_scope);
    RUN_TEST(test_can_frame_request_accepts_classical_format);
    RUN_TEST(test_mdio_write_request_maps_addr_meta_and_packed_words);
    RUN_TEST(test_mdio_write_request_rejects_empty_payload);
    RUN_TEST(test_mdio_write_request_rejects_odd_payload_length);
    RUN_TEST(test_wakeup_sleepcmd_round_trips_without_timed_meta);
    RUN_TEST(test_discovery_response_maps_fields_and_server_stream_id);

    RUN_TEST(test_discovery_request_round_trips_default_read_size);
    RUN_TEST(test_uart_write_request_round_trips);
    RUN_TEST(test_adc_read_request_uses_default_read_size_when_meta_absent);
    RUN_TEST(test_pwm_out_read_request_round_trips);
    RUN_TEST(test_pwm_out_write_request_maps_payload_and_evt_meta);
    RUN_TEST(test_pwm_out_write_request_rejects_wrong_payload_length);
    RUN_TEST(test_pwm_in_read_request_round_trips);
    RUN_TEST(test_lin_command_request_round_trips);
    RUN_TEST(test_mdio_read_request_maps_addr_and_word_count_meta);
    RUN_TEST(test_wakeup_wakeup_request_round_trips);
    RUN_TEST(test_message_to_request_rejects_out_of_range_op);
    RUN_TEST(test_message_to_request_tolerates_null_out_err);
    RUN_TEST(test_response_to_message_tolerates_null_out_err);
    RUN_TEST(test_spi_transfer_response_maps_channel_and_payload);
    RUN_TEST(test_i2c_transfer_response_reports_read_size_for_read_direction);
    RUN_TEST(test_i2c_transfer_response_reports_zero_read_size_for_write_direction);
    RUN_TEST(test_uart_write_response_maps_accepted_payload);
    RUN_TEST(test_uart_read_response_maps_rx_payload);
    RUN_TEST(test_adc_read_response_maps_values_big_endian_and_count_meta);
    RUN_TEST(test_pwm_out_response_maps_value_payload_for_read_and_write_ops);
    RUN_TEST(test_pwm_in_read_response_maps_value_payload);
    RUN_TEST(test_lin_command_response_maps_rx_payload);
    RUN_TEST(test_can_frame_response_maps_frame_fields_and_payload);
    RUN_TEST(test_iseled_command_response_maps_rx_payload);
    RUN_TEST(test_mdio_read_response_maps_words_payload);
    RUN_TEST(test_mdio_write_response_maps_words_payload);
    RUN_TEST(test_wakeup_wakeup_response_round_trips);
    RUN_TEST(test_response_to_message_rejects_out_of_range_op);
    RUN_TEST(test_spi_transfer_response_decode_failure_is_reported);
    RUN_TEST(test_i2c_transfer_response_decode_failure_is_reported);
    RUN_TEST(test_uart_write_response_decode_failure_is_reported);
    RUN_TEST(test_uart_read_response_decode_failure_is_reported);
    RUN_TEST(test_adc_read_response_decode_failure_is_reported);
    RUN_TEST(test_pwm_out_response_decode_failure_is_reported);
    RUN_TEST(test_pwm_in_response_decode_failure_is_reported);
    RUN_TEST(test_lin_command_response_decode_failure_is_reported);
    RUN_TEST(test_can_frame_response_decode_failure_is_reported);
    RUN_TEST(test_iseled_command_response_decode_failure_is_reported);
    RUN_TEST(test_mdio_read_response_decode_failure_is_reported);
    RUN_TEST(test_mdio_write_response_decode_failure_is_reported);
    RUN_TEST(test_wakeup_sleepcmd_response_decode_failure_is_reported);
    RUN_TEST(test_wakeup_wakeup_response_decode_failure_is_reported);
    RUN_TEST(test_discovery_response_decode_failure_is_reported);

    RUN_TEST(test_adapt_returns_non_null);
    RUN_TEST(test_adapt_protocol_returns_rcp);
    RUN_TEST(test_adapt_rejects_op_outside_bound_kind);
    RUN_TEST(test_adapt_send_transmits_a_valid_gpio_write_request);
    RUN_TEST(test_adapt_send_reports_encode_error_when_ntscf_wrap_allocation_fails);
    RUN_TEST(test_adapt_send_returns_transport_error_when_queue_is_full);
    RUN_TEST(test_adapt_call_gpio_read_returns_mapped_response);
    RUN_TEST(test_adapt_call_discovery_returns_mapped_response);
    RUN_TEST(test_adapt_call_returns_decode_error_on_malformed_ntscf_reply);
    RUN_TEST(test_adapt_call_returns_transport_error_on_oversized_reply);
    RUN_TEST(test_adapt_subscribe_is_not_supported);
    RUN_TEST(test_adapt_close_is_idempotent);
    RUN_TEST(test_caller_retain_returns_same_pointer_and_keeps_it_alive);

    RUN_TEST(test_send_closed_error_is_relay_equivalent);
    RUN_TEST(test_call_timeout_error_is_relay_equivalent);
    RUN_TEST(test_ok_and_adapt_specific_errors_have_no_relay_equivalent);

    return UNITY_END();
}
