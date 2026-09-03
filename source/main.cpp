// NextendoHub — Nintendo Switch homebrew (borealis GUI). Full port of the
// desktop NextendoHub: live Status + Online, login/register, multi-account,
// friends, profile edit, cloud saves, favourite mods, settings.
//
// Electron/HTML can't run under homebrew, so borealis draws the UI (Switch-native
// look, themed, real fonts) and net.cpp is the window.nextendo.* bridge.

#include <borealis.hpp>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cctype>
#include "net.hpp"
#include "i18n.hpp"
#include "cJSON.h"

using i18n::T;
using net::gs;
using net::gi;
using net::gb;

static const char* APP_NAME = "Nextendo Hub";

// ================================================================ small helpers
static brls::ListItem* row(const std::string& label, const std::string& value = "") {
    auto* it = new brls::ListItem(label);
    if (!value.empty()) it->setValue(value);
    return it;
}
static void note(brls::List* l, const std::string& text) {
    l->addView(new brls::Label(brls::LabelStyle::DESCRIPTION, text, true));
}
static std::string usernameError(const std::string& u) {           // "" == ok
    if (u.size() < 3 || u.size() > 16) return "3-16 characters";
    for (char c : u) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return "letters, digits, _ or -";
    return "";
}
static bool passwordOk(const std::string& p) {
    bool digit = false, special = false;
    for (char c : p) { if (std::isdigit((unsigned char)c)) digit = true; else if (!std::isalnum((unsigned char)c)) special = true; }
    return p.size() >= 8 && digit && special;
}

// borealis Swkbd is one field at a time — chain callbacks for multi-field forms.
static void askText(const std::string& header, std::function<void(std::string)> cb, const std::string& initial = "") {
    brls::Swkbd::openForText([cb](std::string s) { cb(s); }, header, "", 128, initial);
}

// ================================================================ LiveList
// A borealis List that re-fetches on an interval on a worker thread and rebuilds
// itself on the UI thread. Used for the two polling feeds (Status / Online).
class LiveList : public brls::List {
public:
    using Fetch  = std::function<cJSON*(long*)>;
    using Render = std::function<void(brls::List*, cJSON*, long)>;

    LiveList(Fetch f, Render r, int intervalMs)
        : fetch(std::move(f)), render(std::move(r)), interval(intervalMs) {
        kick();
        this->registerAction(T("refresh_hint"), brls::Key::X, [this]() { kick(); return true; });
    }
    ~LiveList() override { alive = false; if (worker.joinable()) worker.join(); }

    void frame(brls::FrameContext* ctx) override {
        if (ready.exchange(false)) {
            this->clear();
            cJSON* d;
            long st;
            { std::lock_guard<std::mutex> lk(mtx); d = data; data = nullptr; st = status; }
            render(this, d, st);
            if (d) cJSON_Delete(d);
        }
        brls::List::frame(ctx);
        auto now = std::chrono::steady_clock::now();
        if (!busy && std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() >= interval)
            kick();
    }

private:
    void kick() {
        if (busy.exchange(true)) return;
        last = std::chrono::steady_clock::now();
        if (worker.joinable()) worker.join();
        worker = std::thread([this]() {
            long st = 0;
            cJSON* d = fetch(&st);
            { std::lock_guard<std::mutex> lk(mtx); if (data) cJSON_Delete(data); data = d; status = st; }
            ready = true;
            busy = false;
        });
    }
    Fetch fetch; Render render; int interval;
    std::thread worker;
    std::mutex mtx;
    cJSON* data = nullptr;
    long status = 0;
    std::atomic<bool> busy{false}, ready{false}, alive{true};
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::time_point::min();
};

// ================================================================ renderers
static void renderOnline(brls::List* l, cJSON* d, long st) {
    if (!d) { note(l, st ? (std::string(T("unreachable")) + " (HTTP " + std::to_string(st) + ")") : T("unreachable")); return; }
    int total = gi(d, "total");
    cJSON* jeux = cJSON_GetObjectItem(d, "jeux");
    int active = 0;
    cJSON* it;
    cJSON_ArrayForEach(it, jeux) if (gi(it, "joueurs") > 0) active++;
    l->addView(new brls::Header(std::to_string(total) + " " + T("players_ingame") + "  ·  " +
                                std::to_string(active) + " " + T("games_active")));
    cJSON_ArrayForEach(it, jeux) {
        int n = gi(it, "joueurs");
        if (n <= 0) continue;
        l->addView(row(gs(it, "nom"), std::to_string(n) + " " + (n == 1 ? T("player") : T("players"))));
    }
    if (!active) note(l, T("no_data"));
}

