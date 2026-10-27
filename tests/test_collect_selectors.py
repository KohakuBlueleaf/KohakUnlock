import unittest

from tools.collect_selectors import summarize


def records(values):
    return '\n'.join(f'[1.0] pci 0000:01:00.0: tu102_diag selectors sample={i} '
                     f'before_ns={100+i*250} after_ns={110+i*250} ss0={a:#x} ss1={b:#x}'
                     for i, (a, b) in enumerate(values))


class SelectorCaptureTests(unittest.TestCase):
    def test_complete_expected_pair_is_not_execution_permission(self):
        result = summarize(records([(0x500060, 5)] * 5))
        self.assertTrue(result['complete'])
        self.assertTrue(result['stable_within_sample_window'])
        self.assertTrue(result['all_match_pinned_pair'])
        self.assertFalse(result['execution_allowed'])
        self.assertFalse(result['cross_boot_repeatability_established'])

    def test_changed_or_unknown_pair_is_preserved(self):
        for values in [[(0x500061, 5)] * 5, [(0x500060, 5), (0x123456, 5)] * 2 + [(0, 0)]]:
            result = summarize(records(values))
            self.assertTrue(result['complete'])
            self.assertFalse(result['all_match_pinned_pair'])
            self.assertEqual(result['samples'][1]['ss0'], hex(values[1][0]))

    def test_missing_samples_or_invalid_readouts_do_not_validate_capture(self):
        self.assertFalse(summarize('')['complete'])
        self.assertFalse(summarize(records([(0x500060, 5)] * 4))['complete'])
        for value in (0xffffffff, 0xbadf0001):
            self.assertFalse(summarize(records([(value, 5)] * 5))['all_readouts_valid'])

    def test_duplicates_truncation_and_bad_timing_are_rejected(self):
        text = records([(0x500060, 5)] * 5)
        for bad in [text + '\n' + text, text[:-3], text.replace('before_ns=350', 'before_ns=1'),
                    text.replace('after_ns=110', 'after_ns=1')]:
            with self.assertRaises(ValueError):
                summarize(bad)
