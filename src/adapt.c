/* SPDX-License-Identifier: MPL-2.0 */
#include "rcp/adapt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#include "rcp/discovery.h"
#include "rcp/ep_adc.h"
#include "rcp/ep_can.h"
#include "rcp/ep_gpio.h"
#include "rcp/ep_i2c.h"
#include "rcp/ep_iseled.h"
#include "rcp/ep_lin.h"
#include "rcp/ep_mdio.h"
#include "rcp/ep_pwm.h"
#include "rcp/ep_spi.h"
#include "rcp/ep_uart.h"
#include "rcp/ep_wakeup.h"
#include "rcp/power.h"
#include "rcp/alloc.h"

/* ── Op <-> kind / string (see adapt.h's file header table) ─────────────────── */

//cfusa:req REQ-RELAY-007
rcp_adapt_ep_kind_t rcp_adapt_op_kind(rcp_adapt_op_t op)
{
    switch (op) {
    case RCP_ADAPT_OP_GPIO_READ:       return RCP_ADAPT_EP_GPIO;
    case RCP_ADAPT_OP_GPIO_WRITE:      return RCP_ADAPT_EP_GPIO;
    case RCP_ADAPT_OP_SPI_TRANSFER:    return RCP_ADAPT_EP_SPI;
    case RCP_ADAPT_OP_I2C_TRANSFER:    return RCP_ADAPT_EP_I2C;
    case RCP_ADAPT_OP_UART_WRITE:      return RCP_ADAPT_EP_UART;
    case RCP_ADAPT_OP_UART_READ:       return RCP_ADAPT_EP_UART;
    case RCP_ADAPT_OP_ADC_READ:        return RCP_ADAPT_EP_ADC;
    case RCP_ADAPT_OP_PWM_OUT_READ:    return RCP_ADAPT_EP_PWM_OUT;
    case RCP_ADAPT_OP_PWM_OUT_WRITE:   return RCP_ADAPT_EP_PWM_OUT;
    case RCP_ADAPT_OP_PWM_IN_READ:     return RCP_ADAPT_EP_PWM_IN;
    case RCP_ADAPT_OP_LIN_COMMAND:     return RCP_ADAPT_EP_LIN;
    case RCP_ADAPT_OP_CAN_FRAME:       return RCP_ADAPT_EP_CAN;
    case RCP_ADAPT_OP_ISELED_COMMAND:  return RCP_ADAPT_EP_ISELED;
    case RCP_ADAPT_OP_MDIO_READ:       return RCP_ADAPT_EP_MDIO;
    case RCP_ADAPT_OP_MDIO_WRITE:      return RCP_ADAPT_EP_MDIO;
    case RCP_ADAPT_OP_WAKEUP_SLEEPCMD: return RCP_ADAPT_EP_WAKEUP;
    case RCP_ADAPT_OP_WAKEUP_WAKEUP:   return RCP_ADAPT_EP_WAKEUP;
    case RCP_ADAPT_OP_DISCOVERY:       return RCP_ADAPT_EP_DISCOVERY;
    default:                           return RCP_ADAPT_EP_DISCOVERY;
    }
}

typedef struct {
    rcp_adapt_op_t op;
    const char    *name;
} op_name_entry_t;

static const op_name_entry_t OP_NAMES[] = {
    { RCP_ADAPT_OP_GPIO_READ,       "gpio_read" },
    { RCP_ADAPT_OP_GPIO_WRITE,      "gpio_write" },
    { RCP_ADAPT_OP_SPI_TRANSFER,    "spi_transfer" },
    { RCP_ADAPT_OP_I2C_TRANSFER,    "i2c_transfer" },
    { RCP_ADAPT_OP_UART_WRITE,      "uart_write" },
    { RCP_ADAPT_OP_UART_READ,       "uart_read" },
    { RCP_ADAPT_OP_ADC_READ,        "adc_read" },
    { RCP_ADAPT_OP_PWM_OUT_READ,    "pwm_out_read" },
    { RCP_ADAPT_OP_PWM_OUT_WRITE,   "pwm_out_write" },
    { RCP_ADAPT_OP_PWM_IN_READ,     "pwm_in_read" },
    { RCP_ADAPT_OP_LIN_COMMAND,     "lin_command" },
    { RCP_ADAPT_OP_CAN_FRAME,       "can_frame" },
    { RCP_ADAPT_OP_ISELED_COMMAND,  "iseled_command" },
    { RCP_ADAPT_OP_MDIO_READ,       "mdio_read" },
    { RCP_ADAPT_OP_MDIO_WRITE,      "mdio_write" },
    { RCP_ADAPT_OP_WAKEUP_SLEEPCMD, "wakeup_sleepcmd" },
    { RCP_ADAPT_OP_WAKEUP_WAKEUP,   "wakeup_wakeup" },
    { RCP_ADAPT_OP_DISCOVERY,       "discovery" },
};
#define OP_NAMES_COUNT (sizeof(OP_NAMES) / sizeof(OP_NAMES[0]))

//cfusa:req REQ-RELAY-007
const char *rcp_adapt_op_string(rcp_adapt_op_t op)
{
    size_t i;
    for (i = 0; i < OP_NAMES_COUNT; i++) {
        if (OP_NAMES[i].op == op) return OP_NAMES[i].name;
    }
    return "unknown";
}

