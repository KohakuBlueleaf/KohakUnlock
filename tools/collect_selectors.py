"""Archive a saved pre-driver selector diagnostic; never access hardware."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil

try:
    from .collect_trial import FILES
except ImportError:
    from collect_trial import FILES


def summarize(text):
    pattern = re.compile(r'\btu102_diag selectors sample=(\d+) before_ns=(\d+) after_ns=(\d+) '
                         r'ss0=(0x[0-9a-f]+|0) ss1=(0x[0-9a-f]+|0)\b')
    samples = []
    for line in text.splitlines():
        if 'tu102_diag selectors sample=' not in line:
            continue
        match = pattern.search(line)
        if not match:
            raise ValueError('malformed or truncated selector record')
        index, before, after = map(int, match.group(1, 2, 3))
        ss0, ss1 = (int(value, 0) for value in match.group(4, 5))
        if index != len(samples) or index >= 5 or before > after:
            raise ValueError('missing, duplicate, unordered or invalid sample')
        if samples and before < samples[-1]['after_ns']:
            raise ValueError('nonmonotonic sample timestamps')
        if max(ss0, ss1) > 0xffffffff:
            raise ValueError('selector wider than 32 bits')
        valid = all(value != 0xffffffff and value & 0xffff0000 != 0xbadf0000 for value in (ss0, ss1))
        samples.append(dict(index=index, before_ns=before, after_ns=after,
                            ss0=hex(ss0), ss1=hex(ss1), readout_valid=valid,
                            matches_pinned_pair=(ss0, ss1) == (0x500060, 5)))
    pairs = {(sample['ss0'], sample['ss1']) for sample in samples}
    return dict(samples=samples, complete=len(samples) == 5,
                window_ns=samples[-1]['after_ns']-samples[0]['before_ns'] if samples else None,
                all_readouts_valid=bool(samples) and all(s['readout_valid'] for s in samples),
                stable_within_sample_window=bool(samples) and len(pairs) == 1,
                all_match_pinned_pair=bool(samples) and all(s['matches_pinned_pair'] for s in samples),
                cross_boot_repeatability_established=False, hardware_unlock_validated=False,
                execution_allowed=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--power-state', choices=['unknown', 'warm', 'cold'], default='unknown')
    args = parser.parse_args()
    if not args.source.is_dir():
        parser.error('saved capture directory not found')
    os.umask(0o077)
    dest = Path(__file__).resolve().parents[1] / 'local' / ('selector-evidence-' +
        datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
    dest.mkdir(mode=0o700)
    hashes = {}
    for name in FILES:
        source = args.source / name
        if source.is_symlink():
            raise ValueError('unexpected saved file symlink')
        if source.is_file():
            target = dest / name
            shutil.copyfile(source, target)
            target.chmod(0o600)
            hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    try:
        report = summarize((dest / 'kernel.log').read_text())
    except (ValueError, FileNotFoundError) as error:
        report = dict(parse_error=str(error), execution_allowed=False, hardware_unlock_validated=False)
    report.update(user_reported_power_state=args.power_state, files_sha256=hashes)
    (dest / 'assessment.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Saved private selector evidence:', dest)
    print('Complete five-sample capture:', report.get('complete', False))


if __name__ == '__main__':
    main()
