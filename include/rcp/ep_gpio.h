/* SPDX-License-Identifier: MPL-2.0 */
//cfusa:req REQ-GPIO-001
//cfusa:req REQ-GPIO-002
//cfusa:req REQ-GPIO-003
//cfusa:req REQ-GPIO-004
//cfusa:req REQ-GPIO-005
//cfusa:req REQ-GPIO-006
//cfusa:req REQ-GPIO-007
//cfusa:req REQ-GPIO-008
//cfusa:req REQ-GPIO-009
//cfusa:req REQ-GPIO-010
//cfusa:req REQ-GPIO-011
//cfusa:req REQ-GPIO-012
//cfusa:req REQ-GPIO-013
//cfusa:req REQ-GPIO-014
//cfusa:req REQ-GPIO-015
//cfusa:req REQ-GPIO-016
//cfusa:req REQ-GPIO-017
//cfusa:req REQ-GPIO-018
//cfusa:req REQ-GPIO-019
//cfusa:req REQ-GPIO-020
//cfusa:req REQ-GPIO-021
//cfusa:req REQ-GPIO-022
//cfusa:req REQ-GPIO-023
//cfusa:req REQ-GPIO-024
//cfusa:req REQ-GPIO-025
//cfusa:req REQ-GPIO-026
//cfusa:req REQ-GPIO-027
//cfusa:req REQ-GPIO-028
//cfusa:req REQ-GPIO-029
//cfusa:req REQ-GPIO-030
//cfusa:req REQ-GPIO-031
//cfusa:req REQ-GPIO-032

/* TC18 requirements-corpus completeness pass (v0.105.0): the ids below
 * are catalogued in .fusa-reqs.json with a "tc18" citation and a
 * "status" of "implemented", "partial" or "not-implemented". The
 * not-implemented and partial ones describe normative TC18 behaviour
 * this module does NOT provide; their tests pin the deviation. */
//cfusa:req REQ-GPIO-033
//cfusa:req REQ-GPIO-034
//cfusa:req REQ-GPIO-035
//cfusa:req REQ-GPIO-036

/* Input-pin write masking (issue #105): a write request to a pin
 * configured as input is now ignored for that pin (13.7.4.3), via
 * rcp_ep_gpio_apply_masked_write() -- see its own doc comment below. */