//cfusa:req REQ-RELAY-007
bool rcp_adapt_op_from_string(const char *name, rcp_adapt_op_t *out)
{
    size_t i;
    if (!name) return false;
    for (i = 0; i < OP_NAMES_COUNT; i++) {
        if (strcmp(OP_NAMES[i].name, name) == 0) {
            *out = OP_NAMES[i].op;
            return true;
        }
    }
    return false;
}

//cfusa:req REQ-RELAY-011
const char *rcp_adapt_strerror(rcp_adapt_errc_t e)
{
    switch (e) {
    case RCP_ADAPT_OK:               return "ok";
    case RCP_ADAPT_ERR_CLOSED:       return "closed";
    case RCP_ADAPT_ERR_TIMEOUT:      return "timeout";
    case RCP_ADAPT_ERR_ENCODE:       return "could not encode message as a wire request";
    case RCP_ADAPT_ERR_DECODE:       return "could not decode wire response as a message";
    case RCP_ADAPT_ERR_TRANSPORT:    return "underlying transport failure";
    case RCP_ADAPT_ERR_NOT_SUPPORTED: return "not supported";
    default:                         return "unknown rcp_adapt_errc_t";
    }
}

/* ── Meta helpers (decimal-string convention, see adapt.h's field table) ────── */

static bool meta_get_u32(const relay_message_t *msg, const char *key, uint32_t *out)
{
    const char *v = relay_message_get_meta(msg, key);
    char *end = NULL;
    unsigned long parsed;
    if (!v || !*v) return false;
    parsed = strtoul(v, &end, 10);
    if (!end || *end != '\0') return false;
    *out = (uint32_t)parsed;
    return true;
}

static uint32_t meta_get_u32_default(const relay_message_t *msg, const char *key, uint32_t def)
{
    uint32_t v;
    return meta_get_u32(msg, key, &v) ? v : def;
}

static void meta_set_u32(relay_message_t *msg, const char *key, uint32_t v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)v);
    relay_message_set_meta(msg, key, buf);
}

static void meta_set_u64(relay_message_t *msg, const char *key, uint64_t v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
    relay_message_set_meta(msg, key, buf);
}

static void meta_set_bool(relay_message_t *msg, const char *key, bool v)
{
    relay_message_set_meta(msg, key, v ? "true" : "false");
}

/* The largest ADC response this bridge decodes into a relay message. An
 * ADC response carries N 2-octet measurement values (ep_adc.h); this bound
 * caps the on-stack landing buffers here without constraining ep_adc.c's
 * own RCP_EP_ADC_MAX_VALUES ceiling. A response carrying more values than
 * this is rejected as undecodable rather than truncated. */
#define RCP_ADAPT_ADC_MAX_VALUES ((size_t)64u)

/* ── Fixed-width big-endian scalar pack/unpack (GPIO/ADC/PWM_OUT/PWM_IN) ─────── */

