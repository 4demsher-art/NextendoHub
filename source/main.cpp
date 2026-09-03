// NextendoHub — Nintendo Switch homebrew (.nro)
//
// A native libnx client for nextendo.network. This is the Switch equivalent of
// the Electron NextendoHub: Electron/HTML can't run under homebrew, so the
// window becomes a console UI and the `window.nextendo.*` bridge becomes the
// libcurl helpers below.
//
// Scope of this file: init + networking + Keychain-equivalent token store on the
// SD card + swkbd login + three read-only screens (Status / Online / Friends).
// The remaining screens (profile edit, saves, avatar gallery, multi-account)
// re-use `httpJson()` the same way — see PLAN.md.

#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <switch.h>
#include <curl/curl.h>
#include "cJSON.h"

// ------------------------------------------------------------------- config
static const char* API      = "https://nextendo.network";
static const char* STATUS   = "https://status.nextendo.network";
static const char* CAINFO   = "romfs:/cacert.pem";
static const char* TOKEN_DIR  = "sdmc:/switch/nextendo-hub";
static const char* TOKEN_PATH = "sdmc:/switch/nextendo-hub/session.dat";

// ------------------------------------------------------------------- http
struct Buf { std::string s; };
static size_t writeCb(char* p, size_t sz, size_t n, void* u) {
    ((Buf*)u)->s.append(p, sz * n);
    return sz * n;
}

struct Resp { long status = 0; std::string body; };

// method: "GET" | "POST" | "PUT" | "DELETE".  body: JSON string or "".  bearer: "" for none.
static Resp httpRaw(const char* method, const std::string& url,
                    const std::string& body, const std::string& bearer) {
    Resp r;
    CURL* c = curl_easy_init();
    if (!c) return r;
    Buf buf;
    struct curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Accept: application/json");
    hdr = curl_slist_append(hdr, "User-Agent: NextendoHub-Switch/1.0");
    std::string auth;
    if (!bearer.empty()) { auth = "Authorization: Bearer " + bearer; hdr = curl_slist_append(hdr, auth.c_str()); }
    if (!body.empty())     hdr = curl_slist_append(hdr, "Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_CAINFO, CAINFO);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    if (strcmp(method, "GET") != 0) curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    }
    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK) {
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
        r.body = std::move(buf.s);
    }
    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return r;
}

// Parsed variant. Caller owns the cJSON* (cJSON_Delete). NULL on parse failure.
static cJSON* httpJson(const char* method, const std::string& path,
                       const std::string& body = "", const std::string& bearer = "",
                       long* outStatus = nullptr, const char* base = nullptr) {
    Resp r = httpRaw(method, std::string(base ? base : API) + path, body, bearer);
    if (outStatus) *outStatus = r.status;
    return r.body.empty() ? nullptr : cJSON_Parse(r.body.c_str());
}

// ------------------------------------------------------------------- token store
// The Switch has no Keychain; the token lives on the SD card, readable only with
// the console unlocked + this homebrew running. (No DPAPI equivalent.)
static std::string g_token;

static void tokenLoad() {
    FILE* f = fopen(TOKEN_PATH, "rb");
    if (!f) return;
    char b[4096]; size_t n = fread(b, 1, sizeof(b) - 1, f); fclose(f);
    b[n] = 0;
    g_token.assign(b);
}
static void tokenSave(const std::string& t) {
    g_token = t;
    mkdir(TOKEN_DIR, 0777);
    FILE* f = fopen(TOKEN_PATH, "wb");
    if (f) { fwrite(t.data(), 1, t.size(), f); fclose(f); }
}
static void tokenClear() {
    g_token.clear();
    remove(TOKEN_PATH);
}

// ------------------------------------------------------------------- swkbd
static bool kbd(const char* header, bool password, std::string& out) {
    SwkbdConfig cfg;
    if (R_FAILED(swkbdCreate(&cfg, 0))) return false;
    swkbdConfigMakePresetDefault(&cfg);
    swkbdConfigSetHeaderText(&cfg, header);
    swkbdConfigSetType(&cfg, SwkbdType_All);
    swkbdConfigSetStringLenMax(&cfg, 128);
    if (password) swkbdConfigSetPasswordFlag(&cfg, 1);
    char buf[256] = {0};
    Result rc = swkbdShow(&cfg, buf, sizeof(buf));
    swkbdClose(&cfg);
    if (R_FAILED(rc)) return false;
    out.assign(buf);
    return !out.empty();
}

