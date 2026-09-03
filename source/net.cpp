#include "net.hpp"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <algorithm>
#include <utility>
#include <sys/stat.h>
#include <switch.h>
#include <curl/curl.h>

namespace net {

const char* API_BASE    = "https://nextendo.network";
const char* STATUS_BASE = "https://status.nextendo.network";

static const char* CAINFO      = "romfs:/cacert.pem";
static const char* DIR         = "sdmc:/switch/nextendo-hub";
static const char* CONFIG_PATH = "sdmc:/switch/nextendo-hub/config.json";
static const char* SAVES_DIR   = "sdmc:/switch/nextendo-hub/saves";

// ---------------------------------------------------------------- config.json
static cJSON* g_cfg = nullptr;

static void cfgLoad() {
    if (g_cfg) return;
    FILE* f = fopen(CONFIG_PATH, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string s(n > 0 ? n : 0, 0);
        if (n > 0) fread(&s[0], 1, n, f);
        fclose(f);
        g_cfg = cJSON_Parse(s.c_str());
    }
    if (!g_cfg || !cJSON_IsObject(g_cfg)) {
        if (g_cfg) cJSON_Delete(g_cfg);
        g_cfg = cJSON_CreateObject();
    }
    if (!cJSON_GetObjectItem(g_cfg, "accounts"))
        cJSON_AddItemToObject(g_cfg, "accounts", cJSON_CreateObject());
    if (!cJSON_GetObjectItem(g_cfg, "current"))
        cJSON_AddStringToObject(g_cfg, "current", "");
}
static void cfgSave() {
    mkdir(DIR, 0777);
    char* s = cJSON_PrintUnformatted(g_cfg);
    FILE* f = fopen(CONFIG_PATH, "wb");
    if (f) { fwrite(s, 1, strlen(s), f); fclose(f); }
    cJSON_free(s);
}

void init() {
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_ALL);
    mkdir(DIR, 0777);
    cfgLoad();
}
void shutdown() {
    curl_global_cleanup();
    socketExit();
    if (g_cfg) { cJSON_Delete(g_cfg); g_cfg = nullptr; }
}

// ---------------------------------------------------------------- HTTP
struct Sink { std::string s; };
static size_t wcb(char* p, size_t sz, size_t n, void* u) { ((Sink*)u)->s.append(p, sz * n); return sz * n; }

Resp raw(const char* method, const std::string& url, const std::string& body, const std::string& bearer) {
    Resp r;
    CURL* c = curl_easy_init();
    if (!c) return r;
    Sink sink;
    struct curl_slist* h = nullptr;
    h = curl_slist_append(h, "Accept: application/json");
    h = curl_slist_append(h, "User-Agent: NextendoHub-Switch/1.0");
    std::string auth;
    if (!bearer.empty()) { auth = "Authorization: Bearer " + bearer; h = curl_slist_append(h, auth.c_str()); }
    if (!body.empty())    h = curl_slist_append(h, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wcb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_CAINFO, CAINFO);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    if (strcmp(method, "GET") != 0) curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    }
    if (curl_easy_perform(c) == CURLE_OK) {
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
        r.body = std::move(sink.s);
    }
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    return r;
}
cJSON* json(const char* method, const std::string& path, const std::string& body,
            const std::string& bearer, long* outStatus, const char* base) {
    Resp r = raw(method, std::string(base ? base : API_BASE) + path, body, bearer);
    if (outStatus) *outStatus = r.status;
    return r.body.empty() ? nullptr : cJSON_Parse(r.body.c_str());
}

// ---------------------------------------------------------------- small helpers
std::string jstr(const std::string& v) {
    std::string o = "\"";
    for (char ch : v) switch (ch) {
        case '"':  o += "\\\""; break; case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break; case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break; default:   o += ch;
    }
    return o + "\"";
}
const char* gs(cJSON* o, const char* k, const char* d) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : d;
}
int gi(cJSON* o, const char* k, int d) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    return (v && cJSON_IsNumber(v)) ? (int)v->valuedouble : d;
}
bool gb(cJSON* o, const char* k, bool d) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsBool(v))   return cJSON_IsTrue(v);
    if (cJSON_IsNumber(v)) return v->valuedouble != 0;
    return d;
}
std::string b64(const unsigned char* p, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned x = p[i] << 16;
        if (i + 1 < n) x |= p[i + 1] << 8;
        if (i + 2 < n) x |= p[i + 2];
        o += T[(x >> 18) & 63];
        o += T[(x >> 12) & 63];
        o += (i + 1 < n) ? T[(x >> 6) & 63] : '=';
        o += (i + 2 < n) ? T[x & 63] : '=';
    }
    return o;
}

