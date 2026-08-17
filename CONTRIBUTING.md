# Contributing to c-RCP

## DCO sign-off

Every commit must include a Developer Certificate of Origin sign-off:

```
Signed-off-by: Your Name <your@email.com>
```

Pass `-s` to `git commit` to add this automatically.

## Branch workflow

1. `git checkout main && git pull`
2. `git checkout -b feat/<feature-name>`
3. Implement, test, update requirements and roadmap
4. `git commit -s`
5. `git push -u origin feat/<feature-name>`
6. `gh pr create --base main`

## Pull request checklist

- [ ] Feature implemented
- [ ] Tests added or updated
- [ ] `.fusa-reqs.json` updated with any new requirements (see "Writing
      a requirement" below for the atomicity and tagging convention)
- [ ] `ROADMAP.md` updated
- [ ] `CHANGELOG.md` updated (required for any deprecation, replacement, or
      removal, per RELAY spec §19.2; otherwise a one-line entry alongside
      the `ROADMAP.md` update is enough)
- [ ] Lint passes (`cfusa lint`, `cfusa check`)
- [ ] All tests pass (`ctest`)
- [ ] DCO sign-off present on every commit

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Code style

C99, no undefined behaviour in safety-critical paths, prefer explicit `int`
return codes (`rcp_errc_t`) over `errno` or silent failure. Every heap
allocation has one documented owner; ownership transfer is explicit
(`_free`/`_release` functions), never implicit.

## Writing a requirement

Every `.fusa-reqs.json` entry and the `//cfusa:req`/`//cfusa:test` tags
that trace to it exist so that `cfusa trace --req-coverage 100
--sec-tested 100` (CI's hard gate) actually means what it says: every
requirement has a real implementation and a real, distinct test. Both
guarantees depend on how the requirement is written and where its tags
are placed, and `cfusa` itself cannot enforce either — its
`--func-coverage` metric is file-level ("does this *file* carry at
least one `//cfusa:req` tag"), not function-level, and it has no notion
of whether a requirement's `text` describes one behaviour or several.
The convention below is what makes the 100% gates trustworthy; follow
it for every new or edited requirement.

**One requirement, one shall-statement.** Each `REQ-*` id must assert
exactly one independently-testable behaviour — one function's one
contract, or one `switch`/`if` arm's one outcome — not a compound
sentence bundling several. This is standard EARS/INCOSE practice
("a requirement is atomic if it cannot be split into two
verifiable statements without loss"), and this codebase already does
it well in most places:

- `REQ-PWM-002` through `REQ-PWM-009` and `REQ-PWM-056` each assert
  exactly one `evt[2:0]` write-semantics outcome (REPLACE / OR / AND /
  XOR / ADD / SUB / RESERVED4 / RECONFIG / duty-cycle clamp) as its
  own id — independently testable — even though all nine are
  implemented by the same `rcp_ep_pwm_out_apply_write()` switch, with
  nine separate `//cfusa:req` tags stacked above it.
- `REQ-AUTH-010` ("`rcp_authz_policy_retain(p)` shall increment p's
  refcount and return p unchanged") and `REQ-AUTH-011` (the paired
  release/free contract) are each one function's one contract, not
  folded together.

Do **not** write a requirement like `REQ-AUTH-009`
("`rcp_authz_policy_new()` shall … *and* `rcp_authz_policy_retain(NULL)`
shall … *and* `rcp_authz_policy_release(NULL)` shall …") — three
different functions' behaviour under one id. A quick smell test: if a
requirement's `text` contains "shall" more than once, or describes more
than one function/branch, split it before adding it.

**Tags sit directly above the specific function or test they
describe, never only at a file header.** A `//cfusa:req REQ-ID` comment
belongs immediately above the function that actually implements that
one behaviour — including small static helpers, not just the
public entry point that calls them. `saturating_add_u16()`/
`saturating_sub_u16()` in `src/ep_pwm.c` perform the saturating-clamp
arithmetic REQ-PWM-006/REQ-PWM-007 describe and should carry those
tags themselves, the same way the file's other ~40 functions each
carry their own — relying on some *other* function in the file being
tagged is what makes `--func-coverage` report a helper as "covered"
when nothing actually traces to its own behaviour.

The same rule applies to `//cfusa:test REQ-ID`: put it directly above
the specific test function that proves that one requirement, not only
once at the top of the test file. A file-header block of stacked
`//cfusa:test` tags satisfies `cfusa trace --sec-tested 100` for every
requirement in the file regardless of which test function (if any)
actually exercises each one — so deleting the one test that really
covers a requirement can leave the gate green. Per-function tags don't
have this blind spot: if the test that proves a requirement is
deleted, coverage for that requirement visibly drops.

**When splitting a bundled requirement**, give each new id its own
distinct test assertion — not a shared assertion that happens to touch
both split behaviours in passing — and update every `//cfusa:req`/
`//cfusa:test` tag that pointed at the old id to point at the specific
new id it actually supports.