// ------------------------------------------------------------------- helpers
static std::string js(const std::string& v) {                 // JSON-encode a string
    std::string o = "\"";
    for (char c : v) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    o += "\"";
    return o;
}
static const char* cs(cJSON* o, const char* k, const char* dflt = "") {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : dflt;
}
static int ci(cJSON* o, const char* k, int dflt = 0) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsNumber(v)) ? (int)v->valuedouble : dflt;
}

// ------------------------------------------------------------------- auth
static bool doLogin() {
    std::string email, pw;
    if (!kbd("Nextendo e-mail", false, email)) return false;
    if (!kbd("Nextendo password", true, pw)) return false;
    std::string body = "{\"login\":" + js(email) + ",\"password\":" + js(pw) + "}";
    long st = 0;
    cJSON* j = httpJson("POST", "/api/login", body, "", &st);
    bool ok = false;
    if (j && st == 200) {
        const char* tok = cs(j, "token");
        if (tok && *tok) { tokenSave(tok); ok = true; }
    }
    if (j) cJSON_Delete(j);
    return ok;
}

// ------------------------------------------------------------------- screens
enum Screen { SC_STATUS, SC_ONLINE, SC_FRIENDS, SC_COUNT };
static const char* SCREEN_NAME[] = { "Server status", "In game now", "Friends" };

static void drawStatus() {
    printf("\n  Fetching status.nextendo.network ...\n");
    consoleUpdate(NULL);
    long a = 0, b = 0;
    cJSON* cfg = httpJson("GET", "/api/status-page/next", "", "", &a, STATUS);
    cJSON* hb  = httpJson("GET", "/api/status-page/heartbeat/next", "", "", &b, STATUS);
    printf("\x1b[2J\x1b[H");
    printf("  NextendoHub   [ %s ]   L/R switch   + exit\n", SCREEN_NAME[SC_STATUS]);
    printf("  ------------------------------------------------------------\n");
    if (!cfg || a != 200) { printf("\n  unreachable (HTTP %ld)\n", a); if (cfg) cJSON_Delete(cfg); if (hb) cJSON_Delete(hb); return; }
    cJSON* beats = hb ? cJSON_GetObjectItem(hb, "heartbeatList") : nullptr;
    int up = 0, down = 0, total = 0;
    cJSON* groups = cJSON_GetObjectItem(cfg, "publicGroupList");
    cJSON* g;
    cJSON_ArrayForEach(g, groups) {
        cJSON* mons = cJSON_GetObjectItem(g, "monitorList");
        cJSON* m;
        cJSON_ArrayForEach(m, mons) {
            int id = ci(m, "id");
            char key[16]; snprintf(key, sizeof(key), "%d", id);
            cJSON* list = beats ? cJSON_GetObjectItem(beats, key) : nullptr;
            cJSON* last = list ? cJSON_GetArrayItem(list, cJSON_GetArraySize(list) - 1) : nullptr;
            int s = last ? ci(last, "status", -1) : -1;
            total++;
            if (s == 1 || s == 3) up++; else if (s == 0 || s == 2) down++;
        }
    }
    printf("\n  %s   (%d / %d up)\n\n", down ? "SERVICE(S) DOWN" : "All systems operational", up, total);
    cJSON_ArrayForEach(g, groups) {
        printf("  %s\n", cs(g, "name", "Nextendo"));
        cJSON* mons = cJSON_GetObjectItem(g, "monitorList");
        cJSON* m;
        cJSON_ArrayForEach(m, mons) {
            int id = ci(m, "id");
            char key[16]; snprintf(key, sizeof(key), "%d", id);
            cJSON* list = beats ? cJSON_GetObjectItem(beats, key) : nullptr;
            cJSON* last = list ? cJSON_GetArrayItem(list, cJSON_GetArraySize(list) - 1) : nullptr;
            int s = last ? ci(last, "status", -1) : -1;
            int ping = last ? ci(last, "ping") : 0;
            printf("    [%s] %-34s %4d ms\n",
                   s == 1 ? "UP " : s == 0 ? "DWN" : s == 3 ? "MNT" : " ? ",
                   cs(m, "name"), ping);
        }
    }
    cJSON_Delete(cfg); if (hb) cJSON_Delete(hb);
}

