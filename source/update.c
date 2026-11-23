// Mises à jour automatiques : la branche « builds » du dépôt GitHub contient
// version.txt (« build=N » et « version=X.Y.Z ») et le dernier hn-screen.cia.
// Si N est plus grand que le numéro de cette build, l'appli propose de s'installer
// elle-même (comme FBI), puis se relance.
#include "update.h"
#include "config.h"
#include "share.h"
#include <3ds.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_BUILD
#define APP_BUILD 0
#endif

static struct {
    volatile UpdateState state;
    volatile bool cancel;
    Thread thread;
    int remoteBuild;
    char remoteVersion[24];
    char error[128];
    volatile u64 total, done;
    volatile bool checked;
    u64 lastTry;
    Handle cia;
    u64 written;
} U;

UpdateState update_state(void) { return U.state; }
int update_remote_build(void) { return U.remoteBuild; }
const char *update_remote_version(void) { return U.remoteVersion; }
const char *update_error(void) { return U.error; }
float update_progress(void) { return U.total ? (float)U.done / U.total : 0; }

static void join(void)
{
    if (!U.thread) return;
    threadJoin(U.thread, U64_MAX);
    threadFree(U.thread);
    U.thread = NULL;
}

static void common(CURL *c, const char *url)
{
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
}

typedef struct {
    char buf[512];
    size_t len;
} Small;

static size_t small_write(void *p, size_t sz, size_t nm, void *u)
{
    Small *s = u;
    size_t n = sz * nm;
    if (s->len + n >= sizeof(s->buf)) return 0;
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = 0;
    return n;
}

static void check_thread(void *arg)
{
    (void)arg;
    CURL *c = curl_easy_init();
    if (!c) {
        U.state = UPD_IDLE;
        return;
    }
    Small s = {0};
    common(c, UPDATE_MANIFEST);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, small_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    int build = 0;
    char ver[24] = "";
    if (rc == CURLE_OK) {
        char *p = strstr(s.buf, "build=");
        if (p) build = atoi(p + 6);
        p = strstr(s.buf, "version=");
        if (p) sscanf(p + 8, "%23[0-9.]", ver);
    }
    if (rc != CURLE_OK) snprintf(U.error, sizeof(U.error), "%s", curl_easy_strerror(rc));
    else if (!build) snprintf(U.error, sizeof(U.error), "Réponse illisible");
    else U.error[0] = 0;
    U.checked = rc == CURLE_OK && build > 0;
    U.remoteBuild = build;
    snprintf(U.remoteVersion, sizeof(U.remoteVersion), "%s", ver[0] ? ver : "?");
    U.state = build > APP_BUILD ? UPD_AVAILABLE : UPD_IDLE;
}

void update_check_async(void)
{
    if (U.state != UPD_IDLE || U.checked) return;
    U.lastTry = osGetTime();
    join();
    u32 wifi = 0;
    if (R_FAILED(ACU_GetWifiStatus(&wifi)) || !wifi) {
        snprintf(U.error, sizeof(U.error), "Pas de Wi-Fi");
        return;
    }
    if (!net_init()) {
        snprintf(U.error, sizeof(U.error), "Réseau indisponible");
        return;
    }
    U.state = UPD_CHECKING;
    U.thread = threadCreate(check_thread, NULL, 512 * 1024, 0x31, -1, false);
    if (!U.thread) U.state = UPD_IDLE;
}

static size_t cia_write(void *p, size_t sz, size_t nm, void *u)
{
    (void)u;
    size_t n = sz * nm;
    u32 w = 0;
    if (R_FAILED(FSFILE_Write(U.cia, &w, U.written, p, n, 0)) || w != n) return 0;
    U.written += n;
    U.done = U.written;
    return n;
}

static int on_progress(void *u, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
    (void)u; (void)dn; (void)ut; (void)un;
    if (dt > 0) U.total = dt;
    return U.cancel ? 1 : 0;
}

static void install_thread(void *arg)
{
    (void)arg;
    U.written = U.done = U.total = 0;
    if (R_FAILED(AM_StartCiaInstall(MEDIATYPE_SD, &U.cia))) {
        snprintf(U.error, sizeof(U.error), "Impossible de démarrer l'installation");
        U.state = UPD_ERROR;
        return;
    }
    CURL *c = curl_easy_init();
    CURLcode rc = CURLE_FAILED_INIT;
    if (c) {
        common(c, UPDATE_CIA);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 128 * 1024L);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, cia_write);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
        rc = curl_easy_perform(c);
        curl_easy_cleanup(c);
    }
    if (rc != CURLE_OK || U.written < 0x2020) {
        AM_CancelCIAInstall(U.cia);
        snprintf(U.error, sizeof(U.error), "Téléchargement interrompu : %s", curl_easy_strerror(rc));
        U.state = UPD_ERROR;
        return;
    }
    Result r = AM_FinishCiaInstall(U.cia);
    if (R_FAILED(r)) {
        snprintf(U.error, sizeof(U.error), "Installation refusée (0x%08lX)", (unsigned long)r);
        U.state = UPD_ERROR;
        return;
    }
    U.state = UPD_DONE;
}

void update_install_async(void)
{
    if (U.state != UPD_AVAILABLE && U.state != UPD_ERROR) return;
    join();
    if (!net_init()) {
        snprintf(U.error, sizeof(U.error), "Réseau indisponible");
        U.state = UPD_ERROR;
        return;
    }
    U.cancel = false;
    U.state = UPD_INSTALLING;
    U.thread = threadCreate(install_thread, NULL, 512 * 1024, 0x31, -1, false);
    if (!U.thread) {
        snprintf(U.error, sizeof(U.error), "Mémoire insuffisante");
        U.state = UPD_ERROR;
    }
}

// À appeler à chaque image : réessaie toutes les 10 s tant que la vérification n'a pas abouti.
void update_poll(void)
{
    if (!U.checked && U.state == UPD_IDLE && osGetTime() - U.lastTry > 10000) update_check_async();
}

const char *update_status_text(void)
{
    static char buf[160];
    switch (U.state) {
    case UPD_CHECKING: return "Recherche de mise à jour…";
    case UPD_AVAILABLE: snprintf(buf, sizeof(buf), "Mise à jour disponible (build %d)", U.remoteBuild); return buf;
    case UPD_INSTALLING: return "Installation de la mise à jour…";
    default: break;
    }
    if (U.checked) return U.remoteBuild > APP_BUILD ? "Mise à jour reportée" : "À jour";
    if (U.error[0]) {
        snprintf(buf, sizeof(buf), "Mises à jour : %s", U.error);
        return buf;
    }
    return "";
}

void update_dismiss(void)
{
    if (U.state == UPD_AVAILABLE || U.state == UPD_ERROR) U.state = UPD_IDLE;
}

void update_exit(void)
{
    U.cancel = true;
    join();
}

void update_relaunch(void)
{
    static u8 param[0x300], hmac[0x20];
    memset(param, 0, sizeof(param));
    memset(hmac, 0, sizeof(hmac));
    APT_PrepareToDoApplicationJump(0, APP_TITLE_ID, MEDIATYPE_SD);
    APT_DoApplicationJump(param, sizeof(param), hmac);
}