// ---------------------------------------------------------------- prefs
std::string getPref(const char* key, const std::string& dflt) {
    cfgLoad();
    const char* v = gs(g_cfg, key, nullptr);
    return v ? std::string(v) : dflt;
}
void setPref(const char* key, const std::string& value) {
    cfgLoad();
    cJSON_DeleteItemFromObject(g_cfg, key);
    cJSON_AddStringToObject(g_cfg, key, value.c_str());
    cfgSave();
}

// ---------------------------------------------------------------- multi-account
void tokenLoadAll() { cfgLoad(); }

static cJSON* accObj() { cfgLoad(); return cJSON_GetObjectItem(g_cfg, "accounts"); }
std::string currentId() { cfgLoad(); return std::string(gs(g_cfg, "current", "")); }

std::string token() {
    cJSON* a = cJSON_GetObjectItem(accObj(), currentId().c_str());
    return a ? std::string(gs(a, "token", "")) : std::string();
}
bool signedIn() { return !token().empty(); }

std::vector<Account> accounts() {
    std::vector<Account> v;
    std::string cur = currentId();
    cJSON* a = accObj(); cJSON* it;
    cJSON_ArrayForEach(it, a) {
        Account ac;
        ac.id = it->string ? it->string : "";
        ac.username = gs(it, "username", ac.id.c_str());
        ac.current = (ac.id == cur);
        v.push_back(ac);
    }
    std::sort(v.begin(), v.end(), [](const Account& x, const Account& y) { return x.id < y.id; });
    return v;
}
void addAccount(const std::string& tok, const std::string& username) {
    cJSON* a = accObj();
    std::string id = username.empty() ? ("account-" + std::to_string(cJSON_GetArraySize(a) + 1)) : username;
    cJSON_DeleteItemFromObject(a, id.c_str());
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "token", tok.c_str());
    cJSON_AddStringToObject(o, "username", (username.empty() ? id : username).c_str());
    cJSON_AddItemToObject(a, id.c_str(), o);
    cJSON_DeleteItemFromObject(g_cfg, "current");
    cJSON_AddStringToObject(g_cfg, "current", id.c_str());
    cfgSave();
}
int removeCurrent() {
    cJSON* a = accObj();
    cJSON_DeleteItemFromObject(a, currentId().c_str());
    std::string next;
    cJSON* it;
    cJSON_ArrayForEach(it, a) { if (it->string) { next = it->string; break; } }
    cJSON_DeleteItemFromObject(g_cfg, "current");
    cJSON_AddStringToObject(g_cfg, "current", next.c_str());
    cfgSave();
    return cJSON_GetArraySize(a);
}
bool setCurrent(const std::string& id) {
    cJSON* a = accObj();
    if (!cJSON_GetObjectItem(a, id.c_str())) return false;
    cJSON_DeleteItemFromObject(g_cfg, "current");
    cJSON_AddStringToObject(g_cfg, "current", id.c_str());
    cfgSave();
    return true;
}
void removeAccount(const std::string& id) {
    cJSON* a = accObj();
    cJSON_DeleteItemFromObject(a, id.c_str());
    if (currentId() == id) { cJSON_DeleteItemFromObject(g_cfg, "current"); cJSON_AddStringToObject(g_cfg, "current", ""); }
    cfgSave();
}
void setCurrentUsername(const std::string& name) {
    cJSON* a = cJSON_GetObjectItem(accObj(), currentId().c_str());
    if (a) { cJSON_DeleteItemFromObject(a, "username"); cJSON_AddStringToObject(a, "username", name.c_str()); cfgSave(); }
}