static void renderStatus(brls::List* l, cJSON* d, long st) {
    if (!d) { note(l, T("unreachable")); return; }
    int up = gi(d, "up"), down = gi(d, "down"), total = gi(d, "total");
    l->addView(new brls::Header(down ? (std::to_string(down) + " " + T("services_down")) : T("all_ok")));
    note(l, std::to_string(up) + " / " + std::to_string(total) + " " + T("up_of"));
    cJSON* g;
    cJSON_ArrayForEach(g, cJSON_GetObjectItem(d, "groups")) {
        l->addView(new brls::Header(gs(g, "name", "Nextendo")));
        cJSON* m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItem(g, "monitors")) {
            int s = gi(m, "status", -1);
            const char* tag = s == 1 ? T("st_up") : s == 0 ? T("st_down") : s == 3 ? T("st_maint") : "?";
            std::string v = tag;
            if (cJSON_GetObjectItem(m, "ping")) v += "  ·  " + std::to_string(gi(m, "ping")) + " ms";
            l->addView(row(gs(m, "name"), v));
        }
    }
    cJSON* inc;
    cJSON_ArrayForEach(inc, cJSON_GetObjectItem(d, "incidents"))
        note(l, std::string("⚠ ") + gs(inc, "title") + "\n" + gs(inc, "content"));
}

// ================================================================ FRIENDS tab
static void buildFriends(brls::List* l);   // fwd

static void doLoginFlow(brls::List* l, bool registerMode) {
    askText(T("email"), [l, registerMode](std::string email) {
        if (email.empty()) return;
        askText(T("password"), [l, registerMode, email](std::string pw) {
            if (pw.empty()) return;
            if (registerMode) {
                if (!passwordOk(pw)) { brls::Application::notify(T("failed")); return; }
                askText(T("password_again"), [l, email, pw](std::string pw2) {
                    if (pw2 != pw) { brls::Application::notify("passwords differ"); return; }
                    askText(T("username"), [l, email, pw](std::string u) {
                        if (!usernameError(u).empty()) { brls::Application::notify(usernameError(u)); return; }
                        net::AuthResult r = net::reg(u, email, pw, "");
                        brls::Application::notify(r.ok ? T("saved") : r.error);
                        if (r.ok) buildFriends(l);
                    });
                });
            } else {
                net::AuthResult r = net::login(email, pw);
                brls::Application::notify(r.ok ? (std::string("✓ ") + r.username) : r.error);
                if (r.ok) buildFriends(l);
            }
        });
    });
}

