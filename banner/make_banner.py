#!/usr/bin/env python3
"""Bannière 3D « l'appareil photo vivant » (menu HOME de la 3DS).

Un appareil photo rond et brillant (crème, bande corail, gros objectif bleuté,
flash jaune) tourne doucement. Toutes les quelques secondes : le déclencheur
s'enfonce, l'appareil s'écrase un peu (effet dessin animé), le flash éclate et
une petite photo sort par le dessus. Boucle de 5 s.

Écrit banner.gltf (+ banner.bin) avec la bibliothèque standard uniquement.
Règles suivies pour ne pas figer le menu HOME : animation rigide (translation /
rotation / échelle de nœuds nommés), pas de transparence, petites textures,
scène cadrée dans la zone sûre.

Usage :
    python3 make_banner.py                               # caméra par défaut
    python3 make_banner.py --camera banner-camera.gltf   # caméra du menu HOME (pycgfx)

(L'ancien concept « diorama » est dans make_banner_diorama.py.)
"""
import argparse
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
FPS = 15
LOOP = 5.0
CLICK = 2.0  # instant du « clic » dans la boucle

# ---------------------------------------------------------------------------
# Maths (quaternions x, y, z, w comme glTF)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qaxis(axis, deg):
    h = math.radians(deg) / 2
    s = math.sin(h)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(h))