// ---------------------------------------------------------------- iso helpers
static std::string isoNow() {
    time_t t = time(nullptr);
    struct tm tmv; gmtime_r(&t, &tmv);
    char b[32]; strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return b;
}
static std::string stripNextendo(const char* s) {
    std::string v = s ? s : "";
    size_t d = v.find(" - ");
    if (d == std::string::npos) d = v.find(" – ");
    return d == std::string::npos ? v : v.substr(d + 3);
}
static std::string imgURI(const char* v) {
    std::string s = v ? v : "";
    if (s.rfind("data:image/", 0) == 0) {
        if (s.compare(0, 20, "data:image/svg+xml", 0, 18) == 0) return "";  // no svg
        return s;
    }
    if (s.size() > 64) {
        bool ok = true;
        for (char c : s) if (!(isalnum((unsigned char)c) || c == '+' || c == '/' || c == '=' || isspace((unsigned char)c))) { ok = false; break; }
        if (ok) { std::string t; for (char c : s) if (!isspace((unsigned char)c)) t += c; return "data:image/jpeg;base64," + t; }
    }
    return "";
}

// ---------------------------------------------------------------- feeds
cJSON* serverStatus(long* st) {
    long a = 0, b = 0;
    cJSON* cfg = json("GET", "/api/status-page/next", "", "", &a, STATUS_BASE);
    cJSON* hb  = json("GET", "/api/status-page/heartbeat/next", "", "", &b, STATUS_BASE);
    if (st) *st = a;
    if (!cfg || a != 200) { if (cfg) cJSON_Delete(cfg); if (hb) cJSON_Delete(hb); return nullptr; }
    cJSON* beats  = hb ? cJSON_GetObjectItem(hb, "heartbeatList") : nullptr;
    cJSON* uptime = hb ? cJSON_GetObjectItem(hb, "uptimeList") : nullptr;
    cJSON* out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "fetched", isoNow().c_str());
    int up = 0, down = 0, total = 0;
    cJSON* groups = cJSON_CreateArray();
    cJSON* g;
    cJSON_ArrayForEach(g, cJSON_GetObjectItem(cfg, "publicGroupList")) {
        cJSON* go = cJSON_CreateObject();
        cJSON_AddStringToObject(go, "name", stripNextendo(gs(g, "name", "Nextendo")).c_str());
        cJSON* mons = cJSON_CreateArray();
        cJSON* m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItem(g, "monitorList")) {
            char key[16]; snprintf(key, sizeof(key), "%d", gi(m, "id"));
            cJSON* lst  = beats ? cJSON_GetObjectItem(beats, key) : nullptr;
            cJSON* last = lst ? cJSON_GetArrayItem(lst, cJSON_GetArraySize(lst) - 1) : nullptr;
            int s = last ? gi(last, "status", -1) : -1;
            total++; if (s == 1 || s == 3) up++; else if (s == 0 || s == 2) down++;
            cJSON* mo = cJSON_CreateObject();
            cJSON_AddStringToObject(mo, "name", stripNextendo(gs(m, "name")).c_str());
            cJSON_AddNumberToObject(mo, "status", s);
            if (last && cJSON_IsNumber(cJSON_GetObjectItem(last, "ping")))
                cJSON_AddNumberToObject(mo, "ping", gi(last, "ping"));
            char uk[24]; snprintf(uk, sizeof(uk), "%d_24", gi(m, "id"));
            cJSON* uv = uptime ? cJSON_GetObjectItem(uptime, uk) : nullptr;
            if (uv && cJSON_IsNumber(uv)) cJSON_AddNumberToObject(mo, "uptime24", uv->valuedouble);
            cJSON_AddItemToArray(mons, mo);
        }
        cJSON_AddItemToObject(go, "monitors", mons);
        cJSON_AddItemToArray(groups, go);
    }
    cJSON_AddItemToObject(out, "groups", groups);
    cJSON_AddNumberToObject(out, "up", up);
    cJSON_AddNumberToObject(out, "down", down);
    cJSON_AddNumberToObject(out, "total", total);
    cJSON_AddItemToObject(out, "incidents", cJSON_Duplicate(cJSON_GetObjectItem(cfg, "incidents"), 1));
    cJSON_Delete(cfg); if (hb) cJSON_Delete(hb);
    return out;
}