static void buildFriends(brls::List* l) {
    l->clear();

    if (!net::signedIn()) {
        note(l, T("not_signed_in"));
        auto* si = row(T("sign_in"));
        si->getClickEvent()->subscribe([l](brls::View*) { doLoginFlow(l, false); });
        l->addView(si);
        auto* cr = row(T("create_account"));
        cr->getClickEvent()->subscribe([l](brls::View*) { doLoginFlow(l, true); });
        l->addView(cr);
        return;
    }

    // account switcher
    auto accs = net::accounts();
    l->addView(new brls::Header(T("account")));
    if (accs.size() > 1) {
        auto* sw = row(T("switch_account"));
        sw->getClickEvent()->subscribe([l, accs](brls::View*) {
            std::vector<std::string> names;
            int sel = 0;
            for (size_t i = 0; i < accs.size(); i++) { names.push_back(accs[i].username); if (accs[i].current) sel = (int)i; }
            brls::Dropdown::open(T("switch_account"), names, [l, accs](int idx) {
                if (idx >= 0 && idx < (int)accs.size() && net::setCurrent(accs[idx].id)) buildFriends(l);
            }, sel);
        });
        l->addView(sw);
    }
    auto* add = row(T("add_account"));
    add->getClickEvent()->subscribe([l](brls::View*) { doLoginFlow(l, false); });
    l->addView(add);
    auto* out = row(T("sign_out"));
    out->getClickEvent()->subscribe([l](brls::View*) { net::logout(); buildFriends(l); });
    l->addView(out);

    // add friend by code
    auto* af = row(T("add_friend"));
    af->getClickEvent()->subscribe([l](brls::View*) {
        askText(T("friend_code"), [l](std::string code) {
            if (code.empty()) return;
            std::string e;
            bool ok = net::friendAdd(code, e);
            brls::Application::notify(ok ? T("saved") : e);
            if (ok) buildFriends(l);
        });
    });
    l->addView(af);

    long st = 0;
    cJSON* d = net::friends(&st);
    if (st == 401 || !net::signedIn()) { note(l, T("session_expired")); if (d) cJSON_Delete(d); return; }
    if (!d) { note(l, std::string(T("unreachable")) + " (HTTP " + std::to_string(st) + ")"); return; }

    cJSON* cnt = cJSON_GetObjectItem(d, "counts");
    l->addView(new brls::Header(std::string(T("friends_hdr")) + "  ·  " +
        std::to_string(gi(cnt, "online")) + " " + T("online_word") + "  ·  " +
        std::to_string(gi(cnt, "inGame")) + " " + T("in_game")));

    cJSON* reqs = cJSON_GetObjectItem(d, "requests");
    if (cJSON_GetArraySize(reqs) > 0) {
        l->addView(new brls::Header(T("friend_requests")));
        cJSON* r;
        cJSON_ArrayForEach(r, reqs) {
            int pid = gi(r, "pid");
            auto* it = row(gs(r, "name"), std::string(T("wants_to_add")));
            it->getClickEvent()->subscribe([l, pid](brls::View*) {
                auto* dlg = new brls::Dialog(T("friend_requests"));
                dlg->addButton(T("accept"),  [l, pid](brls::View*) { net::friendAccept(pid);  buildFriends(l); });
                dlg->addButton(T("decline"), [l, pid](brls::View*) { net::friendDecline(pid); buildFriends(l); });
                dlg->open();
            });
            l->addView(it);
        }
    }

    l->addView(new brls::Header(T("friends_hdr")));
    cJSON* f;
    cJSON_ArrayForEach(f, cJSON_GetObjectItem(d, "friends")) {
        std::string state = gb(f, "inGame")
            ? (std::string(T("playing")) + " " + (gs(f, "game")[0] ? gs(f, "game") : "..."))
            : gb(f, "online") ? T("online_word") : T("offline_word");
        std::string name = gs(f, "name");
        if (gb(f, "favorite")) name += "  ★";
        l->addView(row(name, state));
    }
    if (gi(cnt, "total") == 0) note(l, T("no_friends"));
    cJSON_Delete(d);
}

// ================================================================ SETTINGS tab
static const char* SWATCHES[] = { "#1ca9e0","#e4404a","#36ce73","#8b5cf6","#f59e0b","#ec4899",
                                  "#14b8a6","#eab308","#6366f1","#ef7c3a","#22d3ee","#64748b" };

static void applyTheme(const std::string& t) {
    // borealis auto-follows the console theme; "light"/"dark" force it.
    if (t == "light") brls::Application::setThemeVariant(brls::ThemeVariant::LIGHT);
    else if (t == "dark") brls::Application::setThemeVariant(brls::ThemeVariant::DARK);
    // "system" -> leave borealis' auto-detection alone
}

static void buildAvatarGallery(brls::List* parent) {
    long st = 0;
    cJSON* m = net::avatarsList(&st);
    if (!m) { brls::Application::notify(T("failed")); return; }
    auto* list = new brls::List();
    auto addSet = [&](const char* key, const char* header) {
        list->addView(new brls::Header(header));
        cJSON* a = cJSON_GetObjectItem(m, key);
        cJSON* it;
        cJSON_ArrayForEach(it, a) {
            if (!cJSON_IsString(it)) continue;
            std::string name = it->valuestring;
            auto* r = row(name);
            r->getClickEvent()->subscribe([name, parent](brls::View*) {
                std::string bytes;
                if (!net::avatarImageBytes(name, bytes)) { brls::Application::notify(T("failed")); return; }
                std::string img = "data:image/png;base64," +
                    net::b64((const unsigned char*)bytes.data(), bytes.size());
                std::string avatarJson = "{\"char\":\"" + name + "\"}";
                std::string e;
                bool ok = net::profileSave("", img, "", avatarJson, e);
                brls::Application::notify(ok ? T("saved") : e);
                if (ok) brls::Application::popView();
            });
            list->addView(r);
        }
    };
    addSet("firmware", "Switch");
    addSet("custom", "Custom");
    cJSON_Delete(m);
    brls::Application::pushView(list);
}

