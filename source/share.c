// Partage d'une capture :
// 1) envoi sur heivoli-network.fr (Worker Cloudflare), le QR code ouvre la page du site ;
// 2) en secours, petit serveur HTTP sur la console (même Wi-Fi).
#include "share.h"
#include "config.h"
#include "img.h"
#include <arpa/inet.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SOC_ALIGN 0x1000
#define SOC_SIZE  0x100000

typedef struct {
    u8 *data;
    size_t len;
} Blob;

static struct {
    bool socReady;
    u32 *socBuf;
    volatile ShareState state;
    volatile bool stop;
    Thread thread;
    bool threadRunning;
    Blob top, bot, both;
    char url[128];
    char label[128];
    char error[128];
    int listenFd;
} S = {.listenFd = -1};

ShareState share_state(void) { return S.state; }
const char *share_url(void) { return S.url; }
const char *share_label(void) { return S.label; }
const char *share_error(void) { return S.error; }

bool net_init(void)
{
    if (S.socReady) return true;
    S.socBuf = memalign(SOC_ALIGN, SOC_SIZE);
    if (!S.socBuf) return false;
    if (R_FAILED(socInit(S.socBuf, SOC_SIZE))) {
        free(S.socBuf);
        S.socBuf = NULL;
        return false;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    S.socReady = true;
    return true;
}

static bool wifi_ok(void)
{
    u32 st = 0;
    return R_SUCCEEDED(ACU_GetWifiStatus(&st)) && st != 0;
}

static void blob_free(Blob *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
}

static void join_thread(void)
{
    if (!S.threadRunning) return;
    S.stop = true;
    threadJoin(S.thread, U64_MAX);
    threadFree(S.thread);
    S.threadRunning = false;
    S.stop = false;
}

// ---------------------------------------------------------------------------
// Serveur local
static void send_all(int fd, const void *data, size_t len)
{
    const u8 *p = data;
    while (len > 0) {
        ssize_t n = send(fd, p, len > 32768 ? 32768 : len, 0);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                svcSleepThread(2 * 1000 * 1000);
                continue;
            }
            return;
        }
        p += n;
        len -= n;
    }
}

static void respond(int fd, const char *type, const void *body, size_t len, const char *extra)
{
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\n%sConnection: close\r\nCache-Control: no-store\r\n\r\n",
                     type, (unsigned)len, extra ? extra : "");
    send_all(fd, head, n);
    send_all(fd, body, len);
}

static const char PAGE[] =
    "<!doctype html><html lang=\"fr\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Hn screen !</title>"
    "<style>body{margin:0;font-family:system-ui,sans-serif;background:repeating-linear-gradient(135deg,#f4f4f4 0 6px,#eaeaea 6px 12px);color:#3c3c3c}"
    "main{max-width:520px;margin:0 auto;padding:20px}h1{font-size:22px}figure{margin:0 0 18px;background:#fff;border:1px solid #b8b8b8;border-radius:10px;padding:10px}"
    "img{width:100%;image-rendering:pixelated;display:block;border-radius:4px}"
    "a{display:inline-block;margin-top:8px;padding:10px 16px;border-radius:8px;color:#fff;text-decoration:none;font-weight:700;"
    "background:linear-gradient(#5fd2ff,#12a8e8 50%,#0090d4);border:1px solid #0079b5}</style></head><body><main>"
    "<h1>Hn screen !</h1>"
    "<figure><img src=\"/haut.png\" alt=\"Écran du haut\"><a href=\"/haut.png\" download=\"hn-screen-haut.png\">Télécharger l'écran du haut</a></figure>"
    "%BOT%"
    "<figure><img src=\"/deux.png\" alt=\"Les deux écrans\"><a href=\"/deux.png\" download=\"hn-screen.png\">Télécharger les deux écrans</a></figure>"
    "</main></body></html>";

static const char BOT_PART[] =
    "<figure><img src=\"/bas.png\" alt=\"Écran du bas\"><a href=\"/bas.png\" download=\"hn-screen-bas.png\">Télécharger l'écran du bas</a></figure>";