//cfusa:req REQ-GPIO-037
/*
 * ep_gpio.h -- GPIO endpoint for the TC18 Remote Control Protocol wire layer
 * (ROADMAP.md Phase 16, "Basic Endpoints", milestone 64).
 *
 * This is new, additive protocol-core surface layered on top of the AVTPDU
 * framing (avtp.h/avtp.c, milestone 59), the ACF message format (acf.h/
 * acf.c, milestone 60), the RC Server lifecycle state machine (server.h/
 * server.c, milestone 61), the register-map model (regmap.h/regmap.c,
 * milestone 62), and discovery (discovery.h/discovery.c, milestone 63).
 * Nothing in rcp.h, wire.c, avtp.h/avtp.c, acf.h/acf.c, server.h/server.c,
 * regmap.h/regmap.c, discovery.h/discovery.c, or any satellite package is
 * touched here.
 *
 * All type, field, and constant names in this header are this
 * implementation's own original engineering design unless a comment says
 * otherwise. This module implements original behavior *described by* the
 * confidential OPEN Alliance TC18 Remote Control Protocol Specification
 * v0.5.1_RC (referenced here by name only, per this project's standing
 * policy) -- no spec prose, bit layout, or numeric constant is reproduced.
 *
 * ── Framing level ────────────────────────────────────────────────────────────
 *
 * Unlike discovery.h (which is NTSCF-only and therefore owns its own
 * AVTPDU wrapping), a GPIO request/response is ordinary endpoint traffic:
 * whether it rides an NTSCF or TSCF frame is a transport/scheduling choice
 * made by the caller (avtp.h), not a property of the GPIO endpoint itself.
 * This module therefore operates at the ACF level only (acf.h's
 * rcp_acf_encode_abb()/_encode_gbb() and their decode counterparts) -- a
 * caller wraps (or unwraps) the frames this module produces (or consumes)
 * in NTSCF/TSCF using avtp.h directly, exactly as acf.c itself does not
 * wrap AVTP either.
 *
 * ── Request/response payload ─────────────────────────────────────────────────
 *
 * Both the request and the response payload are exactly
 * RCP_EP_GPIO_PAYLOAD_LEN (4) octets: a big-endian bitmask with one bit per
 * GPIO pin (RCP_EP_GPIO_MAX_PINS = 32 pins addressable). A read request
 * carries no payload of its own (mirroring discovery's own read-request
 * shape); a write request's payload is the 4-byte value to apply per the
 * write-semantics selected by evt[2:0] (see below). A response -- whether
 * answering a read or echoing the result of a write -- always carries the
 * resulting 4-byte bitmask.
 *
 * A response is encoded as ACF_ABB when untimed, or ACF_GBB (carrying a
 * message_timestamp) when the endpoint's ep_response_ts_enable functional-
 * config flag (regmap.h's rcp_regmap_ep_functional_cfg_t, composed into
 * rcp_ep_gpio_functional_cfg_t below) is set -- that flag's value is a
 * caller-supplied bool here, this module never itself reaches into a
 * register map to read it, matching this codebase's standing convention of
 * consuming already-classified inputs.
 *
 * ── evt[2:0]: the eight write-semantics variants ────────────────────────────
 *
 * A write request's ACF byte_message_info.evt field (acf.h; a 4-bit field
 * on the wire) carries the write semantics to apply in its low three bits,
 * evt[2:0] -- rcp_ep_gpio_write_semantics_t enumerates the eight values
 * that occupy (extraction §4.5 Group C). Six of the eight are ordinary
 * whole-register arithmetic/logical operations against the endpoint's
 * current 32-bit bitmask (replace/OR/AND/XOR/add/subtract); a seventh
 * (RCP_EP_GPIO_WRITE_RESERVED4, value 4) carries no assigned meaning and is
 * treated here as a documented no-op, matching this codebase's fail-safe
 * convention for a wire value with no defined write behavior. This
 * ordering (4=reserved, 5=add, 6=subtract) was independently cross-checked
 * against cpp-RCP's own WriteSemantics enum (derived from the same
 * structured spec extraction this module cites) and corrected accordingly
 * -- see issue #104. The eighth, RCP_EP_GPIO_WRITE_RECONFIG (value 7), is the
 * "reconfiguration escape hatch" -- see rcp_ep_gpio_apply_reconfig(), below,
 * for what it actually does.
 *
 * CORRECTED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group G, REQ-GPIO-013):
 * this escape hatch previously reinterpreted the 32-bit payload as a
 * per-pin selector and toggled each selected pin's direction
 * (RCP_REGMAP_PIN_PROP_OUTPUT <-> RCP_REGMAP_PIN_PROP_INPUT, regmap.h) --
 * a mechanism this file's own header, before this fix, already and
 * honestly labelled "this module's own original design for what the
 * escape hatch accomplishes". That candor was the tell: TC18 §12.7.1
 * (Figure 18, "Configuration request") defines exactly one mechanism for
 * evt[2:0]=111b, common to every endpoint type -- the byte_msg_payload is
 * a relative start address into the endpoint's own EP_func register block
 * followed by the data octets to patch there -- and ep_pwm.h's
 * rcp_ep_pwm_out_apply_reconfig() already implements it correctly. The
 * old pin-direction-toggle behavior corresponded to no TC18 mechanism at
 * all (Table 41, the GPIO functional-configuration register block, has no
 * per-pin direction field whatsoever -- direction lives in HW_config's
 * own hw_pin_type, a different subsystem entirely, configured only in
 * HW_UNCONFIGURED state, not via a per-request evt value). Retained, under
 * an honestly-scoped new name (rcp_ep_gpio_toggle_pin_direction(), below)
 * since it is still a real, useful caller-side convenience -- but it is no
 * longer described as, or reachable from, the evt[2:0]=111b wire
 * mechanism. rcp_ep_gpio_apply_reconfig() now names the real one.
 *
 * Add and subtract saturate at the bitmask's own 32-bit boundaries
 * (0x00000000 / 0xFFFFFFFF) rather than wrapping -- this module's own
 * extension, to GPIO's 32-bit register width, of the write-semantics
 * saturation behavior described generically (at a narrower word width) by
 * the specification for other endpoint types' fields.
 *
 * Subtract's operand order is the requested payload value minus the
 * current register value ("request minus current"), not the reverse: the
 * single evt[2:0]=110b row that defines the operation covers GPIO and
 * PWM_OUT jointly and states that order normatively (extraction §4.5
 * Group C). The row's parenthetical remark about decreasing a PWM duty
 * cycle is an illustrative note, not a competing definition. Add is
 * commutative and so is unaffected by this distinction.
 *
 * ── Per-pin trigger signals ──────────────────────────────────────────────────
 *
 * rcp_ep_gpio_trigger_t names the three asynchronous-event trigger modes a
 * pin's functional config may select (any-change, rising, falling), plus
 * NONE; rcp_ep_gpio_trigger_fires() is the pure, directly-testable
 * evaluation of one level transition against a selected mode.
 *
 * ── Functional config and register-locking-by-lifecycle-state ──────────────
 *
 * rcp_ep_gpio_functional_cfg_t composes regmap.h's
 * rcp_regmap_ep_functional_cfg_t as its own first member (per that module's
 * documented convention) and adds one rcp_ep_gpio_pin_cfg_t per pin: a
 * pin_property byte (regmap.h's RCP_REGMAP_PIN_PROP_* bitmask, mirroring
 * the HW pin-mapping table's own direction/pull-up/pull-down/drive-strength
 * fields, but here independently runtime-adjustable per pin) and a trigger
 * mode. rcp_ep_gpio_functional_cfg_writable() is a thin, named wrapper over
 * server.h's rcp_lifecycle_field_writable() (RCP_LIFECYCLE_FIELD_FUNCTIONAL_W --
 * this configuration is functionally, not permanently, re-lockable once
 * RCP_CONFIGURED, per server.h's own distinction between _W and _W_STAR);
 * rcp_ep_gpio_set_pin_property()/_set_pin_trigger() are this module's own
 * mutators consulting that authorization before ever touching cfg, reusing
 * -- never duplicating -- server.h's/regmap.h's existing authorization
 * logic (rcp_lifecycle_field_writable()/rcp_regmap_writer_ctx()), per the
 * roadmap's explicit instruction.
 *
 * ── The EP_func register block (evt[2:0] == 111b) ──────────────────────────
 *
 * ADDED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group G, REQ-GPIO-013):
 * rcp_ep_gpio_functional_cfg_t's ep_status/clk_divider/debounce fields and
 * rcp_ep_gpio_render_registers()/rcp_ep_gpio_apply_reconfig() give GPIO the
 * same real EP_func register block ep_pwm.h's PWM_OUT already has, at the
 * widths and order TC18's own GPIO functional-configuration register table
 * assigns them (relative addresses 0x0000 gpio_ep_len through 0x0028
 * gpio_debounce_IO31): gpio_ep_len(R)/gpio_io_max(R)/gpio_ep_enable&clr/
 * gpio_ep_options (both regmap.h's shared common-entries prefix)/
 * gpio_base_clk(R)/gpio_ep_status/gpio_clk_divider/gpio_debounce_IO0..IO31
 * (32 independent 8-bit debounce registers, one per pin).
 *
 * Known editorial defect in the source table, resolved the same way
 * ep_pwm.h's own file header resolves its analogous EP_LEN defect: the
 * table's own explicit, non-elided rows establish gpio_debounce_IO0 at
 * 0x0009 and gpio_debounce_IO1 at 0x000A -- one octet per register, in
 * pin order -- but the table's own summary label for the elided range's
 * last entry reads "0x0024 gpio_debounce_IO31", which is arithmetically
 * inconsistent with that same stated pattern (0x0009 + 31 = 0x0028, not
 * 0x0024) for an endpoint the table's own prose says handles "up to 32
 * IOs". The explicit, non-elided starting rows are the authoritative
 * statement of the pattern, so this module places gpio_debounce_IO31 at
 * the arithmetically-consistent 0x0028 (RCP_EP_GPIO_EP_FUNC_LEN = 0x0029,
 * 41 octets total) rather than at the table's own inconsistent elided-row
 * label.
 *
 * gpio_io_max(0x0001, R) is rendered as RCP_EP_GPIO_MAX_PINS (32) -- this
 * module's own reading of "the maximum IO Pin number supported by the
 * GPIO EP" as a pin *count* (matching how RCP_EP_GPIO_MAX_PINS is used as
 * a count everywhere else in this module), since the register table does
 * not itself disambiguate a count from a maximum zero-based index.
 *
 * The old evt[2:0]=111b pin-direction-toggle behavior (this module's own
 * invented mechanism, corresponding to no TC18 register at all -- see the
 * "eight write-semantics variants" section, above) is retained as
 * rcp_ep_gpio_toggle_pin_direction(), an honestly-scoped caller-side
 * convenience no longer described as, or reachable from, evt[2:0]=111b.
 */