cJSON* onlineCounts(long* st) {
    cJSON* j = json("GET", "/api/online-counts", "", "", st);
    if (!j || (st && *st != 200)) { if (j) cJSON_Delete(j); return nullptr; }
    cJSON* src = cJSON_GetObjectItem(j, "jeux");
    std::vector<std::pair<std::string, int>> v;
    cJSON* it;
    cJSON_ArrayForEach(it, src) v.push_back({ gs(it, "nom", "?"), gi(it, "joueurs") });
    std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.second > y.second; });
    cJSON* out = cJSON_CreateObject();
    int total = 0;
    cJSON* arr = cJSON_CreateArray();
    for (auto& p : v) {
        total += p.second;
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "nom", p.first.c_str());
        cJSON_AddNumberToObject(o, "joueurs", p.second);
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddNumberToObject(out, "total", total);
    cJSON_AddItemToObject(out, "jeux", arr);
    cJSON_Delete(j);
    return out;
}

// ---------------------------------------------------------------- auth
static void shapeMe(cJSON* meR, cJSON* prR, std::string& username, std::string& code, std::string& avatar) {
    cJSON* acc  = meR ? (cJSON_GetObjectItem(meR, "account") ? cJSON_GetObjectItem(meR, "account") : meR) : nullptr;
    cJSON* prof = prR ? (cJSON_GetObjectItem(prR, "profile") ? cJSON_GetObjectItem(prR, "profile") : prR) : nullptr;
    if (acc) { username = gs(acc, "username", ""); code = gs(acc, "friend_code", ""); }
    if (prof) avatar = imgURI(gs(prof, "image", ""));
}

static AuthResult finishAuth(cJSON* r, long st, const std::string& fallbackName) {
    AuthResult a;
    if (!r || st != 200 || !gs(r, "token")[0]) {
        a.error = (r && gs(r, "error")[0]) ? gs(r, "error") : ("Failed (HTTP " + std::to_string(st) + ")");
        return a;
    }
    std::string tok = gs(r, "token");
    long ms = 0, ps = 0;
    cJSON* me = json("GET", "/api/me", "", tok, &ms);
    cJSON* pr = json("GET", "/api/profile", "", tok, &ps);
    std::string u, code, avatar;
    shapeMe(me, pr, u, code, avatar);
    if (u.empty() && cJSON_GetObjectItem(r, "account"))
        shapeMe(r, nullptr, u, code, avatar);
    if (u.empty()) u = fallbackName;
    addAccount(tok, u);
    a.ok = true; a.username = u; a.code = code;
    if (me) cJSON_Delete(me);
    if (pr) cJSON_Delete(pr);
    return a;
}

AuthResult login(const std::string& email, const std::string& password) {
    std::string body = "{\"login\":" + jstr(email) + ",\"password\":" + jstr(password) + "}";
    long st = 0;
    cJSON* r = json("POST", "/api/login", body, "", &st);
    AuthResult a = finishAuth(r, st, "");
    if (r) cJSON_Delete(r);
    return a;
}
AuthResult reg(const std::string& username, const std::string& email,
               const std::string& password, const std::string& country) {
    std::string body = "{\"username\":" + jstr(username) + ",\"email\":" + jstr(email) +
                       ",\"password\":" + jstr(password);
    if (!country.empty()) body += ",\"country\":" + jstr(country);
    body += "}";
    long st = 0;
    cJSON* r = json("POST", "/api/register", body, "", &st);
    AuthResult a = finishAuth(r, st, username);
    if (r) cJSON_Delete(r);
    return a;
}
void logout() {
    std::string t = token();
    if (!t.empty()) { long s; cJSON* x = json("POST", "/api/logout", "", t, &s); if (x) cJSON_Delete(x); }
    removeCurrent();
}

