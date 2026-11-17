"""Convert Soldier of Fortune II gore assets into W3D meshes and TGA textures for Renegade.

Reads your own SoF2 install's pk3 files (models/characters/gore, models/characters/caps and the
average_sleeves character's stump caps) and writes, into --out (normally Renegade's Data folder):

  G_CAP_NECK/SHLDR/ELBOW/HIP/KNEE.w3d    stump caps, +X out of the stump (body side)
  G_CAP_*_R.w3d                          the same turned round (the flying part's side)
  G_BONE.w3d                             a bone gib
  G_CHUNK_SM/MD/LG.w3d, G_CHUNK_GIB_MD/LG.w3d   flesh chunks
  G_GUTS/LUNG/STOMACH/RIBS/BRAIN.w3d     organs
  g_*.tga                                their textures, plus blood sprays, splats and pools

Names match Code/Combat/goremanager.cpp. Everything is in metres, Z up, like Renegade.

    py -3 -m pip install pillow
    py -3 convert_sof2_gore.py --sof2 "C:/GOG Games/Soldier of Fortune II/base" --out "C:/Westwood/Renegade/Data"

The SoF2 assets belong to Raven Software / Activision. The output is for use with your own copy
of SoF2 and must not be redistributed or committed to this repository.
"""
import argparse
import io
import math
import os
import struct
import sys
import zipfile

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is needed: py -3 -m pip install pillow")

UNITS_TO_METRES = 0.0254        # SoF2 (Quake 3 engine) units are about an inch

GORE = "models/characters/gore/"

# W3D mesh name -> (source model, source texture, extra scale)
# SoF2 sizes its chunks in its effect files, so the raw meshes run from 1 cm to 64 cm; the
# extra scale brings each to a believable size on a Renegade soldier (checked on a preview
# sheet: small chunk ~4 cm, large ~10-12 cm, brain ~15 cm, rib cage ~38 cm).
# G_CHUNK_MD / G_CHUNK_GIB_MD are thin spikes (SoF2 streaks); converted but not thrown.
PIECES = {
    "G_CHUNK_SM":     (GORE + "chunk_smll/chunk_smll.md3", GORE + "chunk_smll/chunk_smll.jpg", 4.0),
    "G_CHUNK_MD":     (GORE + "chunk_med/chunk_med.md3",   GORE + "chunk_med/chunk_med.jpg",   1.0),
    "G_CHUNK_LG":     (GORE + "chunk_lrg/chunk_lrg.md3",   GORE + "chunk_lrg/chunk_lrg.jpg",   2.0),
    "G_CHUNK_GIB_MD": (GORE + "chunk_gib_med.md3",         GORE + "chunk_gib_med/chunk_med.jpg", 0.5),
    "G_CHUNK_GIB_LG": (GORE + "chunk_gib_lrg.md3",         GORE + "chunk_gib_lrg/chunk_lrg.jpg", 0.5),
    "G_GUTS":         (GORE + "guts.md3",                  GORE + "rib_cage/rib_cage.png",     1.0),
    "G_LUNG":         (GORE + "lung.md3",                  GORE + "rib_cage/rib_cage.png",     0.6),
    "G_STOMACH":      (GORE + "stomach.md3",               GORE + "rib_cage/rib_cage.png",     1.0),
    "G_RIBS":         (GORE + "rib_cage/rib_cage.md3",     GORE + "rib_cage/rib_cage.png",     0.6),
    "G_BRAIN":        (GORE + "brain/brain.md3",           GORE + "brain/brain.jpg",           0.7),
    "G_BONE":         (GORE + "bone_small/bone_small.glm", GORE + "bone_long/bone_long.png",   1.0),
}