static void buildSettings(brls::List* l) {
    l->clear();

    // ---- appearance ----
    l->addView(new brls::Header(T("tab_settings")));
    std::string theme = net::getPref("theme", "system");
    auto* th = row(T("theme"), theme == "light" ? T("th_light") : theme == "dark" ? T("th_dark") : T("th_system"));
    th->getClickEvent()->subscribe([l](brls::View*) {
        brls::Dropdown::open(T("theme"), { T("th_system"), T("th_light"), T("th_dark") }, [l](int i) {
            const char* v = i == 1 ? "light" : i == 2 ? "dark" : "system";
            net::setPref("theme", v); applyTheme(v); buildSettings(l);
        }, net::getPref("theme", "system") == "light" ? 1 : net::getPref("theme", "system") == "dark" ? 2 : 0);
    });
    l->addView(th);

    auto* lang = row(T("language"), i18n::lang() == "fr" ? "Français" : i18n::lang() == "es" ? "Español" : "English");
    lang->getClickEvent()->subscribe([l](brls::View*) {
        brls::Dropdown::open(T("language"), { "English", "Français", "Español" }, [l](int i) {
            i18n::setLang(i == 1 ? "fr" : i == 2 ? "es" : "en");
            buildSettings(l);
            brls::Application::notify("Reopen tabs to fully re-translate");
        }, i18n::lang() == "fr" ? 1 : i18n::lang() == "es" ? 2 : 0);
    });
    l->addView(lang);

    auto* su = row(T("startup"), T("startup_na"));
    su->setValue(T("startup_na"), true);
    l->addView(su);

    // ---- account section (only when signed in) ----
    if (net::signedIn()) {
        long st = 0;
        cJSON* p = net::profileGet(&st);
        l->addView(new brls::Header(T("profile")));

        // username
        std::string curU = p ? gs(p, "username", "") : "";
        auto* un = row(T("username"), curU);
        un->getClickEvent()->subscribe([l, curU](brls::View*) {
            askText(T("username"), [l](std::string u) {
                std::string err = usernameError(u);
                if (!err.empty()) { brls::Application::notify(err); return; }
                if (!net::usernameAvailable(u)) { brls::Application::notify(T("taken")); return; }
                std::string e;
                bool ok = net::usernameSet(u, e);
                brls::Application::notify(ok ? T("saved") : e);
                if (ok) buildSettings(l);
            }, curU);
        });
        l->addView(un);

        // country
        std::string curC = p ? gs(p, "country", "") : "";
        auto* cn = row(T("country"), curC.empty() ? "--" : curC);
        cn->getClickEvent()->subscribe([l](brls::View*) {
            auto codes = net::countryList();
            if (codes.empty()) { brls::Application::notify(T("failed")); return; }
            int sel = 0;
            std::string cur = net::getPref("_country_cache", "");
            for (size_t i = 0; i < codes.size(); i++) if (codes[i] == cur) sel = (int)i;
            brls::Dropdown::open(T("country"), codes, [l, codes](int i) {
                if (i < 0 || i >= (int)codes.size()) return;
                std::string e;
                bool ok = net::countrySet(codes[i], e);
                net::setPref("_country_cache", codes[i]);
                brls::Application::notify(ok ? T("saved") : e);
                if (ok) buildSettings(l);
            }, sel);
        });
        l->addView(cn);

        // profile picture — gallery
        auto* pic = row(T("choose_avatar"));
        pic->getClickEvent()->subscribe([l](brls::View*) { buildAvatarGallery(l); });
        l->addView(pic);

        // colour
        auto* col = row(T("color"), p ? gs(p, "color", "") : "");
        col->getClickEvent()->subscribe([l](brls::View*) {
            std::vector<std::string> opts(SWATCHES, SWATCHES + 12);
            brls::Dropdown::open(T("color"), opts, [l, opts](int i) {
                if (i < 0 || i >= 12) return;
                std::string e;
                bool ok = net::profileSave("", "", opts[i], std::string("\0", 1), e);
                brls::Application::notify(ok ? T("saved") : e);
                if (ok) buildSettings(l);
            }, 0);
        });
        l->addView(col);

        // ---- cloud saves ----
        l->addView(new brls::Header(T("cloud_saves")));
        long ss = 0;
        cJSON* sv = net::savesList(&ss);
        if (!sv) {
            note(l, T("unreachable"));
        } else if (!gb(sv, "eligible")) {
            note(l, std::string(gs(sv, "reasonCode")) == "email" ? T("gate_email") : T("gate_discord"));
        } else {
            int used = gi(sv, "totalSize"), lim = gi(sv, "limit", 1);
            char q[64]; snprintf(q, sizeof(q), "%.1f / %.1f MB", used / 1048576.0, lim / 1048576.0);
            note(l, q);
            cJSON* s;
            int scount = 0;
            cJSON_ArrayForEach(s, cJSON_GetObjectItem(sv, "saves")) {
                scount++;
                std::string tid = gs(s, "titleId"), nm = gs(s, "name");
                int sz = gi(s, "size", -1);
                std::string sub = sz >= 0 ? (std::to_string(sz / 1024) + " KB") : "";
                auto* it = row(nm, sub);
                it->getClickEvent()->subscribe([l, tid, nm](brls::View*) {
                    auto* dlg = new brls::Dialog(nm);
                    dlg->addButton(T("download"), [l, tid, nm](brls::View*) {
                        std::string path, e;
                        bool ok = net::saveDownload(tid, nm, path, e);
                        brls::Application::notify(ok ? (std::string(T("downloaded")) + " " + path) : e);
                    });
                    dlg->addButton(T("del"), [l, tid](brls::View*) {
                        std::string e; net::saveDelete(tid, e); buildSettings(l);
                    });
                    dlg->open();
                });
                l->addView(it);
            }
            if (!scount) note(l, T("no_saves"));
        }
        if (sv) cJSON_Delete(sv);

        // ---- favourite mods ----
        l->addView(new brls::Header(T("fav_mods")));
        long ms = 0;
        cJSON* mods = net::modsFavorites(&ms);
        if (!mods || cJSON_GetArraySize(mods) == 0) {
            note(l, T("no_mods"));
        } else {
            cJSON* m;
            cJSON_ArrayForEach(m, mods) {
                std::string sub = gs(m, "game");
                if (gs(m, "author")[0]) sub += std::string(sub.empty() ? "" : "  ·  ") + gs(m, "author");
                l->addView(row(gs(m, "name"), sub));
            }
        }
        if (mods) cJSON_Delete(mods);

        if (p) cJSON_Delete(p);
    } else {
        note(l, T("not_signed_in") + std::string("  (") + T("tab_friends") + ")");
    }

    // ---- credit ----
    l->addView(new brls::Header(T("about")));
    note(l, "NextendoHub - unofficial client for nextendo.network.");
    l->addView(row(T("made_by"), "adxmm"));
    l->addView(row(T("friend_code"), net::getPref("credit_fc", "SW-????-????-????")));
    l->addView(row("Discord", "diavolo.__"));
    l->addView(row(T("founders"), "JuanBrew  ·  Kazu"));
    l->addView(row(T("version"), "1.0.0"));

    // capture the maker's own friend code the first time adxmm signs in
    if (net::signedIn()) {
        long st = 0;
        cJSON* me = net::profileGet(&st);
        if (me) {
            std::string u = gs(me, "username", "");
            std::string fc = gs(me, "friend_code", "");
            for (auto& c : u) c = (char)tolower((unsigned char)c);
            if (u == "adxmm" && !fc.empty()) net::setPref("credit_fc", fc);
            cJSON_Delete(me);
        }
    }
}

