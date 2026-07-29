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
- [ ] `.fusa-reqs.json` updated with any new requirements
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