static void handle_client(int fd)
{
    char req[1024];
    int got = 0;
    u64 start = osGetTime();
    while (got < (int)sizeof(req) - 1 && osGetTime() - start < 3000) {
        ssize_t n = recv(fd, req + got, sizeof(req) - 1 - got, 0);
        if (n > 0) {
            got += n;
            req[got] = 0;
            if (strstr(req, "\r\n\r\n")) break;
        } else if (n == 0) {
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            svcSleepThread(5 * 1000 * 1000);
        } else {
            break;
        }
    }
    req[got] = 0;
    char path[64] = "/";
    sscanf(req, "GET %63s", path);
    const char *dl = "Content-Disposition: inline\r\n";
    if (!strcmp(path, "/haut.png") && S.top.data) respond(fd, "image/png", S.top.data, S.top.len, dl);
    else if (!strcmp(path, "/bas.png") && S.bot.data) respond(fd, "image/png", S.bot.data, S.bot.len, dl);
    else if (!strcmp(path, "/deux.png") && S.both.data) respond(fd, "image/png", S.both.data, S.both.len, dl);
    else {
        static char page[sizeof(PAGE) + sizeof(BOT_PART)];
        const char *mark = strstr(PAGE, "%BOT%");
        size_t a = mark - PAGE;
        memcpy(page, PAGE, a);
        size_t n = a;
        if (S.bot.data) {
            memcpy(page + n, BOT_PART, sizeof(BOT_PART) - 1);
            n += sizeof(BOT_PART) - 1;
        }
        strcpy(page + n, mark + 5);
        respond(fd, "text/html; charset=utf-8", page, strlen(page), NULL);
    }
    close(fd);
}

static void server_thread(void *arg)
{
    (void)arg;
    while (!S.stop) {
        struct sockaddr_in cli;
        socklen_t cl = sizeof(cli);
        int fd = accept(S.listenFd, (struct sockaddr *)&cli, &cl);
        if (fd < 0) {
            svcSleepThread(30 * 1000 * 1000);
            continue;
        }
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
        handle_client(fd);
    }
}

static bool start_server(void)
{
    if (S.listenFd < 0) {
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (fd < 0) return false;
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_port = htons(LOCAL_PORT);
        a.sin_addr.s_addr = gethostid();
        if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(fd, 4) < 0) {
            close(fd);
            return false;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        S.listenFd = fd;
    }
    struct in_addr ip = {.s_addr = gethostid()};
    snprintf(S.url, sizeof(S.url), "http://%s:%d/", inet_ntoa(ip), LOCAL_PORT);
    snprintf(S.label, sizeof(S.label), "%s:%d", inet_ntoa(ip), LOCAL_PORT);
    S.stop = false;
    S.thread = threadCreate(server_thread, NULL, 64 * 1024, 0x31, -1, false);
    if (!S.thread) return false;
    S.threadRunning = true;
    S.state = SHARE_LOCAL;
    return true;
}

// ---------------------------------------------------------------------------
// Envoi sur le site
typedef struct {
    char buf[512];
    size_t len;
} Resp;

static size_t on_resp(void *p, size_t sz, size_t nm, void *u)
{
    Resp *r = u;
    size_t n = sz * nm;
    if (r->len + n >= sizeof(r->buf)) n = sizeof(r->buf) - 1 - r->len;
    memcpy(r->buf + r->len, p, n);
    r->len += n;
    r->buf[r->len] = 0;
    return sz * nm;
}

static int on_progress(void *u, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
    (void)u; (void)dt; (void)dn; (void)ut; (void)un;
    return S.stop ? 1 : 0;
}