static uint16_t be16_decode(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

static void be16_encode(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint32_t be32_decode(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void be32_encode(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static rcp_ep_pwm_value_t pwm_value_decode(const uint8_t *p)
{
    rcp_ep_pwm_value_t v;
    v.period          = be16_decode(p);
    v.active_duration = be16_decode(p + 2);
    return v;
}

static void pwm_value_encode(uint8_t *p, rcp_ep_pwm_value_t v)
{
    be16_encode(p, v.period);
    be16_encode(p + 2, v.active_duration);
}

/* ── rcp_message_to_request() ─────────────────────────────────────────────── */

static rcp_bytes_t fail_encode(rcp_adapt_errc_t *out_err)
{
    rcp_bytes_t zero = {0};
    if (out_err) *out_err = RCP_ADAPT_ERR_ENCODE;
    return zero;
}

//cfusa:req REQ-RELAY-005
rcp_bytes_t rcp_message_to_request(rcp_adapt_op_t op, rcp_byte_bus_id_t byte_bus_id,
                                    rcp_stream_id_t requester_stream_id,
                                    const relay_message_t *msg, uint8_t transaction_num,
                                    rcp_adapt_errc_t *out_err)
{
    rcp_bytes_t result = {0};

    if (out_err) *out_err = RCP_ADAPT_OK;

    switch (op) {
    case RCP_ADAPT_OP_DISCOVERY: {
        uint32_t read_size = meta_get_u32_default(msg, "rcp.discovery.read_size",
                                                    (uint32_t)RCP_DISCOVERY_GENERAL_SLICE_LEN);
        result = rcp_discovery_encode_request(requester_stream_id, (uint8_t)read_size,
                                               transaction_num);
        break;
    }

    case RCP_ADAPT_OP_GPIO_READ:
        result = rcp_ep_gpio_encode_read_request(byte_bus_id, transaction_num);
        break;

    case RCP_ADAPT_OP_GPIO_WRITE: {
        uint32_t evt;
        uint32_t bitmask;
        if (msg->payload.len != RCP_EP_GPIO_PAYLOAD_LEN) return fail_encode(out_err);
        evt     = meta_get_u32_default(msg, "rcp.gpio.evt", 0);
        bitmask = be32_decode(msg->payload.data);
        result  = rcp_ep_gpio_encode_write_request(byte_bus_id, bitmask,
                                                    (rcp_ep_gpio_write_semantics_t)evt,
                                                    transaction_num);
        break;
    }

    case RCP_ADAPT_OP_SPI_TRANSFER: {
        /* read_size (TC18 §13.7.3.3) defaults to the payload's own length
         * when absent -- an unannotated request still asks for exactly
         * what it sends back, the same "no zero-fill, no truncation"
         * behavior rcp_ep_spi_transfer_length() computes for that case. */
        uint32_t channel   = meta_get_u32_default(msg, "rcp.spi.channel", 0);
        uint32_t read_size = meta_get_u32_default(msg, "rcp.spi.read_size",
                                                    (uint32_t)msg->payload.len);
        result = rcp_ep_spi_encode_transfer_request(byte_bus_id, (uint8_t)channel,
                                                     msg->payload.data, msg->payload.len,
                                                     (uint16_t)read_size, transaction_num);
        break;
    }

    case RCP_ADAPT_OP_I2C_TRANSFER: {
        /* An I2C transfer is half duplex: the payload's own address octet
         * carries the I2C-bus-level R/W bit, and the ACF op sense says
         * whether a payload-bearing response is expected (ep_i2c.h). A
         * caller asking for octets back selects the read direction by
         * setting rcp.i2c.read_size; the default (absent or 0) is the
         * write direction. */
        uint32_t read_size = meta_get_u32_default(msg, "rcp.i2c.read_size", 0);
        if (read_size > RCP_EP_I2C_MAX_READ_SIZE) return fail_encode(out_err);
        result = rcp_ep_i2c_encode_transfer_request(
            byte_bus_id, read_size != 0 ? RCP_EP_I2C_DIR_READ : RCP_EP_I2C_DIR_WRITE,
            msg->payload.data, msg->payload.len, (uint16_t)read_size, transaction_num);
        break;
    }

    case RCP_ADAPT_OP_UART_WRITE:
        result = rcp_ep_uart_encode_write_request(byte_bus_id, msg->payload.data,
                                                    msg->payload.len, transaction_num);
        break;

    case RCP_ADAPT_OP_UART_READ: {
        uint32_t read_size;
        if (!meta_get_u32(msg, "rcp.uart.read_size", &read_size)) return fail_encode(out_err);
        result = rcp_ep_uart_encode_read_request(byte_bus_id, (uint16_t)read_size,
                                                  transaction_num);
        break;
    }

    case RCP_ADAPT_OP_ADC_READ: {
        /* read_size selects how many 2-octet measurement values the
         * response is to carry (ep_adc.h); default to a single value's
         * worth so an unannotated request still asks for something
         * well-formed. */
        uint32_t read_size = meta_get_u32_default(msg, "rcp.adc.read_size",
                                                    (uint32_t)RCP_EP_ADC_VALUE_LEN);
        result = rcp_ep_adc_encode_read_request(byte_bus_id, (uint16_t)read_size,
                                                 transaction_num);
        break;
    }

    case RCP_ADAPT_OP_PWM_OUT_READ:
        result = rcp_ep_pwm_out_encode_read_request(byte_bus_id, transaction_num);
        break;

    case RCP_ADAPT_OP_PWM_OUT_WRITE: {
        uint32_t evt;
        rcp_ep_pwm_value_t value;
        if (msg->payload.len != RCP_EP_PWM_PAYLOAD_LEN) return fail_encode(out_err);
        evt   = meta_get_u32_default(msg, "rcp.pwm.evt", 0);
        value = pwm_value_decode(msg->payload.data);
        result = rcp_ep_pwm_out_encode_write_request(byte_bus_id, value,
                                                       (rcp_ep_pwm_out_write_semantics_t)evt,
                                                       transaction_num);
        break;
    }

    case RCP_ADAPT_OP_PWM_IN_READ:
        result = rcp_ep_pwm_in_encode_read_request(byte_bus_id, transaction_num);
        break;

    case RCP_ADAPT_OP_LIN_COMMAND:
        result = rcp_ep_lin_encode_command_request(byte_bus_id, msg->payload.data,
                                                    msg->payload.len, transaction_num);
        break;

    case RCP_ADAPT_OP_CAN_FRAME: {
        uint32_t frame_format;
        uint32_t arbitration_id;
        if (!meta_get_u32(msg, "rcp.can.frame_format", &frame_format)) return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.can.arbitration_id", &arbitration_id)) return fail_encode(out_err);
        /* xl_header is always NULL here: ep_can.h's own encoder already
         * requires a non-NULL xl_header for the two CAN XL formats and
         * fails (zeroed rcp_bytes_t) when it's missing -- see adapt.h's
         * file header on why CAN XL is out of this interim mapping's
         * scope. */
        result = rcp_ep_can_encode_frame_request(byte_bus_id,
                                                  (rcp_ep_can_frame_format_t)frame_format,
                                                  arbitration_id, NULL, msg->payload.data,
                                                  msg->payload.len, transaction_num);
        break;
    }

    case RCP_ADAPT_OP_ISELED_COMMAND: {
        /* FIXED (issue #471): an ISELED command request is half duplex,
         * exactly like RCP_ADAPT_OP_I2C_TRANSFER above -- a caller asking
         * for octets back selects the read direction by setting
         * rcp.iseled.read_size; the default (absent or 0) is the write
         * direction, unchanged from before this fix. */
        uint32_t read_size = meta_get_u32_default(msg, "rcp.iseled.read_size", 0);
        if (read_size > RCP_EP_ISELED_MAX_READ_SIZE) return fail_encode(out_err);
        if (read_size != 0) {
            result = rcp_ep_iseled_encode_read_request(byte_bus_id, msg->payload.data,
                                                         msg->payload.len, (uint16_t)read_size,
                                                         transaction_num);
        } else {
            result = rcp_ep_iseled_encode_command_request(byte_bus_id, msg->payload.data,
                                                           msg->payload.len, transaction_num);
        }
        break;
    }

    case RCP_ADAPT_OP_MDIO_READ: {
        uint32_t clause, prtad, devad, regad, word_count;
        rcp_ep_mdio_addr_t addr;
        if (!meta_get_u32(msg, "rcp.mdio.clause", &clause))          return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.mdio.prtad", &prtad))            return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.mdio.devad", &devad))            return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.mdio.regad", &regad))            return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.mdio.word_count", &word_count))  return fail_encode(out_err);
        addr.clause = (rcp_ep_mdio_clause_t)clause;
        addr.prtad  = (uint8_t)prtad;
        addr.devad  = (uint8_t)devad;
        addr.regad  = (uint16_t)regad;
        result = rcp_ep_mdio_encode_read_request(byte_bus_id, addr, (size_t)word_count,
                                                  transaction_num);
        break;
    }

    case RCP_ADAPT_OP_MDIO_WRITE: {
        uint32_t clause, prtad, devad, regad;
        rcp_ep_mdio_addr_t addr;
        size_t word_count;
        uint16_t words[RCP_EP_MDIO_MAX_BURST_WORDS];
        size_t i;
        if (!meta_get_u32(msg, "rcp.mdio.clause", &clause)) return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.mdio.prtad", &prtad))   return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.mdio.devad", &devad))   return fail_encode(out_err);
        if (!meta_get_u32(msg, "rcp.mdio.regad", &regad))   return fail_encode(out_err);
        if (msg->payload.len == 0 || (msg->payload.len % 2) != 0) return fail_encode(out_err);
        word_count = msg->payload.len / 2;
        if (word_count > RCP_EP_MDIO_MAX_BURST_WORDS) return fail_encode(out_err);
        for (i = 0; i < word_count; i++) {
            words[i] = rcp_ep_mdio_unpack_word_at(msg->payload.data, i);
        }
        addr.clause = (rcp_ep_mdio_clause_t)clause;
        addr.prtad  = (uint8_t)prtad;
        addr.devad  = (uint8_t)devad;
        addr.regad  = (uint16_t)regad;
        result = rcp_ep_mdio_encode_write_request(byte_bus_id, addr, words, word_count,
                                                   transaction_num);
        break;
    }

    case RCP_ADAPT_OP_WAKEUP_SLEEPCMD:
        /* REQ-WAKEUP-010 (corrected 2026-08-10, c-RCP-AUDIT-06, issue
         * #256 Group E): the WakeUp endpoint's own SleepCMD wire message
         * carries no target-mode field at all (TC18 §13.7.2.3 Figure 22)
         * -- this op no longer reads a "rcp.wakeup.target_mode" metadata
         * field, since there is nothing left to select. */
        result = rcp_ep_wakeup_encode_sleepcmd_request(byte_bus_id, transaction_num);
        break;

    case RCP_ADAPT_OP_WAKEUP_WAKEUP:
        result = rcp_ep_wakeup_encode_wakeup_message(byte_bus_id, transaction_num);
        break;

    default:
        return fail_encode(out_err);
    }

    if (!result.data) {
        if (out_err) *out_err = RCP_ADAPT_ERR_ENCODE;
    }
    return result;
}