// ---------------------------------------------------------------- friends
static std::string gameName(const std::string& appId, const std::string& tok) {
    if (appId.empty()) return "";
    long s = 0;
    cJSON* g = json("GET", "/api/gameinfo?title_id=" + appId, "", tok, &s);
    std::string n;
    if (g && s == 200) { n = gs(g, "name", gs(g, "title", "")); }
    if (g) cJSON_Delete(g);
    return n;
}
cJSON* friends(long* st) {
    std::string t = token();
    if (t.empty()) { if (st) *st = 0; return nullptr; }
    long ms = 0, ps = 0, fs = 0;
    cJSON* meR = json("GET", "/api/me", "", t, &ms);
    cJSON* prR = json("GET", "/api/profile", "", t, &ps);
    cJSON* frR = json("GET", "/api/friends", "", t, &fs);
    if (st) *st = fs;
    if (fs == 401) { removeCurrent(); if (meR) cJSON_Delete(meR); if (prR) cJSON_Delete(prR); if (frR) cJSON_Delete(frR); return nullptr; }
    if (!frR || fs != 200) { if (meR) cJSON_Delete(meR); if (prR) cJSON_Delete(prR); if (frR) cJSON_Delete(frR); return nullptr; }

    cJSON* rawF = cJSON_IsArray(frR) ? frR : cJSON_GetObjectItem(frR, "friends");
    cJSON* rawR = cJSON_GetObjectItem(frR, "requests");

    cJSON* out = cJSON_CreateObject();
    std::string u, code, avatar;
    shapeMe(meR, prR, u, code, avatar);
    cJSON* meo = cJSON_CreateObject();
    cJSON_AddStringToObject(meo, "username", u.c_str());
    cJSON_AddStringToObject(meo, "code", code.c_str());
    cJSON_AddStringToObject(meo, "avatar", avatar.c_str());
    cJSON_AddItemToObject(out, "me", meo);

    int total = 0, online = 0, ingame = 0;
    cJSON* farr = cJSON_CreateArray();
    cJSON* f;
    cJSON_ArrayForEach(f, rawF) {
        cJSON* p = cJSON_GetObjectItem(f, "presence");
        int s = p ? gi(p, "status") : 0;
        bool on = s > 0;
        std::string appId = p ? gs(p, "app_id", gs(p, "appId", "")) : "";
        bool ig = on && !appId.empty();
        total++; if (on) online++; if (ig) ingame++;
        cJSON* fo = cJSON_CreateObject();
        cJSON_AddStringToObject(fo, "name", gs(f, "name", gs(f, "username", "-")));
        cJSON_AddStringToObject(fo, "code", gs(f, "friend_code", gs(f, "code", "")));
        cJSON_AddBoolToObject(fo, "favorite", gb(f, "favorite", gb(f, "fav")));
        cJSON_AddBoolToObject(fo, "online", on);
        cJSON_AddBoolToObject(fo, "inGame", ig);
        cJSON_AddNumberToObject(fo, "pid", gi(f, "pid", gi(f, "id")));
        std::string gname = ig ? gameName(appId, t) : "";
        if (gname.empty() && p) gname = gs(p, "app_detail", "");
        cJSON_AddStringToObject(fo, "game", gname.c_str());
        cJSON_AddItemToArray(farr, fo);
    }
    // sort: in-game, online, offline; favourites first within tier
    int n = cJSON_GetArraySize(farr);
    std::vector<cJSON*> vec; vec.reserve(n);
    for (int i = 0; i < n; i++) vec.push_back(cJSON_GetArrayItem(farr, i));
    auto rank = [](cJSON* x) { return gb(x, "inGame") ? 0 : gb(x, "online") ? 1 : 2; };
    std::stable_sort(vec.begin(), vec.end(), [&](cJSON* x, cJSON* y) {
        if (rank(x) != rank(y)) return rank(x) < rank(y);
        return gb(x, "favorite") > gb(y, "favorite");
    });
    cJSON* sorted = cJSON_CreateArray();
    for (auto* v : vec) cJSON_AddItemToArray(sorted, cJSON_Duplicate(v, 1));
    cJSON_Delete(farr);
    cJSON_AddItemToObject(out, "friends", sorted);

    cJSON* rarr = cJSON_CreateArray();
    cJSON* r;
    cJSON_ArrayForEach(r, rawR) {
        cJSON* ro = cJSON_CreateObject();
        cJSON_AddStringToObject(ro, "name", gs(r, "name", gs(r, "username", "-")));
        cJSON_AddStringToObject(ro, "code", gs(r, "friend_code", gs(r, "code", "")));
        cJSON_AddNumberToObject(ro, "pid", gi(r, "pid", gi(r, "id")));
        cJSON_AddItemToArray(rarr, ro);
    }
    cJSON_AddItemToObject(out, "requests", rarr);

    cJSON* cnt = cJSON_CreateObject();
    cJSON_AddNumberToObject(cnt, "total", total);
    cJSON_AddNumberToObject(cnt, "online", online);
    cJSON_AddNumberToObject(cnt, "inGame", ingame);
    cJSON_AddItemToObject(out, "counts", cnt);

    if (meR) cJSON_Delete(meR);
    if (prR) cJSON_Delete(prR);
    cJSON_Delete(frR);
    return out;
}
static bool simplePost(const std::string& path, const std::string& body, std::string& err) {
    long s = 0;
    cJSON* r = json("POST", path, body, token(), &s);
    bool ok = (s == 200);
    if (!ok && r && gs(r, "error")[0]) err = gs(r, "error");
    else if (!ok) err = "HTTP " + std::to_string(s);
    if (r) cJSON_Delete(r);
    return ok;
}
bool friendAdd(const std::string& code, std::string& err) { return simplePost("/api/friends", "{\"friend_code\":" + jstr(code) + "}", err); }
bool friendAccept(int pid) { std::string e; return simplePost("/api/friends/accept", "{\"pid\":" + std::to_string(pid) + "}", e); }
bool friendDecline(int pid) { std::string e; return simplePost("/api/friends/decline", "{\"pid\":" + std::to_string(pid) + "}", e); }

