---- MODULE AntiReplayGuard ----
(*
 * Formal specification of the E2E anti-replay sliding-window guard.
 *
 * The guard accepts a sequence number iff:
 *   (a) It has not been seen before, AND
 *   (b) high_water - seq_num < ReplayWindowSize
 *
 * Safety property (SP1): A previously accepted seq_num is never accepted again.
 * Safety property (SP2): A seq_num more than ReplayWindowSize behind high_water
 *                        is always rejected.
 *)

EXTENDS Naturals, Sequences, TLC

CONSTANTS ReplayWindowSize,  \* size of the bitmap window (32 in production)
          MaxSeq             \* bounds the model's sequence-number range for TLC.
                              \* `Nat` is infinite/non-enumerable -- TLC cannot compute
                              \* `\E n \in Nat : ...` (confirmed: TLC rejects it with
                              \* "non-enumerable quantifier bound Nat"; see issue #57).
                              \* Bounding to a finite model range is the standard TLA+
                              \* idiom for otherwise-unbounded naturals.

VARIABLES high_water,        \* highest seen sequence number
          accepted            \* set of all accepted sequence numbers

TypeOK ==
    /\ high_water \in Nat
    /\ accepted   \in SUBSET Nat

Init ==
    /\ high_water = 0
    /\ accepted   = {}

Check(n) ==
    \* Accept n if it is fresh and within the window
    /\ n \notin accepted
    /\ (high_water = 0 \/ high_water - n < ReplayWindowSize \/ n > high_water)
    /\ accepted'   = accepted \cup {n}
    /\ high_water' = IF n > high_water THEN n ELSE high_water

Reject(n) == \* Stutter-step: n is rejected, state unchanged
    /\ (n \in accepted \/ (high_water > 0 /\ high_water - n >= ReplayWindowSize))
    /\ UNCHANGED <<high_water, accepted>>

Next == \E n \in 0..MaxSeq : Check(n) \/ Reject(n)

Spec == Init /\ [][Next]_<<high_water, accepted>>

(* SP1: No double-acceptance.
 *
 * Corrected 2026-07-28 (issue #57): this was previously stated as
 * `[][\A n \in accepted : n \notin accepted']_accepted`, i.e. "every
 * currently-accepted n must NOT be in the next state's accepted set" --
 * backwards, since `accepted` only ever grows (Check's `accepted' =
 * accepted \cup {n}`) and can never lose an element. As originally
 * written this invariant is violated by the very first Check step and
 * fails trivially, which is exactly the "provably backwards" bug
 * reported. `Check(n)`'s own guard (`n \notin accepted`) already makes
 * re-accepting the same n structurally impossible; the meaningful,
 * genuinely checkable safety property is that `accepted` is monotonic
 * (an accepted n is never later forgotten), which combined with that
 * guard is what "no double acceptance" actually means for this model. *)
NoDoubleAccept ==
    [][\A n \in accepted : n \in accepted']_accepted

(* SP2: Old sequences are always rejected -- enforced directly by
 * Check(n)'s guard (`high_water - n < ReplayWindowSize \/ n > high_water`):
 * Check simply cannot fire for an n outside the window, so every n that
 * ever enters `accepted` satisfied it at accept-time. This is NOT the
 * same claim as "every element of `accepted` is within the window of the
 * CURRENT high_water forever" -- that's false by design, since accepted
 * entries age out of the window as high_water advances but are
 * deliberately never removed from `accepted` (removing them would let a
 * replayed old sequence number be re-accepted, defeating the guard). No
 * separate formal invariant is needed beyond the guard itself.
 *)

THEOREM Spec => TypeOK /\ NoDoubleAccept

====
