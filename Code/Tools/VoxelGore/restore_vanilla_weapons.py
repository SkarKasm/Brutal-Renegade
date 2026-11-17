"""Retire only manifested weapon overrides from the separate test copy.

Default is a dry run; --apply moves exact files to a dated backup. Never writes
through the test copy's asset hardlinks. Terrain and non-weapon changes survive.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import shutil

ROOT = Path(r'D:\Command & Conquer Renegade(tm)')
TOOLS = ROOT / 'RenegadeVR Tools'
TEST = ROOT / 'RenegadeVR Test'
DATA = TEST / 'Data'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    names = set()
    installed = json.loads((TOOLS / 'weapon-models-2026-10-20/installed.json').read_text())
    names.update(row['file'].lower() for row in installed['files'])
    packs = ('flatscreen-weapons-2026-10-26', 'authored-reloads-2026-10-26',
             'reference-reloads-2026-10-26', 'faction-rifles-2026-10-26',
             'source-rifles-2026-10-26', 'vr-wheel-2026-10-25',
             'vr-integration-2026-10-24')
    for pack in packs:
        manifest = json.loads((TOOLS / pack / 'deployment.json').read_text())
        for row in manifest['files']:
            path = Path(row['path'])
            if path.parent != DATA:
                continue
            name = path.name.lower()
            # These packs also contained terrain/debug and engine changes.
            if name.startswith(('f_gm_', 'f_cm_', 'br_ak_', 'br_m4_', 'br_b_',
                                'br_w_', 'br_src_', 'br_socom_', 'br_mk23_',
                                'br_rifle_flash')):
                names.add(name)
    assert all(Path(name).name == name for name in names)
    paths = sorted((DATA / name for name in names if (DATA / name).is_file()), key=str)
    backup = TEST / ('Vanilla-weapons-backup-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    report = {'target': str(TEST), 'applied': args.apply, 'backup': str(backup),
              'files': [], 'main_install_changed': False}
    if args.apply:
        (backup / 'Data').mkdir(parents=True, exist_ok=False)
    for path in paths:
        dest = backup / 'Data' / path.name
        report['files'].append({'path': str(path), 'backup': str(dest),
                                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        if args.apply:
            # Same-volume rename removes this directory entry only, preserving
            # hardlinked assets in the main install and in other backups.
            path.rename(dest)
    if args.apply:
        # Canonical model/clip names must now resolve from stock archives.
        from prepare import read_asset
        proof = []
        for name in ('f_gm_pist.w3d', 'f_cm_pist.w3d', 'f_gm_rifl.w3d',
                     'f_cm_rifl.w3d', 'f_gm_snip.w3d'):
            data, provenance = read_asset(DATA, name)
            assert '::' in provenance, 'unexpected remaining loose override: ' + name
            proof.append({'asset': name, 'source': provenance,
                          'sha256': hashlib.sha256(data).hexdigest()})
        report['stock_archive_resolution'] = proof
        (backup / 'manifest.json').write_text(json.dumps(report, indent=2))
        (Path(__file__).parent / 'output/vanilla-weapons-deployment.json').write_text(json.dumps(report, indent=2))
    print(f"{'RESTORED' if args.apply else 'DRY RUN'}: {len(paths)} weapon asset overrides; main install untouched")
    if args.apply:
        print('Stock pistol, rifle, sniper and magazine archive resolution verified.')


if __name__ == '__main__':
    main()
