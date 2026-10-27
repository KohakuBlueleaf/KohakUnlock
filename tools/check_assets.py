"""Verify privately obtained inputs. Does not download, patch or execute firmware."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def verify(directory, profile):
    results = []
    for asset in profile['assets']:
        name = asset['name']
        if Path(name).name != name:
            raise ValueError('asset name must be a basename')
        path = directory / name
        row = {'name': name, 'valid': False}
        try:
            with path.open('rb') as handle:
                digest = hashlib.sha256()
                size = 0
                for chunk in iter(lambda: handle.read(65536), b''):
                    size += len(chunk)
                    digest.update(chunk)
            row.update(bytes=size, sha256=digest.hexdigest())
            row['valid'] = size == asset['bytes'] and digest.hexdigest() == asset['sha256']
        except OSError as error:
            row['error'] = str(error)
        results.append(row)
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets', type=Path, required=True)
    p.add_argument('--profile', type=Path, default=ROOT / 'profiles/leadtek-tu102-22g.json')
    args = p.parse_args()
    profile = json.loads(args.profile.read_text())
    results = verify(args.assets, profile)
    print(json.dumps(results, indent=2))
    return 0 if all(row['valid'] for row in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