// ================================================================ main
int main(int argc, char* argv[]) {
    brls::Logger::setLogLevel(brls::LogLevel::INFO);
    if (!brls::Application::init(APP_NAME)) return EXIT_FAILURE;
    net::init();
    i18n::loadLang();
    applyTheme(net::getPref("theme", "system"));

    auto* root = new brls::TabFrame();
    root->setTitle(APP_NAME);
    root->setIcon("romfs:/icon.jpg");

    root->addTab(T("tab_online"),  new LiveList(net::onlineCounts, renderOnline, 15000));
    root->addTab(T("tab_status"),  new LiveList(net::serverStatus, renderStatus, 60000));

    auto* friends = new brls::List();
    buildFriends(friends);
    friends->registerAction(T("refresh_hint"), brls::Key::X, [friends]() { buildFriends(friends); return true; });
    root->addTab(T("tab_friends"), friends);

    root->addSeparator();

    auto* settings = new brls::List();
    buildSettings(settings);
    settings->registerAction(T("refresh_hint"), brls::Key::X, [settings]() { buildSettings(settings); return true; });
    root->addTab(T("tab_settings"), settings);

    brls::Application::pushView(root);
    while (brls::Application::mainLoop());

    net::shutdown();
    return EXIT_SUCCESS;
}
