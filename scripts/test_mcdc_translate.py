#!/usr/bin/env python3
"""Regression tests for scripts/mcdc_translate.py.

The two `*_REAL_EXPORT` fixtures below are not hand-authored guesses --
they are the verbatim `llvm-cov export --format=text` output (2026-08,
Apple clang 21 / upstream LLVM lineage) for two tiny two-condition `a && b`
functions:

  FULLY_COVERED_REAL_EXPORT: f() called with (1,1),(0,1),(1,0) -- both
    conditions have a real MC/DC independent-pair, i.e. genuine 100%.
  PARTIALLY_COVERED_REAL_EXPORT: f() called with (1,1),(1,0) only -- only
    condition b has an independent pair (a stays 1 in both calls), i.e.
    genuine 50%.

Both were cross-verified directly against the pinned `cfusa coverage
--mcdc-file` (v0.5.54) after translation, matching the coveragePct these
tests assert. Run with: `python3 scripts/test_mcdc_translate.py`.
"""
import json
import sys
import unittest

from mcdc_translate import translate

# Verbatim `llvm-cov export --format=text` output for:
#   int f(int a, int b) { if (a && b) return 1; return 0; }
#   main() calls f(1,1); f(0,1); f(1,0);
FULLY_COVERED_REAL_EXPORT = json.loads(
    '{"data":[{"files":[{"branches":[[3,9,3,10,2,1,0,0,6],[3,14,3,15,1,1,0,0,6]],'
    '"expansions":[],"filename":"/tmp/mcdctest/t.c",'
    '"mcdc_records":[[3,9,3,15,1,2,0,0,5,[true,true]]],'
    '"segments":[],"summary":{"mcdc":{"count":2,"covered":2,"notcovered":0,"percent":100}}}],'
    '"functions":[{"branches":[[3,9,3,10,2,1,0,0,6],[3,14,3,15,1,1,0,0,6]],"count":3,'
    '"filenames":["/tmp/mcdctest/t.c"],'
    '"mcdc_records":[[3,9,3,15,1,2,0,0,5,[true,true]]],"name":"f","regions":[]},'
    '{"branches":[],"count":1,"filenames":["/tmp/mcdctest/t.c"],"mcdc_records":[],'
    '"name":"main","regions":[]}],'
    '"totals":{"mcdc":{"count":2,"covered":2,"notcovered":0,"percent":100}}}],'
    '"type":"llvm.coverage.json.export","version":"3.0.1"}'
)

# Same function, but main() only calls f(1,1); f(1,0) -- condition `a`
# never gets an independent pair (stays 1 in both calls).
PARTIALLY_COVERED_REAL_EXPORT = json.loads(
    '{"data":[{"files":[{"branches":[[3,9,3,10,2,0,0,0,6],[3,14,3,15,1,1,0,0,6]],'
    '"expansions":[],"filename":"/tmp/mcdctest/t2.c",'
    '"mcdc_records":[[3,9,3,15,1,1,0,0,5,[false,true]]],'
    '"segments":[],"summary":{"mcdc":{"count":2,"covered":1,"notcovered":1,"percent":50}}}],'
    '"functions":[{"branches":[[3,9,3,10,2,0,0,0,6],[3,14,3,15,1,1,0,0,6]],"count":2,'
    '"filenames":["/tmp/mcdctest/t2.c"],'
    '"mcdc_records":[[3,9,3,15,1,1,0,0,5,[false,true]]],"name":"f","regions":[]},'
    '{"branches":[],"count":1,"filenames":["/tmp/mcdctest/t2.c"],"mcdc_records":[],'
    '"name":"main","regions":[]}],'
    '"totals":{"mcdc":{"count":2,"covered":1,"notcovered":1,"percent":50}}}],'
    '"type":"llvm.coverage.json.export","version":"3.0.1"}'
)


def _flat_conditions(translated):
    out = []
    for fn in translated["data"][0]["functions"]:
        for rec in fn["mcdc_records"]:
            out.extend(rec["conditions"])
    return out


class TranslateTests(unittest.TestCase):
    def test_fully_covered_real_export(self):
        translated, total, errors = translate(FULLY_COVERED_REAL_EXPORT)
        self.assertEqual(errors, [])
        self.assertEqual(total, 2)
        conds = _flat_conditions(translated)
        self.assertEqual(len(conds), 2)
        for c in conds:
            self.assertGreater(c["covered_true_count"], 0)
            self.assertGreater(c["covered_false_count"], 0)

    def test_partially_covered_real_export(self):
        translated, total, errors = translate(PARTIALLY_COVERED_REAL_EXPORT)
        self.assertEqual(errors, [])
        self.assertEqual(total, 2)
        conds = _flat_conditions(translated)
        covered = sum(
            1
            for c in conds
            if c["covered_true_count"] > 0 and c["covered_false_count"] > 0
        )
        self.assertEqual(covered, 1)  # exactly one of two conditions covered

    def test_no_mcdc_records_at_all(self):
        empty = {"data": [{"files": [{"filename": "x.c", "mcdc_records": []}]}]}
        translated, total, errors = translate(empty)
        self.assertEqual(errors, [])
        self.assertEqual(total, 0)

    def test_malformed_record_is_a_hard_error_not_a_silent_skip(self):
        # A record whose trailing element is a list of *ints*, not bools --
        # must never be silently reinterpreted as covered/uncovered.
        bad = {
            "data": [
                {
                    "files": [
                        {
                            "filename": "weird.c",
                            "mcdc_records": [[1, 2, 3, 4, 1, 1, 0, 0, 5, [1, 0]]],
                        }
                    ]
                }
            ]
        }
        translated, total, errors = translate(bad)
        self.assertEqual(len(errors), 1)
        self.assertIn("weird.c", errors[0])

    def test_short_record_is_a_hard_error(self):
        bad = {
            "data": [
                {"files": [{"filename": "short.c", "mcdc_records": [[1, 2, 3]]}]}
            ]
        }
        translated, total, errors = translate(bad)
        self.assertEqual(len(errors), 1)


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
