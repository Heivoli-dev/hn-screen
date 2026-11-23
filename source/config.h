#pragma once

#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif

#define APP_NAME "Hn screen !"
#define USER_AGENT "HnScreen/" APP_VERSION " (Nintendo 3DS)"

// Captures prises avec le menu Rosalina (L + Bas + Select).
#define ROSALINA_DIR "sdmc:/luma/screenshots"

// Données de l'appli sur la carte SD.
#define DATA_DIR  "sdmc:/3ds/hn-screen"
#define CACHE_DIR DATA_DIR "/cache"
#define TAGS_PATH DATA_DIR "/jeux.txt"
// Captures du futur plugin Luma (raccourci L + R + Bas), rangées par jeu.
#define PLUGIN_DIR DATA_DIR "/captures"

// Partage par le site heivoli-network.fr (Worker Cloudflare « hn-screen »).
#define UPLOAD_URL "https://hn-screen.heivoli-discord-auth.workers.dev/upload"
#define SHARE_URL  "https://heivoli-network.fr/s/?"
#define SHARE_HOST "heivoli-network.fr/s/?"

// Partage de secours en Wi-Fi local.
#define LOCAL_PORT 8080

#define CA_BUNDLE "romfs:/cacert.pem"

// Mises à jour automatiques (branche « builds » remplie par GitHub Actions).
#define UPDATE_BASE     "https://raw.githubusercontent.com/Heivoli-dev/hn-screen/builds"
#define UPDATE_MANIFEST UPDATE_BASE "/version.txt"
#define UPDATE_CIA      UPDATE_BASE "/hn-screen.cia"
#define APP_TITLE_ID    0x000400000FE5C000ULL
