"""Synthetic ROM and log fixtures only; no private firmware or GPU access."""
import copy
import hashlib
import json
import os
import shlex
import struct
import subprocess
import unittest
from pathlib import Path

from tools.check_profile import validate

ROOT = Path(__file__).resolve().parents[1]


class CandidateEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.profile = json.loads((ROOT / 'profiles/nvidia-tu102-300a-22g.json').read_text())
        # A synthetic standard image containing an otherwise empty native image.
        # Its offsets/hashes intentionally differ from private hardware captures.
        rom = bytearray(1048576)
        rom[:2] = b'\x55\xaa'
        struct.pack_into('<H', rom, 0x18, 0x40)
        rom[0x40:0x44] = b'PCIR'
        struct.pack_into('<H', rom, 0x50, 128)
        rom[0x55] = 0x80
        rom[0x100:0x106] = b'\xff\xb8BIT\x00'
        rom[0x108:0x10b] = bytes([12, 6, 1])
        rom[0x10b] = -sum(rom[0x100:0x10c]) % 256
        rom[0x10c:0x112] = b'p\x02\x04\x00\x20\x01'
        struct.pack_into('<I', rom, 0x120, 0x140)
        rom[0x140:0x146] = bytes([1, 6, 6, 1, 0, 0])
        rom[0x146:0x148] = b'\x85\x00'
        struct.pack_into('<I', rom, 0x148, 0x200)
        fields = [int(word, 0) for word in self.profile['native']['fwsec_descriptor_words']]
        struct.pack_into('<15I', rom, 0x200, *fields)
        image = 0x23c
        dmem = image + fields[10]
        rom[dmem + 0xdc:dmem + 0xe0] = bytes([1, 4, 8, 2])
        struct.pack_into('<II', rom, dmem + 0xe0, 4, 0x380)
        struct.pack_into('<4I', rom, dmem + 0x380, 0x50414d44, 0x00400003, 0x3d0, 64)
        self.profile['native'].update(fwsec_descriptor_offset='0x200',
                                      fwsec_image_offset='0x23c',
                                      fwsec_sha256=hashlib.sha256(rom[image:image + fields[1]]).hexdigest())
        self.rom = bytes(rom)
        self.checksum = hashlib.sha256(self.rom).hexdigest() + '  prom.bin'
        self.metadata = '\n'.join([
            'target=0000:01:00.0', 'vendor=0x10de', 'device=0x1e07',
            'subsystem_vendor=0x10de', 'subsystem_device=0x12a4',
            'driver_before_capture=unbound',
        ])
        self.log = 'Kohak TU102: capture-start before udev\n' + '\n'.join(
            f'pci 0000:01:00.0: tu102_diag readonly offset={off} value={value}'
            for off, value in self.profile['registers'].items())

    def check(self, **overrides):
        arguments = dict(profile=self.profile, rom=self.rom, capture_log=self.metadata,
                         kernel_log=self.log, status='raw-capture-complete', checksum=self.checksum)
        arguments.update(overrides)
        return validate(**arguments)

    def test_consistent_files_never_enable_execution(self):
        report = self.check()
        self.assertTrue(report['evidence_matches'])
        self.assertEqual(report['framebuffer_bytes'], 22 << 30)
        self.assertFalse(report['execution_allowed'])
        self.assertFalse(report['hardware_unlock_validated'])
        self.assertIn('unknown', report['boot_power_state'])

    def test_firmware_layout_mismatch_is_rejected(self):
        for key, value in [('command_offset', '0x3e0'), ('fwsec_image_offset', '0x3edd4'),
                           ('fwsec_sha256', '0' * 64)]:
            profile = copy.deepcopy(self.profile)
            profile['native'][key] = value
            self.assertFalse(self.check(profile=profile)['evidence_matches'])

    def test_historical_or_changed_selector_is_rejected(self):
        for value in ('0x0', '0x500061', '0x88888888'):
            report = self.check(kernel_log=self.log.replace('value=0x500060', f'value={value}'))
            self.assertFalse(report['evidence_matches'])

    def test_wrong_target_post_driver_or_missing_marker_is_rejected(self):
        self.assertFalse(self.check(kernel_log=self.log.replace('01:00.0', '42:00.0'))['evidence_matches'])
        self.assertFalse(self.check(capture_log=self.metadata.replace('unbound', 'nvidia'))['evidence_matches'])
        self.assertFalse(self.check(kernel_log=self.log.split('\n', 1)[1])['evidence_matches'])

    def test_missing_or_repeated_register_is_rejected(self):
        lines = self.log.splitlines()
        self.assertFalse(self.check(kernel_log='\n'.join(lines[:-1]))['evidence_matches'])
        with self.assertRaisesRegex(ValueError, 'repeated'):
            self.check(kernel_log=self.log + '\n' + lines[-1])

    def test_modified_or_partial_capture_is_rejected(self):
        self.assertFalse(self.check(checksum='0' * 64)['evidence_matches'])
        self.assertFalse(self.check(status='failed')['evidence_matches'])
        self.assertFalse(self.check(rom=self.rom[:65536])['evidence_matches'])

    def test_ambiguous_capture_identity_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'repeated capture metadata'):
            self.check(capture_log=self.metadata + '\ntarget=0000:42:00.0')

    def test_candidate_requires_explicit_trial_build_and_v4_guards(self):
        compiler = shlex.split(os.environ.get('CC', 'cc'))
        for definitions, error in [
            (['-D__KERNEL__', '-DTX_EXPERIMENT_V4'], 'requires an explicit target trial build'),
            (['-D__KERNEL__', '-DTX_EXPERIMENT_V4', '-DKOH_TARGET_NVIDIA_TRIAL'],
             'requires an explicit target trial build'),
            ([], 'candidate requires the reviewed V4 guard path'),
        ]:
            result = subprocess.run(
                compiler + ['-E', '-DTX_PROFILE_NVIDIA_1E07_12A4'] + definitions +
                ['-x', 'c', str(ROOT / 'kernel/tu102_profile.h')],
                text=True, capture_output=True, check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(error, result.stderr)
        result = subprocess.run(
            compiler + ['-E', '-D__KERNEL__', '-DTX_EXPERIMENT_V4',
                        '-DTX_PROFILE_NVIDIA_1E07_12A4', '-DKOH_TARGET_NVIDIA_TRIAL',
                        '-DKOH_UNLOCK_REVIEWED_PROFILE', '-x', 'c',
                        str(ROOT / 'kernel/tu102_profile.h')],
            text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_trial_make_selection_requires_experiment_flag(self):
        result = subprocess.run(
            ['make', '-f', str(ROOT / 'kernel/Makefile'), '-n',
             'EXEC_PROFILE=nvidia-1e07-12a4-trial', 'ENABLE_EXPERIMENT=0'],
            text=True, capture_output=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('requires ENABLE_EXPERIMENT=1', result.stderr)
