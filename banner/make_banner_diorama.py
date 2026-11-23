#!/usr/bin/env python3
"""Bannière 3D « diorama » de l'appli de captures (menu HOME de la 3DS).

Une petite 3DS ouverte : une capture sur l'écran du haut, un QR code sur l'écran
du bas qui s'envole vers un téléphone flottant, qui la reçoit. Boucle de 4 s.

Écrit banner.gltf (+ banner.bin) à côté de ce script, avec la bibliothèque
standard uniquement. Règles suivies pour ne pas figer le menu HOME : animation
rigide (translation / rotation / échelle de nœuds), nœuds nommés, pas de
transparence, textures petites (puissances de 2), scène cadrée dans la zone sûre.

Usage :
    python3 make_banner.py                         # caméra par défaut
    python3 make_banner.py --camera banner-camera.gltf   # caméra du menu HOME (pycgfx)
"""
import argparse
import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
FPS = 15
LOOP = 4.0

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
    # v' = q v q*
    ix = w * vx + y * vz - z * vy
    iy = w * vy + z * vx - x * vz
    iz = w * vz + x * vy - y * vx
    iw = -x * vx - y * vy - z * vz
    return (ix * w + iw * -x + iy * -z - iz * -y,
            iy * w + iw * -y + iz * -x - ix * -z,
            iz * w + iw * -z + ix * -y - iy * -x)


# ---------------------------------------------------------------------------
# Géométrie

PALETTE = open(os.path.join(HERE, "textures", "palette.txt")).read().split()


