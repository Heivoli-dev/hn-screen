#!/bin/bash
# Fabrique la bannière 3D (banner.bnr) de l'appli de captures, sur Mac.
#   cd hn-screen/banner && bash build-banner.sh
#
# Étapes : récupère pycgfx (convertisseur glTF -> CGFX, commit figé),
# cadre la scène sur la caméra du menu HOME, convertit, puis emballe avec bannertool.
set -e
cd "$(dirname "$0")"
PYCGFX_COMMIT=1f78850086f3a77c41e07162e842f97a5bf3c18a
TOOLS="$PWD/.tools"
export PATH="$PWD/../tools/bin:$PATH"

if [ ! -d "$TOOLS/pycgfx" ]; then
  echo "Téléchargement de pycgfx..."
  git clone -q https://github.com/skyfloogle/pycgfx.git "$TOOLS/pycgfx"
  git -C "$TOOLS/pycgfx" checkout -q "$PYCGFX_COMMIT"
fi
if [ ! -x "$TOOLS/venv/bin/python" ]; then
  echo "Préparation de Python (gltflib, Pillow)..."
  python3 -m venv "$TOOLS/venv"
  "$TOOLS/venv/bin/pip" install -q gltflib pillow numpy
fi
PY="$TOOLS/venv/bin/python"

if [ -f model/scene.gltf ] && [ ! -f model/console.json ]; then
  echo "0/3 Import du modèle 3DS (simplification + couleurs)"
  "$PY" -m pip install -q numpy >/dev/null
  "$PY" import_model.py
fi
echo "1/3 Scène glTF cadrée sur la caméra du menu HOME"
"$PY" make_banner.py --camera "$TOOLS/pycgfx/banner-camera.gltf"

echo "2/3 Conversion en CGFX"
rm -f ./*.cgfx
"$PY" "$TOOLS/pycgfx/main.py" banner.gltf
CGFX=$(ls -t ./*.cgfx 2>/dev/null | head -1)
if [ -z "$CGFX" ]; then echo "pycgfx n'a pas produit de .cgfx"; exit 1; fi
[ "$CGFX" != "./banner.cgfx" ] && mv "$CGFX" banner.cgfx
SIZE=$(wc -c < banner.cgfx)
echo "   banner.cgfx : $((SIZE / 1024)) Ko (limite de la console : 512 Ko)"
if [ "$SIZE" -gt 524288 ]; then echo "Trop gros pour la 3DS."; exit 1; fi

echo "3/3 Bannière"
if ! command -v bannertool >/dev/null; then
  echo "bannertool introuvable : mets-le dans hn-screen/tools/bin/ puis relance."
  echo "(banner.cgfx est prêt, il ne manque que l'emballage.)"
  exit 1
fi
bannertool makebanner -ci banner.cgfx -a jingle.wav -o banner.bnr
echo "OK : banner.bnr (bannière 3D + jingle)."
echo "Pour l'icône, l'appli devra être créée avec le drapeau extendedbanner :"
echo "  bannertool makesmdh ... -f visible,allow3d,recordusage,extendedbanner"