/* ── rcp_response_to_message() ────────────────────────────────────────────── */

static relay_message_t fail_decode(rcp_adapt_errc_t *out_err)
{
    relay_message_t zero;
    relay_message_init(&zero);
    if (out_err) *out_err = RCP_ADAPT_ERR_DECODE;
    return zero;
}

/* Common tail for every op whose own ep_*.h codec reports timed/timestamp/
 * transaction_num (every op except the two RCP_ADAPT_OP_WAKEUP_* ops and
 * RCP_ADAPT_OP_DISCOVERY -- see adapt.h's field table). */
static relay_message_t finish_timed_response(bool timed, uint64_t timestamp,
                                              uint8_t transaction_num)
{
    relay_message_t msg;
    relay_message_init(&msg);
    msg.protocol     = RELAY_PROTOCOL_RCP;
    msg.timestamp_ms = rcp_wallclock_ms();
    meta_set_bool(&msg, "rcp.timed", timed);
    if (timed) meta_set_u64(&msg, "rcp.timestamp", timestamp);
    meta_set_u32(&msg, "rcp.transaction_num", transaction_num);
    return msg;
}

/* The actual per-op decode/mapping switch, unchanged from before RELAY
 * spec v2.0 landed -- rcp_response_to_message() below wraps this to add
 * the one new field v2.0's §15.7.5 base mapping requires that this
 * function's own per-op branches don't already set (see that wrapper's
 * own comment for why it's a wrapper and not folded in per-branch). */
