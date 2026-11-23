#pragma once
#include <3ds.h>
#include <stdbool.h>

typedef enum {
    SHARE_IDLE,
    SHARE_SENDING,   // envoi sur heivoli-network.fr
    SHARE_ONLINE,    // lien du site prêt
    SHARE_LOCAL,     // serveur Wi-Fi local prêt
    SHARE_NO_WIFI,
    SHARE_ERROR
} ShareState;

// Prépare le réseau (sockets + curl) une seule fois.
bool net_init(void);

// Prépare les images (haut, bas, les deux) puis lance l'envoi sur le site.
// `top` et `bot` : chemins des BMP (NULL si absent).
bool share_start(const char *top, const char *bot, bool localOnly);
// Passe au partage en Wi-Fi local (garde les images préparées).
void share_go_local(void);
// À appeler à chaque image : bascule sur le Wi-Fi local si l'envoi a échoué.
void share_poll_fallback(void);
void share_stop(void);
void share_exit(void);

ShareState share_state(void);
// Adresse à mettre dans le QR code, et texte court à afficher.
const char *share_url(void);
const char *share_label(void);
const char *share_error(void);