CAP_MODEL = "models/characters/average_sleeves/average_sleeves.glm"
CAP_TEXTURE = "models/characters/caps/caps.jpg"
# SoF2 names its caps cap_<surface it sits on>_<surface that came off>_off.
# W3D name -> (body side: [(cap surface, surface it caps off)...],
#              cut-off part's side: [(cap surface, the body surface it faced)...] or None to
#              reuse the body cap turned round, rim radius in metres, depth squash)
# SoF2 has no neck cap; like Brutally RealRTCW, the neck uses the shoulder's.
CAPS = {
    "G_CAP_NECK":  ([("cap_torso_r_arm_off", "arm_uppr_r")], None, 0.055, 0.8),
    "G_CAP_SHLDR": ([("cap_torso_r_arm_off", "arm_uppr_r")], [("cap_arm_uppr_r_torso_off", "torso_r")], 0.065, 1.0),
    "G_CAP_ELBOW": ([("cap_arm_uppr_r_lwr_off", "arm_lwr_r")], [("cap_arm_lwr_r_uppr_off", "arm_uppr_r")], 0.045, 1.0),
    # cap_hip_r_off is a lumpy pelvis piece, not a disc; the top of the leg's cap reads better
    # on both sides (as Brutally RealRTCW does)
    "G_CAP_HIP":   ([("cap_leg_uppr_r_hip_off", "leg_uppr_r")], [("cap_leg_uppr_r_hip_off", "hip_r")], 0.085, 1.0),
    "G_CAP_KNEE":  ([("cap_leg_uppr_r_lwr_off", "leg_lwr_r")], [("cap_leg_lwr_r_uppr_off", "leg_uppr_r")], 0.055, 1.0),
}

# Plain textures: output name -> candidate sources (first found wins)
TEXTURES = {
    "g_tissue.tga": [GORE + "rib_cage/rib_cage.png"],
    "g_wound1.tga": [GORE + "gil_bullet_holef.png"],
    "g_wound2.tga": [GORE + "gil_bullet_holee_128.png"],
    "g_wound3.tga": [GORE + "gil_bullet_holeg.png"],
    "g_wound4.tga": [GORE + "gil_bullet_holeh.png"],
    # blood_drop reads as droplets; gil_spurt is a streak sheet that looks like scribbles as a sprite
    "g_blood_spray.tga": [GORE + "blood_drop.png", GORE + "gil_blood_drop.png", GORE + "gil_spurt.png"],
    "g_blood_mist.tga":  [GORE + "gil_mist.png"],
    "g_splat1.tga":      [GORE + "splat1.png"],
    "g_splat2.tga":      [GORE + "splat2.png"],
    "g_splat3.tga":      [GORE + "splat3.png"],
    "g_wall_splat.tga":  [GORE + "gil_wall_splat_512.png", GORE + "gil_big_splatter.png"],
    "g_pool1.tga":       [GORE + "gil_bloodpool1.png"],
    "g_pool2.tga":       [GORE + "gil_bloodpool2.png", GORE + "gil_bloodpool1.png"],
}


# ---------------------------------------------------------------------------------------------
# pk3 access: later pk3s (alphabetically, like the engine) override earlier ones
# ---------------------------------------------------------------------------------------------
class Pk3Set:
    def __init__(self, base):
        self.files = {}
        names = sorted(n for n in os.listdir(base) if n.lower().endswith(".pk3"))
        if not names:
            sys.exit("no .pk3 files in %s (point --sof2 at SoF2's base folder)" % base)
        for n in names:
            z = zipfile.ZipFile(os.path.join(base, n))
            for info in z.infolist():
                self.files[info.filename.lower()] = z

    def read(self, path):
        z = self.files.get(path.lower())
        if z is None:
            return None
        for info in z.infolist():
            if info.filename.lower() == path.lower():
                return z.read(info)
        return None


