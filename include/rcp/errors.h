/* SPDX-License-Identifier: MPL-2.0 */
/*
 * errors.h -- the TC18 Remote Control Protocol wire error-code table
 * (ROADMAP.md Phase 23, closing the numeric-error-code gap the 2026-07-30
 * ecosystem audit found: this project shipped `rcp_errc_t`/module-local
 * `rcp_<mod>_errc_t` enums throughout, but no enum modeling the protocol's
 * own numbered wire error codes -- the values an RC Server actually places
 * in an Acknowledge/Response frame's err field to tell an RC Client what
 * went wrong).
 *
 * `rcp_wire_error_t` below assigns the seventeen numbered codes named,
 * described, and numbered by the OPEN Alliance TC18 Remote Control
 * Protocol Specification v0.5.1_RC's own error-code table (referenced
 * here by name only, per this project's standing policy against
 * reproducing spec prose -- the numeric values themselves are the
 * protocol's on-wire contract, not descriptive text, so they are
 * necessarily reproduced exactly; the enumerator names and one-line
 * descriptions below are this implementation's own paraphrase, not the
 * spec's wording).
 *
 * This header intentionally does not attempt to wire every internal
 * `rcp_errc_t`/`rcp_<mod>_errc_t` value in the codebase onto one of these
 * seventeen codes: most internal errors (e.g. "malloc failed", "buffer
 * too small") have no wire representation at all -- they never leave the
 * local process, so there is nothing in the spec's table for them to map
 * onto. `rcp_e2e_wire_error()` (`e2e.h`) was this project's own first
 * concrete mapping (a CRC32 mismatch, c-RCP-04); a module-by-module audit
 * across several later milestones (most recently issue #163) has since
 * added a dedicated `rcp_<mod>_wire_error()`-style mapping for every code
 * this codebase can genuinely detect: `rcp_ep_gpio_wire_error()`,
 * `rcp_ep_pwm_out_wire_error()`, `rcp_ep_pwm_in_wire_error()` (`ep_gpio.h`/
 * `ep_pwm.h`), `rcp_lifecycle_field_write_error()` (`lifecycle.h`),
 * `rcp_sequencer_wire_error()` (`request_sequencer.h`), and
 * `rcp_timed_wire_error()` (`request_timed.h`), plus several codes
 * (`RCP_ERROR_INVALID_PARAMETER`, `RCP_ERROR_EP_NOT_FOUND`,
 * `RCP_ERROR_REQUEST_STORAGE_OVERFLOW`, `RCP_ERROR_REQUEST_REJECTED`,
 * `RCP_ERROR_REQUEST_NOT_FOUND`, `RCP_ERROR_REQUEST_CANCELED`,
 * `RCP_ERROR_CHAIN_ABORTED`, `RCP_ERROR_CHAIN_ERROR`) reported directly
 * by `regmap.c`/`server.c`/`mock.c`'s own dispatch logic, with no
 * intermediate mapping function of their own. Two codes remain
 * genuinely unresolved, each with its own doc-comment explanation rather
 * than a forced mapping: `RCP_ERROR_EP_ERROR` (7) -- every endpoint
 * module's own `ep_status` register is deliberately treated as opaque,
 * TC18-undefined content throughout this codebase, so no internal
 * "endpoint execution fault" condition exists to map onto it -- and
 * `RCP_ERROR_PRESENTATION_TIME_TOO_FAR` (13)'s own dispatch-side
 * rejection, whose "product specific limit" (TC18 §11.2.2.7) has no
 * configured admission-horizon value anywhere in this codebase's
 * register map yet (see `rcp_timed_wire_error()`'s own doc comment,
 * `request_timed.h`).
 */
#ifndef RCP_ERRORS_H
#define RCP_ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

//cfusa:req REQ-WIREERR-001
/* The protocol's numbered wire error codes, exactly as assigned by the
 * governing spec's own error-code table. RCP_ERROR_NONE = 0 is this
 * implementation's own addition (not part of the spec's table, which
 * starts at 1) -- a sentinel for "no applicable wire code", returned by
 * mapping functions like rcp_e2e_wire_error() when the input has no
 * wire-error-code counterpart. */
typedef enum {
    RCP_ERROR_NONE                      = 0,
    RCP_ERROR_UNSUPPORTED_CMD           = 1,  /* the addressed operation/endpoint doesn't support the requested command */
    RCP_ERROR_SEQUENCER_NOT_KNOWN       = 2,  /* the referenced sequencer index isn't configured */
    RCP_ERROR_UNAUTHORIZED_ACCESS       = 3,  /* the caller isn't permitted to perform this request */
    RCP_ERROR_LOCKED_MEM_ACCESS         = 4,  /* the addressed register/memory is locked against this access */
    RCP_ERROR_REQUEST_CANCELED          = 5,  /* the request was canceled before it completed */
    RCP_ERROR_REQUEST_NOT_FOUND         = 6,  /* the referenced request (e.g. to cancel) is not known */
    RCP_ERROR_EP_ERROR                  = 7,  /* the addressed endpoint reported an internal fault */
    RCP_ERROR_EP_NOT_FOUND              = 8,  /* the addressed endpoint (byte_bus_id) does not exist */
    RCP_ERROR_PWM_IN_NO_SIGNAL          = 9,  /* a PWM input endpoint has no signal to report */
    RCP_ERROR_REQUEST_STORAGE_OVERFLOW  = 10, /* the request-storage capacity (e.g. sequencer/compound state) is exhausted */
    RCP_ERROR_REQUEST_REJECTED          = 11, /* the request was well-formed but rejected on other grounds */
    RCP_ERROR_POCI_FAILURE              = 12, /* the request's CRC32 (E2E protection) did not match -- see rcp_e2e_wire_error() */
    RCP_ERROR_PRESENTATION_TIME_TOO_FAR = 13, /* a timed request's presentation_time is outside the acceptable window */
    RCP_ERROR_GPTP_FAIL                 = 14, /* gPTP time sync is unavailable or has failed */
    RCP_ERROR_INVALID_PARAMETER         = 15, /* a request field's value is out of range or otherwise invalid */
    RCP_ERROR_CHAIN_ABORTED             = 16, /* a chained request sequence was aborted before completion */
    RCP_ERROR_CHAIN_ERROR               = 17, /* a chained request sequence failed internally */
} rcp_wire_error_t;

//cfusa:req REQ-WIREERR-002
/* Unique, non-empty, human-readable description for a rcp_wire_error_t
 * value. Never returns NULL; returns a generic "unknown" message for any
 * value not listed above. */
const char *rcp_wire_error_string(rcp_wire_error_t e);

#ifdef __cplusplus
}
#endif

#endif /* RCP_ERRORS_H */
