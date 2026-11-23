#!/usr/bin/env python3
"""Importe le modèle 3DS (Sketchfab, CC-BY 4.0) dans la bannière.

Modèle : « Nintendo 3DS » par no o — https://sketchfab.com/nonobourdio
Source : https://sketchfab.com/3d-models/nintendo-3ds-adf64efa517449678c369dc8fa3f05dd
Licence : CC-BY 4.0 (http://creativecommons.org/licenses/by/4.0/) — crédit obligatoire.
Modifications : maillage simplifié, couleurs changées, écrans remplacés.

Lit model/scene.gltf + scene.bin, aplatit la hiérarchie, simplifie les pièces lourdes
(regroupement de sommets), recolore avec la palette pastel et écrit model/console.json.
"""
import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "model")

# Couleur de palette (textures/palette.txt) pour chaque matériau du modèle
MATERIALS = {
    "Body": "shell", "Grey": "hinge", "Plastic": "bezel", "Joystick": "pad",
    "Camera_1": "bezel", "Camera_2": "bezel", "Glass_border": "bezel",
    "Emission": "btn_x", "material_0": "shell_dark", "material": "SCREENS",
}
# Les 4 boutons d'action en couleurs (nom des pièces dans le modèle)
BUTTONS = {"Object_34": "btn_y", "Object_36": "btn_b", "Object_38": "btn_a", "Object_40": "btn_x"}
MAX_TRIS_PER_PART = 300     # au-delà, la pièce est simplifiée
CELL = 0.022                # taille de la grille de simplification (unités du modèle)


def load():
    g = json.load(open(os.path.join(SRC, "scene.gltf")))
    blob = open(os.path.join(SRC, "scene.bin"), "rb").read()

    def acc(i):
        a = g["accessors"][i]
        v = g["bufferViews"][a["bufferView"]]
        n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16}[a["componentType"]]
        stride = v.get("byteStride", n * np.dtype(dt).itemsize)
        off = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        out = np.empty((a["count"], n), dt)
        for k in range(a["count"]):
            out[k] = np.frombuffer(blob, dt, n, off + k * stride)
        return out.astype(np.float64) if dt == np.float32 else out.astype(np.int64)

    return g, acc


def node_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"]).reshape(4, 4).T
    M = np.eye(4)
    t = n.get("translation", [0, 0, 0])
    x, y, z, w = n.get("rotation", [0, 0, 0, 1])
    s = n.get("scale", [1, 1, 1])
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M[:3, :3] = R * np.array(s)
    M[:3, 3] = t
    return M


def simplify(P, N, U, I):
    """Regroupement de sommets sur une grille : réduit fortement les triangles."""
    keys = np.floor(P / CELL).astype(np.int64)
    # on sépare aussi selon l'orientation de la normale pour garder les arêtes vives
    nk = np.round(N * 1.5).astype(np.int64)
    k = np.concatenate([keys, nk], axis=1)
    uniq, inv = np.unique(k, axis=0, return_inverse=True)
    inv = inv.ravel()
    cnt = np.bincount(inv).astype(float)[:, None]
    P2 = np.zeros((len(uniq), 3))
    N2 = np.zeros((len(uniq), 3))
    U2 = np.zeros((len(uniq), 2))
    np.add.at(P2, inv, P)
    np.add.at(N2, inv, N)
    np.add.at(U2, inv, U)
    P2 /= cnt
    U2 /= cnt
    N2 /= np.linalg.norm(N2, axis=1, keepdims=True) + 1e-12
    T = inv[I.reshape(-1, 3)]
    ok = (T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])
    T = np.unique(np.sort(T[ok], axis=1), axis=0, return_index=True)[1]
    T = inv[I.reshape(-1, 3)][ok][np.sort(T)]
    return P2, N2, U2, T.ravel()


def main():
    g, acc = load()
    nodes = g["nodes"]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    world = {}

    def wm(i):
        if i not in world:
            world[i] = (wm(parent[i]) @ node_matrix(nodes[i])) if i in parent else node_matrix(nodes[i])
        return world[i]

    parts = []
    before = after = 0
    for i, n in enumerate(nodes):
        if "mesh" not in n:
            continue
        prim = g["meshes"][n["mesh"]]["primitives"][0]
        matname = g["materials"][prim["material"]]["name"]
        P = acc(prim["attributes"]["POSITION"])
        N = acc(prim["attributes"]["NORMAL"])
        U = acc(prim["attributes"]["TEXCOORD_0"]) if "TEXCOORD_0" in prim["attributes"] else np.zeros((len(P), 2))
        I = acc(prim["indices"]).ravel()
        M = wm(i)
        P = (M @ np.c_[P, np.ones(len(P))].T).T[:, :3]
        N = (np.linalg.inv(M[:3, :3]).T @ N.T).T
        N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
        if np.linalg.det(M[:3, :3]) < 0:  # miroir : on inverse l'ordre des sommets
            I = I.reshape(-1, 3)[:, ::-1].ravel()
        before += len(I) // 3
        if len(I) // 3 > MAX_TRIS_PER_PART and MATERIALS.get(matname) != "SCREENS":
            P, N, U, I = simplify(P, N, U, I)
        after += len(I) // 3
        parts.append({"name": n["name"], "material": matname, "color": BUTTONS.get(n["name"], MATERIALS.get(matname, "shell")),
                      "P": P, "N": N, "U": U, "I": I})

    allP = np.concatenate([p["P"] for p in parts])
    lo, hi = allP.min(0), allP.max(0)
    screens = [p for p in parts if p["color"] == "SCREENS"]
    out = {
        "credit": "Modèle « Nintendo 3DS » par no o (sketchfab.com/nonobourdio), CC-BY 4.0, modifié.",
        "bbox": [lo.tolist(), hi.tolist()],
        "screens": [{"name": p["name"], "center": p["P"].mean(0).tolist(),
                     "normal": p["N"].mean(0).tolist()} for p in screens],
        "parts": [{"name": p["name"], "color": p["color"],
                   "P": np.round(p["P"], 5).tolist(), "N": np.round(p["N"], 4).tolist(),
                   "U": np.round(p["U"], 5).tolist(), "I": p["I"].tolist()} for p in parts],
    }
    json.dump(out, open(os.path.join(SRC, "console.json"), "w"))
    print("triangles : %d -> %d ; pièces : %d ; taille : %s -> %s"
          % (before, after, len(parts), np.round(lo, 3), np.round(hi, 3)))
    for s in out["screens"]:
        print("écran", s["name"], np.round(s["center"], 3), np.round(s["normal"], 2))


if __name__ == "__main__":
    sys.exit(main())
