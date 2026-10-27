"""Validate an offline candidate against saved capture files; never accesses hardware."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path

try:
    from .check_assets import verify
    from .rom_analyze import parse
except ImportError:
    from check_assets import verify
    from rom_analyze import parse

ROOT = Path(__file__).resolve().parents[1]


def number(value):
    return int(value, 0) if isinstance(value, str) else value


def validate(profile, rom, capture_log, kernel_log, status, checksum):
    native = parse(rom)
    checks = {}
    checks['offline_candidate'] = (profile['execution_allowed'] is False and
                                   profile['hardware_unlock_validated'] is False)
    checks['complete_capture'] = status.strip() == 'raw-capture-complete'
    checks['capture_hash'] = (len(rom) == 1048576 and
                              hashlib.sha256(rom).hexdigest() == checksum.split()[0])
    metadata = {}
    for line in capture_log.splitlines():
        if '=' in line:
            key, value = line.split('=', 1)
            if key in metadata:
                raise ValueError('ambiguous repeated capture metadata')
            metadata[key] = value
    pci, subsystem = profile['pci_id'].split(':'), profile['subsystem_id'].split(':')
    checks['capture_identity'] = all(metadata.get(key) == value for key, value in {
        'target': profile['historical_target'], 'vendor': f'0x{pci[0]}',
        'device': f'0x{pci[1]}', 'subsystem_vendor': f'0x{subsystem[0]}',
        'subsystem_device': f'0x{subsystem[1]}',
    }.items())
    checks['unbound_capture'] = metadata.get('driver_before_capture') == 'unbound'
    checks['pre_udev_marker'] = 'Kohak TU102: capture-start before udev' in kernel_log
    for key in ('fwsec_descriptor_offset', 'fwsec_image_offset',
                'fwsec_descriptor_words', 'fwsec_bytes', 'fwsec_sha256',
                'mapper_offset', 'command_offset', 'command_size'):
        expected = profile['native'][key]
        if key == 'fwsec_descriptor_words':
            expected = tuple(number(word) for word in expected)
        elif key != 'fwsec_sha256':
            expected = number(expected)
        checks[f'native.{key}'] = native[key] == expected

    pattern = r'pci (\S+): tu102_diag readonly offset=(0x[0-9a-f]+) value=(0x[0-9a-f]+)'
    registers = {}
    targets = set()
    for bdf, offset, value in re.findall(pattern, kernel_log):
        targets.add(bdf)
        offset = int(offset, 16)
        if offset in registers:
            raise ValueError('ambiguous repeated register snapshot')
        registers[offset] = int(value, 16)
    checks['register_target'] = targets == {profile['historical_target']}
    for offset, expected in profile['registers'].items():
        checks[f'register.{offset}'] = registers.get(number(offset)) == number(expected)
    capacity = registers.get(0x100ce0)
    fb = None
    if capacity is not None and capacity != 0xffffffff and capacity >> 16 != 0xbadf:
        fb = ((capacity >> 4) & 63) << ((capacity & 15) + 20)
        if capacity & (1 << 30):
            fb = fb // 16 * 15
    checks['framebuffer_capacity'] = fb == profile['framebuffer_bytes']
    return {
        'kind': 'offline-candidate-evidence-check', 'profile': profile['profile'],
        'profile_canonical_sha256': hashlib.sha256(
            json.dumps(profile, sort_keys=True, separators=(',', ':')).encode()).hexdigest(),
        'rom_sha256': native['rom_sha256'],
        'native_fwsec_sha256': native['fwsec_sha256'],
        'execution_allowed': False, 'hardware_unlock_validated': False,
        'evidence_matches': all(checks.values()), 'checks': checks,
        'framebuffer_bytes': fb,
        'boot_power_state': 'unknown; pre-driver does not establish cold power-on',
        'remaining_reviews': profile['remaining_reviews'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, default=ROOT / 'profiles/nvidia-tu102-300a-22g.json')
    parser.add_argument('--capture', type=Path, required=True, help='existing private capture directory')
    parser.add_argument('--assets', type=Path, help='existing private candidate model inputs')
    parser.add_argument('--output', type=Path, required=True, help='new private JSON report')
    args = parser.parse_args()
    try:
        profile = json.loads(args.profile.read_text())
        capture = args.capture
        report = validate(profile, (capture / 'prom.bin').read_bytes(),
                          (capture / 'capture.log').read_text(),
                          (capture / 'kernel-diag.log').read_text(),
                          (capture / 'status').read_text(),
                          (capture / 'prom.sha256').read_text())
        if args.assets:
            report['assets'] = verify(args.assets, profile)
            report['assets_valid'] = all(row['valid'] for row in report['assets'])
        with open(args.output, 'x', opener=lambda path, flags: os.open(path, flags, 0o600)) as handle:
            json.dump(report, handle, indent=2)
            handle.write('\n')
    except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
        parser.error(str(error))
    print(f'Saved {args.output}; evidence matches: {report["evidence_matches"]}; execution disabled.')
    return 0 if report['evidence_matches'] and report.get('assets_valid', True) else 1


if __name__ == '__main__':
    raise SystemExit(main())