#ifndef RCP_EP_GPIO_H
#define RCP_EP_GPIO_H

#include "rcp/acf.h"
#include "rcp/avtp.h"
#include "rcp/rcp.h"
#include "rcp/regmap.h"
#include "rcp/lifecycle.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Pin addressing ────────────────────────────────────────────────────────── */

/* The largest number of GPIO pins this endpoint type addresses via its
 * 4-byte bitmask. */
#define RCP_EP_GPIO_MAX_PINS ((uint8_t)32u)

/* True iff pin_index is a valid pin index (0..RCP_EP_GPIO_MAX_PINS-1). */
bool rcp_ep_gpio_pin_index_valid(uint8_t pin_index);

/* The single-bit mask for pin_index (1u << pin_index), or 0 if pin_index is
 * not rcp_ep_gpio_pin_index_valid(). */
uint32_t rcp_ep_gpio_pin_mask(uint8_t pin_index);

/* True iff pin_index's bit is set in bitmask. False (never an error code)
 * for an invalid pin_index, since rcp_ep_gpio_pin_mask() already returns 0
 * for one. */
bool rcp_ep_gpio_pin_get(uint32_t bitmask, uint8_t pin_index);

/* ── evt[2:0]: the eight write-semantics variants ──────────────────────────── */

typedef enum {
    RCP_EP_GPIO_WRITE_REPLACE   = 0,
    RCP_EP_GPIO_WRITE_OR        = 1,
    RCP_EP_GPIO_WRITE_AND       = 2,
    RCP_EP_GPIO_WRITE_XOR       = 3,
    RCP_EP_GPIO_WRITE_RESERVED4 = 4, /* documented no-op; see the file header */
    RCP_EP_GPIO_WRITE_ADD       = 5,
    RCP_EP_GPIO_WRITE_SUB       = 6,
    RCP_EP_GPIO_WRITE_RECONFIG  = 7, /* the reconfiguration escape hatch */
} rcp_ep_gpio_write_semantics_t;

