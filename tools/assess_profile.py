"""Compare saved inventory/native ROM with the historical profile; file-only evidence."""
import argparse
import json
import re
from pathlib import Path

try:
    from .check_assets import verify
    from .rom_analyze import parse
except ImportError:  # Direct script invocation.
    from check_assets import verify
    from rom_analyze import parse

ROOT = Path(__file__).resolve().parents[1]


def assess(inventory, bdf, profile, rom=None, assets=None):
    if not re.fullmatch(r'[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]', bdf):
        raise ValueError('expected a full lowercase PCI BDF')
    device = inventory['devices'][bdf]

    def pair(first, second):
        return f'{int(device[first], 16):04x}:{int(device[second], 16):04x}'

    pci_id = pair('vendor', 'device')
    subsystem_id = pair('subsystem_vendor', 'subsystem_device')
    comparisons = {
        'pci_id': {'observed': pci_id, 'expected': profile['pci_id']},
        'subsystem_id': {'observed': subsystem_id, 'expected': profile['subsystem_id']},
        'bdf': {'observed': bdf, 'expected': profile['historical_target']},
    }
    for row in comparisons.values():
        row['matches'] = row['observed'] == row['expected']
    report = {
        'kind': 'offline-port-assessment',
        'execution_allowed': False,
        'hardware_validated': False,
        'profile': profile['profile'],
        'inventory_utc': inventory.get('utc'),
        'driver_in_saved_inventory': device.get('driver'),
        'snapshot_provenance': 'saved OS metadata; cold/pre-driver state not established',
        'identity': comparisons,
        'native_rom': {'provided': rom is not None},
        'assets': {'provided': assets is not None},
        'remaining_reviews': [
            'independent console and full power recovery verified',
            'native VBIOS identity and complete PROM provenance',
            'native descriptor/interface/mapper/command compatibility',
            'pre-driver register snapshot and capacity/workspace/WPR layout',
            'HAL architecture dispatch and runtime guards',
            'equivalent driver override exclusion on the target kernel',
            'profile-specific private model tests and independent review',
            'one-shot image/order/hashes and known-good default boot',
        ],
    }
    if rom is not None:
        native = parse(rom)
        expected = next(a for a in profile['assets']
                        if a['name'] == profile.get('native_fwsec_asset', 'leadtek-fwsec.bin'))
        report['native_rom'].update(
            analysis=native,
            profile_fwsec_matches=(native['fwsec_bytes'] == expected['bytes'] and
                                   native['fwsec_sha256'] == expected['sha256']),
        )
        if 'native_fwsec_asset' not in profile:
            report['native_rom']['historical_fwsec_matches'] = report['native_rom']['profile_fwsec_matches']
    if assets is not None:
        report['assets']['historical_verification'] = verify(assets, profile)
    # Even perfect file matches cannot establish recovery, cold register state,
    # kernel exclusion or hardware behavior. This report never enables execution.
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--bdf', required=True)
    parser.add_argument('--rom', type=Path, help='existing private complete native PROM')
    parser.add_argument('--assets', type=Path, help='existing private historical input directory')
    parser.add_argument('--output', type=Path, required=True, help='new private report file')
    parser.add_argument('--profile', type=Path, default=ROOT / 'profiles/leadtek-tu102-22g.json')
    args = parser.parse_args()
    try:
        inventory = json.loads(args.inventory.read_text())
        profile = json.loads(args.profile.read_text())
        rom = args.rom.read_bytes() if args.rom else None
        report = assess(inventory, args.bdf, profile, rom, args.assets)
        with args.output.open('x') as handle:
            json.dump(report, handle, indent=2)
            handle.write('\n')
    except (OSError, ValueError, KeyError, TypeError, StopIteration) as error:
        parser.error(str(error))
    print(f'Saved {args.output}; offline evidence only, execution_allowed=false.')


if __name__ == '__main__':
    main()
