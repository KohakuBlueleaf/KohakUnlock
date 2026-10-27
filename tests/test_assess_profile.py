import unittest

from tools.assess_profile import assess


class ProfileAssessmentTests(unittest.TestCase):
    def setUp(self):
        self.bdf = '0000:01:00.0'
        self.inventory = {'devices': {self.bdf: {
            'vendor': '0x10de', 'device': '0x1e07',
            'subsystem_vendor': '0x10de', 'subsystem_device': '0x12a4',
            'driver': 'nvidia',
        }}}
        self.profile = {'profile': 'historical', 'pci_id': '10de:1e04',
                        'subsystem_id': '107d:1e04', 'historical_target': '0000:42:00.0'}

    def test_different_card_is_evidence_only(self):
        report = assess(self.inventory, self.bdf, self.profile)
        self.assertEqual(report['identity']['pci_id']['observed'], '10de:1e07')
        self.assertTrue(all(not row['matches'] for row in report['identity'].values()))
        self.assertFalse(report['execution_allowed'])
        self.assertFalse(report['native_rom']['provided'])
        self.assertEqual(report['driver_in_saved_inventory'], 'nvidia')

    def test_matching_metadata_never_authorizes_execution(self):
        self.profile.update(pci_id='10de:1e07', subsystem_id='10de:12a4',
                            historical_target=self.bdf)
        report = assess(self.inventory, self.bdf, self.profile)
        self.assertTrue(all(row['matches'] for row in report['identity'].values()))
        self.assertFalse(report['execution_allowed'])
        self.assertFalse(report['hardware_validated'])
        self.assertTrue(report['remaining_reviews'])

    def test_invalid_or_partial_rom_does_not_produce_report(self):
        with self.assertRaises(ValueError):
            assess(self.inventory, self.bdf, self.profile, rom=bytes(512))

    def test_target_must_exist_and_be_full_bdf(self):
        with self.assertRaises(ValueError):
            assess(self.inventory, '01:00.0', self.profile)
        with self.assertRaises(KeyError):
            assess(self.inventory, '0000:02:00.0', self.profile)