/* True iff v (a raw evt[2:0] value as decoded off the wire) is one of the
 * eight defined write-semantics values, i.e. v <= 7. */
bool rcp_ep_gpio_write_semantics_valid(uint8_t v);

/* Computes the new 32-bit register value from current and a write request
 * of value request under evt's semantics. evt must be one of
 * RCP_EP_GPIO_WRITE_REPLACE .. _SUB or _RESERVED4 -- never
 * RCP_EP_GPIO_WRITE_RECONFIG (use rcp_ep_gpio_apply_reconfig() for that
 * one instead; see the file header). RCP_EP_GPIO_WRITE_ADD/_SUB saturate at
 * 0xFFFFFFFF/0x00000000 respectively rather than wrapping;
 * RCP_EP_GPIO_WRITE_SUB computes request - current (see the file header).
 * RCP_EP_GPIO_WRITE_RESERVED4 returns current unchanged. */
uint32_t rcp_ep_gpio_apply_write(uint32_t current, uint32_t request,
                                  rcp_ep_gpio_write_semantics_t evt);

/* RENAMED 2026-08-11 (c-RCP-AUDIT-06, issue #256 Group G, REQ-GPIO-013):
 * this was formerly named rcp_ep_gpio_apply_reconfig() and documented as
 * the evt[2:0]==7 handler; it is neither -- it corresponds to no TC18
 * register or mechanism at all (see the file header's "EP_func register
 * block" section for what evt[2:0]==7 actually does now, and its own
 * corrected note in "the eight write-semantics variants" section for why).
 * Retained under this honest name as a real, still-useful, purely
 * caller-side convenience: for every pin index i in
 * 0..RCP_EP_GPIO_MAX_PINS-1 whose bit is set in toggle_mask, toggles
 * pins[i] between RCP_REGMAP_PIN_PROP_OUTPUT and RCP_REGMAP_PIN_PROP_INPUT
 * (regmap.h), preserving pins[i]'s other pin_property bits unchanged;
 * every pin whose bit is clear in toggle_mask is left entirely untouched.
 * pins must point to an array of exactly RCP_EP_GPIO_MAX_PINS entries. */
void rcp_ep_gpio_toggle_pin_direction(uint8_t pins[RCP_EP_GPIO_MAX_PINS], uint32_t toggle_mask);