def qrot(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    ix = w * vx + y * vz - z * vy
    iy = w * vy + z * vx - x * vz
    iz = w * vz + x * vy - y * vx
    iw = -x * vx - y * vy - z * vz
    return (ix * w + iw * -x + iy * -z - iz * -y,
            iy * w + iw * -y + iz * -x - ix * -z,
            iz * w + iw * -z + ix * -y - iy * -x)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(v):
    n = math.sqrt(dot(v, v)) or 1.0
    return (v[0] / n, v[1] / n, v[2] / n)


# ---------------------------------------------------------------------------
# Géométrie

PALETTE = open(os.path.join(HERE, "textures", "palette.txt")).read().split()


def swatch_uv(name):
    i = PALETTE.index(name)
    return (((i % 8) * 8 + 4) / 64.0, ((i // 8) * 8 + 4) / 64.0)


def fix_winding(pos, nrm, idx):
    """Oriente chaque triangle vers l'extérieur (d'après les normales)."""
    out = []
    for k in range(0, len(idx), 3):
        a, b, c = idx[k:k + 3]
        fn = cross(sub(pos[b], pos[a]), sub(pos[c], pos[a]))
        avg = tuple(nrm[a][i] + nrm[b][i] + nrm[c][i] for i in range(3))
        out += [a, b, c] if dot(fn, avg) >= 0 else [a, c, b]
    return out


def colored(pos, nrm, idx, color):
    u, v = swatch_uv(color)
    return pos, nrm, [(u, v)] * len(pos), fix_winding(pos, nrm, idx)


def _f(w, e):
    return math.copysign(abs(w) ** e, w)


def superellipsoid(a, b, c, e, color, center=(0, 0, 0), lat=14, lon=28, theta=(-90, 90)):
    """Forme « savon » arrondie (e petit = presque un cube aux coins ronds)."""
    cx, cy, cz = center
    pos, nrm, idx = [], [], []
    t0, t1 = math.radians(theta[0]), math.radians(theta[1])
    for i in range(lat + 1):
        th = t0 + (t1 - t0) * i / lat
        for j in range(lon + 1):
            ph = -math.pi + 2 * math.pi * j / lon
            ct, st, cp, sp = math.cos(th), math.sin(th), math.cos(ph), math.sin(ph)
            pos.append((cx + a * _f(ct, e) * _f(cp, e), cy + b * _f(st, e), cz + c * _f(ct, e) * _f(sp, e)))
            nrm.append(norm((_f(ct, 2 - e) * _f(cp, 2 - e) / a, _f(st, 2 - e) / b, _f(ct, 2 - e) * _f(sp, 2 - e) / c)))
    w = lon + 1
    for i in range(lat):
        for j in range(lon):
            p = i * w + j
            idx += [p, p + 1, p + w + 1, p, p + w + 1, p + w]
    return colored(pos, nrm, idx, color)


def cylinder(r, z0, z1, color, center=(0, 0), seg=28, caps=(True, True), axis="z"):
    """Cylindre le long de Z (ou Y), avec couvercles."""
    cx, cy = center
    pos, nrm, idx = [], [], []

    def P(x, y, z):
        return (cx + x, cy + y, z) if axis == "z" else (cx + x, z, cy + y)

    def Nv(x, y, z):
        return (x, y, z) if axis == "z" else (x, z, y)

    for zz in (z0, z1):
        for j in range(seg + 1):
            ang = 2 * math.pi * j / seg
            pos.append(P(r * math.cos(ang), r * math.sin(ang), zz))
            nrm.append(Nv(math.cos(ang), math.sin(ang), 0))
    w = seg + 1
    for j in range(seg):
        idx += [j, j + 1, w + j + 1, j, w + j + 1, w + j]
    for k, (zz, on) in enumerate(((z0, caps[0]), (z1, caps[1]))):
        if not on:
            continue
        s = -1 if k == 0 else 1
        c0 = len(pos)
        pos.append(P(0, 0, zz))
        nrm.append(Nv(0, 0, s))
        for j in range(seg + 1):
            ang = 2 * math.pi * j / seg
            pos.append(P(r * math.cos(ang), r * math.sin(ang), zz))
            nrm.append(Nv(0, 0, s))
        for j in range(seg):
            idx += [c0, c0 + 1 + j, c0 + 2 + j]
    return colored(pos, nrm, idx, color)


def torus(R, r, z, color, seg=28, rings=10):
    """Anneau (bague d'objectif) autour de l'axe Z."""
    pos, nrm, idx = [], [], []
    for i in range(seg + 1):
        u = 2 * math.pi * i / seg
        for j in range(rings + 1):
            v = 2 * math.pi * j / rings
            n = (math.cos(v) * math.cos(u), math.cos(v) * math.sin(u), math.sin(v))
            pos.append(((R + r * math.cos(v)) * math.cos(u), (R + r * math.cos(v)) * math.sin(u), z + r * math.sin(v)))
            nrm.append(n)
    w = rings + 1
    for i in range(seg):
        for j in range(rings):
            p = i * w + j
            idx += [p, p + w, p + w + 1, p, p + w + 1, p + 1]
    return colored(pos, nrm, idx, color)


def box(w, h, d, color, center=(0, 0, 0)):
    cx, cy, cz = center
    x, y, z = w / 2, h / 2, d / 2
    faces = [((0, 0, 1), [(-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)]),
             ((0, 0, -1), [(x, -y, -z), (-x, -y, -z), (-x, y, -z), (x, y, -z)]),
             ((1, 0, 0), [(x, -y, z), (x, -y, -z), (x, y, -z), (x, y, z)]),
             ((-1, 0, 0), [(-x, -y, -z), (-x, -y, z), (-x, y, z), (-x, y, -z)]),
             ((0, 1, 0), [(-x, y, z), (x, y, z), (x, y, -z), (-x, y, -z)]),
             ((0, -1, 0), [(-x, -y, -z), (x, -y, -z), (x, -y, z), (-x, -y, z)])]
    pos, nrm, idx = [], [], []
    for n, q in faces:
        b = len(pos)
        for p in q:
            pos.append((p[0] + cx, p[1] + cy, p[2] + cz))
            nrm.append(n)
        idx += [b, b + 1, b + 2, b, b + 2, b + 3]
    return colored(pos, nrm, idx, color)


def quad(w, h, center=(0, 0, 0)):
    """Rectangle texturé face +Z."""
    cx, cy, cz = center
    x, y = w / 2, h / 2
    pos = [(cx - x, cy - y, cz), (cx + x, cy - y, cz), (cx + x, cy + y, cz), (cx - x, cy + y, cz)]
    return pos, [(0, 0, 1)] * 4, [(0, 1), (1, 1), (1, 0), (0, 0)], [0, 1, 2, 0, 2, 3]


def star(r_out, r_in, z, color, points=8):
    """Étoile plate (éclat du flash), visible des deux côtés."""
    pos, nrm, idx = [(0, 0, z)], [(0, 0, 1)], []
    for k in range(points * 2 + 1):
        ang = math.pi / 2 + math.pi * k / points
        r = r_out if k % 2 == 0 else r_in
        pos.append((r * math.cos(ang), r * math.sin(ang), z))
        nrm.append((0, 0, 1))
    for k in range(points * 2):
        idx += [0, k + 1, k + 2]
    u, v = swatch_uv(color)
    back = len(pos)
    pos += [p for p in pos]
    nrm += [(0, 0, -1)] * back
    idx += [i + back for k in range(0, len(idx), 3) for i in (idx[k], idx[k + 2], idx[k + 1])]
    return pos, nrm, [(u, v)] * len(pos), idx


# ---------------------------------------------------------------------------
# Scène : (nom, parent, mesh(geom, texture) ou None, T, R, S)

SCENE = []


def node(name, parent, mesh=None, t=(0, 0, 0), r=(0, 0, 0, 1), s=(1, 1, 1)):
    SCENE.append({"name": name, "parent": parent, "mesh": mesh, "t": t, "r": r, "s": s})


PAL = "palette.png"


def build_scene():
    node("Banner", None)                         # racine, cadrée sur la caméra
    node("Scene", "Banner", t=(0, -1.6, 0))       # recentre (la photo monte au-dessus)
    node("Camera", "Scene")                      # tout l'appareil : tourne doucement
    node("Squash", "Camera")                     # écrasement au moment du clic
    # Corps : savon crème + bande corail
    node("Body", "Squash", (superellipsoid(4.2, 2.7, 1.9, 0.32, "cream"), PAL))
    node("Band", "Squash", (superellipsoid(4.26, 1.05, 1.96, 0.32, "coral", theta=(-28, 28)), PAL))
    # Fente d'où sort la photo (sur le dessus)
    node("Slot", "Squash", (box(3.0, 0.12, 0.5, "lens_dark", center=(0, 2.68, -0.2)), PAL))
    # Objectif : barillet foncé, bague claire, verre bleu et reflet
    node("LensBarrel", "Squash", (cylinder(1.75, 1.2, 2.55, "lens_dark"), PAL))
    node("LensRing", "Squash", (torus(1.75, 0.22, 2.55, "lens_ring"), PAL))
    node("LensGlass", "Squash", (cylinder(1.35, 2.5, 2.62, "glass", caps=(False, True)), PAL))
    node("LensShine", "Squash", (superellipsoid(0.38, 0.38, 0.08, 0.9, "glass_light", center=(-0.45, 0.45, 2.66)), PAL))
    # Flash (en haut à gauche) et déclencheur (dessus, à droite)
    node("Flash", "Squash", (superellipsoid(0.75, 0.48, 0.25, 0.3, "flash", center=(-2.6, 1.6, 1.85)), PAL))
    node("Shutter", "Squash", t=(2.6, 2.6, 0.2))
    node("ShutterButton", "Shutter", (cylinder(0.55, 0.0, 0.42, "coral_dark", axis="y"), PAL))
    # Petit logo hn sur la face avant (en bas à droite)
    node("Logo", "Squash", (quad(0.9, 0.9, center=(2.9, -1.55, 1.92)), "lid_logo.png"))
    # Éclat du flash : étoile qui apparaît au clic
    node("FlashBurst", "Camera", (star(1.6, 0.6, 0.0, "flash_glow"), PAL), t=(-2.6, 1.6, 2.3), s=(0.01, 0.01, 0.01))
    # La photo qui sort par le dessus (cadre blanc + image)
    node("Photo", "Camera", t=(0, 1.2, -0.2), s=(0.01, 0.01, 0.01))
    node("PhotoFrame", "Photo", (box(2.8, 3.1, 0.12, "white"), PAL))
    node("PhotoImage", "Photo", (quad(2.4, 2.4, center=(0, 0.2, 0.07)), "photo.png"))


ANIMATED = ["Camera", "Squash", "Shutter", "FlashBurst", "Photo"]


def smooth(a, b, k):
    k = max(0.0, min(1.0, k))
    k = k * k * (3 - 2 * k)
    return a + (b - a) * k


def bounce(k):
    """0 -> 1 avec un petit dépassement (effet élastique)."""
    k = max(0.0, min(1.0, k))
    return 1 + 2.2 * (k - 1) ** 3 + 1.2 * (k - 1) ** 2 if k < 1 else 1.0


def animate(name, t):
    """Pose (T, R, S) d'un nœud animé à l'instant t (secondes, 0..LOOP)."""
    dt = t - CLICK
    if name == "Camera":
        # de face pour le clic, puis un tour complet sur lui-même autour de son centre
        start = CLICK + 0.7
        if t < start:
            return (0, 0.12 * math.sin(t / start * 2 * math.pi), 0), (0, 0, 0, 1), (1, 1, 1)
        k = (t - start) / (LOOP - start)
        k = k * k * (3 - 2 * k)
        return (0, 0, 0), qaxis((0, 1, 0), 360.0 * k), (1, 1, 1)
    if name == "Squash":
        if 0 <= dt < 0.45:  # écrasé puis rebond
            k = dt / 0.45
            sy = 1 - 0.14 * math.sin(math.pi * k) * (1 - k * 0.3)
            sx = 1 + 0.08 * math.sin(math.pi * k)
            return (0, 0, 0), (0, 0, 0, 1), (sx, sy, sx)
        return (0, 0, 0), (0, 0, 0, 1), (1, 1, 1)
    if name == "Shutter":
        down = 0.28 if -0.12 <= dt < 0.18 else 0.0
        return (2.6, 2.6 - down, 0.2), (0, 0, 0, 1), (1, 1, 1)
    if name == "FlashBurst":
        if 0 <= dt < 0.35:
            k = dt / 0.35
            s = math.sin(math.pi * k) * 1.2 + 0.01
            return (-2.6, 1.6, 2.3), qaxis((0, 0, 1), 40 * k), (s, s, s)
        return (-2.6, 1.6, 2.3), (0, 0, 0, 1), (0.01, 0.01, 0.01)
    if name == "Photo":
        base, top = 1.2, 4.6
        if dt < 0.25 or dt >= LOOP - CLICK - 0.05:
            return (0, base, -0.2), (0, 0, 0, 1), (0.01, 0.01, 0.01)
        if dt < 1.15:  # sort de la fente avec un petit rebond
            k = (dt - 0.25) / 0.9
            y = base + (top - base) * bounce(k)
            return (0, y, -0.2), (0, 0, 0, 1), (1, 1, 1)
        if dt < 2.55:  # se balance doucement
            k = (dt - 1.15) / 1.4
            return (0, top, -0.2), qaxis((0, 0, 1), 6 * math.sin(k * 3 * math.pi)), (1, 1, 1)
        k = (dt - 2.55) / max(LOOP - CLICK - 0.05 - 2.55, 0.01)  # disparaît en « pop »
        s = max(0.01, 1 - k)
        return (0, top + 0.6 * k, -0.2), (0, 0, 0, 1), (s, s, s)
    return None


# ---------------------------------------------------------------------------
# Cadrage sur la caméra du menu HOME


def fit_to_camera(camera_path):
    """Place la scène au centre de l'image du menu HOME.

    La caméra du menu HOME est en (0, 1, 44.8) et regarde vers -Z (champ vertical 30°,
    plan proche à 26.5) : à l'origine, l'image fait ±12 unités de haut. On laisse donc la
    scène à l'origine, à une échelle qui la fait remplir l'image sans être coupée."""
    cam_y = 1.0
    if camera_path:
        g = json.load(open(camera_path))
        for n in g.get("nodes", []):
            if "camera" in n:
                cam_y = n.get("translation", (0, 1, 0))[1]
                break
    scale = 2.1  # l'appareil remplit bien l'image, la photo reste dans le cadre
    return (0.0, cam_y + 1.2, 0.0), qaxis((1, 0, 0), 12), (scale, scale, scale)


# ---------------------------------------------------------------------------
# Écriture du glTF


def write_gltf(out_dir, camera_path=None):
    SCENE.clear()
    build_scene()
    root = SCENE[0]
    root["t"], root["r"], root["s"] = fit_to_camera(camera_path)

    blob = bytearray()
    views, accessors = [], []

    def add(data, fmt, count, comp, typ, target=None, minmax=None):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob)
        blob.extend(b"".join(struct.pack("<" + fmt, d) for d in data))
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(blob) - off}
        if target:
            v["target"] = target
        views.append(v)
        a = {"bufferView": len(views) - 1, "componentType": comp, "count": count, "type": typ}
        if minmax:
            a["min"], a["max"] = minmax
        accessors.append(a)
        return len(accessors) - 1

    textures = sorted({n["mesh"][1] for n in SCENE if n["mesh"]})
    images = [{"uri": "textures/" + t} for t in textures]
    tex = [{"source": i, "sampler": 0} for i in range(len(textures))]
    materials = [{"name": "mat_" + t.split(".")[0],
                  "pbrMetallicRoughness": {"baseColorTexture": {"index": i},
                                           "metallicFactor": 0.0, "roughnessFactor": 0.45}}
                 for i, t in enumerate(textures)]

    meshes, nodes = [], []
    for n in SCENE:
        nd = {"name": n["name"], "translation": list(n["t"]), "rotation": list(n["r"]), "scale": list(n["s"])}
        if n["mesh"]:
            (pos, nrm, uv, idx), texname = n["mesh"]
            mn = [min(p[i] for p in pos) for i in range(3)]
            mx = [max(p[i] for p in pos) for i in range(3)]
            ap = add([c for p in pos for c in p], "f", len(pos), 5126, "VEC3", 34962, (mn, mx))
            an = add([c for p in nrm for c in p], "f", len(nrm), 5126, "VEC3", 34962)
            au = add([c for p in uv for c in p], "f", len(uv), 5126, "VEC2", 34962)
            ai = add(idx, "H", len(idx), 5123, "SCALAR", 34963)
            meshes.append({"name": n["name"] + "_mesh", "primitives": [{
                "attributes": {"POSITION": ap, "NORMAL": an, "TEXCOORD_0": au},
                "indices": ai, "material": textures.index(texname)}]})
            nd["mesh"] = len(meshes) - 1
        nodes.append(nd)
    names = [n["name"] for n in SCENE]
    for i, n in enumerate(SCENE):
        kids = [j for j, m in enumerate(SCENE) if m["parent"] == n["name"]]
        if kids:
            nodes[i]["children"] = kids

    frames = int(LOOP * FPS) + 1
    times = [i / FPS for i in range(frames)]
    at = add(times, "f", frames, 5126, "SCALAR", None, ([0.0], [times[-1]]))
    samplers, channels = [], []
    for name in ANIMATED:
        poses = [animate(name, t) for t in times]
        for path, k, typ in (("translation", 0, "VEC3"), ("rotation", 1, "VEC4"), ("scale", 2, "VEC3")):
            ao = add([c for p in poses for c in p[k]], "f", frames, 5126, typ)
            samplers.append({"input": at, "output": ao, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": names.index(name), "path": path}})

    gltf = {
        "asset": {"version": "2.0", "generator": "heivoli make_banner.py (appareil photo)"},
        "scene": 0,
        "scenes": [{"name": "Banner", "nodes": [0]}],
        "nodes": nodes, "meshes": meshes, "materials": materials, "textures": tex,
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": images, "accessors": accessors, "bufferViews": views,
        "buffers": [{"uri": "banner.bin", "byteLength": len(blob)}],
        "animations": [{"name": "Loop", "samplers": samplers, "channels": channels}],
    }
    with open(os.path.join(out_dir, "banner.bin"), "wb") as f:
        f.write(blob)
    with open(os.path.join(out_dir, "banner.gltf"), "w") as f:
        json.dump(gltf, f, indent=1)
    return gltf


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--camera", help="banner-camera.gltf de pycgfx (caméra du menu HOME)")
    ap.add_argument("--out", default=HERE)
    a = ap.parse_args()
    g = write_gltf(a.out, a.camera)
    tris = sum(accessor["count"] for accessor in g["accessors"] if accessor.get("componentType") == 5123) // 3
    print("banner.gltf : %d nœuds, %d maillages, ~%d triangles, boucle %.1f s"
          % (len(g["nodes"]), len(g["meshes"]), tris, LOOP))
