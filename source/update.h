#pragma once
#include <stdbool.h>

typedef enum {
    UPD_IDLE,        // rien à signaler
    UPD_CHECKING,    // vérification en cours (silencieuse)
    UPD_AVAILABLE,   // une nouvelle version attend
    UPD_INSTALLING,  // téléchargement + installation
    UPD_DONE,        // installée : redémarrage
    UPD_ERROR
} UpdateState;

void update_check_async(void);  // au lancement, si le Wi-Fi est là
void update_install_async(void);
void update_dismiss(void);
void update_poll(void);
const char *update_status_text(void); // ligne d'état (version, erreur…)
void update_exit(void);

UpdateState update_state(void);
int update_remote_build(void);
const char *update_remote_version(void);
float update_progress(void);   // 0..1 pendant l'installation
const char *update_error(void);
// Relance l'appli fraîchement installée (à appeler quand l'état est UPD_DONE).
void update_relaunch(void);