/* rcp_ep_gpio_apply_write() plus TC18 13.7.4.3's input-pin write rule ("A
 * write request to an input pin is ignored for this input pin"): computes
 * the same combined value rcp_ep_gpio_apply_write(current, request, evt)
 * would, then commits it only for bit positions whose pins[i] has
 * RCP_REGMAP_PIN_PROP_OUTPUT set, leaving current's bit unchanged wherever
 * pins[i] does not (issue #105). evt == RCP_EP_GPIO_WRITE_RECONFIG is
 * accepted -- rcp_ep_gpio_apply_write() already returns current unchanged
 * for it, so masking is a no-op; a caller routing a decoded write request
 * here is responsible for having checked evt[2:0] and, for
 * RCP_EP_GPIO_WRITE_RECONFIG, routed to rcp_ep_gpio_apply_reconfig()
 * instead (below) rather than here. pins must point to an array of exactly
 * RCP_EP_GPIO_MAX_PINS entries. */
uint32_t rcp_ep_gpio_apply_masked_write(uint32_t current, uint32_t request,
                                         rcp_ep_gpio_write_semantics_t evt,
                                         const uint8_t pins[RCP_EP_GPIO_MAX_PINS]);

/* ── Per-pin trigger signals ────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_GPIO_TRIGGER_NONE       = 0,
    RCP_EP_GPIO_TRIGGER_ANY_CHANGE = 1,
    RCP_EP_GPIO_TRIGGER_RISING     = 2,
    RCP_EP_GPIO_TRIGGER_FALLING    = 3,
} rcp_ep_gpio_trigger_t;

/* True iff a level transition from prev_level to new_level satisfies
 * trigger: never for NONE; for ANY_CHANGE iff prev_level != new_level; for
 * RISING iff prev_level is false and new_level is true; for FALLING iff
 * prev_level is true and new_level is false. */
bool rcp_ep_gpio_trigger_fires(rcp_ep_gpio_trigger_t trigger, bool prev_level, bool new_level);

/* ── Functional config ─────────────────────────────────────────────────────── */

/* One pin's runtime-adjustable functional configuration -- see the file
 * header. */
typedef struct {
    uint8_t pin_property; /* RCP_REGMAP_PIN_PROP_* bitmask (regmap.h) */
    uint8_t trigger;      /* rcp_ep_gpio_trigger_t */
} rcp_ep_gpio_pin_cfg_t;

typedef struct {
    rcp_regmap_ep_functional_cfg_t common; /* regmap.h's shared functional-
                                               config prefix, composed as the
                                               first member -- see the file
                                               header */
    rcp_ep_gpio_pin_cfg_t          pins[RCP_EP_GPIO_MAX_PINS];
    uint16_t                       ep_status;               /* gpio_ep_status */
    uint8_t                        clk_divider;              /* gpio_clk_divider */
    uint8_t                        debounce[RCP_EP_GPIO_MAX_PINS]; /* gpio_debounce_IO0..IO31,
                                                                        see the file header */
} rcp_ep_gpio_functional_cfg_t;

/* Zero-initializes cfg (common's flags all false, every pin's pin_property
 * 0 and trigger RCP_EP_GPIO_TRIGGER_NONE, ep_status 0, clk_divider 0, every
 * debounce entry 0 -- "0: no debounce", per the file header's own register
 * table). */
void rcp_ep_gpio_functional_cfg_init(rcp_ep_gpio_functional_cfg_t *cfg);

/* True iff this endpoint's functional config is writable in state by
 * writer -- a thin, named wrapper over rcp_lifecycle_field_writable()
 * (server.h) with kind RCP_LIFECYCLE_FIELD_FUNCTIONAL_W; see the file header.
 * Reuses, and never duplicates, that function's authorization logic. */
bool rcp_ep_gpio_functional_cfg_writable(rcp_lifecycle_state_t state,
                                         rcp_lifecycle_writer_ctx_t writer);

/* Sets cfg->pins[pin_index].pin_property to pin_property iff pin_index is
 * rcp_ep_gpio_pin_index_valid() and rcp_ep_gpio_functional_cfg_writable()
 * authorizes the write for state/writer; returns whether the write was
 * applied. cfg is left entirely unchanged when it returns false. */
bool rcp_ep_gpio_set_pin_property(rcp_ep_gpio_functional_cfg_t *cfg, uint8_t pin_index,
                                   uint8_t pin_property, rcp_lifecycle_state_t state,
                                   rcp_lifecycle_writer_ctx_t writer);

/* Same authorization/validity rule as rcp_ep_gpio_set_pin_property(), for
 * cfg->pins[pin_index].trigger. */
bool rcp_ep_gpio_set_pin_trigger(rcp_ep_gpio_functional_cfg_t *cfg, uint8_t pin_index,
                                  rcp_ep_gpio_trigger_t trigger, rcp_lifecycle_state_t state,
                                  rcp_lifecycle_writer_ctx_t writer);

