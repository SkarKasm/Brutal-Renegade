"""Check generated proof artifacts, x86 target, and rejection of damaged inputs."""
from pathlib import Path
import hashlib
import json
import math
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
OUT = HERE / 'output'
BUILD = HERE / 'build'


def obj(path):
    verts, faces, colors = [], [], 0
    for line in path.read_text().splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == 'v':
            values = list(map(float, words[1:]))
            assert len(values) in (3, 6) and all(map(math.isfinite, values)), path
            if len(values) == 6:
                assert all(0 <= c <= 1 for c in values[3:]), path
                colors += 1
            verts.append(values[:3])
        elif words[0] == 'f':
            assert len(words) == 4, path
            faces.append([int(w) - 1 for w in words[1:]])
    assert verts and faces, path
    area = 0.0
    for face in faces:
        assert all(0 <= i < len(verts) for i in face), path
        a, b, c = [verts[i] for i in face]
        ab, ac = [[v[i] - a[i] for i in range(3)] for v in (b, c)]
        cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
        area += math.sqrt(sum(x*x for x in cross)) / 2
    return {'vertices': len(verts), 'triangles': len(faces), 'area_m2': area, 'colored_vertices': colors}


def main():
    result = {}
    for path in sorted(OUT.glob('*.obj')):
        if path.name != 'bones.obj':
            result[path.name] = obj(path)
    rest = result['rest.obj']
    assert rest['vertices'] == 4754 and rest['triangles'] == 8022, 'default fixture changed; review counts'
    assert result['cut_skin.obj']['area_m2'] < rest['area_m2'], 'cut did not remove skin area'
    assert result['stump_skin.obj']['area_m2'] + result['piece0_skin.obj']['area_m2'] < rest['area_m2'], 'severed skin duplicates the original'
    assert result['flesh.obj']['colored_vertices'] == result['flesh.obj']['vertices'], 'missing layered flesh colors'
    exe = (BUILD / 'offline.exe').read_bytes()
    pe, = struct.unpack_from('<I', exe, 0x3c)
    assert exe[pe:pe+4] == b'PE\0\0' and struct.unpack_from('<H', exe, pe+4)[0] == 0x14c, 'not an x86 PE'
    result['target'] = 'x86 PE (machine 0x014c)'
    manifest = json.loads((OUT / 'manifest.json').read_text())
    for source in manifest['sources']:
        data = (OUT / 'input' / source['input']).read_bytes()
        assert len(data) == source['size'] and hashlib.sha256(data).hexdigest() == source['sha256']
    # Reuse output-independent temp working directories because the harness writes a log.
    with tempfile.TemporaryDirectory(dir=BUILD, prefix='reject_') as td:
        td = Path(td)
        body = (OUT / 'input/body.w3d').read_bytes()
        tree = OUT / 'input/hierarchy.w3d'
        cases = {'truncated': body[:100], 'oversize_chunk': body[:4] + struct.pack('<I', 0x7fffffff) + body[8:]}
        # A valid chunk tree with a poisoned vertex influence must fail before voxel allocation.
        poisoned = bytearray(body)
        pos, mesh_end = 8, 8 + (struct.unpack_from('<I', body, 4)[0] & 0x7fffffff)
        while pos < mesh_end:
            tag, size = struct.unpack_from('<II', body, pos)
            if tag == 0x0e:
                struct.pack_into('<H', poisoned, pos + 8, 65535)
                break
            pos += 8 + (size & 0x7fffffff)
        else:
            raise AssertionError('fixture has no influences')
        cases['invalid_bone'] = bytes(poisoned)
        result['input_rejections'] = {}
        for name, data in cases.items():
            file = td / f'{name}.w3d'
            file.write_bytes(data)
            run = subprocess.run([str(BUILD / 'offline.exe'), str(file), str(tree)], cwd=td, capture_output=True, text=True)
            assert run.returncode != 0 and 'ERROR:' in run.stderr, name
            result['input_rejections'][name] = run.stderr.strip()
    (OUT / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Verified finite OBJ geometry/indices/colors, removed skin area, x86 PE, input hashes and 3 malformed-input rejections.')


if __name__ == '__main__':
    main()
