"""Archive saved trial files and summarize reported state; no GPU access."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil

FILES = ('events.log', 'gate.log', 'kernel.log', 'kernel-live.log', 'status',
         'kernel.txt', 'cmdline.txt', 'start-uptime.txt', 'end-uptime.txt', 'manifest.json')


def summarize(events):
    records = []
    for line in events.splitlines():
        if not line.strip():
            continue
        fields = line.split()
        if any('=' not in field for field in fields):
            raise ValueError('malformed or truncated event record')
        record = dict(field.split('=', 1) for field in fields)
        if len(record) != len(fields):
            raise ValueError('repeated event field')
        records.append(record)
    handoffs = [r for r in records if r.get('phase') == 'handoff']
    cores = [r for r in records if r.get('phase') == 'core-return']
    if len(handoffs) > 1 or len(cores) > 1:
        raise ValueError('ambiguous multiple transactions')
    terminal = handoffs[-1] if handoffs else {}
    core = cores[-1] if cores else {}
    good = {'code': '0', 'stage': '11', 'mutated': '1', 'unlocked': '1', 'quiescent': '1'}
    success = (all(terminal.get(k) == v and core.get(k) == v for k, v in good.items())
               and terminal.get('safe') == '1')
    return {'transaction_reported_success': success, 'hardware_unlock_validated': False,
            'performance_and_correctness_validated': False,
            'last_record': records[-1] if records else None,
            'last_stage': records[-1].get('stage') if records else None,
            'record_count': len(records),
            'evidence_scope': 'Saved log report only; not independent hardware attestation'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path,
                        help='new private directory, default local/trial-evidence-UTC')
    args = parser.parse_args()
    if not args.source.is_dir():
        parser.error('trial directory missing; a prepared image is not a completed attempt')
    os.umask(0o077)
    destination = args.output or Path(__file__).resolve().parents[1] / 'local' / (
        'trial-evidence-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
    destination.mkdir(mode=0o700)
    hashes = {}
    for name in FILES:
        source = args.source / name
        if source.is_symlink():
            raise ValueError(f'unexpected log-file symlink: {name}')
        if source.is_file():
            target = destination / name
            shutil.copyfile(source, target)
            target.chmod(0o600)
            hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    events = destination / 'events.log'
    try:
        report = summarize(events.read_text() if events.exists() else '')
    except ValueError as error:
        report = {'transaction_reported_success': False, 'hardware_unlock_validated': False,
                  'parse_error': str(error)}
    report['files_sha256'] = hashes
    (destination / 'assessment.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Saved private trial evidence:', destination)
    print('Transaction reports success:', report['transaction_reported_success'])
    print('Hardware unlock/performance validation remains separate.')


if __name__ == '__main__':
    main()