/* ── The EP_func register block (evt[2:0] == 111b) -- see the file header ──── */

/* Relative addresses within this endpoint's own EP_func block, at the
 * widths and in the order TC18's own GPIO functional-configuration
 * register table assigns them -- see the file header for the
 * gpio_debounce_IO31 offset's own resolved editorial defect. Offsets
 * marked R are read-only: a configuration write covering them leaves them
 * unchanged (see rcp_ep_gpio_apply_reconfig()). */
#define RCP_EP_GPIO_REG_EP_LEN          ((uint16_t)0x0000u) /*  8 bit, R   */
#define RCP_EP_GPIO_REG_IO_MAX          ((uint16_t)0x0001u) /*  8 bit, R   */
#define RCP_EP_GPIO_REG_EP_ENABLE_CLR   ((uint16_t)0x0002u) /*  8 bit, R/W */
#define RCP_EP_GPIO_REG_EP_OPTIONS      ((uint16_t)0x0003u) /*  8 bit, R/W */
#define RCP_EP_GPIO_REG_BASE_CLK        ((uint16_t)0x0004u) /* 16 bit, R   */
#define RCP_EP_GPIO_REG_EP_STATUS       ((uint16_t)0x0006u) /* 16 bit, R/W */
#define RCP_EP_GPIO_REG_CLK_DIVIDER     ((uint16_t)0x0008u) /*  8 bit, R/W */
#define RCP_EP_GPIO_REG_DEBOUNCE_IO0    ((uint16_t)0x0009u) /*  8 bit, R/W;
                                                                 IO(n) at
                                                                 0x0009+n */

/* The block's own length in octets -- one past the last assigned offset
 * (gpio_debounce_IO31 at 0x0028, see the file header), i.e. the value the
 * endpoint reports at RCP_EP_GPIO_REG_EP_LEN and the bound the "write
 * beyond EP_LEN is ignored" rule (extraction §3.7.1) is applied against. */
#define RCP_EP_GPIO_EP_FUNC_LEN         ((uint16_t)0x0029u)

/* The fixed width (octets) of the relative-start-address prefix every
 * configuration request's payload begins with -- the address is a 16-bit
 * big-endian field, followed by the configuration data octets to write
 * from that address onward (extraction §3.7.1), matching ep_pwm.h's own
 * RCP_EP_PWM_OUT_RECONFIG_ADDR_LEN convention. */
#define RCP_EP_GPIO_RECONFIG_ADDR_LEN   ((size_t)2u)

typedef enum {
    RCP_EP_GPIO_RECONFIG_OK               = 0,
    RCP_EP_GPIO_RECONFIG_ERR_SHORT        = 1, /* payload carries no address
                                                    prefix, or an address
                                                    prefix with no data
                                                    octet after it */
    RCP_EP_GPIO_RECONFIG_ERR_OUT_OF_RANGE = 2, /* start_address + data
                                                    length exceeds
                                                    RCP_EP_GPIO_EP_FUNC_LEN
                                                    -- the whole write is
                                                    ignored, per the
                                                    specification's own
                                                    rule */
} rcp_ep_gpio_reconfig_errc_t;

/* Human-readable message for an rcp_ep_gpio_reconfig_errc_t value. Never
 * returns NULL. */
const char *rcp_ep_gpio_reconfig_strerror(rcp_ep_gpio_reconfig_errc_t e);

/* Serializes cfg's EP_func registers into out[0..RCP_EP_GPIO_EP_FUNC_LEN)
 * exactly as a configuration *read* of the whole block would report them
 * -- the inverse of rcp_ep_gpio_apply_reconfig()'s own parse step, and the
 * same rendering that function patches in place. */
void rcp_ep_gpio_render_registers(const rcp_ep_gpio_functional_cfg_t *cfg,
                                   uint8_t out[RCP_EP_GPIO_EP_FUNC_LEN]);

