"""Prepare an isolated offline build and read-only working copies of real W3D assets.
No game launch, deployment, or engine build. Python stdlib only.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
HANDOFF = ROOT / 'voxel_gore_handoff'


def chunk_list(data, start=0, end=None):
    end = len(data) if end is None else end
    while start < end:
        if end - start < 8:
            raise ValueError('truncated chunk header')
        tag, length = struct.unpack_from('<II', data, start)
        length &= 0x7fffffff
        stop = start + 8 + length
        if stop > end:
            raise ValueError('chunk exceeds parent')
        yield tag, data[start + 8:stop]
        start = stop


def read_asset(directory, name):
    # Explicit loose override first, then documented stock always archives. TT VFS unsupported.
    path = directory / name.lower()
    if path.exists():
        return path.read_bytes(), str(path)
    for archive in ('always3.dat', 'always2.dat', 'always.dat'):
        path = directory / archive
        if not path.exists():
            continue
        with path.open('rb') as f:
            header = f.read(16)
            if len(header) != 16 or header[:4] != b'MIX1':
                raise ValueError(f'not MIX1: {path}')
            index, names = struct.unpack_from('<II', header, 4)
            size = path.stat().st_size
            if not (16 <= index <= size - 4 and 16 <= names <= size - 4):
                raise ValueError('invalid archive offsets')
            f.seek(index)
            count, = struct.unpack('<I', f.read(4))
            if count > (size - index - 4) // 12:
                raise ValueError('archive index overrun')
            records = [struct.unpack('<III', f.read(12)) for _ in range(count)]
            f.seek(names)
            n, = struct.unpack('<I', f.read(4))
            if n != count:
                raise ValueError('archive name count mismatch')
            # mixfile.cpp::MixFileCreator sorts FileInfo before writing BOTH tables.
            for record in records:
                length = f.read(1)
                if not length:
                    raise ValueError('truncated name table')
                raw = f.read(length[0])
                if len(raw) != length[0] or not raw.endswith(b'\0'):
                    raise ValueError('truncated archive name')
                found = raw[:-1].decode('ascii')
                if found.lower() == name.lower():
                    _, offset, length = record
                    if offset > size or length > size - offset:
                        raise ValueError('archive data overrun')
                    f.seek(offset)
                    result = f.read(length)
                    return result, f'{path}::{found}@{offset}+{length}'
    raise FileNotFoundError(name)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data', type=Path, required=True)
    ap.add_argument('--model', default='c_nod_mg_l0.w3d')
    ap.add_argument('--vcvars', type=Path, default=Path('E:/VisualStudio/VC/Auxiliary/Build/vcvarsall.bat'))
    args = ap.parse_args()
    if Path(args.model).name != args.model:
        ap.error('--model must be an asset basename')
    build = HERE / 'build'
    out = HERE / 'output'
    for path in (build, out, out / 'input', build / 'tmp', build / 'cgame', build / 'qcommon', build / 'gore'):
        path.mkdir(parents=True, exist_ok=True)
    body, body_source = read_asset(args.data, args.model)
    hlod = [payload for tag, payload in chunk_list(body) if tag == 0x700]
    if len(hlod) != 1:
        raise ValueError('expected one HLOD')
    headers = [p for t, p in chunk_list(hlod[0]) if t == 0x701]
    if len(headers) != 1 or len(headers[0]) != 40:
        raise ValueError('HLOD header')
    hierarchy = headers[0][24:40].split(b'\0')[0].decode('ascii')
    tree, tree_source = read_asset(args.data, hierarchy + '.w3d')
    manifest = {'model': args.model, 'hierarchy': hierarchy, 'sources': [],
                'limits': 'Single-LOD visible file skins only; no animation/rigid heads/TT VFS/material export. Engine parity not measured.'}
    for name, data, source in [('body.w3d', body, body_source), ('hierarchy.w3d', tree, tree_source)]:
        (out / 'input' / name).write_bytes(data)
        manifest['sources'].append({'input': name, 'source': source, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    # The reference wrapper's generic algorithms are kept verbatim. Only unit conversion changes.
    text = (HANDOFF / 'rtcw_reference/cg_gore.cpp').read_text()
    old = 'const float METRES = 0.0254f;'
    if text.count(old) != 1:
        raise ValueError('reference wrapper unit constant changed; review adapter')
    text = text.replace(old, 'const float METRES = 1.0f;')
    (build / 'cgame/cg_gore.cpp').write_text(text)
    shutil.copyfile(HANDOFF / 'rtcw_reference/cg_gore.h', build / 'cgame/cg_gore.h')
    shutil.copyfile(HANDOFF / 'rtcw_reference/vr_gore.h', build / 'qcommon/vr_gore.h')
    (build / 'gore/gorecore.h').write_text('#include "../../../../../voxel_gore_handoff/core/gorecore.h"\n')
    # Work in build so all object/PDB/temp writes stay here; never invoke install helper scripts.
    cmd = [
        '@echo off',
        f'call "{args.vcvars}" x86 || exit /b 1',
        'cd /d "%~dp0"',
        'set "TMP=%~dp0tmp"', 'set "TEMP=%~dp0tmp"',
        'cl /nologo /std:c++17 /O2 /EHsc /fp:precise /D_CRT_SECURE_NO_WARNINGS ../offline.cpp ../../../../voxel_gore_handoff/core/gorecore.cpp /Fe:offline.exe || exit /b 1',
        'cl /nologo /std:c++17 /O2 /EHsc /fp:precise ../../../../voxel_gore_handoff/core/test_gorecore.cpp ../../../../voxel_gore_handoff/core/gorecore.cpp /Fe:core_test.exe || exit /b 1',
        'cl /nologo /std:c++17 /O2 /EHsc /D_USE_MATH_DEFINES /D_CRT_SECURE_NO_WARNINGS ../../../../voxel_gore_handoff/tools/view.cpp /Fe:view.exe || exit /b 1',
        '"%~dp0core_test.exe" || exit /b 1',
        'cd /d "%~dp0..\\output"',
        '"%~dp0offline.exe" input/body.w3d input/hierarchy.w3d || exit /b 1',
        '"%~dp0view.exe" rest.ppm 960 600 "0,0;90,0;35,15" rest.obj:0.75,0.7,0.6 || exit /b 1',
        '"%~dp0view.exe" wounds.ppm 960 600 "0,0;90,0;35,15" cut_skin.obj:0.75,0.7,0.6 flesh.obj:0.75,0.08,0.07 || exit /b 1',
        '"%~dp0view.exe" severed.ppm 960 600 "0,0;90,0;35,15" stump_skin.obj:0.75,0.7,0.6 stump_flesh.obj:0.75,0.08,0.07 piece0_skin.obj:0.65,0.65,0.55 piece0_flesh.obj:0.75,0.08,0.07 || exit /b 1',
        'python ../../../../voxel_gore_handoff/tools/ppm2png.py rest.ppm rest.png || exit /b 1',
        'python ../../../../voxel_gore_handoff/tools/ppm2png.py wounds.ppm wounds.png || exit /b 1',
        'python ../../../../voxel_gore_handoff/tools/ppm2png.py severed.ppm severed.png || exit /b 1',
        'python ../verify.py || exit /b 1',
    ]
    (build / 'build_and_run.cmd').write_text('\n'.join(cmd) + '\n')
    print(json.dumps(manifest, indent=2))
    print('Prepared:', build / 'build_and_run.cmd')


if __name__ == '__main__':
    main()