static relay_message_t response_to_message_impl(rcp_adapt_op_t op, rcp_byte_bus_id_t byte_bus_id,
                                                  const uint8_t *b, size_t len,
                                                  rcp_adapt_errc_t *out_err)
{
    if (out_err) *out_err = RCP_ADAPT_OK;

    switch (op) {
    case RCP_ADAPT_OP_DISCOVERY: {
        rcp_discovery_result_t result;
        relay_message_t msg;
        char id_buf[17];
        if (rcp_discovery_decode_response(b, len, &result) != RCP_DISCOVERY_OK) {
            return fail_decode(out_err);
        }
        relay_message_init(&msg);
        msg.protocol     = RELAY_PROTOCOL_RCP;
        msg.timestamp_ms = rcp_wallclock_ms();
        snprintf(id_buf, sizeof(id_buf), "%016llx",
                 (unsigned long long)rcp_stream_id_to_u64(result.server_stream_id));
        relay_message_set_id(&msg, id_buf);
        meta_set_u32(&msg, "rcp.discovery.magic", result.magic);
        meta_set_u32(&msg, "rcp.discovery.svr_version", result.svr_version);
        meta_set_u32(&msg, "rcp.discovery.vendor_id", result.vendor_id);
        meta_set_u32(&msg, "rcp.discovery.device_id", result.device_id);
        meta_set_u32(&msg, "rcp.discovery.svr_ep_count", result.svr_ep_count);
        return msg;
    }

    case RCP_ADAPT_OP_GPIO_READ:
    case RCP_ADAPT_OP_GPIO_WRITE: {
        uint32_t bitmask;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        uint8_t payload[RCP_EP_GPIO_PAYLOAD_LEN];
        relay_message_t msg;
        if (rcp_ep_gpio_decode_response(b, len, byte_bus_id, &bitmask, &timed, &timestamp,
                                         &transaction_num) != RCP_EP_GPIO_OK) {
            return fail_decode(out_err);
        }
        be32_encode(payload, bitmask);
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(payload, sizeof(payload));
        return msg;
    }

    case RCP_ADAPT_OP_SPI_TRANSFER: {
        uint8_t channel;
        const uint8_t *rx_data;
        size_t rx_len;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_spi_decode_response(b, len, byte_bus_id, &channel, &rx_data, &rx_len, &timed,
                                        &timestamp, &transaction_num) != RCP_EP_SPI_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(rx_data, rx_len);
        meta_set_u32(&msg, "rcp.spi.channel", channel);
        return msg;
    }

    case RCP_ADAPT_OP_I2C_TRANSFER: {
        rcp_ep_i2c_dir_t direction;
        const uint8_t *rx_data;
        size_t rx_len;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_i2c_decode_response(b, len, byte_bus_id, &direction, &rx_data, &rx_len, &timed,
                                        &timestamp, &transaction_num) != RCP_EP_I2C_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(rx_data, rx_len);
        /* A read response carries the octets read back; a write response
         * carries none (ep_i2c.h). Report which this was in the same key
         * the request used to ask for it. */
        meta_set_u32(&msg, "rcp.i2c.read_size",
                     direction == RCP_EP_I2C_DIR_READ ? (uint32_t)rx_len : 0u);
        return msg;
    }

    case RCP_ADAPT_OP_UART_WRITE: {
        const uint8_t *accepted_data;
        size_t accepted_len;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_uart_decode_write_response(b, len, byte_bus_id, &accepted_data, &accepted_len,
                                               &timed, &timestamp, &transaction_num)
            != RCP_EP_UART_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(accepted_data, accepted_len);
        return msg;
    }

    case RCP_ADAPT_OP_UART_READ: {
        const uint8_t *rx_data;
        size_t rx_len;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_uart_decode_read_response(b, len, byte_bus_id, &rx_data, &rx_len, &timed,
                                              &timestamp, &transaction_num) != RCP_EP_UART_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(rx_data, rx_len);
        return msg;
    }

    case RCP_ADAPT_OP_ADC_READ: {
        uint16_t values[RCP_ADAPT_ADC_MAX_VALUES];
        size_t value_count;
        size_t i;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        uint8_t payload[RCP_ADAPT_ADC_MAX_VALUES * 2u];
        relay_message_t msg;
        if (rcp_ep_adc_decode_response(b, len, byte_bus_id, values, RCP_ADAPT_ADC_MAX_VALUES,
                                        &value_count, &timed, &timestamp,
                                        &transaction_num) != RCP_EP_ADC_OK) {
            return fail_decode(out_err);
        }
        /* An ADC response carries N measurement values (ep_adc.h); the
         * bridged payload is all of them, big-endian, in order. */
        for (i = 0; i < value_count; i++) {
            be16_encode(&payload[i * 2u], values[i]);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(payload, value_count * 2u);
        meta_set_u32(&msg, "rcp.adc.value_count", (uint32_t)value_count);
        return msg;
    }

    case RCP_ADAPT_OP_PWM_OUT_READ:
    case RCP_ADAPT_OP_PWM_OUT_WRITE: {
        rcp_ep_pwm_value_t value;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        uint8_t payload[RCP_EP_PWM_PAYLOAD_LEN];
        relay_message_t msg;
        if (rcp_ep_pwm_out_decode_response(b, len, byte_bus_id, &value, &timed, &timestamp,
                                            &transaction_num) != RCP_EP_PWM_OUT_OK) {
            return fail_decode(out_err);
        }
        pwm_value_encode(payload, value);
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(payload, sizeof(payload));
        return msg;
    }

    case RCP_ADAPT_OP_PWM_IN_READ: {
        rcp_ep_pwm_value_t value;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        uint8_t payload[RCP_EP_PWM_PAYLOAD_LEN];
        relay_message_t msg;
        if (rcp_ep_pwm_in_decode_response(b, len, byte_bus_id, &value, &timed, &timestamp,
                                           &transaction_num) != RCP_EP_PWM_IN_OK) {
            return fail_decode(out_err);
        }
        pwm_value_encode(payload, value);
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(payload, sizeof(payload));
        return msg;
    }

    case RCP_ADAPT_OP_LIN_COMMAND: {
        const uint8_t *rx_data;
        size_t rx_len;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_lin_decode_response(b, len, byte_bus_id, &rx_data, &rx_len, &timed, &timestamp,
                                        &transaction_num) != RCP_EP_LIN_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(rx_data, rx_len);
        return msg;
    }

    case RCP_ADAPT_OP_CAN_FRAME: {
        rcp_ep_can_frame_format_t frame_format;
        uint32_t arbitration_id;
        rcp_ep_can_xl_header_t xl_header;
        const uint8_t *rx_data;
        size_t rx_len;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_can_decode_frame_response(b, len, byte_bus_id, &frame_format, &arbitration_id,
                                              &xl_header, &rx_data, &rx_len, &timed, &timestamp,
                                              &transaction_num) != RCP_EP_CAN_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(rx_data, rx_len);
        meta_set_u32(&msg, "rcp.can.frame_format", (uint32_t)frame_format);
        meta_set_u32(&msg, "rcp.can.arbitration_id", arbitration_id);
        return msg;
    }

    case RCP_ADAPT_OP_ISELED_COMMAND: {
        const uint8_t *rx_data;
        size_t rx_len;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_iseled_decode_response(b, len, byte_bus_id, &rx_data, &rx_len, &timed,
                                           &timestamp, &transaction_num) != RCP_EP_ISELED_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(rx_data, rx_len);
        return msg;
    }

    case RCP_ADAPT_OP_MDIO_READ: {
        const uint8_t *words_data;
        size_t word_count;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_mdio_decode_read_response(b, len, byte_bus_id, &words_data, &word_count,
                                              &timed, &timestamp, &transaction_num)
            != RCP_EP_MDIO_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(words_data, word_count * 2u);
        return msg;
    }

    case RCP_ADAPT_OP_MDIO_WRITE: {
        const uint8_t *words_data;
        size_t word_count;
        bool timed;
        uint64_t timestamp;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_mdio_decode_write_response(b, len, byte_bus_id, &words_data, &word_count,
                                               &timed, &timestamp, &transaction_num)
            != RCP_EP_MDIO_OK) {
            return fail_decode(out_err);
        }
        msg = finish_timed_response(timed, timestamp, transaction_num);
        msg.payload = relay_bytes_dup(words_data, word_count * 2u);
        return msg;
    }

    case RCP_ADAPT_OP_WAKEUP_SLEEPCMD: {
        rcp_pwrmode_entry_result_t result;
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_wakeup_decode_sleepcmd_response(b, len, byte_bus_id, &result,
                                                    &transaction_num) != RCP_EP_WAKEUP_OK) {
            return fail_decode(out_err);
        }
        relay_message_init(&msg);
        msg.protocol     = RELAY_PROTOCOL_RCP;
        msg.timestamp_ms = rcp_wallclock_ms();
        meta_set_u32(&msg, "rcp.transaction_num", transaction_num);
        meta_set_u32(&msg, "rcp.wakeup.result", (uint32_t)result);
        return msg;
    }

    case RCP_ADAPT_OP_WAKEUP_WAKEUP: {
        uint8_t transaction_num;
        relay_message_t msg;
        if (rcp_ep_wakeup_decode_wakeup_message(b, len, byte_bus_id, &transaction_num)
            != RCP_EP_WAKEUP_OK) {
            return fail_decode(out_err);
        }
        relay_message_init(&msg);
        msg.protocol     = RELAY_PROTOCOL_RCP;
        msg.timestamp_ms = rcp_wallclock_ms();
        meta_set_u32(&msg, "rcp.transaction_num", transaction_num);
        return msg;
    }

    default:
        return fail_decode(out_err);
    }
}