# ---------------------------------------------------------------------------------------------
# Readers. A surface is (name, verts [(pos, normal, (s, t))], tris [(a, b, c)])
# ---------------------------------------------------------------------------------------------
def read_md3(data):
    ident, ver = struct.unpack("<4si", data[:8])
    if ident != b"IDP3":
        raise ValueError("not an md3")
    num_surfaces, ofs_surfaces = struct.unpack("<i", data[84:88])[0], struct.unpack("<i", data[100:104])[0]
    surfaces = []
    o = ofs_surfaces
    for _ in range(num_surfaces):
        (_, name, _, _, _, nv, nt, ofs_tri, _, ofs_st, ofs_xyz, ofs_end) = struct.unpack("<4s64s10i", data[o:o + 108])
        tris = [struct.unpack("<3i", data[o + ofs_tri + i * 12:o + ofs_tri + i * 12 + 12]) for i in range(nt)]
        verts = []
        for j in range(nv):
            x, y, z, lat, lng = struct.unpack("<3h2B", data[o + ofs_xyz + j * 8:o + ofs_xyz + j * 8 + 8])
            la, lo = lat * 2 * math.pi / 255, lng * 2 * math.pi / 255
            normal = (math.cos(lo) * math.sin(la), math.sin(lo) * math.sin(la), math.cos(la))
            st = struct.unpack("<2f", data[o + ofs_st + j * 8:o + ofs_st + j * 8 + 8])
            verts.append(((x / 64.0, y / 64.0, z / 64.0), normal, st))
        surfaces.append((name.split(b"\0")[0].decode("latin1"), verts, tris))
        o += ofs_end
    return surfaces


def read_glm(data, lod=0):
    """Ghoul 2 mdxm (version 6) as SoF2 ships it: base pose of one LOD."""
    if data[:4] != b"2LGM":
        raise ValueError("not a glm")
    (_, num_bones, _, ofs_lods, num_surfaces, _, _) = struct.unpack("<7i", data[136:164])
    hier_ofs = struct.unpack("<%di" % num_surfaces, data[164:164 + 4 * num_surfaces])
    names = []
    for i in range(num_surfaces):
        o = 164 + hier_ofs[i]
        names.append(data[o:o + 64].split(b"\0")[0].decode("latin1"))
    o = ofs_lods
    for _ in range(lod):
        o += struct.unpack("<i", data[o:o + 4])[0]
    base = o + 4
    surf_ofs = struct.unpack("<%di" % num_surfaces, data[base:base + 4 * num_surfaces])
    surfaces = []
    for i in range(num_surfaces):
        so = base + surf_ofs[i]
        (_, _, _, nv, ofs_v, nt, ofs_t, _, _, _) = struct.unpack("<10i", data[so:so + 40])
        verts = []
        for j in range(nv):
            vo = so + ofs_v + j * 32
            normal = struct.unpack("<3f", data[vo:vo + 12])
            pos = struct.unpack("<3f", data[vo + 12:vo + 24])
            verts.append([pos, normal, None])
        to = so + ofs_v + nv * 32
        for j in range(nv):
            verts[j][2] = struct.unpack("<2f", data[to + j * 8:to + j * 8 + 8])
        tris = [struct.unpack("<3i", data[so + ofs_t + k * 12:so + ofs_t + k * 12 + 12]) for k in range(nt)]
        surfaces.append((names[i], [tuple(v) for v in verts], tris))
    return surfaces


# ---------------------------------------------------------------------------------------------
# Small vector helpers (no numpy needed)
# ---------------------------------------------------------------------------------------------
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))
def unit(a):
    l = length(a)
    return mul(a, 1.0 / l) if l > 1e-9 else (0.0, 0.0, 1.0)
def centroid(points):
    n = float(len(points))
    return (sum(p[0] for p in points) / n, sum(p[1] for p in points) / n, sum(p[2] for p in points) / n)


def merge(surfaces):
    """All surfaces as one vertex/triangle list."""
    verts, tris = [], []
    for _, vs, ts in surfaces:
        base = len(verts)
        verts.extend(vs)
        tris.extend((a + base, b + base, c + base) for a, b, c in ts)
    return verts, tris


def recentre(verts, scale):
    c = centroid([v[0] for v in verts])
    return [(mul(sub(v[0], c), scale), v[1], v[2]) for v in verts]


def fix_winding(verts, tris):
    """W3D faces are counter-clockwise (right-handed). Make the geometric normals agree with
    the vertex normals, whichever way the source wound its triangles."""
    agree = 0
    for a, b, c in tris:
        n = cross(sub(verts[b][0], verts[a][0]), sub(verts[c][0], verts[a][0]))
        agree += 1 if dot(n, add(add(verts[a][1], verts[b][1]), verts[c][1])) >= 0 else -1
    return tris if agree >= 0 else [(a, c, b) for a, b, c in tris]