static void drawOnline() {
    printf("\n  Fetching nextendo.network/api/online-counts ...\n");
    consoleUpdate(NULL);
    long st = 0;
    cJSON* j = httpJson("GET", "/api/online-counts", "", "", &st);
    printf("\x1b[2J\x1b[H");
    printf("  NextendoHub   [ %s ]   L/R switch   + exit\n", SCREEN_NAME[SC_ONLINE]);
    printf("  ------------------------------------------------------------\n\n");
    if (!j || st != 200) { printf("  unreachable (HTTP %ld)\n", st); if (j) cJSON_Delete(j); return; }
    cJSON* jeux = cJSON_GetObjectItem(j, "jeux");
    int total = 0;
    cJSON* it;
    cJSON_ArrayForEach(it, jeux) total += ci(it, "joueurs");
    printf("  %d players in game right now\n\n", total);
    cJSON_ArrayForEach(it, jeux) {
        int n = ci(it, "joueurs");
        if (n <= 0) continue;
        printf("    %-38s %3d\n", cs(it, "nom"), n);
    }
    cJSON_Delete(j);
}

static void drawFriends() {
    printf("\x1b[2J\x1b[H");
    printf("  NextendoHub   [ %s ]   L/R switch   + exit\n", SCREEN_NAME[SC_FRIENDS]);
    printf("  ------------------------------------------------------------\n\n");
    if (g_token.empty()) { printf("  Not signed in.  Press Y to sign in.\n"); return; }
    consoleUpdate(NULL);
    long st = 0;
    cJSON* j = httpJson("GET", "/api/friends", "", g_token, &st);
    if (st == 401) { tokenClear(); printf("  Session expired.  Press Y to sign in.\n"); if (j) cJSON_Delete(j); return; }
    if (!j || st != 200) { printf("  could not load friends (HTTP %ld)\n", st); if (j) cJSON_Delete(j); return; }
    cJSON* arr = cJSON_IsArray(j) ? j : cJSON_GetObjectItem(j, "friends");
    cJSON* f;
    int online = 0, count = 0;
    cJSON_ArrayForEach(f, arr) {
        count++;
        cJSON* pr = cJSON_GetObjectItem(f, "presence");
        int s = pr ? ci(pr, "status") : 0;
        const char* app = pr ? cs(pr, "app_id") : "";
        bool on = s > 0;
        if (on) online++;
        const char* name = cs(f, "name", cs(f, "username", "-"));
        printf("    %s %-24s %s\n",
               on && *app ? "[GAME]" : on ? "[ ON ]" : "[ off]",
               name, cs(f, "friend_code"));
    }
    printf("\n  %d friends  ·  %d online\n", count, online);
    cJSON_Delete(j);
}

// ------------------------------------------------------------------- main
int main(int argc, char** argv) {
    consoleInit(NULL);
    romfsInit();
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_ALL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    tokenLoad();

    int screen = SC_ONLINE;
    bool dirty = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        if (down & HidNpadButton_Plus) break;
        if (down & HidNpadButton_R) { screen = (screen + 1) % SC_COUNT; dirty = true; }
        if (down & HidNpadButton_L) { screen = (screen + SC_COUNT - 1) % SC_COUNT; dirty = true; }
        if (down & HidNpadButton_X) dirty = true;                       // refresh
        if ((down & HidNpadButton_Y) && screen == SC_FRIENDS && g_token.empty()) {
            if (doLogin()) dirty = true;
        }
        if (down & HidNpadButton_Minus && !g_token.empty()) {           // sign out
            tokenClear(); dirty = true;
        }

        if (dirty) {
            printf("\x1b[2J\x1b[H");
            switch (screen) {
                case SC_STATUS:  drawStatus();  break;
                case SC_ONLINE:  drawOnline();  break;
                case SC_FRIENDS: drawFriends(); break;
            }
            printf("\n  ------------------------------------------------------------\n");
            printf("  L/R screen   X refresh   %s   + exit\n",
                   screen == SC_FRIENDS ? (g_token.empty() ? "Y sign in" : "- sign out") : "");
            dirty = false;
        }

        consoleUpdate(NULL);
    }

    curl_global_cleanup();
    socketExit();
    romfsExit();
    consoleExit(NULL);
    return 0;
}