/* RELAY spec v2.0 §15.7.5 defines RCP's own canonical ToMessage() mapping:
 * a response Message's `ID` is the responding endpoint's ByteBusID as a
 * decimal string. This module's own per-op mapping (this file's header
 * comment explains why it is a richer superset of that base shape, not a
 * literal implementation of it) never set relay_message_t::id for any op
 * except RCP_ADAPT_OP_DISCOVERY, whose id instead names the responding
 * server's stream_id (discovery has no single meaningful ByteBusID of its
 * own to report -- it's always issued against the fixed discovery bus,
 * per lifecycle.h's RCP_LIFECYCLE_DISCOVERY_BYTE_BUS_ID -- and per
 * §15.7.5's own closing paragraph, extending `ID`'s encoding for a
 * multi-stream-capable adapter is an explicitly sanctioned deviation from
 * the single-field base shape, provided it's documented, which this is).
 * Every other op has exactly one well-defined ByteBusID -- the byte_bus_id
 * parameter already threaded through every branch above -- so setting it
 * uniformly here, once, is both correct and simpler than repeating a
 * relay_message_set_id() call in all 17 non-discovery branches. */
//cfusa:req REQ-RELAY-006
relay_message_t rcp_response_to_message(rcp_adapt_op_t op, rcp_byte_bus_id_t byte_bus_id,
                                         const uint8_t *b, size_t len,
                                         rcp_adapt_errc_t *out_err)
{
    rcp_adapt_errc_t err = RCP_ADAPT_OK;
    relay_message_t  msg = response_to_message_impl(op, byte_bus_id, b, len, &err);

    if (out_err) *out_err = err;
    if (err == RCP_ADAPT_OK && op != RCP_ADAPT_OP_DISCOVERY) {
        /* byte_bus_id is 0-2047 (REQ-RMAP-053/REQ-ACF-020) -- "2047" +
         * NUL is 5 bytes; this used to be char[4] ("255"+NUL) back when
         * rcp_byte_bus_id_t was 8 bits wide. snprintf itself was never
         * unsafe (it always NUL-terminates within the buffer it's
         * given), but a 4-byte buffer would have silently truncated
         * any 4-digit id to 3 digits plus NUL -- a real formatting bug
         * once the type widened, fixed here alongside it. */
        char id_buf[6];
        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)byte_bus_id);
        relay_message_set_id(&msg, id_buf);
    }
    return msg;
}

