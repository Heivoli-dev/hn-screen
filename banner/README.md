# Bannière 3D « l'appareil photo vivant » — appli de captures

Un appareil photo rond et brillant (crème, bande corail, objectif bleuté, flash jaune) qui
tourne doucement. Au « clic » : le déclencheur s'enfonce, l'appareil s'écrase un peu, le flash
éclate et une photo sort par le dessus. Boucle de 5 s + jingle (clic-clac et 4 notes).

L'ancien concept « diorama » (3DS + QR code + téléphone) reste dans `make_banner_diorama.py`.

- `import_model.py` : importe le modèle 3DS (`model/`), le simplifie (~5 200 triangles) et le recolore
- `make_banner.py` : génère la scène et l'animation (`banner.gltf` + `banner.bin`)
- `textures/` : textures de la scène (petites, puissances de 2)
- `jingle.wav` : son de la bannière (2,2 s, la limite est 3 s)
- `preview.py` : aperçu approximatif (`preview.gif`), pas le rendu exact du menu HOME
- `build-banner.sh` : sur Mac, convertit avec pycgfx et produit `banner.bnr`

La bannière ne s'affiche qu'avec une appli installée en CIA dont le SMDH a le drapeau
`extendedbanner`.

## Crédits

Modèle 3D « Nintendo 3DS » par **no o** (https://sketchfab.com/nonobourdio),
source : https://sketchfab.com/3d-models/nintendo-3ds-adf64efa517449678c369dc8fa3f05dd,
licence **CC-BY 4.0** (http://creativecommons.org/licenses/by/4.0/).
Modifications : maillage simplifié, couleurs changées, textures d'écran remplacées.
