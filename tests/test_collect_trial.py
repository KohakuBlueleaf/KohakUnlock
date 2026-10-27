import unittest

from tools.collect_trial import summarize


class TrialLogTests(unittest.TestCase):
    def setUp(self):
        self.good = '\n'.join(
            f'phase={phase} code=0 stage=11 mutated=1 unlocked=1 quiescent=1 safe={safe}'
            for phase, safe in [('core-return', '0'), ('handoff', '1')])

    def test_transaction_report_never_claims_performance_validation(self):
        report = summarize(self.good)
        self.assertTrue(report['transaction_reported_success'])
        self.assertFalse(report['hardware_unlock_validated'])
        self.assertFalse(report['performance_and_correctness_validated'])

    def test_missing_failed_or_unsafe_records_are_not_success(self):
        for log in ['', self.good.splitlines()[0],
                    self.good.replace('safe=1', 'safe=0'),
                    self.good.replace('code=0', 'code=-5'),
                    self.good.replace('quiescent=1', 'quiescent=0'),
                    self.good.replace('stage=11', 'stage=4')]:
            with self.subTest(log=log):
                self.assertFalse(summarize(log)['transaction_reported_success'])

    def test_ambiguous_or_truncated_logs_are_rejected(self):
        for log in [self.good + '\n' + self.good, self.good + '\nphase',
                    self.good.replace('code=0', 'code=0 code=-5')]:
            with self.assertRaises(ValueError):
                summarize(log)
