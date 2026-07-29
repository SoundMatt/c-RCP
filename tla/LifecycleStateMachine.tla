---- MODULE LifecycleStateMachine ----
(*
 * Formal specification of the TC18 RC Server lifecycle state machine
 * (ROADMAP.md Phase 14, "RC Server lifecycle state machine", milestone
 * 61; ISO 26262 ASIL-B). Replaces WatchdogProtocol.tla and
 * HealthStateMachine.tla, whose Healthy/Degraded/Faulted zone-controller
 * model was retired along with rcp_zone_t/rcp_controller_t (see
 * ROADMAP.md's Protocol Replacement Notice and Satellite Package
 * Disposition table).
 *
 * An RC Server is a single node with exactly one lifecycle state (no
 * per-zone fan-out the way the retired watchdog model had one state per
 * zone -- "redundancy.h/redundancy.c" | DEPRECATE | Built entirely on
 * Controller/Zone; an RC Server is a single node with one lifecycle
 * state"). The three states and the two upward-transition admission
 * gates below are this project's own symbolic names for concepts
 * `include/rcp/lifecycle.h` already implements as
 * `rcp_lifecycle_state_t`/`rcp_lifecycle_transition()`; no spec prose or
 * on-wire numeric constant is reproduced here.
 *
 * Safety property (SP1): the server can never reach RcpConfigured
 * directly from HwUnconfigured -- every upward transition passes through
 * HwConfigured.
 * Safety property (SP2): once a field class is locked while the server
 * is RcpConfigured, it never becomes unlocked again while the server
 * remains RcpConfigured (a lock can only be cleared by demoting all the
 * way back to HwUnconfigured -- a full reset).
 *)

EXTENDS TLC

HwUnconfigured  == "HwUnconfigured"
HwConfigured    == "HwConfigured"
RcpConfigured   == "RcpConfigured"

LifecycleStates == {HwUnconfigured, HwConfigured, RcpConfigured}

Unlocked == "Unlocked"
Locked   == "Locked"
LockStates == {Unlocked, Locked}

VARIABLES state,              \* current lifecycle state
          hw_cfg_consistent,  \* HW_CFG_INCONSISTENT check's current verdict
          rcp_cfg_consistent, \* RCP_CFG_INCONSISTENT check's current verdict
          field_lock          \* FUNCTIONAL_W_STAR-class field lock state

TypeOK ==
    /\ state              \in LifecycleStates
    /\ hw_cfg_consistent   \in BOOLEAN
    /\ rcp_cfg_consistent  \in BOOLEAN
    /\ field_lock          \in LockStates

Init ==
    /\ state              = HwUnconfigured
    /\ hw_cfg_consistent   \in BOOLEAN
    /\ rcp_cfg_consistent  \in BOOLEAN
    /\ field_lock          = Unlocked

(* Manifest/HW config may change between transition attempts -- the
 * plausibility checks' verdicts are re-evaluated each time
 * rcp_lifecycle_transition() runs, not cached once and for all. *)
ReviseHwConsistency ==
    /\ hw_cfg_consistent' \in BOOLEAN
    /\ UNCHANGED <<state, rcp_cfg_consistent, field_lock>>

ReviseRcpConsistency ==
    /\ rcp_cfg_consistent' \in BOOLEAN
    /\ UNCHANGED <<state, hw_cfg_consistent, field_lock>>

(* HW_UNCONFIGURED -> HW_CONFIGURED, gated by the HW_CFG_INCONSISTENT
 * check (REQ-LIFECYCLE-008). *)
PromoteToHwConfigured ==
    /\ state = HwUnconfigured
    /\ hw_cfg_consistent
    /\ state' = HwConfigured
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent, field_lock>>

(* HW_CONFIGURED -> RCP_CONFIGURED, gated by the RCP_CFG_INCONSISTENT
 * check (REQ-LIFECYCLE-009). HW_GENERIC fields become read-only and
 * FUNCTIONAL_W_STAR fields become permanently locked for the remainder
 * of this configured session on this same transition
 * (REQ-LIFECYCLE-018, REQ-LIFECYCLE-020). *)
PromoteToRcpConfigured ==
    /\ state = HwConfigured
    /\ rcp_cfg_consistent
    /\ state' = RcpConfigured
    /\ field_lock' = Locked
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent>>

(* HW_CONFIGURED -> HW_UNCONFIGURED demotion is unconditional
 * (REQ-LIFECYCLE-010). *)
DemoteToHwUnconfigured ==
    /\ state = HwConfigured
    /\ state' = HwUnconfigured
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent, field_lock>>

(* RCP_CONFIGURED -> HW_UNCONFIGURED full-reset demotion is unconditional
 * (REQ-LIFECYCLE-011) and is the only way to clear a field lock -- a
 * full reset re-opens hardware configuration from scratch, so whatever
 * FUNCTIONAL_W_STAR fields were locked for the just-ended configured
 * session no longer apply to the next one. This is a modeling
 * assumption, not a literal reading of any single requirement above;
 * see FORMAL_VERIFICATION.md's "Assumptions and Abstractions". *)
FullReset ==
    /\ state = RcpConfigured
    /\ state' = HwUnconfigured
    /\ field_lock' = Unlocked
    /\ UNCHANGED <<hw_cfg_consistent, rcp_cfg_consistent>>

Next ==
    \/ ReviseHwConsistency
    \/ ReviseRcpConsistency
    \/ PromoteToHwConfigured
    \/ PromoteToRcpConfigured
    \/ DemoteToHwUnconfigured
    \/ FullReset

Spec == Init /\ [][Next]_<<state, hw_cfg_consistent, rcp_cfg_consistent, field_lock>>

(* SP1: No skip-configuration transition -- RcpConfigured is only ever
 * reached from HwConfigured, never directly from HwUnconfigured. *)
NoSkipConfiguration ==
    [][state = HwUnconfigured => state' # RcpConfigured]_<<state>>

(* SP2: A field lock, once set while RcpConfigured, is never cleared
 * except by the full-reset transition back to HwUnconfigured -- i.e. it
 * never silently reverts to Unlocked while the server remains
 * RcpConfigured. *)
FieldLockMonotonicWhileConfigured ==
    [][ (state = RcpConfigured /\ field_lock = Locked /\ state' = RcpConfigured)
            => field_lock' = Locked ]_<<state, field_lock>>

THEOREM Spec => TypeOK /\ NoSkipConfiguration /\ FieldLockMonotonicWhileConfigured

====
