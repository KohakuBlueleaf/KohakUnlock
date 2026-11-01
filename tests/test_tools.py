import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.check_assets import verify
from tools.inventory import collect
from tools.rom_analyze import parse
from tools.profile_gen import validate

ROOT = Path(__file__).resolve().parents[1]


class ToolsTests(unittest.TestCase):
    def test_asset_missing_and_mismatch(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            profile = {'assets': [{'name': 'a.bin', 'bytes': 3, 'sha256': hashlib.sha256(b'abc').hexdigest()}]}
            self.assertFalse(verify(root, profile)[0]['valid'])
            (root / 'a.bin').write_bytes(b'abc')
            self.assertTrue(verify(root, profile)[0]['valid'])
            (root / 'a.bin').write_bytes(b'abd')
            self.assertFalse(verify(root, profile)[0]['valid'])

    def test_asset_path_traversal_rejected(self):
        with self.assertRaises(ValueError):
            verify(Path('.'), {'assets': [{'name': '../a.bin'}]})

    def test_inventory_is_file_read_only(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            device = root / '0000:01:00.0'
            device.mkdir()
            (device / 'vendor').write_text('0x10de\n')
            before = (device / 'vendor').read_bytes()
            result = collect(['0000:01:00.0'], root)
            self.assertEqual(result['devices']['0000:01:00.0']['vendor'], '0x10de')
            self.assertEqual(before, (device / 'vendor').read_bytes())
            self.assertIsNone(result['devices']['0000:01:00.0']['driver'])
        with self.assertRaises(ValueError):
            collect(['../../etc'])

    def test_bad_rom_refused(self):
        for data in (b'', bytes(511), bytes(1024), b'\x55\xaa' + bytes(510), bytes(1048577)):
            with self.assertRaises(ValueError):
                parse(data)

    def test_default_execution_gate_present(self):
        root = Path(__file__).resolve().parents[1]
        for name in ('kernel/tu102_execute_main.c', 'kernel/tu104_execute_main.c'):
            source = (root / name).read_text()
            self.assertIn('#ifndef KOH_UNLOCK_REVIEWED_PROFILE', source)
            self.assertIn('return -EOPNOTSUPP;', source)
            self.assertIn('if (!execute || !target', source)
        probe = (root / 'kernel/tu102_diag.c').read_text()
        tu104_probe = (root / 'kernel/tu104_diag.c').read_text()
        for forbidden in ('writel(', 'pci_write_', 'dma_alloc', 'request_firmware(', 'pcie_flr('):
            self.assertNotIn(forbidden, probe)
            self.assertNotIn(forbidden, tu104_probe)

    LEADTEK_REPORT = {
        'rom_sha256': '00' * 32,
        'fwsec_descriptor_offset': 0x3ed98,
        'fwsec_descriptor_words': [0x3c0201, 0x9c20, 0x9c20, 0, 0xe0, 0,
                                   0x9800, 0, 0x400, 0x9400, 0x9800, 0,
                                   0x420, 0x9800, 0x6638],
        'fwsec_image_offset': 0x3edd4,
        'fwsec_bytes': 39968,
        'fwsec_sha256': 'c6fa28fa2458f7bce2aeb422a2e57cce3885ff65c779e1077f682c23d097a635',
        'fwsec_interface_word': 0x02080401,
        'mapper_offset': 0x390,
        'command_offset': 0x3e0,
        'command_size': 0x40,
    }
    LEADTEK_OBS = {
        'bdf': '0000:42:00.0',
        'device_id': '0x1e04',
        'subsystem_vendor': '0x107d',
        'subsystem_device': '0x1e04',
        'boot0': '0x162000a1',
        'capacity_register': '0x2c9',
        'workspace_register': '0x1',
        'wpr2_low_init': '0x1ffffe00',
        'wpr2_high_init': '0x0',
        'selector0_init': '0x0',
        'selector1_init': '0x0',
        'feature_word': '0x2f3',
        'readout_word': '0x00400080',
        'gfw_word': '0x3ff',
    }

    def leadtek_obs(self, **overrides):
        obs = dict(self.LEADTEK_OBS)
        obs.update(overrides)
        return obs

    def test_profile_gen_matches_published_leadtek_constants(self):
        v = validate(dict(self.LEADTEK_REPORT), self.leadtek_obs())
        self.assertEqual(v['chip'], 'TU102')
        self.assertEqual(v['fb'], 0x580000000)
        self.assertEqual(v['frts'], 0x57fe00000)
        self.assertEqual(v['heap_mib'], 107)
        self.assertEqual(v['fields'][1], 39968)
        self.assertEqual(v['mapper'], 0x390)
        self.assertEqual(v['cmd_off'], 0x3e0)
        self.assertEqual(v['cmd_size'], 0x40)
        self.assertEqual(v['iface_word'], 0x02080401)
        self.assertEqual(v['fwsec_desc_off'], 0x3ed98)
        self.assertEqual(v['fwsec_img_off'], 0x3edd4)

    def test_profile_gen_decodes_6gib_tu104(self):
        report = json.loads(json.dumps(self.LEADTEK_REPORT))
        report['fwsec_descriptor_offset'] = 0x3f024
        report['fwsec_image_offset'] = 0x3f060
        report['fwsec_descriptor_words'] = [0x3c0201, 0x9420, 0x9420, 0, 0xd8, 0,
                                            0x9000, 0, 0x300, 0x8d00, 0x9000, 0,
                                            0x420, 0x9000, 0x6600]
        report['fwsec_bytes'] = 0x9420
        report['fwsec_sha256'] = 'ab' * 32
        report['fwsec_interface_word'] = 0x03080401
        report['mapper_offset'] = 0x2f0
        report['command_offset'] = 0x350
        obs = self.leadtek_obs(
            bdf='0000:01:00.0',
            device_id='0x1e89',
            subsystem_vendor='0x1458',
            subsystem_device='0x3fc1',
            boot0='0x164000a1',
            # synthetic encoding: 48 << (7+20) = 0x180000000 = 6 GiB.
            # The real observed register value must come from tu104_diag.
            capacity_register='0x307',
        )
        v = validate(report, obs)
        self.assertEqual(v['chip'], 'TU104')
        self.assertEqual(v['fb'], 6 << 30)
        self.assertEqual(v['frts'], 0x17fe00000)
        self.assertEqual(v['heap_mib'], 105)

    def test_profile_gen_refuses_unknown_chip_and_bad_desc(self):
        report = dict(self.LEADTEK_REPORT)
        with self.assertRaises(ValueError):
            validate(report, self.leadtek_obs(boot0='0x170000a1'))
        bad_desc = json.loads(json.dumps(self.LEADTEK_REPORT))
        bad_desc['fwsec_descriptor_words'] = list(bad_desc['fwsec_descriptor_words'])
        bad_desc['fwsec_descriptor_words'][1] = 0x9c21
        with self.assertRaises(ValueError):
            validate(bad_desc, self.leadtek_obs())
        bad_bytes = json.loads(json.dumps(self.LEADTEK_REPORT))
        bad_bytes['fwsec_bytes'] = 39969
        with self.assertRaises(ValueError):
            validate(bad_bytes, self.leadtek_obs())
        bad_wpr = json.loads(json.dumps(self.LEADTEK_REPORT))
        with self.assertRaises(ValueError):
            validate(bad_wpr, self.leadtek_obs(wpr2_low_init='0x10'))
        with self.assertRaises(ValueError):
            validate(report, self.leadtek_obs(bdf='nope'))

    def test_profile_gen_cli_writes_header(self):
        report = json.loads(json.dumps(self.LEADTEK_REPORT))
        report['rom_sha256'] = '00' * 32
        obs = self.leadtek_obs()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'report.json').write_text(json.dumps(report))
            (root / 'obs.json').write_text(json.dumps(obs))
            out = root / 'gen.h'
            subprocess.run(
                ['python3', str(ROOT / 'tools/profile_gen.py'),
                 '--rom-report', str(root / 'report.json'),
                 '--observations', str(root / 'obs.json'),
                 '--profile-name', 'selftest', '--header', str(out)],
                check=True, capture_output=True, text=True, cwd=str(ROOT))
            text = out.read_text()
            self.assertIn('#define KOH_FB_SIZE 0x580000000ULL', text)
            self.assertIn('#define KOH_HEAP_MIB 107u', text)
            self.assertIn('0x3c0201u, 0x9c20u', text)
            self.assertIn('c6fa28fa2458f7bce2aeb422a2e57cce3885ff65c779e1077f682c23d097a635', text)


if __name__ == '__main__':
    unittest.main()