# ---------------------------------------------------------------------------------------------
# W3D writer (ww3d2/w3d_file.h): one static, textured, single-pass mesh per file
# ---------------------------------------------------------------------------------------------
W3D_CHUNK_MESH = 0x00
W3D_CHUNK_VERTICES = 0x02
W3D_CHUNK_VERTEX_NORMALS = 0x03
W3D_CHUNK_MESH_HEADER3 = 0x1F
W3D_CHUNK_TRIANGLES = 0x20
W3D_CHUNK_VERTEX_SHADE_INDICES = 0x22
W3D_CHUNK_MATERIAL_INFO = 0x28
W3D_CHUNK_SHADERS = 0x29
W3D_CHUNK_VERTEX_MATERIALS = 0x2A
W3D_CHUNK_VERTEX_MATERIAL = 0x2B
W3D_CHUNK_VERTEX_MATERIAL_NAME = 0x2C
W3D_CHUNK_VERTEX_MATERIAL_INFO = 0x2D
W3D_CHUNK_TEXTURES = 0x30
W3D_CHUNK_TEXTURE = 0x31
W3D_CHUNK_TEXTURE_NAME = 0x32
W3D_CHUNK_MATERIAL_PASS = 0x38
W3D_CHUNK_VERTEX_MATERIAL_IDS = 0x39
W3D_CHUNK_SHADER_IDS = 0x3A
W3D_CHUNK_TEXTURE_STAGE = 0x48
W3D_CHUNK_TEXTURE_IDS = 0x49
W3D_CHUNK_STAGE_TEXCOORDS = 0x4A

W3D_MESH_VERSION = (4 << 16) | 2
W3D_MESH_FLAG_TWO_SIDED = 0x00002000
W3D_VERTEX_CHANNEL_LOCATION = 0x1
W3D_VERTEX_CHANNEL_NORMAL = 0x2
W3D_VERTEX_CHANNEL_TEXCOORD = 0x4
W3D_FACE_CHANNEL_FACE = 0x1


def chunk(kind, payload):
    return struct.pack("<II", kind, len(payload)) + payload


def wrapper(kind, children):
    payload = b"".join(children)
    return struct.pack("<II", kind, len(payload) | 0x80000000) + payload   # has sub-chunks


def name16(s):
    b = s.encode("ascii")[:15]
    return b + b"\0" * (16 - len(b))