// ---------------------------------------------------------------- profile / account
cJSON* profileGet(long* st) {
    std::string t = token();
    if (t.empty()) { if (st) *st = 0; return nullptr; }
    long ms = 0, ps = 0;
    cJSON* me = json("GET", "/api/me", "", t, &ms);
    cJSON* pr = json("GET", "/api/profile", "", t, &ps);
    if (st) *st = ms;
    if (ms == 401) { removeCurrent(); if (me) cJSON_Delete(me); if (pr) cJSON_Delete(pr); return nullptr; }
    cJSON* acc  = me ? (cJSON_GetObjectItem(me, "account") ? cJSON_GetObjectItem(me, "account") : me) : nullptr;
    cJSON* prof = pr ? (cJSON_GetObjectItem(pr, "profile") ? cJSON_GetObjectItem(pr, "profile") : pr) : nullptr;
    cJSON* out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "username", acc ? gs(acc, "username", "") : "");
    cJSON_AddStringToObject(out, "country", acc ? gs(acc, "country", (prof ? gs(prof, "country", "") : "")) : "");
    cJSON_AddStringToObject(out, "friend_code", acc ? gs(acc, "friend_code", "") : "");
    cJSON_AddStringToObject(out, "image", prof ? imgURI(gs(prof, "image", "")).c_str() : "");
    std::string col = prof ? gs(prof, "color", "") : "";
    if (col.size() != 7 || col[0] != '#') col = "";
    cJSON_AddStringToObject(out, "color", col.c_str());
    if (me) cJSON_Delete(me);
    if (pr) cJSON_Delete(pr);
    return out;
}
bool profileSave(const std::string& username, const std::string& image,
                 const std::string& color, const std::string& avatarJson, std::string& err) {
    std::string t = token();
    if (t.empty()) { err = "Not signed in"; return false; }
    long ms = 0, ps = 0;
    cJSON* me = json("GET", "/api/me", "", t, &ms);
    cJSON* pr = json("GET", "/api/profile", "", t, &ps);
    cJSON* acc  = me ? (cJSON_GetObjectItem(me, "account") ? cJSON_GetObjectItem(me, "account") : me) : nullptr;
    cJSON* prof = pr ? (cJSON_GetObjectItem(pr, "profile") ? cJSON_GetObjectItem(pr, "profile") : pr) : nullptr;

    auto hex = [](const std::string& c) { return (c.size() == 7 && c[0] == '#') ? c : std::string(); };
    std::string b = "{";
    b += "\"name\":"  + jstr(!username.empty() ? username : (acc ? gs(acc, "username", "") : ""));
    b += ",\"mii\":"  + jstr(prof ? gs(prof, "mii", "") : "");
    b += ",\"image\":" + jstr(!image.empty() ? image : (prof ? gs(prof, "image", "") : ""));
    std::string col = !hex(color).empty() ? hex(color) : (prof ? hex(gs(prof, "color", "")) : "");
    b += ",\"color\":" + jstr(col.empty() ? "#1ca9e0" : col);
    b += ",\"avatar\":" + jstr(avatarJson != std::string("\0", 1) ? avatarJson : (prof ? gs(prof, "avatar", "") : ""));
    b += "}";
    long s = 0;
    cJSON* r = json("PUT", "/api/profile", b, t, &s);
    bool ok = (s == 200);
    if (!ok) err = (r && gs(r, "error")[0]) ? gs(r, "error") : ("HTTP " + std::to_string(s));
    if (r) cJSON_Delete(r);
    if (me) cJSON_Delete(me);
    if (pr) cJSON_Delete(pr);
    return ok;
}
bool usernameSet(const std::string& u, std::string& err) {
    long s = 0;
    cJSON* r = json("PUT", "/api/username", "{\"username\":" + jstr(u) + "}", token(), &s);
    bool ok = (s == 200);
    if (ok) {
        cJSON* acc = cJSON_GetObjectItem(r, "account");
        setCurrentUsername(acc ? gs(acc, "username", u.c_str()) : u.c_str());
    } else err = (r && gs(r, "error")[0]) ? gs(r, "error") : ("HTTP " + std::to_string(s));
    if (r) cJSON_Delete(r);
    return ok;
}
bool usernameAvailable(const std::string& u) {
    long s = 0;
    cJSON* r = json("GET", "/api/username-available?username=" + u, "", token(), &s);
    bool a = r && (gb(r, "available") || gb(r, "free") || gb(r, "ok"));
    if (r) cJSON_Delete(r);
    return s == 200 && a;
}
std::vector<std::string> countryList() {
    long s = 0;
    cJSON* r = json("GET", "/api/country", "", token(), &s);
    std::vector<std::string> v;
    cJSON* arr = r ? cJSON_GetObjectItem(r, "countries") : nullptr;
    cJSON* it;
    cJSON_ArrayForEach(it, arr)
        if (cJSON_IsString(it) && strlen(it->valuestring) == 2) v.push_back(it->valuestring);
    if (r) cJSON_Delete(r);
    return v;
}
bool countrySet(const std::string& code, std::string& err) {
    long s = 0;
    cJSON* r = json("POST", "/api/country", "{\"country\":" + jstr(code) + "}", token(), &s);
    bool ok = (s == 200);
    if (!ok) err = (r && gs(r, "error")[0]) ? gs(r, "error") : ("HTTP " + std::to_string(s));
    if (r) cJSON_Delete(r);
    return ok;
}