/* ── Error wrapping (§5.2) ─────────────────────────────────────────────────── */

//cfusa:req REQ-RELAY-017
bool rcp_errc_to_relay_errc(int rcp_ec, relay_errc_t *out)
{
    switch (rcp_ec) {
    case RCP_ERR_CLOSED:  *out = RELAY_ERRC_CLOSED;  return true;
    case RCP_ERR_TIMEOUT: *out = RELAY_ERRC_TIMEOUT; return true;
    default:              return false;
    }
}

/* ── RcpAdapter — implements rcp_relay_caller_t over rcp_avtp_transport_t ───── */

/* The largest AVTPDU this adapter ever needs to receive: every op it
 * supports rides NTSCF framing only (see adapt.h's file header), so the
 * ceiling is RCP_AVTP_NTSCF_MAX_PAYLOAD plus headroom for the NTSCF fixed
 * header itself -- comfortably under a kilobyte in practice, nowhere near
 * TSCF's much larger 65535-octet ceiling this adapter never needs. */
#define ADAPT_RECV_BUF_LEN ((size_t)(RCP_AVTP_NTSCF_MAX_PAYLOAD + 64u))

typedef struct {
    rcp_relay_caller_t    base;
    rcp_avtp_transport_t *transport; /* retained */
    rcp_stream_id_t        local_stream_id;
    rcp_byte_bus_id_t      byte_bus_id;
    rcp_adapt_ep_kind_t    kind;
    volatile int            next_transaction;
    volatile int            next_sequence;
} rcp_adapter_t;

static relay_protocol_t adapter_protocol(rcp_relay_caller_t *self)
{
    (void)self;
    return RELAY_PROTOCOL_RCP;
}

static bool resolve_op(const rcp_adapter_t *a, const relay_message_t *msg, rcp_adapt_op_t *out_op)
{
    rcp_adapt_op_t op;
    if (!rcp_adapt_op_from_string(relay_message_get_meta(msg, "rcp.adapt.op"), &op)) return false;
    if (rcp_adapt_op_kind(op) != a->kind) return false;
    *out_op = op;
    return true;
}

/* Packs msg into a fully wire-ready frame for op: an ACF request wrapped
 * in NTSCF for every op except RCP_ADAPT_OP_DISCOVERY, which
 * rcp_message_to_request() already returns fully NTSCF-framed. */
static rcp_bytes_t build_frame(rcp_adapter_t *a, rcp_adapt_op_t op, const relay_message_t *msg,
                                uint8_t transaction_num, rcp_adapt_errc_t *out_err)
{
    rcp_bytes_t body = rcp_message_to_request(op, a->byte_bus_id, a->local_stream_id, msg,
                                               transaction_num, out_err);
    rcp_avtp_ntscf_header_t hdr;
    rcp_bytes_t frame;

    if (!body.data) return body;
    if (op == RCP_ADAPT_OP_DISCOVERY) return body; /* already NTSCF-framed */

    hdr.sv                = 1;
    hdr.version            = 0;
    hdr.ntscf_data_length  = 0; /* recomputed by the encoder */
    hdr.sequence_num       = (uint8_t)rcp_atomic_inc(&a->next_sequence);
    hdr.stream_id          = a->local_stream_id;

    frame = rcp_avtp_encode_ntscf(&hdr, body.data, body.len);
    rcp_bytes_free(&body);
    if (!frame.data && out_err) *out_err = RCP_ADAPT_ERR_ENCODE;
    return frame;
}