def write_w3d(path, mesh_name, texture_name, verts, tris, two_sided=False):
    tris = fix_winding(verts, tris)
    points = [v[0] for v in verts]
    mn = tuple(min(p[i] for p in points) for i in range(3))
    mx = tuple(max(p[i] for p in points) for i in range(3))
    centre = mul(add(mn, mx), 0.5)
    radius = max(length(sub(p, centre)) for p in points)

    header = struct.pack("<II16s16s7I2I3f3f3ff",
                         W3D_MESH_VERSION, W3D_MESH_FLAG_TWO_SIDED if two_sided else 0,
                         name16(mesh_name), name16(""),
                         len(tris), len(verts), 1, 0, 0, 0, 0,
                         W3D_VERTEX_CHANNEL_LOCATION | W3D_VERTEX_CHANNEL_NORMAL | W3D_VERTEX_CHANNEL_TEXCOORD,
                         W3D_FACE_CHANNEL_FACE,
                         *mn, *mx, *centre, radius)

    tri_data = b""
    for a, b, c in tris:
        n = unit(cross(sub(points[b], points[a]), sub(points[c], points[a])))
        tri_data += struct.pack("<3II3ff", a, b, c, 0, *n, dot(n, points[a]))

    shader = bytes([
        3,   # DepthCompare: PASS_LEQUAL
        1,   # DepthMask: WRITE_ENABLE
        0,   # ColorMask (obsolete)
        0,   # DestBlend: ZERO
        0,   # FogFunc (obsolete)
        1,   # PriGradient: MODULATE
        0,   # SecGradient: DISABLE
        1,   # SrcBlend: ONE
        1,   # Texturing: ENABLE
        0, 0, 0,
        0,   # AlphaTest: DISABLE
        0, 0, 0])
    white, black = struct.pack("<4B", 255, 255, 255, 0), struct.pack("<4B", 0, 0, 0, 0)
    vmat = struct.pack("<I", 0) + white + white + black + black + struct.pack("<3f", 1.0, 1.0, 0.0)

    mesh = wrapper(W3D_CHUNK_MESH, [
        chunk(W3D_CHUNK_MESH_HEADER3, header),
        chunk(W3D_CHUNK_VERTICES, b"".join(struct.pack("<3f", *p) for p in points)),
        chunk(W3D_CHUNK_VERTEX_NORMALS, b"".join(struct.pack("<3f", *unit(v[1])) for v in verts)),
        chunk(W3D_CHUNK_TRIANGLES, tri_data),
        chunk(W3D_CHUNK_VERTEX_SHADE_INDICES, b"".join(struct.pack("<I", i) for i in range(len(verts)))),
        chunk(W3D_CHUNK_MATERIAL_INFO, struct.pack("<4I", 1, 1, 1, 1)),
        chunk(W3D_CHUNK_SHADERS, shader),
        wrapper(W3D_CHUNK_VERTEX_MATERIALS, [wrapper(W3D_CHUNK_VERTEX_MATERIAL, [
            chunk(W3D_CHUNK_VERTEX_MATERIAL_NAME, b"gore\0"),
            chunk(W3D_CHUNK_VERTEX_MATERIAL_INFO, vmat)])]),
        wrapper(W3D_CHUNK_TEXTURES, [wrapper(W3D_CHUNK_TEXTURE, [
            chunk(W3D_CHUNK_TEXTURE_NAME, texture_name.encode("ascii") + b"\0")])]),
        wrapper(W3D_CHUNK_MATERIAL_PASS, [
            chunk(W3D_CHUNK_VERTEX_MATERIAL_IDS, struct.pack("<I", 0)),
            chunk(W3D_CHUNK_SHADER_IDS, struct.pack("<I", 0)),
            wrapper(W3D_CHUNK_TEXTURE_STAGE, [
                chunk(W3D_CHUNK_TEXTURE_IDS, struct.pack("<I", 0)),
                # W3D flips V on load (meshmdlio.cpp), Quake's t runs down the image
                chunk(W3D_CHUNK_STAGE_TEXCOORDS, b"".join(struct.pack("<2f", v[2][0], 1.0 - v[2][1]) for v in verts))])]),
    ])
    with open(path, "wb") as f:
        f.write(mesh)


# ---------------------------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------------------------
def pot(n):
    p = 1
    while p * 2 <= n and p < 512:
        p *= 2
    return p


def write_texture(pk3, source, out_path):
    data = pk3.read(source)
    if data is None:
        return False
    img = Image.open(io.BytesIO(data)).convert("RGBA")
    w, h = img.size
    if (w, h) != (pot(w), pot(h)):
        img = img.resize((pot(w), pot(h)), Image.BICUBIC)   # D3D8-era cards want powers of two
    img.save(out_path)
    return True


def texture_name_for(source):
    return "g_" + os.path.splitext(os.path.basename(source))[0].lower() + ".tga"


# ---------------------------------------------------------------------------------------------
# Stump caps: SoF2's cap surface in its own frame, +X towards the part it closes off
# ---------------------------------------------------------------------------------------------
def build_cap(surfaces, pairs, rim_radius, squash):
    """pairs: (cap surface, surface it faces) candidates, tried in order."""
    by_name = dict((s[0], s) for s in surfaces)
    pair = next(((by_name[c], by_name[r]) for c, r in pairs if c in by_name and r in by_name), None)
    if pair is None:
        return None
    cap, removed = pair
    pts = [v[0] for v in cap[1]]
    c = centroid(pts)
    x = unit(sub(centroid([v[0] for v in removed[1]]), c))
    y = unit(cross((0.0, 0.0, 1.0), x) if abs(x[2]) < 0.9 else cross((1.0, 0.0, 0.0), x))
    z = cross(x, y)

    def to_frame(p):
        return (dot(p, x), dot(p, y), dot(p, z))

    local = [to_frame(sub(p, c)) for p in pts]
    rims = sorted(math.hypot(p[1], p[2]) for p in local)
    rim = rims[int(len(rims) * 0.9)] or 1.0
    s = rim_radius / rim
    verts = [((p[0] * s * squash, p[1] * s, p[2] * s), to_frame(v[1]), v[2]) for p, v in zip(local, cap[1])]
    return verts, list(cap[2])