def swatch_uv(name):
    i = PALETTE.index(name)
    return (((i % 8) * 8 + 4) / 64.0, ((i // 8) * 8 + 4) / 64.0)


def box(w, h, d, color, center=(0, 0, 0)):
    """Pavé coloré (uv au centre d'une case de la palette)."""
    cx, cy, cz = center
    x, y, z = w / 2, h / 2, d / 2
    u, v = swatch_uv(color)
    faces = [  # normale, 4 coins
        ((0, 0, 1), [(-x, -y, z), (x, -y, z), (x, y, z), (-x, y, z)]),
        ((0, 0, -1), [(x, -y, -z), (-x, -y, -z), (-x, y, -z), (x, y, -z)]),
        ((1, 0, 0), [(x, -y, z), (x, -y, -z), (x, y, -z), (x, y, z)]),
        ((-1, 0, 0), [(-x, -y, -z), (-x, -y, z), (-x, y, z), (-x, y, -z)]),
        ((0, 1, 0), [(-x, y, z), (x, y, z), (x, y, -z), (-x, y, -z)]),
        ((0, -1, 0), [(-x, -y, -z), (x, -y, -z), (x, -y, z), (-x, -y, z)]),
    ]
    pos, nrm, uv, idx = [], [], [], []
    for n, quad in faces:
        b = len(pos)
        for p in quad:
            pos.append((p[0] + cx, p[1] + cy, p[2] + cz))
            nrm.append(n)
            uv.append((u, v))
        idx += [b, b + 1, b + 2, b, b + 2, b + 3]
    return pos, nrm, uv, idx


def quad(w, h, normal="z", center=(0, 0, 0)):
    """Rectangle texturé entier. normal='z' (face +Z) ou 'y' (face +Y)."""
    cx, cy, cz = center
    x, y = w / 2, h / 2
    if normal == "z":
        pts = [(-x, -y, 0), (x, -y, 0), (x, y, 0), (-x, y, 0)]
        n = (0, 0, 1)
    else:  # posé à plat, le haut de la texture vers -Z (vers le fond)
        pts = [(-x, 0, y), (x, 0, y), (x, 0, -y), (-x, 0, -y)]
        n = (0, 1, 0)
    pos = [(p[0] + cx, p[1] + cy, p[2] + cz) for p in pts]
    uv = [(0, 1), (1, 1), (1, 0), (0, 0)]
    return pos, [n] * 4, uv, [0, 1, 2, 0, 2, 3]


# ---------------------------------------------------------------------------
# Scène : (nom, parent, mesh(mesh_fn, texture) ou None, T, R, S)

SCENE = []


def node(name, parent, mesh=None, t=(0, 0, 0), r=(0, 0, 0, 1), s=(1, 1, 1)):
    SCENE.append({"name": name, "parent": parent, "mesh": mesh, "t": t, "r": r, "s": s})


MODEL_JSON = os.path.join(HERE, "model", "console.json")
QR_START = [0.0, 1.2, 0.2]   # point de départ du QR (centre de l'écran du bas), mis à jour par le modèle


def add_box_console():
    """Console simplifiée en pavés (si le modèle 3D n'est pas là)."""
    node("Base", "Scene", (box(13, 1.0, 7.4, "shell"), "palette.png"), t=(0, 0.5, 0))
    node("BottomScreen", "Scene", (quad(6.6, 5.0, "y"), "screen_bottom.png"), t=(0, 1.02, 0.2))
    node("Pad", "Scene", (box(1.6, 0.35, 1.6, "pad"), "palette.png"), t=(-4.9, 1.15, 0.6))
    node("BtnA", "Scene", (box(0.7, 0.35, 0.7, "btn_a"), "palette.png"), t=(5.6, 1.15, 0.6))
    node("BtnB", "Scene", (box(0.7, 0.35, 0.7, "btn_b"), "palette.png"), t=(4.9, 1.15, 1.3))
    node("BtnX", "Scene", (box(0.7, 0.35, 0.7, "btn_x"), "palette.png"), t=(4.9, 1.15, -0.1))
    node("BtnY", "Scene", (box(0.7, 0.35, 0.7, "btn_y"), "palette.png"), t=(4.2, 1.15, 0.6))
    node("Hinge", "Scene", (box(13, 1.0, 1.0, "hinge"), "palette.png"), t=(0, 1.0, -3.7))
    node("Lid", "Scene", t=(0, 1.3, -3.9), r=qaxis((1, 0, 0), -18))
    node("LidShell", "Lid", (box(13, 7.6, 0.7, "shell"), "palette.png"), t=(0, 3.8, 0))
    node("TopBezel", "Lid", (box(10.6, 6.4, 0.1, "bezel"), "palette.png"), t=(0, 4.0, 0.38))
    node("TopScreen", "Lid", (quad(9.6, 5.6, "z"), "screen_top.png"), t=(0, 4.0, 0.45))
    node("LidLogo", "Lid", (quad(1.0, 1.0, "z"), "lid_logo.png"), t=(-5.8, 0.75, 0.38))


def add_model_console():
    """Modèle 3DS (Sketchfab, CC-BY 4.0, voir import_model.py), réorienté et mis à l'échelle."""
    d = json.load(open(MODEL_JSON))
    lo, hi = d["bbox"]
    width = hi[2] - lo[2]                      # la largeur du modèle est sur son axe Z
    k = 13.0 / width                           # même largeur que l'ancienne console
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    # modèle -> scène : X = z, Y = y, Z = -x (face avant vers la caméra), base posée sur y = 0
    def conv(p):
        return ((p[2] - cz) * k, (p[1] - lo[1]) * k, -(p[0] - cx) * k)

    def convn(n):
        return (n[2], n[1], -n[0])

    node("Console", "Scene")
    for i, part in enumerate(d["parts"]):
        pos = [conv(p) for p in part["P"]]
        nrm = [convn(n) for n in part["N"]]
        if part["color"] == "SCREENS":
            mesh = ((pos, nrm, [tuple(u) for u in part["U"]], part["I"]), "screens.png")
        else:
            u, v = swatch_uv(part["color"])
            mesh = ((pos, nrm, [(u, v)] * len(pos), part["I"]), "palette.png")
        node("Console_%02d" % i, "Console", mesh)
    # départ du QR : juste au-dessus du centre de l'écran du bas (la plus basse des deux zones d'écran)
    bottom = min(d["screens"], key=lambda sc: sc["center"][1])
    c = conv(bottom["center"])
    QR_START[:] = [c[0], c[1] + 0.3, c[2]]


def build_scene():
    node("Diorama", None)                       # racine, cadrée sur la caméra
    node("Scene", "Diorama", t=(-3.0, -5.0, 0))  # recentre la scène
    # --- la console ---
    if os.path.exists(MODEL_JSON):
        add_model_console()
    else:
        add_box_console()
    # --- le téléphone qui reçoit la capture ---
    node("Phone", "Scene", t=(11.0, 6.0, 1.0), r=qaxis((0, 1, 0), -22))
    node("PhoneBody", "Phone", (box(3.6, 7.0, 0.5, "phone_edge"), "palette.png"))
    node("PhoneScreen", "Phone", (quad(3.2, 6.4, "z"), "screen_phone.png"), t=(0, 0, 0.26))
    # --- le QR code qui s'envole ---
    node("QRCard", "Scene", (quad(2.4, 2.4, "z"), "qr.png"), t=tuple(QR_START), s=(0.01, 0.01, 0.01))


def smooth(a, b, k):
    k = max(0.0, min(1.0, k))
    k = k * k * (3 - 2 * k)
    return a + (b - a) * k


def animate(name, t):
    """Pose (T, R, S) d'un nœud animé à l'instant t (secondes, 0..LOOP)."""
    if name == "Phone":
        bob = math.sin(t / LOOP * 2 * math.pi) * 0.35
        r = qmul(qaxis((0, 1, 0), -22), qaxis((0, 0, 1), math.sin(t / LOOP * 2 * math.pi) * 3))
        # petit « saut » de joie quand le QR arrive
        hop = 0.5 * math.sin(math.pi * (t - 2.3) / 0.4) if 2.3 <= t <= 2.7 else 0.0
        return (11.0, 6.0 + bob + hop, 1.0), r, (1, 1, 1)
    if name == "QRCard":
        start, end = tuple(QR_START), (11.0, 6.0, 1.6)
        if t < 0.3:  # sort de l'écran du bas
            k = t / 0.3
            return (start[0], smooth(start[1], start[1] + 1.0, k), start[2]), qaxis((1, 0, 0), -90 * (1 - k)), (k, k, k)
        if t < 2.3:  # vole en arc en tournant
            k = (t - 0.3) / 2.0
            e = smooth(0, 1, k)
            x = start[0] + (end[0] - start[0]) * e
            z = start[2] + (end[2] - start[2]) * e
            y = smooth(start[1] + 1.0, end[1], k) + 3.5 * math.sin(math.pi * e)
            return (x, y, z), qaxis((0, 1, 0), 720 * e), (1, 1, 1)
        if t < 2.5:  # entre dans le téléphone
            k = 1 - (t - 2.3) / 0.2
            k = max(k, 0.01)
            return end, (0, 0, 0, 1), (k, k, k)
        return end, (0, 0, 0, 1), (0.01, 0.01, 0.01)  # caché jusqu'à la boucle suivante
    return None


ANIMATED = ["Phone", "QRCard"]

# ---------------------------------------------------------------------------
# Cadrage sur la caméra du menu HOME


def fit_to_camera(camera_path):
    """Place la racine devant la caméra, vue de trois quarts par le dessus."""
    cam_t, cam_r, yfov = (0.0, 0.0, 40.0), (0, 0, 0, 1), math.radians(30)
    if camera_path:
        g = json.load(open(camera_path))
        cams = g.get("cameras", [])
        for n in g.get("nodes", []):
            if "camera" in n:
                cam_t = tuple(n.get("translation", (0, 0, 0)))
                cam_r = tuple(n.get("rotation", (0, 0, 0, 1)))
                p = cams[n["camera"]].get("perspective", {})
                yfov = p.get("yfov", yfov)
                break
    fwd = qrot(cam_r, (0, 0, -1))
    radius = 11.5  # demi-taille de la scène (unités du modèle)
    dist = radius / math.tan(yfov / 2) * 1.08  # petite marge (zone sûre)
    center = tuple(cam_t[i] + fwd[i] * dist for i in range(3))
    # Orientation : la scène fait face à la caméra, inclinée vers elle et tournée de 3/4.
    tilt = qmul(qaxis((1, 0, 0), 24), qaxis((0, 1, 0), -16))
    rot = qmul(cam_r, tilt)
    return center, rot, (1, 1, 1)


# ---------------------------------------------------------------------------
# Écriture du glTF


def write_gltf(out_dir, camera_path=None):
    SCENE.clear()
    build_scene()
    root = next(n for n in SCENE if n["name"] == "Diorama")
    root["t"], root["r"], root["s"] = fit_to_camera(camera_path)

    blob = bytearray()
    views, accessors = [], []

    def add(data, fmt, count, comp, typ, target=None, minmax=None):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob)
        blob.extend(struct.pack("<" + fmt * 1, *data) if False else b"".join(struct.pack("<" + fmt, d) for d in data))
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
    materials = []
    for t in textures:
        m = {"name": "mat_" + t.split(".")[0],
             "pbrMetallicRoughness": {"baseColorTexture": {"index": textures.index(t)},
                                      "metallicFactor": 0.0, "roughnessFactor": 1.0}}
        if t == "qr.png":
            m["doubleSided"] = True
        materials.append(m)

    meshes, nodes = [], []
    for n in SCENE:
        nd = {"name": n["name"], "translation": list(n["t"]), "rotation": list(n["r"]), "scale": list(n["s"])}
        if n["mesh"]:
            (pos, nrm, uv, idx), texname = n["mesh"]
            flat = [c for p in pos for c in p]
            mn = [min(p[i] for p in pos) for i in range(3)]
            mx = [max(p[i] for p in pos) for i in range(3)]
            ap = add(flat, "f", len(pos), 5126, "VEC3", 34962, (mn, mx))
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

    # Animation : échantillonnée à FPS, boucle de LOOP secondes
    frames = int(LOOP * FPS) + 1
    times = [i / FPS for i in range(frames)]
    at = add(times, "f", frames, 5126, "SCALAR", None, ([0.0], [times[-1]]))
    samplers, channels = [], []
    for name in ANIMATED:
        poses = [animate(name, t) for t in times]
        for path, k, typ in (("translation", 0, "VEC3"), ("rotation", 1, "VEC4"), ("scale", 2, "VEC3")):
            data = [c for p in poses for c in p[k]]
            ao = add(data, "f", frames, 5126, typ)
            samplers.append({"input": at, "output": ao, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": names.index(name), "path": path}})

    gltf = {
        "asset": {"version": "2.0", "generator": "heivoli make_banner.py"},
        "scene": 0,
        "scenes": [{"name": "Banner", "nodes": [names.index("Diorama")]}],
        "nodes": nodes,
        "meshes": meshes,
        "materials": materials,
        "textures": tex,
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": images,
        "accessors": accessors,
        "bufferViews": views,
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
    print("banner.gltf : %d nœuds, %d maillages, %d textures, boucle %.1f s"
          % (len(g["nodes"]), len(g["meshes"]), len(g["images"]), LOOP))
