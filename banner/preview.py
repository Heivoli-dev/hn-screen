#!/usr/bin/env python3
"""Aperçu logiciel de banner.gltf (rendu approximatif, pas celui du menu HOME)."""
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_banner as mb  # noqa: E402

W, H = 400, 240


def mat_trs(t, r, s):
    x, y, z, w = r
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M = np.eye(4)
    M[:3, :3] = R * np.array(s)
    M[:3, 3] = t
    return M


def render(g, blob, t, texcache):
    nodes = g["nodes"]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    local = []
    for n in nodes:
        pose = mb.animate(n["name"], t)
        tt, rr, ss = pose if pose else (n["translation"], n["rotation"], n["scale"])
        local.append(mat_trs(tt, rr, ss))
    world = [None] * len(nodes)

    def wm(i):
        if world[i] is None:
            world[i] = (wm(parent[i]) @ local[i]) if i in parent else local[i]
        return world[i]

    yfov = math.radians(30)
    f = 1 / math.tan(yfov / 2)
    img = np.zeros((H, W, 3))
    yy = np.linspace(0, 1, H)[:, None]
    img[:] = (np.array([0.96, 0.95, 1.0]) * (1 - yy) + np.array([0.86, 0.93, 1.0]) * yy)[:, None, :] if False else 0
    for y in range(H):
        k = y / H
        img[y, :] = (0.97 - 0.06 * k, 0.96 - 0.02 * k, 1.0)
    zbuf = np.full((H, W), np.inf)
    light = np.array([0.3, 0.8, 0.6])
    light /= np.linalg.norm(light)

    def acc(i):
        a = g["accessors"][i]
        v = g["bufferViews"][a["bufferView"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        fmt = "f" if a["componentType"] == 5126 else "H"
        data = struct.unpack_from("<%d%s" % (a["count"] * n, fmt), blob, v["byteOffset"])
        return np.array(data).reshape(a["count"], n)

    for i, n in enumerate(nodes):
        if "mesh" not in n:
            continue
        prim = g["meshes"][n["mesh"]]["primitives"][0]
        P = acc(prim["attributes"]["POSITION"])
        N = acc(prim["attributes"]["NORMAL"])
        U = acc(prim["attributes"]["TEXCOORD_0"])
        I = acc(prim["indices"]).astype(int).ravel()
        mat = g["materials"][prim["material"]]
        texname = g["images"][mat["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["uri"]
        tex = texcache.setdefault(texname, np.asarray(Image.open(os.path.join(HERE, texname)).convert("RGB")) / 255.0)
        M = wm(i)
        Pw = (M @ np.c_[P, np.ones(len(P))].T).T[:, :3]
        Nw = (M[:3, :3] @ N.T).T
        Nw /= np.linalg.norm(Nw, axis=1, keepdims=True) + 1e-9
        z = -Pw[:, 2] + 44.786  # caméra du menu HOME : (0, 1, 44.786)
        sx = (Pw[:, 0] / z * f * (H / 2)) + W / 2
        sy = H / 2 - ((Pw[:, 1] - 1.0) / z * f * (H / 2))
        for k in range(0, len(I), 3):
            a, b, c = I[k:k + 3]
            nrm = Nw[a]
            if not mat.get("doubleSided") and nrm[2] < -0.05 and False:
                continue
            xs, ys = np.array([sx[a], sx[b], sx[c]]), np.array([sy[a], sy[b], sy[c]])
            x0, x1 = int(max(0, xs.min())), int(min(W - 1, xs.max() + 1))
            y0, y1 = int(max(0, ys.min())), int(min(H - 1, ys.max() + 1))
            if x0 > x1 or y0 > y1:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(d) < 1e-9:
                continue
            l0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
            l1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
            l2 = 1 - l0 - l1
            inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            if not inside.any():
                continue
            zz = l0 * z[a] + l1 * z[b] + l2 * z[c]
            sub = zbuf[y0:y1 + 1, x0:x1 + 1]
            m = inside & (zz < sub)
            if not m.any():
                continue
            uu = l0 * U[a, 0] + l1 * U[b, 0] + l2 * U[c, 0]
            vv = l0 * U[a, 1] + l1 * U[b, 1] + l2 * U[c, 1]
            th, tw = tex.shape[:2]
            col = tex[np.clip((vv * th).astype(int), 0, th - 1), np.clip((uu * tw).astype(int), 0, tw - 1)]
            shade = 0.72 + 0.28 * max(0.0, float(abs(nrm @ light)))
            sub[m] = zz[m]
            img[y0:y1 + 1, x0:x1 + 1][m] = col[m] * shade
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))


if __name__ == "__main__":
    g = mb.write_gltf(HERE, None)
    blob = open(os.path.join(HERE, "banner.bin"), "rb").read()
    cache = {}
    frames = [render(g, blob, i / 10.0, cache) for i in range(int(mb.LOOP * 10))]
    big = [f.resize((W * 2, H * 2), Image.NEAREST) for f in frames]
    big[0].save(os.path.join(HERE, "preview.gif"), save_all=True, append_images=big[1:], duration=100, loop=0)
    strip = Image.new("RGB", (W * 3 + 20, H), (255, 255, 255))
    for k, t in enumerate((8, 22, 34)):
        strip.paste(frames[t], (k * (W + 10), 0))
    strip.save(os.path.join(HERE, "preview_strip.png"))
    print("ok")