/* Applies the real configuration escape hatch (evt[2:0] == 111b): payload
 * is NOT presented at the interface but interpreted as an addressed write
 * into this endpoint's own EP_func block -- a 16-bit big-endian relative
 * start address followed by the configuration data octets to write from
 * that address onward (extraction §3.7.1, TC18 §12.7.1 Figure 18). This is
 * a real register write, reaching every R/W register the block defines
 * (enable/options, status, clock divider, all 32 per-pin debounce
 * registers) -- not the invented pin-direction toggle this function used
 * to be (see rcp_ep_gpio_toggle_pin_direction(), above, and the file
 * header).
 *
 * Returns RCP_EP_GPIO_RECONFIG_ERR_SHORT when payload_len is not at least
 * RCP_EP_GPIO_RECONFIG_ADDR_LEN + 1, and
 * RCP_EP_GPIO_RECONFIG_ERR_OUT_OF_RANGE when the addressed span would
 * extend past RCP_EP_GPIO_EP_FUNC_LEN; in both cases cfg is left entirely
 * unchanged, per the specification's own "such a payload is to be
 * ignored" rule. Octets of the addressed span that land on a read-only
 * register (EP_LEN, IO_MAX, base_clk) are left at their current values
 * while the rest of the span is still applied. Partially-covered
 * multi-octet registers are handled correctly: the write is applied at
 * octet granularity over the block's rendered image.
 *
 * A caller routing a decoded write request here is responsible for having
 * checked that evt[2:0] really was RCP_EP_GPIO_WRITE_RECONFIG;
 * rcp_ep_gpio_apply_write() deliberately no-ops for that evt value so a
 * misrouted request cannot silently corrupt the data registers. */
rcp_ep_gpio_reconfig_errc_t
rcp_ep_gpio_apply_reconfig(rcp_ep_gpio_functional_cfg_t *cfg,
                            const uint8_t *payload, size_t payload_len);

/* Encodes an ACF_ABB configuration request (evt[2:0] == 111b) addressed to
 * byte_bus_id: payload is start_address (16-bit big-endian) followed by
 * data[0..data_len). Returns a zeroed rcp_bytes_t (data=NULL) if data_len
 * is 0, if the encoded payload would exceed RCP_ACF_MAX_PAYLOAD, or on
 * allocation failure. Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_gpio_encode_reconfig_request(rcp_byte_bus_id_t byte_bus_id,
                                                 uint16_t start_address,
                                                 const uint8_t *data, size_t data_len,
                                                 uint8_t transaction_num);

/* ── Error codes ───────────────────────────────────────────────────────────── */

typedef enum {
    RCP_EP_GPIO_OK                  = 0,
    RCP_EP_GPIO_ERR_SHORT_FRAME     = 1,
    RCP_EP_GPIO_ERR_BAD_MSG_TYPE    = 2,
    RCP_EP_GPIO_ERR_WRONG_BUS       = 3,
    RCP_EP_GPIO_ERR_WRONG_OP        = 4,
    RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN = 5,
} rcp_ep_gpio_errc_t;

/* Human-readable message for an rcp_ep_gpio_errc_t value. Never returns NULL. */
const char *rcp_ep_gpio_strerror(rcp_ep_gpio_errc_t e);

/* FIXED 2026-08-12 (issue #201, REQ-GPIO-033): maps e to its numbered wire
 * error code (errors.h), for a caller building an Error Response frame
 * (e.g. via acf.h's rcp_acf_build_error_response()) once a request has
 * failed to decode. Returns RCP_ERROR_INVALID_PARAMETER for
 * RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN -- TC18 §13.7.4.1's own numbered code
 * for "a request not having exactly four bytes" -- and RCP_ERROR_NONE for
 * every other rcp_ep_gpio_errc_t value: RCP_EP_GPIO_OK means nothing went
 * wrong, and RCP_EP_GPIO_ERR_SHORT_FRAME/_BAD_MSG_TYPE/_WRONG_BUS/_WRONG_OP
 * are all local framing/routing outcomes a caller resolves before a
 * GPIO-specific Response frame -- addressed via this same byte_bus_id --
 * would even be constructible, matching rcp_e2e_wire_error()'s own
 * disposition for its analogous local-only codes (e2e.h). Matches this
 * codebase's established rcp_<module>_wire_error() naming convention. */
rcp_wire_error_t rcp_ep_gpio_wire_error(rcp_ep_gpio_errc_t e);

/* ── Request/response payload length ───────────────────────────────────────── */

/* The fixed payload length (octets) of a write request or a response --
 * see the file header. A read request itself carries no payload. */
#define RCP_EP_GPIO_PAYLOAD_LEN ((size_t)4u)

