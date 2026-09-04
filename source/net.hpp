// net.hpp — the full `window.nextendo.*` bridge / Electron main.js, ported to C++.
// libcurl + cJSON. Every ipcMain.handle(...) has an equivalent here.
#pragma once
#include <string>
#include <vector>
#include "cJSON.h"

namespace net {

void init();
void shutdown();

// ---------------------------------------------------------------- HTTP core
struct Resp { long status = 0; std::string body; };
Resp   raw(const char* method, const std::string& url, const std::string& body, const std::string& bearer);

extern const char* API_BASE;      // https://nextendo.network
extern const char* STATUS_BASE;   // https://status.nextendo.network

// Caller owns the returned cJSON* (cJSON_Delete). base defaults to API_BASE.
cJSON* json(const char* method, const std::string& path, const std::string& body = "",
            const std::string& bearer = "", long* outStatus = nullptr, const char* base = nullptr);

std::string jstr(const std::string& v);                 // JSON-encode a string
const char* gs(cJSON* o, const char* k, const char* d = "");
int         gi(cJSON* o, const char* k, int d = 0);
bool        gb(cJSON* o, const char* k, bool d = false);
std::string b64(const unsigned char* data, size_t n);
bool        imageBytesFromDataUri(const std::string& uri, std::string& out); // "data:image/...;base64,X" -> raw bytes

// ---------------------------------------------------------------- multi-account token store
// sdmc:/switch/nextendo-hub/config.json : { current, theme, lang, credit{fc}, accounts:{id:{token,username}} }
struct Account { std::string id, username; bool current = false; };

void                 tokenLoadAll();
bool                 signedIn();
std::string          token();                    // current account's token, "" if none
std::string          currentId();
std::vector<Account> accounts();
void                 addAccount(const std::string& token, const std::string& username);
int                  removeCurrent();            // returns #remaining
bool                 setCurrent(const std::string& id);
void                 removeAccount(const std::string& id);
void                 setCurrentUsername(const std::string& name);

// persisted UI prefs
std::string getPref(const char* key, const std::string& dflt);
void        setPref(const char* key, const std::string& value);

// ---------------------------------------------------------------- feeds (public — no token)
cJSON* serverStatus(long* st);   // -> { fetched, up, down, total, groups:[{name,monitors:[{name,status,ping,uptime24}]}], incidents }
cJSON* onlineCounts(long* st);   // -> { total, jeux:[{nom,joueurs}] }  (sorted desc)

// ---------------------------------------------------------------- auth
struct AuthResult { bool ok = false; std::string error; std::string username, code; };
AuthResult login(const std::string& email, const std::string& password);
AuthResult reg(const std::string& username, const std::string& email,
               const std::string& password, const std::string& country);
void       logout();                         // revokes current + switches to next if any

// ---------------------------------------------------------------- friends
cJSON* friends(long* st);        // -> { me{username,code,pid,avatar}, friends:[norm], requests:[norm], counts{total,online,inGame} }
bool   friendAdd(const std::string& code, std::string& err);
bool   friendAccept(int pid);
bool   friendDecline(int pid);

// ---------------------------------------------------------------- profile / account
cJSON* profileGet(long* st);     // -> { username, country, friend_code, image, color }
bool   profileSave(const std::string& username /*""=keep*/, const std::string& image /*""=keep*/,
                   const std::string& color /*""=keep*/, const std::string& avatarJson /*NULL sentinel via "\0"*/,
                   std::string& err);
bool   usernameSet(const std::string& u, std::string& err);
bool   usernameAvailable(const std::string& u);
std::vector<std::string> countryList();
bool   countrySet(const std::string& code, std::string& err);

// ---------------------------------------------------------------- cloud saves
cJSON* savesList(long* st);      // -> { eligible, reasonCode, isBooster, totalSize, limit, saves:[{titleId,name,size,updated}] }
bool   saveDelete(const std::string& titleId, std::string& err);
bool   saveDownload(const std::string& titleId, const std::string& name, std::string& outPath, std::string& err);

// ---------------------------------------------------------------- mods / avatars
cJSON* modsFavorites(long* st);
cJSON* avatarsList(long* st);    // -> { firmware:[name], custom:[name], base }
bool   avatarImageBytes(const std::string& name, std::string& out);   // raw PNG bytes

} // namespace net