def turned_round(verts):
    """180 degrees about Z: +X becomes -X (a rotation, so the winding stays right)."""
    return [((-p[0], -p[1], p[2]), (-n[0], -n[1], n[2]), st) for p, n, st in verts]


# ---------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sof2", required=True, help="Soldier of Fortune II 'base' folder (with the .pk3 files)")
    ap.add_argument("--out", required=True, help="output folder, normally Renegade's Data folder")
    ap.add_argument("--scale", type=float, default=UNITS_TO_METRES, help="metres per SoF2 unit (default %(default)s)")
    args = ap.parse_args()

    pk3 = Pk3Set(args.sof2)
    os.makedirs(args.out, exist_ok=True)
    written, missing = [], []

    for out_name, sources in TEXTURES.items():
        if any(write_texture(pk3, s, os.path.join(args.out, out_name)) for s in sources):
            written.append(out_name)
        else:
            missing.append(out_name + " (" + sources[0] + ")")

    for mesh_name, (model_path, texture_path, extra_scale) in PIECES.items():
        data = pk3.read(model_path)
        if data is None:
            missing.append(mesh_name + " (" + model_path + ")")
            continue
        surfaces = read_glm(data) if model_path.endswith(".glm") else read_md3(data)
        verts, tris = merge(surfaces)
        tex_name = texture_name_for(texture_path)
        if tex_name not in written and write_texture(pk3, texture_path, os.path.join(args.out, tex_name)):
            written.append(tex_name)
        write_w3d(os.path.join(args.out, mesh_name.lower() + ".w3d"), mesh_name, tex_name, recentre(verts, args.scale * extra_scale), tris)
        written.append(mesh_name.lower() + ".w3d")

    cap_data = pk3.read(CAP_MODEL)
    if cap_data is None:
        missing.append("stump caps (" + CAP_MODEL + ")")
    else:
        surfaces = read_glm(cap_data)
        cap_tex = texture_name_for(CAP_TEXTURE)
        if write_texture(pk3, CAP_TEXTURE, os.path.join(args.out, cap_tex)):
            written.append(cap_tex)
        scaled = [(n, [(mul(v[0], args.scale), v[1], v[2]) for v in vs], ts) for n, vs, ts in surfaces]
        for mesh_name, (body_pairs, part_pairs, rim, squash) in CAPS.items():
            cap = build_cap(scaled, body_pairs, rim, squash)
            if cap is None:
                missing.append(mesh_name + " (no surface " + "/".join(p[0] for p in body_pairs) + ")")
                continue
            verts, tris = cap
            write_w3d(os.path.join(args.out, mesh_name.lower() + ".w3d"), mesh_name, cap_tex, verts, tris, True)

            # The cut-off part's end faces back along the bone (-X): its own SoF2 cap pointed
            # at the body, turned round, or else the body's cap turned round.
            part = build_cap(scaled, part_pairs, rim, squash) if part_pairs else None
            part_verts, part_tris = part if part is not None else (verts, tris)
            write_w3d(os.path.join(args.out, mesh_name.lower() + "_r.w3d"), mesh_name + "_R", cap_tex,
                      turned_round(part_verts), part_tris, True)
            written += [mesh_name.lower() + ".w3d", mesh_name.lower() + "_r.w3d"]
        print("cap surfaces in %s: %s" % (CAP_MODEL, ", ".join(s[0] for s in surfaces if s[0].startswith("cap_"))))

    with open(os.path.join(args.out, "gore_assets_manifest.txt"), "w") as f:
        f.write("Converted from Soldier of Fortune II (Raven Software / Activision) by convert_sof2_gore.py.\n")
        f.write("For use with your own copy of SoF2 only. Do not redistribute.\n\n")
        f.write("\n".join(written) + "\n")

    print("wrote %d files to %s" % (len(written), args.out))
    if missing:
        print("not found (the game skips these):\n  " + "\n  ".join(missing))


if __name__ == "__main__":
    main()
