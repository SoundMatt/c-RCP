# TC18 SHOULD/MAY clauses with no corresponding requirement

This document is the companion to `.fusa-reqs.json`'s MUST/SHALL requirement
catalog. Every genuinely testable, implementable capability the OPEN
Alliance TC18 Remote Control Protocol Specification describes with SHOULD
or MAY is either already implemented and cited from an existing
`REQ-*` entry (search `.fusa-reqs.json` for the citation text below to find
which one), or listed here because it is **not** a library-testable
behavior at all: design-goal prose about the standard itself, advice to a
human composing client requests or authoring server configuration, a
hardware-deployment or physical-layer choice outside this protocol
library's scope, or a non-closed-list permission.

Every SHOULD/MAY occurrence in the specification is accounted for in one
of these two places. Nothing was silently dropped. See ROADMAP.md
milestone 116 for the audit that produced this document, and
`.fusa-reqs.json`'s own `tc18`-cited entries for the ones that turned out
to be testable and are tracked there instead.

No text from the specification is reproduced verbatim below (confidentiality
restriction — see this repo's own citation convention); each line is
paraphrased and cited by section/page-line reference into the source PDF's
`pdftotext` extraction.

## SHOULD (10, all non-testable)

| § | Citation | Paraphrase | Why no REQ |
|---|---|---|---|
| §2 (intro) | TC18.txt L773 | Implementation details of remote-controlled interfaces should not be exposed | Design-goal prose about the standard's own philosophy, not a protocol clause a client/server implementation is checked against. |
| §2 | TC18.txt L790 | Communication over unreliable channels should be possible | Same — design goal, not a specific mechanism. |
| §2 | TC18.txt L795 | RCP should enable interchangeability of HW/SW modules | Same. |
| §2 | TC18.txt L798 | RCP should provide an unambiguous definition of behavior | Same. |
| §2 | TC18.txt L803 | RCP should differentiate messages from newer/older versions | Same — no concrete versioning mechanism named at this point in the spec to implement against. |
| §2 | TC18.txt L805 | Older-version implementations should interact safely with newer ones | Same. |
| §11.2.2.1 | TC18.txt L1213 | A repetitive `cmp_start_state=0` compound request should be last-sent or use `cmp_exec_delay>0` | Client request-composition advice (avoiding starvation of lower-priority requests) — nothing for the RC Server itself to enforce or a test to assert. |
| §11.2.2.2 | TC18.txt L1312 | Same advice for compound-wait requests | Same. |
| §12.9.1 | TC18.txt L2988 | Endpoints sharing a `byte_bus_id` in one stream should be the same type | Client/config-authoring guidance ("this has to be ensured by the instance sending the configuration" — the spec's own words a few lines earlier); not RC Server-enforceable at runtime. |
| §13.7.12.2 | TC18.txt L5525 | `iseled_clk_divider` should be nominal 2MHz | Hardware calibration guidance for a config register's chosen value, not a testable software behavior. |

## MAY (26 remaining — not already covered by a `.fusa-reqs.json` citation)

| § | Citation | Paraphrase | Why no REQ |
|---|---|---|---|
| §1 (overview) | TC18.txt L640 | Edge Node role may support additional functionality (PTP, MACsec) | L1/L2 network-topology concern, outside this library's RCP-wire-protocol scope entirely. |
| §11.1 | TC18.txt L909 | RC Server may receive data from a connected device via an interface | Scene-setting prose describing the general RC Server concept, not a specific feature. |
| §11.2.1 | TC18.txt L1024 | Responses/acknowledges of one endpoint may target different streams | Descriptive restatement of the per-stream responder-queue architecture (already implemented; the testable behavior is covered by the responder-queue requirements, not this framing sentence). |
| §11.2.1 | TC18.txt L1025 | RC Clients using the same endpoint may use different `byte_bus_id`s | Descriptive, permissive statement about client behavior; nothing for the server to test. |
| §11.2.2.5 | TC18.txt L1588 | RC Client may want a request executed at a predefined time | Descriptive lead-in to the Timed request feature (the feature itself is implemented and cited elsewhere). |
| §11.4.1 | TC18.txt L1943 | RC Server may synchronize an internal clock to a system clock (gPTP) | Descriptive of the gPTP synchronization mechanism generally (implemented via `clock.c`/`avtp.c` timestamp handling, used throughout timed-request admission logic) — no single function is "the" implementation of this sentence to cite. |
| §12.3 | TC18.txt L2060 | Device may have NVM memory or default settings | Describes a possible hardware implementation choice, not a wire-testable behavior. |
| §12.3 | TC18.txt L2062 | Device may incorporate default settings allowing an advanced start state | Same. |
| §12.3.1 | TC18.txt L2244 | RC Server may already be in RCP_CONFIGURED during vehicle assembly | Illustrative deployment scenario, not itself a requirement. |
| §12.3.1 | TC18.txt L2289 | Post-cold-start configuration may be based on default values | Implementation-choice description. |
| §12.3.2 | TC18.txt L2355 | RC Server may receive a discovery request in any lifecycle state | The permissive half of a sentence whose SHALL half (must send a discovery response) is already in the MUST-clause catalog; the MAY itself just states "any state is fine," which the discovery-acceptance requirements already reflect by not restricting on state. |
| §12.3.2 | TC18.txt L2385 | A discovery request's Ethernet frame may have any DA/SA/VLAN tag | Framing-layer permissiveness (the server must not filter on these), a receiver-side non-restriction rather than a feature to build. |
| §12.3.2 | TC18.txt L2405 | Whether a discovered server is relevant may depend on client-side criteria | Client-side decision, outside server library's testable scope. |
| §12.7.5 | TC18.txt L2565 | `svr_io_pin_count` restrictions may be device-specific | Register-field documentation pointer, not a behavior. |
| §12.7.6 | TC18.txt L2668 | HW pin config options may be RC-Server-specific | Same — documents that a field's meaning is implementation-defined, not a feature. |
| §12.9.1 | TC18.txt L2984 | One RC Client may use multiple streams for the same endpoint | Client-config permission, not server-testable. |
| §12.9.1 | TC18.txt L2986 | A stream may address multiple EPs | Same. |
| §12.9.1 | TC18.txt L2989 | Requests may be filed N times per N matching client_config entries | Descriptive consequence of client configuration choices; nothing structurally prevents this already (no special-case code needed). |
| §12.9.1 | TC18.txt L3197 | The RC Server itself, treated as an endpoint, may have a `byte_bus_id` per stream | Descriptive of the already-implemented EP0-addressing architecture. |
| §12.9.1 | TC18.txt L3206 | An EP may send an acknowledge, depending on configuration | Descriptive of the already-implemented optional-acknowledge (`evt[3]`) mechanism. |
| §12.9.1.1 | TC18.txt L3227 | Request types may be: [list] | Enumeration lead-in; the list's actual entries are Table 5, already in the MUST-clause catalog. |
| §13.1 | TC18.txt L3252 | Whether a conditional request is due may depend on sequencer/trigger/etc. | Descriptive of the already-implemented due-request selection architecture (`select_due`-style logic); not a distinct feature. |
| §13.7.3.1 | TC18.txt L4323 | SPI CS demuxing may be handled via the endpoint's own configuration | Physical-layer/hardware-deployment detail, not software-testable. |
| §13.7.9.1 | TC18.txt L5035 | RC Client may request an ADC reading at any time | Trivially true permissive statement; nothing in the ADC endpoint blocks arbitrary-time reads, so there is no special behavior to test. |
| §13.7.9.2 | TC18.txt L5164 | A measured ADC value may contain the average of multiple samples | Descriptive of the already-implemented averaging feature. |
| §13.7.11.2 | TC18.txt L5358 | CAN's functional-config field list may be extended per implementation | Non-closed-list permission (the list isn't a hard ceiling), not itself a feature to build. |