/* ── Read request ──────────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB read request addressed to byte_bus_id, with no
 * payload. Returns a zeroed rcp_bytes_t (data=NULL) on allocation failure.
 * Caller frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_gpio_encode_read_request(rcp_byte_bus_id_t byte_bus_id, uint8_t transaction_num);

/* Decodes and validates an ACF-level GPIO read request from b[0..len).
 * Fails with RCP_EP_GPIO_ERR_SHORT_FRAME if b is shorter than the ACF_ABB
 * fixed header or its declared payload length; RCP_EP_GPIO_ERR_BAD_MSG_TYPE
 * if b is not an ACF_ABB message; RCP_EP_GPIO_ERR_WRONG_BUS if its
 * byte_bus_id != expected_bus_id; RCP_EP_GPIO_ERR_WRONG_OP if its op is not
 * RCP_ACF_OP_READ. On RCP_EP_GPIO_OK, *out_transaction_num is populated. */
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_read_request(const uint8_t *b, size_t len,
                                                    rcp_byte_bus_id_t expected_bus_id,
                                                    uint8_t *out_transaction_num);

/* ── Write request ─────────────────────────────────────────────────────────── */

/* Encodes an ACF_ABB write request addressed to byte_bus_id: evt's low
 * three bits carry evt (an rcp_ep_gpio_write_semantics_t value, 0-7; any
 * other bits of the ACF header's evt field are left 0), and the payload is
 * bitmask as RCP_EP_GPIO_PAYLOAD_LEN big-endian octets. Returns a zeroed
 * rcp_bytes_t (data=NULL) on allocation failure. Caller frees the result
 * with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_gpio_encode_write_request(rcp_byte_bus_id_t byte_bus_id, uint32_t bitmask,
                                              rcp_ep_gpio_write_semantics_t evt,
                                              uint8_t transaction_num);

/* Decodes and validates a GPIO write request. Same ACF-level failure modes
 * as rcp_ep_gpio_decode_read_request() (short frame / bad msg type / wrong
 * bus), except RCP_EP_GPIO_ERR_WRONG_OP is returned when op is not
 * RCP_ACF_OP_WRITE, and RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN is returned when
 * the payload is not exactly RCP_EP_GPIO_PAYLOAD_LEN octets. On
 * RCP_EP_GPIO_OK, *out_bitmask, *out_evt (evt[2:0] of the header's evt
 * field; see rcp_ep_gpio_write_semantics_valid()), and
 * *out_transaction_num are populated. */
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_write_request(const uint8_t *b, size_t len,
                                                     rcp_byte_bus_id_t expected_bus_id,
                                                     uint32_t *out_bitmask, uint8_t *out_evt,
                                                     uint8_t *out_transaction_num);

/* ── Response ───────────────────────────────────────────────────────────────── */

/* Encodes a GPIO response carrying bitmask as its RCP_EP_GPIO_PAYLOAD_LEN
 * big-endian payload, echoing transaction_num. Encoded as ACF_ABB when
 * timed is false; as ACF_GBB (with message_timestamp set to timestamp,
 * mtv = RCP_ACF_MTV_VALID) when timed is true -- see the file header.
 * Returns a zeroed rcp_bytes_t (data=NULL) on allocation failure. Caller
 * frees the result with rcp_bytes_free(). */
rcp_bytes_t rcp_ep_gpio_encode_response(rcp_byte_bus_id_t byte_bus_id, uint32_t bitmask,
                                         uint8_t transaction_num, bool timed, uint64_t timestamp);

/* Decodes a GPIO response from either an ACF_ABB or ACF_GBB message (this
 * function peeks the ACF message type itself, unlike the request decoders
 * above, since a response's encoding depends on the responding endpoint's
 * own timed/untimed choice). Fails with RCP_EP_GPIO_ERR_SHORT_FRAME (frame
 * too short for the applicable fixed header or a full
 * RCP_EP_GPIO_PAYLOAD_LEN payload), RCP_EP_GPIO_ERR_WRONG_BUS
 * (byte_bus_id != expected_bus_id), or RCP_EP_GPIO_ERR_BAD_PAYLOAD_LEN
 * (payload present but not exactly RCP_EP_GPIO_PAYLOAD_LEN octets). On
 * RCP_EP_GPIO_OK, *out_bitmask and *out_transaction_num are populated;
 * *out_timed and *out_timestamp report whether the message was ACF_GBB
 * with a valid (rcp_acf_gbb_is_timed()) timestamp, and that timestamp's
 * value (0 when !*out_timed). */
rcp_ep_gpio_errc_t rcp_ep_gpio_decode_response(const uint8_t *b, size_t len,
                                                rcp_byte_bus_id_t expected_bus_id,
                                                uint32_t *out_bitmask, bool *out_timed,
                                                uint64_t *out_timestamp,
                                                uint8_t *out_transaction_num);

#ifdef __cplusplus
}
#endif

#endif /* RCP_EP_GPIO_H */