static void upload_thread(void *arg)
{
    (void)arg;
    CURL *c = curl_easy_init();
    bool ok = false;
    if (c) {
        Resp r = {0};
        struct curl_slist *h = NULL;
        h = curl_slist_append(h, "Content-Type: image/png");
        h = curl_slist_append(h, "X-HnScreen: " APP_VERSION);
        curl_easy_setopt(c, CURLOPT_URL, UPLOAD_URL);
        curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, S.both.data);
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)S.both.len);
        curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_resp);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &r);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
        CURLcode rc = curl_easy_perform(c);
        long code = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(h);
        curl_easy_cleanup(c);
        if (S.stop) return;
        if (rc != CURLE_OK) {
            snprintf(S.error, sizeof(S.error), "%s", curl_easy_strerror(rc));
        } else if (code != 200) {
            snprintf(S.error, sizeof(S.error), "Le site a répondu %ld", code);
        } else {
            // Réponse : {"id":"A7K2Q9XB"}
            char *p = strstr(r.buf, "\"id\"");
            char id[24] = "";
            if (p && (p = strchr(p + 4, '"'))) {
                int n = 0;
                p++;
                while (n < 20 && ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) id[n++] = *p++;
                id[n] = 0;
            }
            if (strlen(id) >= 4) {
                snprintf(S.url, sizeof(S.url), "%s%s", SHARE_URL, id);
                snprintf(S.label, sizeof(S.label), "%s%s", SHARE_HOST, id);
                ok = true;
            } else {
                snprintf(S.error, sizeof(S.error), "Réponse du site illisible");
            }
        }
    } else {
        snprintf(S.error, sizeof(S.error), "curl indisponible");
    }
    S.state = ok ? SHARE_ONLINE : SHARE_ERROR;
}

// ---------------------------------------------------------------------------
static bool encode(Blob *b, const RgbImage *img)
{
    if (!img->px) return false;
    b->data = png_encode_rgb(img->px, img->w, img->h, &b->len);
    return b->data != NULL;
}

bool share_start(const char *topPath, const char *botPath, bool localOnly)
{
    share_stop();
    S.error[0] = 0;
    S.url[0] = 0;
    S.label[0] = 0;
    RgbImage top = {0}, bot = {0}, both = {0};
    if (topPath) img_load_bmp(topPath, &top);
    if (botPath) img_load_bmp(botPath, &bot);
    if (!top.px && !bot.px) {
        snprintf(S.error, sizeof(S.error), "Capture illisible");
        S.state = SHARE_ERROR;
        return false;
    }
    encode(&S.top, &top);
    encode(&S.bot, &bot);
    if (img_compose_both(&top, &bot, &both)) encode(&S.both, &both);
    img_free(&top);
    img_free(&bot);
    img_free(&both);
    if (!S.both.data) {
        snprintf(S.error, sizeof(S.error), "Mémoire insuffisante");
        S.state = SHARE_ERROR;
        return false;
    }
    if (!wifi_ok()) {
        S.state = SHARE_NO_WIFI;
        return false;
    }
    if (!net_init()) {
        snprintf(S.error, sizeof(S.error), "Réseau indisponible");
        S.state = SHARE_ERROR;
        return false;
    }
    if (localOnly) {
        share_go_local();
        return S.state == SHARE_LOCAL;
    }
    S.state = SHARE_SENDING;
    S.stop = false;
    S.thread = threadCreate(upload_thread, NULL, 512 * 1024, 0x31, -1, false);
    if (!S.thread) {
        share_go_local();
        return S.state == SHARE_LOCAL;
    }
    S.threadRunning = true;
    return true;
}

void share_go_local(void)
{
    join_thread();
    if (!S.both.data) return;
    if (!wifi_ok()) {
        S.state = SHARE_NO_WIFI;
        return;
    }
    if (!net_init() || !start_server()) {
        snprintf(S.error, sizeof(S.error), "Impossible d'ouvrir le partage local");
        S.state = SHARE_ERROR;
    }
}

// Appelée à chaque image : quand l'envoi échoue, on bascule tout seul sur le Wi-Fi local.
void share_poll_fallback(void)
{
    if (S.state == SHARE_ERROR && S.threadRunning && S.both.data) {
        char keep[128];
        snprintf(keep, sizeof(keep), "%s", S.error);
        share_go_local();
        if (S.state == SHARE_LOCAL) snprintf(S.error, sizeof(S.error), "%s", keep);
    }
}

void share_stop(void)
{
    join_thread();
    if (S.listenFd >= 0) {
        close(S.listenFd);
        S.listenFd = -1;
    }
    blob_free(&S.top);
    blob_free(&S.bot);
    blob_free(&S.both);
    S.state = SHARE_IDLE;
}

void share_exit(void)
{
    share_stop();
    if (S.socReady) {
        curl_global_cleanup();
        socExit();
        free(S.socBuf);
        S.socBuf = NULL;
        S.socReady = false;
    }
}