// ---------------------------------------------------------------- cloud saves
cJSON* savesList(long* st) {
    std::string t = token();
    if (t.empty()) { if (st) *st = 0; return nullptr; }
    cJSON* r = json("GET", "/api/saves", "", t, st);
    if (st && *st == 401) { removeCurrent(); if (r) cJSON_Delete(r); return nullptr; }
    if (!r || (st && *st != 200)) { if (r) cJSON_Delete(r); return nullptr; }
    cJSON* out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "eligible", !(cJSON_IsBool(cJSON_GetObjectItem(r, "eligible")) && !gb(r, "eligible")));
    cJSON_AddStringToObject(out, "reasonCode", gs(r, "reasonCode", ""));
    cJSON_AddBoolToObject(out, "isBooster", gb(r, "isBooster"));
    cJSON_AddNumberToObject(out, "totalSize", gi(r, "totalSize"));
    cJSON_AddNumberToObject(out, "limit", gi(r, "limit", 5 * 1024 * 1024));
    cJSON* arr = cJSON_CreateArray();
    cJSON* s;
    cJSON_ArrayForEach(s, cJSON_GetObjectItem(r, "saves")) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "titleId", gs(s, "titleId", gs(s, "title_id", gs(s, "id", ""))));
        cJSON_AddStringToObject(o, "name", gs(s, "name", gs(s, "title", "-")));
        cJSON_AddNumberToObject(o, "size", gi(s, "size", gi(s, "bytes", -1)));
        cJSON_AddStringToObject(o, "updated", gs(s, "updated", gs(s, "updated_at", "")));
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(out, "saves", arr);
    cJSON_Delete(r);
    return out;
}
bool saveDelete(const std::string& titleId, std::string& err) {
    long s = 0;
    cJSON* r = json("DELETE", "/api/save/" + titleId, "", token(), &s);
    bool ok = (s == 200 || s == 204);
    if (!ok) err = (r && gs(r, "error")[0]) ? gs(r, "error") : ("HTTP " + std::to_string(s));
    if (r) cJSON_Delete(r);
    return ok;
}
bool saveDownload(const std::string& titleId, const std::string& name, std::string& outPath, std::string& err) {
    Resp r = raw("GET", std::string(API_BASE) + "/api/save/" + titleId, "", token());
    if (r.status == 204) { err = "No cloud save for this game."; return false; }
    if (r.status != 200)  { err = "HTTP " + std::to_string(r.status); return false; }
    mkdir(SAVES_DIR, 0777);
    std::string safe;
    for (char c : name) safe += (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')') ? c : '_';
    if (safe.empty()) safe = titleId;
    outPath = std::string(SAVES_DIR) + "/" + safe + ".zip";
    FILE* f = fopen(outPath.c_str(), "wb");
    if (!f) { err = "cannot write SD"; return false; }
    fwrite(r.body.data(), 1, r.body.size(), f);
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- mods / avatars
cJSON* modsFavorites(long* st) {
    std::string t = token();
    if (t.empty()) { if (st) *st = 0; return nullptr; }
    cJSON* r = json("GET", "/api/mod-favorites", "", t, st);
    if (!r || (st && *st != 200)) { if (r) cJSON_Delete(r); return nullptr; }
    cJSON* src = cJSON_IsArray(r) ? r : cJSON_GetObjectItem(r, "favorites");
    if (!src) src = cJSON_GetObjectItem(r, "mods");
    if (!src) src = cJSON_GetObjectItem(r, "list");
    cJSON* out = cJSON_CreateArray();
    cJSON* m;
    cJSON_ArrayForEach(m, src) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", gs(m, "name", gs(m, "title", "-")));
        cJSON_AddStringToObject(o, "game", gs(m, "game", gs(m, "game_name", "")));
        cJSON_AddStringToObject(o, "author", gs(m, "author", gs(m, "by", "")));
        cJSON_AddItemToArray(out, o);
    }
    cJSON_Delete(r);
    return out;
}
cJSON* avatarsList(long* st) {
    cJSON* m = json("GET", "/assets/avatars/manifest.json", "", "", st);
    if (!m || (st && *st != 200)) { if (m) cJSON_Delete(m); return nullptr; }
    cJSON* out = cJSON_CreateObject();
    cJSON_AddItemToObject(out, "firmware", cJSON_Duplicate(cJSON_GetObjectItem(m, "avatars"), 1));
    cJSON_AddItemToObject(out, "custom", cJSON_Duplicate(cJSON_GetObjectItem(m, "custom"), 1));
    cJSON_AddStringToObject(out, "base", "https://nextendo.network/assets/avatars/");
    cJSON_Delete(m);
    return out;
}
bool avatarImageBytes(const std::string& name, std::string& out) {
    for (char c : name) if (!(isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-')) return false;
    if (name.size() < 5 || name.substr(name.size() - 4) != ".png") return false;
    Resp r = raw("GET", "https://nextendo.network/assets/avatars/" + name, "", "");
    if (r.status != 200) return false;
    out = std::move(r.body);
    return true;
}

} // namespace net