/* send maps msg to a request and transmits it, discarding any reply (§10.6). */
//cfusa:req REQ-RELAY-008
static int adapter_send(rcp_relay_caller_t *self, const relay_context_t *ctx,
                         const relay_message_t *msg)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    rcp_adapt_op_t op;
    rcp_bytes_t frame;
    uint8_t transaction_num;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;
    int ec;

    (void)ctx;

    if (!resolve_op(a, msg, &op)) return RCP_ADAPT_ERR_ENCODE;

    transaction_num = (uint8_t)rcp_atomic_inc(&a->next_transaction);
    frame = build_frame(a, op, msg, transaction_num, &err);
    if (!frame.data) return err;

    ec = rcp_avtp_transport_send(a->transport, frame.data, frame.len);
    rcp_bytes_free(&frame);
    if (ec == RCP_OK) return RCP_ADAPT_OK;
    if (ec == RCP_ERR_CLOSED) return RCP_ADAPT_ERR_CLOSED;
    return RCP_ADAPT_ERR_TRANSPORT;
}

/* call maps req to a request, transmits it, blocks (subject to ctx) for
 * exactly one reply frame, and maps that frame back to *out (§10.2). */
//cfusa:req REQ-RELAY-009
static int adapter_call(rcp_relay_caller_t *self, const relay_context_t *ctx,
                         const relay_message_t *req, relay_message_t *out)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    rcp_adapt_op_t op;
    rcp_bytes_t frame;
    uint8_t transaction_num;
    rcp_adapt_errc_t err = RCP_ADAPT_OK;
    int ec;
    uint8_t recv_buf[ADAPT_RECV_BUF_LEN];
    size_t recv_len = 0;
    relay_message_t resp;

    if (!resolve_op(a, req, &op)) return RCP_ADAPT_ERR_ENCODE;

    transaction_num = (uint8_t)rcp_atomic_inc(&a->next_transaction);
    frame = build_frame(a, op, req, transaction_num, &err);
    if (!frame.data) return err;

    ec = rcp_avtp_transport_send(a->transport, frame.data, frame.len);
    rcp_bytes_free(&frame);
    if (ec == RCP_ERR_CLOSED) return RCP_ADAPT_ERR_CLOSED;
    if (ec != RCP_OK) return RCP_ADAPT_ERR_TRANSPORT;

    ec = rcp_avtp_transport_recv(a->transport, ctx, recv_buf, sizeof(recv_buf), &recv_len);
    if (ec == RCP_ERR_TIMEOUT) return RCP_ADAPT_ERR_TIMEOUT;
    if (ec == RCP_ERR_CLOSED) return RCP_ADAPT_ERR_CLOSED;
    if (ec != RCP_OK) return RCP_ADAPT_ERR_TRANSPORT;

    if (op == RCP_ADAPT_OP_DISCOVERY) {
        resp = rcp_response_to_message(op, a->byte_bus_id, recv_buf, recv_len, &err);
    } else {
        rcp_avtp_ntscf_header_t hdr;
        const uint8_t *acf = NULL;
        size_t acf_len = 0;
        if (rcp_avtp_decode_ntscf(recv_buf, recv_len, &hdr, &acf, &acf_len) != RCP_AVTP_OK) {
            return RCP_ADAPT_ERR_DECODE;
        }
        resp = rcp_response_to_message(op, a->byte_bus_id, acf, acf_len, &err);
    }
    if (err != RCP_ADAPT_OK) return err;

    *out = resp;
    return RCP_ADAPT_OK;
}

//cfusa:req REQ-RELAY-010
static int adapter_subscribe(rcp_relay_caller_t *self, const relay_subscriber_options_t *opts,
                              relay_message_channel_t **out)
{
    /* No native TC18 periodic Status-equivalent stream exists to wrap --
     * see adapt.h's file header. */
    (void)self;
    (void)opts;
    (void)out;
    return RCP_ADAPT_ERR_NOT_SUPPORTED;
}

static int adapter_close(rcp_relay_caller_t *self)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    return rcp_avtp_transport_close(a->transport);
}

static void adapter_destroy(rcp_relay_caller_t *self)
{
    rcp_adapter_t *a = (rcp_adapter_t *)self;
    rcp_avtp_transport_release(a->transport);
    rcp_free(a);
    a = NULL;
}

static const rcp_relay_caller_vtable_t adapter_vtable = {
    adapter_protocol,
    adapter_send,
    adapter_call,
    adapter_subscribe,
    adapter_close,
    adapter_destroy,
};

/* ── Adapt() (§10.3) ──────────────────────────────────────────────────────── */

//cfusa:req REQ-RELAY-012
rcp_relay_caller_t *rcp_adapt(rcp_avtp_transport_t *transport, rcp_stream_id_t local_stream_id,
                               rcp_byte_bus_id_t byte_bus_id, rcp_adapt_ep_kind_t kind)
{
    rcp_adapter_t *a = (rcp_adapter_t *)rcp_calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->base.vt           = &adapter_vtable;
    a->base.refcount      = 1;
    a->transport          = rcp_avtp_transport_retain(transport);
    a->local_stream_id    = local_stream_id;
    a->byte_bus_id        = byte_bus_id;
    a->kind               = kind;
    a->next_transaction   = 0;
    a->next_sequence      = 0;
    return &a->base;
}
