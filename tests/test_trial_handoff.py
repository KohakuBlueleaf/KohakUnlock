"""Run the real boot gate's handoff policy without hardware or root access."""
import itertools
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TrialHandoffTests(unittest.TestCase):
    def test_only_confirmed_success_may_start_udev(self):
        for loaded, result, safe in itertools.product(('0', '1', ''), ('0', '-5', ''), ('Y', 'N', '')):
            with self.subTest(loaded=loaded, result=result, safe=safe):
                run = subprocess.run(
                    ['sh', '-c', '. "$1"; kohak_trial_handoff "$2" "$3" "$4"',
                     'test', str(ROOT / 'tools/trial-handoff.sh'), loaded, result, safe],
                    check=False)
                self.assertEqual(run.returncode == 0, (loaded, result, safe) == ('0', '0', 'Y'))
