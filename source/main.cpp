// NextendoHub — Nintendo Switch homebrew (borealis GUI). Full port of the
// desktop NextendoHub: live Status + Online, login/register, multi-account,
// friends, profile edit, cloud saves, favourite mods, settings.
//
// Electron/HTML can't run under homebrew, so borealis draws the UI (Switch-native
// look, themed, real fonts) and net.cpp is the window.nextendo.* bridge.

#include <borealis.hpp>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <functional>
#include <pthread.h>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cctype>
#include <ctime>
#include <switch.h>
#include "net.hpp"
#include "i18n.hpp"
#include "cJSON.h"

using i18n::T;
using net::gs;
using net::gi;
using net::gb;

static const char* APP_NAME = "Nextendo Hub";

// ================================================================ theme
// borealis's default look is Nintendo's own system blue (HorizonLightTheme /
// HorizonDarkTheme, see theme.cpp) — not this app's branding. Subclass both
// and override the accent-bearing fields, the base ground colour, AND the
// sidebar colour to the *actual* desktop app's values, read straight out of
// renderer/index.html's CSS :root / :root[data-theme="dark"] blocks:
//   --accent:#d6197d / #ff3ea5   --ground:#f4f0fa / #0e0a15   --raise:#ffffff / #171021
// The sidebar/dialog fields matter as much as --ground: TabFrame's sidebar
// (and Dialog's own panel) are separate Theme fields from the page
// background — leaving them at Horizon's own defaults while --ground was
// fixed is exactly what produced a sidebar that didn't match the rest of
// the app.
static NVGcolor ACCENT_LIGHT() { return nvgRGB(0xd6, 0x19, 0x7d); }
static NVGcolor ACCENT_DARK()  { return nvgRGB(0xff, 0x3e, 0xa5); }

class NextendoLightTheme : public brls::HorizonLightTheme {
public:
    NextendoLightTheme() : brls::HorizonLightTheme() {
        NVGcolor a = ACCENT_LIGHT();
        activeTabColor                      = a;
        highlightColor1                     = a;
        listItemValueColor                  = a;
        headerRectangleColor                = a;
        buttonPrimaryEnabledBackgroundColor = a;
        dialogButtonColor                   = a;
        backgroundColor[0] = 0xf4 / 255.0f; backgroundColor[1] = 0xf0 / 255.0f; backgroundColor[2] = 0xfa / 255.0f;
        backgroundColorRGB = nvgRGB(0xf4, 0xf0, 0xfa);
        sidebarColor          = nvgRGB(0xff, 0xff, 0xff); // --raise
        dialogColor           = nvgRGB(0xff, 0xff, 0xff); // --raise
        tableEvenBackgroundColor = nvgRGB(0xe9, 0xe2, 0xf3); // --sink
        sidebarSeparatorColor = nvgRGB(0xd8, 0xd0, 0xe6);
        listItemSeparatorColor = nvgRGB(0xe4, 0xdc, 0xf0);
    }
};
class NextendoDarkTheme : public brls::HorizonDarkTheme {
public:
    NextendoDarkTheme() : brls::HorizonDarkTheme() {
        NVGcolor a = ACCENT_DARK();
        activeTabColor                      = a;
        highlightColor1                     = a;
        listItemValueColor                  = a;
        headerRectangleColor                = a;
        buttonPrimaryEnabledBackgroundColor = a;
        dialogButtonColor                   = a;
        backgroundColor[0] = 0x0e / 255.0f; backgroundColor[1] = 0x0a / 255.0f; backgroundColor[2] = 0x15 / 255.0f;
        backgroundColorRGB = nvgRGB(0x0e, 0x0a, 0x15);
        sidebarColor          = nvgRGB(0x17, 0x10, 0x21); // --raise
        dialogColor           = nvgRGB(0x17, 0x10, 0x21); // --raise
        tableEvenBackgroundColor = nvgRGB(0x12, 0x0c, 0x1c); // --sink
        sidebarSeparatorColor = nvgRGB(0x2a, 0x22, 0x38);
        listItemSeparatorColor = nvgRGB(0x28, 0x20, 0x36);
    }
};

// The Switch-native focus highlight (the rounded box that tracks whatever's
// selected — the closest thing this UI has to "a button") was drawn nearly
// square: Style.Highlight.cornerRadius defaults to 0.5f (View::
// getHighlightMetrics(), confirmed by reading view.hpp/view.cpp — it's a
// literal pixel radius passed straight to nvgRoundedRect, not a ratio).
// Rounded that up along with Dialog's and Button's own corner radii for the
// same reason.
class NextendoStyle : public brls::HorizonStyle {
public:
    NextendoStyle() : brls::HorizonStyle() {
        Highlight.cornerRadius = 14.0f;
        Dialog.cornerRadius    = 16.0f;
        Button.cornerRadius    = 12.0f;
    }
};

// The exe's body background is two soft radial glows (--grad-a pink /
// --grad-b purple, at low opacity — --wash) near the top-left/top-right
// corners, over the flat --ground colour above. borealis's Theme struct is
// flat-colour only, but it draws through NanoVG underneath, which does
// support radial gradients (nvgRadialGradient) — paint them directly as a
// custom Background, drawn once per frame right after the flat clear
// (Application::frame() clears to theme->backgroundColorRGB, *then* calls
// Background::frame(), confirmed by reading application.cpp).
class NextendoBackground : public brls::Background {
public:
    void preFrame() override {}
    void postFrame() override {}
    void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override {
        bool dark = brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
        unsigned char washA = dark ? 56 : 36; // --wash: .22 / .14 of 255

        auto blob = [&](float cxFrac, float cyFrac, float rw, float rh, NVGcolor inner) {
            float cx = x + (float)width * cxFrac;
            float cy = y + (float)height * cyFrac;
            nvgSave(vg);
            nvgTranslate(vg, cx, cy);
            nvgScale(vg, 1.0f, rh / rw); // circle -> ellipse (rw half-width, rh half-height)
            NVGpaint p = nvgRadialGradient(vg, 0, 0, 0, rw, inner, nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgRect(vg, -rw * 2, -rw * 2, rw * 4, rw * 4);
            nvgFillPaint(vg, p);
            nvgFill(vg);
            nvgRestore(vg);
        };
        // matches "radial-gradient(700px 440px at 6% -10%, ...)" and
        // "(760px 560px at 114% 8%, ...)" from index.html, in fractions of
        // this frame's own size so it holds up at any resolution.
        blob(0.06f, -0.10f, (float)width * 0.42f, (float)height * 0.55f, nvgRGBA(0xff, 0x2e, 0x97, washA));
        blob(1.14f,  0.08f, (float)width * 0.46f, (float)height * 0.70f, nvgRGBA(0x7b, 0x2f, 0xf7, washA));
    }
};

// ================================================================ small helpers
// Matches the exe's fmtCode(): strip everything but alnum, drop a leading
// "SW", and if what's left is exactly 12 digits, group it as
// SW-XXXX-XXXX-XXXX. Anything else (a code that isn't 12 digits) is
// returned unchanged, same as the exe.
static std::string fmtCode(const std::string& c) {
    if (c.empty()) return c;
    std::string raw;
    for (char ch : c) if (std::isalnum((unsigned char)ch)) raw += (char)std::toupper((unsigned char)ch);
    std::string digits = (raw.rfind("SW", 0) == 0) ? raw.substr(2) : raw;
    if (digits.size() == 12 && digits.find_first_not_of("0123456789") == std::string::npos)
        return "SW-" + digits.substr(0, 4) + "-" + digits.substr(4, 4) + "-" + digits.substr(8, 4);
    return c;
}
static brls::ListItem* row(const std::string& label, const std::string& value = "") {
    auto* it = new brls::ListItem(label);
    if (!value.empty()) it->setValue(value);
    return it;
}
// A plain brls::Label is NOT focusable. borealis's `legacy` branch has a
// confirmed, unfixed bug (natinusala/borealis#37) where a List/ScrollView
// with zero focusable children segfaults (null `this` in
// ScrollView::updateScrolling -> View::getY) as soon as it's shown or
// scrolled — the fix landed only on borealis's later `yoga` rewrite, never
// backported here. Every "note" must therefore be a real (focusable)
// ListItem, never a bare Label, or any all-Label list (e.g. "unreachable" /
// "no data" states) reproduces that exact crash.
static void note(brls::List* l, const std::string& text) {
    l->addView(new brls::ListItem(text));
}

// A ListItem that actually renders its thumbnail in the right place.
//
// Stock borealis (legacy) is buggy here: ListItem::layout() repositions the
// thumbnail Image with setBoundaries() but then only calls invalidate()
// (deferred) on it — so the Image's NanoVG image-pattern (built in
// Image::layout() from getX()/getY()) can stay anchored at the Image's
// previous/zero origin for one or more frames. The rounded-rect is filled at
// the correct spot with a pattern sampled from the wrong spot => the picture
// looks detached from its row (the "floating flag") or smears into a flat
// colour block (the "oversized pink box"). On a row that also has a
// description, ListItem::layout() additionally sizes the thumbnail to the
// description-grown height, making it far too tall.
//
// This subclass re-pins the thumbnail to a fixed square at the row's left
// edge (label line only) and forces an *immediate* re-layout every pass, so
// the pattern is always rebuilt against the final coordinates.
class ThumbListItem : public brls::ListItem {
public:
    ThumbListItem(const std::string& label, const std::string& description = "", const std::string& subLabel = "")
        : brls::ListItem(label, description, subLabel) {}

    // Give the row its picture from raw encoded image bytes (PNG/JPEG).
    // Keeps its own copy; borealis's Image also copies internally.
    // wide == true: use a 4:3 box filled edge-to-edge (SCALE) instead of a
    // square — borealis's FIT math is broken for non-square images (it makes
    // a 4:3 flag *taller* than wide, overflowing the row), so square avatars
    // stay on FIT and only the country flag opts into this.
    void setPicture(const std::string& bytes, bool wide = false) {
        this->picture = bytes;
        this->wideThumb = wide;
        if (this->picture.empty()) return;
        this->setThumbnail((unsigned char*)this->picture.data(), this->picture.size());
        if (this->thumbnailView)
            this->thumbnailView->setScaleType(wide ? brls::ImageScaleType::SCALE
                                                   : brls::ImageScaleType::FIT);
        // nvgCreateImageMem() doesn't finish uploading the texture within the
        // same frame, so the first Image::layout() sees nvgImageSize()==0 ->
        // a NaN aspect ratio -> the picture draws smeared at a garbage size
        // and spot (every avatar piling up in one clump = the "muddled"
        // gallery). Nothing marks the row dirty again once the texture *is*
        // ready, so re-run our layout for a few frames to catch up.
        this->settleFrames = 10;
    }

    void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override {
        brls::ListItem::layout(vg, style, stash);
        if (this->thumbnailView) {
            unsigned pad = style->List.Item.thumbnailPadding;
            unsigned h   = style->List.Item.height - pad * 2; // label line, not the grown height
            unsigned w   = this->wideThumb ? (h * 4) / 3 : h;
            this->thumbnailView->setBoundaries(this->x + pad, this->y + pad, w, h);
            this->thumbnailView->invalidate(true);           // rebuild imgPaint against final coords NOW
        }
    }

    void frame(brls::FrameContext* ctx) override {
        if (this->settleFrames > 0 && this->thumbnailView) {
            this->invalidate(true);   // re-read nvgImageSize now the GL upload has landed
            this->settleFrames--;
        }
        brls::ListItem::frame(ctx);
    }

private:
    std::string picture;
    bool wideThumb = false;
    int  settleFrames = 0;
};
static std::string usernameError(const std::string& u) {           // "" == ok
    if (u.size() < 3 || u.size() > 16) return T("uname_len_rule");
    for (char c : u) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return T("uname_char_rule");
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
// The exe keeps the last successfully-fetched data on screen (with a small
// "offline · HH:MM" tag) when a poll fails, rather than blanking to an error
// — see renderStatus/renderOnline in the desktop app's renderer/app.js
// (statusStale/onlineStale). LiveList now mirrors that: `lastGood` persists
// across failed fetches, `stale` says whether the *current* row is fresh.
class LiveList : public brls::List {
public:
    using Fetch  = std::function<cJSON*(long*)>;
    using Render = std::function<void(brls::List*, cJSON*, long, bool, time_t)>;

    LiveList(Fetch f, Render r, int intervalMs)
        : fetch(std::move(f)), render(std::move(r)), interval(intervalMs) {
        // Must never be empty when first shown — see the note() comment
        // above (natinusala/borealis#37). The background fetch hasn't even
        // started yet at construction time, so add a placeholder row now;
        // frame() replaces it with real content once data arrives.
        this->addView(new brls::ListItem(T("loading")));
        registry().push_back(this);
        kick();
        this->registerAction(T("refresh_hint"), brls::Key::X, [this]() { kick(); return true; });
    }
    ~LiveList() override {
        alive = false;
        if (started) { pthread_join(worker, nullptr); started = false; }
        if (lastGood) cJSON_Delete(lastGood);
        auto& r = registry();
        r.erase(std::remove(r.begin(), r.end(), this), r.end());
    }

    // Both LiveLists are heap-allocated in main() and never explicitly
    // deleted before the app exits (they live for the process's whole
    // lifetime) — so on exit their worker threads can still be mid-fetch,
    // actively using libcurl/sockets, at the exact moment main() runs
    // net::shutdown() (curl_global_cleanup()/socketExit()) right after the
    // main loop ends. That's a live use of a resource that's being torn
    // down from another thread — a real, reproducible crash-on-close, not
    // a hang: worst case this blocks briefly (curl's own 25s timeout caps
    // it) until the in-flight fetch actually finishes.
    static void joinAllBeforeShutdown() {
        for (LiveList* l : registry()) if (l->started) { pthread_join(l->worker, nullptr); l->started = false; }
    }

    void frame(brls::FrameContext* ctx) override {
        if (ready.exchange(false)) {
            cJSON* d; long st;
            { std::lock_guard<std::mutex> lk(mtx); d = data; data = nullptr; st = status; }
            if (d) {
                if (lastGood) cJSON_Delete(lastGood);
                lastGood = d;
                lastGoodAt = time(nullptr);
                stale = false;
            } else {
                stale = true; // keep serving lastGood, just flagged as stale
            }
            this->clear();
            render(this, lastGood, st, stale, lastGoodAt);
            // borealis's View::~View() clears Application's global focus if the
            // view being destroyed was the focused one (see application.cpp).
            // clear() just destroyed whatever was focused (the placeholder on
            // first load, or a real row on a later refresh) — if nothing else
            // claimed focus since, every controller input silently no-ops from
            // here on: Application::navigate() returns immediately when
            // getCurrentFocus() is null. Restore it, mirroring the same
            // giveFocus(view->getDefaultFocus()) double-indirection borealis's
            // own pushView() uses (this List's own getDefaultFocus() alone
            // returns its raw content layout on this branch, not a real item —
            // giveFocus() resolves that one level further internally).
            if (!brls::Application::getCurrentFocus())
                brls::Application::giveFocus(this->getDefaultFocus());
        }
        brls::List::frame(ctx);
        auto now = std::chrono::steady_clock::now();
        if (!busy && std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() >= interval)
            kick();
    }

private:
    static std::vector<LiveList*>& registry() { static std::vector<LiveList*> v; return v; }

    // std::thread's default stack on Switch is too small for libcurl doing a
    // TLS handshake through mbedTLS — it reliably stack-overflows and crashes
    // the whole process. Use pthread directly so we can size the stack.
    static constexpr size_t kWorkerStackSize = 256 * 1024;

    static void* threadMain(void* arg) {
        auto* self = static_cast<LiveList*>(arg);
        long st = 0;
        cJSON* d = self->fetch(&st);
        { std::lock_guard<std::mutex> lk(self->mtx); if (self->data) cJSON_Delete(self->data); self->data = d; self->status = st; }
        self->ready = true;
        self->busy = false;
        return nullptr;
    }

    void kick() {
        if (busy.exchange(true)) return;
        last = std::chrono::steady_clock::now();
        if (started) { pthread_join(worker, nullptr); started = false; }
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, kWorkerStackSize);
        if (pthread_create(&worker, &attr, &LiveList::threadMain, this) == 0) started = true;
        else busy = false; // couldn't spawn — try again next frame
        pthread_attr_destroy(&attr);
    }
    Fetch fetch; Render render; int interval;
    pthread_t worker{};
    bool started = false;
    std::mutex mtx;
    cJSON* data = nullptr;      // raw result from the worker, pending pickup
    long status = 0;
    cJSON* lastGood = nullptr;  // last successfully-fetched payload (retained across failures)
    time_t lastGoodAt = 0;      // 0 == never had a successful fetch yet
    bool stale = false;
    std::atomic<bool> busy{false}, ready{false}, alive{true};
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::time_point::min();
};

static std::string fmtHM(time_t t) {
    if (!t) return "";
    struct tm tmv; localtime_r(&t, &tmv);
    char b[8]; snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return b;
}
// "live · 14:32" / "offline · 14:30" / "connecting…" — same freshness tag
// the exe shows next to each tab's heading (setTag() in app.js).
static std::string freshnessTag(bool haveData, bool stale, time_t at) {
    if (!haveData) return T("connecting");
    return std::string(T(stale ? "offline" : "live")) + " · " + fmtHM(at);
}

// ================================================================ renderers
// Both match the exe's per-tab layout: a heading row with a live/offline
// freshness tag on the right (its <h1> + <span class="tag">), matching text
// exactly (online_heading/status_heading = its "In game right now" /
// "Server status" <h1>s — the taskbar's own short "Online"/"Status" labels
// are separate, see i18n.hpp).
static void renderOnline(brls::List* l, cJSON* d, long st, bool stale, time_t at) {
    auto* head = new brls::ListItem(T("online_heading"));
    head->setValue(freshnessTag(d != nullptr, stale, at));
    l->addView(head);

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

static void renderStatus(brls::List* l, cJSON* d, long st, bool stale, time_t at) {
    auto* head = new brls::ListItem(T("status_heading"));
    head->setValue(freshnessTag(d != nullptr, stale, at));
    l->addView(head);

    if (!d) { note(l, T("unreachable")); return; }
    int up = gi(d, "up"), down = gi(d, "down"), total = gi(d, "total");
    l->addView(new brls::Header(down ? (std::to_string(down) + " " + T(down == 1 ? "down_one" : "down_many")) : T("all_ok")));
    note(l, std::to_string(up) + " / " + std::to_string(total) + " " + T("up_of"));
    cJSON* g;
    cJSON_ArrayForEach(g, cJSON_GetObjectItem(d, "groups")) {
        l->addView(new brls::Header(gs(g, "name", "Nextendo")));
        cJSON* m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItem(g, "monitors")) {
            int s = gi(m, "status", -1);
            const char* tag = s == 1 ? T("st_up") : s == 0 ? T("st_down") : s == 3 ? T("st_maint") : T("st_up");
            // Same meta line as the exe: "<uptime%> · <ping> ms", falling
            // back to the plain status word when neither number is present.
            std::vector<std::string> meta;
            cJSON* up24 = cJSON_GetObjectItem(m, "uptime24");
            if (cJSON_IsNumber(up24)) {
                double pct = up24->valuedouble * 100.0;
                double r = (double)((long long)(pct * 100.0 + 0.5)) / 100.0; // round to 2dp
                char b[16];
                if (r >= 100.0) snprintf(b, sizeof(b), "100%%");
                else if (r == (double)(long long)r) snprintf(b, sizeof(b), "%lld%%", (long long)r);
                else snprintf(b, sizeof(b), "%.2f%%", r);
                meta.push_back(b);
            }
            if (cJSON_GetObjectItem(m, "ping")) meta.push_back(std::to_string(gi(m, "ping")) + " ms");
            std::string v;
            for (size_t i = 0; i < meta.size(); i++) v += (i ? "  ·  " : "") + meta[i];
            if (v.empty()) v = tag;
            l->addView(row(gs(m, "name"), v));
        }
    }
    cJSON* inc;
    cJSON_ArrayForEach(inc, cJSON_GetObjectItem(d, "incidents"))
        l->addView(new brls::ListItem(std::string("⚠ ") + gs(inc, "title"), gs(inc, "content")));
}

// ================================================================ FRIENDS tab
static void buildFriends(brls::List* l);          // fwd (defined after buildFriendsImpl below)
static void buildAvatarGallery(brls::List* l);     // fwd (defined in the Settings section below)

// Each swkbd screen only ever shows one bare field name ("Email address",
// then later "Password") with nothing tying them together as steps of the
// same sign-in — prefix every prompt with which flow it's part of ("Sign
// in — Email address", "Sign in — Password", ...) so it reads as a
// sequence instead of a string of unrelated questions.
static void doLoginFlow(brls::List* l, bool registerMode) {
    std::string mode = T(registerMode ? "create_account" : "sign_in");
    askText(mode + " — " + T("email"), [l, registerMode, mode](std::string email) {
        if (email.empty()) return;
        askText(mode + " — " + T("password"), [l, registerMode, email, mode](std::string pw) {
            if (pw.empty()) return;
            if (registerMode) {
                if (!passwordOk(pw)) { brls::Application::notify(T("failed")); return; }
                askText(mode + " — " + T("password_again"), [l, email, pw, mode](std::string pw2) {
                    if (pw2 != pw) { brls::Application::notify(T("pw_mismatch")); return; }
                    askText(mode + " — " + T("username"), [l, email, pw](std::string u) {
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

// clear()-then-repopulate can null out Application's global focus (see the
// long comment on LiveList::frame()) — buildFriends() is rebuilt from inside
// click handlers on this very list (sign in/out, switch account, accept a
// friend request...), i.e. exactly where the about-to-be-destroyed focused
// view is the button the user just pressed. Restore focus once at the end
// regardless of which of this function's early returns was taken.
static void buildFriendsImpl(brls::List* l) {
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

    // Fetched once, up front, and reused for both the identity block below
    // and the friends list further down (the exe's own /api/friends
    // response already carries `me`, exactly for this reason).
    long st = 0;
    cJSON* d = net::friends(&st);
    bool sessionDead = (st == 401 || !net::signedIn());
    cJSON* me = (d && !sessionDead) ? cJSON_GetObjectItem(d, "me") : nullptr;

    // identity — the exe's prominent .id block: photo, name, friend code.
    if (me) {
        auto* id = new ThumbListItem(gs(me, "username", "?"), fmtCode(gs(me, "code", "")));
        l->addView(id);
        std::string bytes;
        if (net::imageBytesFromDataUri(gs(me, "avatar", ""), bytes))
            id->setPicture(bytes);
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

    // Editing the picture/username lives fully in Settings too (matching the
    // exe's own layout — its Profile-tab identity header isn't editable
    // either, only Settings' #acct section is), but this tab is now literally
    // labelled "Profile", so a direct shortcut to both belongs right here.
    auto* chpic = row(T("choose_avatar"));
    chpic->getClickEvent()->subscribe([l](brls::View*) { buildAvatarGallery(l); });
    l->addView(chpic);
    auto* chname = row(T("username"));
    chname->getClickEvent()->subscribe([l](brls::View*) {
        askText(T("username"), [l](std::string u) {
            std::string err = usernameError(u);
            if (!err.empty()) { brls::Application::notify(err); return; }
            if (!net::usernameAvailable(u)) { brls::Application::notify(T("taken")); return; }
            std::string e;
            bool ok = net::usernameSet(u, e);
            brls::Application::notify(ok ? T("saved") : e);
            if (ok) buildFriends(l);
        });
    });
    l->addView(chname);

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

    // The friends list is the only part that depends on this fetch having
    // worked — sign out / switch / add-account / edit-profile must all stay
    // usable even when it didn't (this used to bail out of the whole
    // function here, silently hiding the sign-out button behind a dead
    // session with no way back except restarting the app).
    if (sessionDead) { note(l, T("session_expired")); if (d) cJSON_Delete(d); return; }
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
            std::string rcode = fmtCode(gs(r, "code", ""));
            auto* it = new ThumbListItem(gs(r, "name"), std::string(T("wants_to_add")) + (rcode.empty() ? "" : "  ·  " + rcode));
            it->getClickEvent()->subscribe([l, pid](brls::View*) {
                auto* dlg = new brls::Dialog(T("friend_requests"));
                dlg->addButton(T("accept"),  [l, pid](brls::View*) { net::friendAccept(pid);  buildFriends(l); });
                dlg->addButton(T("decline"), [l, pid](brls::View*) { net::friendDecline(pid); buildFriends(l); });
                dlg->open();
            });
            l->addView(it);
            std::string rbytes;
            if (net::imageBytesFromDataUri(gs(r, "image", ""), rbytes))
                it->setPicture(rbytes);
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
        // Matches the exe's frow layout: name (+star) on top, status below as
        // a description, formatted friend code on the right as the value,
        // and their actual avatar as the row's thumbnail — the exe shows
        // all four, this port previously showed only name/status.
        auto* fi = new ThumbListItem(name, state);
        std::string fcode = fmtCode(gs(f, "code", ""));
        if (!fcode.empty()) fi->setValue(fcode);
        l->addView(fi);
        std::string fbytes;
        if (net::imageBytesFromDataUri(gs(f, "image", ""), fbytes))
            fi->setPicture(fbytes);
    }
    if (gi(cnt, "total") == 0) note(l, T("no_friends"));
    cJSON_Delete(d);
}
static void buildFriends(brls::List* l) {
    buildFriendsImpl(l);
    if (!brls::Application::getCurrentFocus())
        brls::Application::giveFocus(l->getDefaultFocus());
}

// ================================================================ SETTINGS tab
static const char* SWATCHES[] = { "#1ca9e0","#e4404a","#36ce73","#8b5cf6","#f59e0b","#ec4899",
                                  "#14b8a6","#eab308","#6366f1","#ef7c3a","#22d3ee","#64748b" };

static void applyTheme(const std::string& /*t*/) {
    // This borealis version auto-follows the console's theme and only reads
    // it once at Application::init(), before prefs can even be loaded —
    // there's no supported way to force light/dark from inside the app.
    // The Settings picker still stores the choice (parity with the other
    // platforms) but it has no visible effect here; see theme_na.
}

// The avatar picker: one row per avatar showing *just the image* (no name),
// matching the exe's gallery grid. The PNGs are fetched on a background
// thread and streamed into their rows so opening the picker doesn't freeze
// the UI on dozens of sequential HTTPS GETs.
class AvatarGrid : public brls::List {
public:
    AvatarGrid(std::vector<std::string> names, std::function<void(const std::string&)> onPick)
        : names(std::move(names)), onPick(std::move(onPick)) {
        for (size_t i = 0; i < this->names.size(); i++) {
            auto* it = new ThumbListItem("");            // image only, no label
            std::string n = this->names[i];
            it->getClickEvent()->subscribe([this, n](brls::View*) { this->onPick(n); });
            rows.push_back(it);
            this->addView(it);
        }
        if (rows.empty()) note(this, T("no_data"));     // never leave the list empty (#37)

        // B / press-back always exits the picker (a bare pushed List has no
        // frame chrome, so nothing registers Back otherwise — that's why the
        // menu felt inescapable).
        this->registerAction("Back", brls::Key::B, [] { brls::Application::popView(); return true; });

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 256 * 1024);
        if (pthread_create(&worker, &attr, &AvatarGrid::run, this) == 0) started = true;
        pthread_attr_destroy(&attr);
        registry().push_back(this);
    }
    ~AvatarGrid() override {
        alive = false;
        if (started) { pthread_join(worker, nullptr); started = false; }
        auto& r = registry();
        r.erase(std::remove(r.begin(), r.end(), this), r.end());
    }
    static void joinAll() {
        for (AvatarGrid* g : registry()) if (g->started) { pthread_join(g->worker, nullptr); g->started = false; }
    }
    void frame(brls::FrameContext* ctx) override {
        {
            // Apply a few per frame: each setPicture() creates a GL texture and
            // makes the row re-layout for several frames, so draining dozens at
            // once is a visible hitch.
            std::lock_guard<std::mutex> lk(mtx);
            for (int budget = 4; budget > 0 && next < (int)inbox.size(); budget--, next++) {
                auto& pr = inbox[next];
                if (pr.first >= 0 && pr.first < (int)rows.size() && !pr.second.empty())
                    rows[pr.first]->setPicture(pr.second);
                pr.second.clear();
            }
        }
        brls::List::frame(ctx);
    }
private:
    static std::vector<AvatarGrid*>& registry() { static std::vector<AvatarGrid*> v; return v; }
    static void* run(void* arg) {
        auto* self = static_cast<AvatarGrid*>(arg);
        for (size_t i = 0; i < self->names.size() && self->alive; i++) {
            std::string bytes;
            if (net::avatarImageBytes(self->names[i], bytes) && !bytes.empty()) {
                std::lock_guard<std::mutex> lk(self->mtx);
                self->inbox.emplace_back((int)i, std::move(bytes));
            }
        }
        return nullptr;
    }
    std::vector<std::string> names;
    std::function<void(const std::string&)> onPick;
    std::vector<ThumbListItem*> rows;
    pthread_t worker{};
    bool started = false;
    std::mutex mtx;
    std::vector<std::pair<int, std::string>> inbox;
    int next = 0;                       // how far into inbox we've applied (UI thread only)
    std::atomic<bool> alive{true};
};

static void buildAvatarGallery(brls::List* /*parent*/) {
    long st = 0;
    cJSON* m = net::avatarsList(&st);
    if (!m) { brls::Application::notify(T("failed")); return; }

    std::vector<std::string> names;
    for (const char* key : { "firmware", "custom" }) {
        cJSON* a = cJSON_GetObjectItem(m, key);
        cJSON* it;
        cJSON_ArrayForEach(it, a)
            if (cJSON_IsString(it)) names.push_back(it->valuestring);
    }
    cJSON_Delete(m);

    auto onPick = [](const std::string& name) {
        std::string bytes;
        if (!net::avatarImageBytes(name, bytes)) { brls::Application::notify(T("failed")); return; }
        // /api/profile wants image = raw base64 payload, NO "data:" prefix
        // (see the desktop app's profile:save handler) — sending a data URI
        // is exactly what made the server answer "invalid".
        std::string img = net::b64((const unsigned char*)bytes.data(), bytes.size());
        std::string avatarJson = "{\"char\":\"" + name + "\"}";
        std::string e;
        bool ok = net::profileSave("", img, "", avatarJson, e);
        brls::Application::notify(ok ? T("saved") : e);
        if (ok) brls::Application::popView();
    };

    brls::Application::pushView(new AvatarGrid(std::move(names), onPick));
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
            brls::Application::notify(T("theme_na"));
        }, net::getPref("theme", "system") == "light" ? 1 : net::getPref("theme", "system") == "dark" ? 2 : 0);
    });
    l->addView(th);

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
        auto* cn = new ThumbListItem(T("country"));
        cn->setValue(curC.empty() ? "--" : curC);
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
        // The exe shows the Mario-Kart country flag as an <img> from
        // flagcdn.com; fetch the same PNG and hand it to the row as its
        // thumbnail (ThumbListItem keeps it pinned to the row).
        if (curC.size() == 2 && std::isalpha((unsigned char)curC[0]) && std::isalpha((unsigned char)curC[1])) {
            std::string lc = curC;
            for (auto& c : lc) c = (char)std::tolower((unsigned char)c);
            net::Resp fr = net::raw("GET", "https://flagcdn.com/w80/" + lc + ".png", "", "");
            if (fr.status == 200 && !fr.body.empty())
                cn->setPicture(fr.body, /*wide=*/true);
        }

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
        note(l, T("not_signed_in") + std::string("  (") + T("tab_profile") + ")");
    }

    // ---- credit ----
    l->addView(new brls::Header(T("about")));
    {
        // Just the app name, centred — not the long descriptive paragraph.
        auto* title = new brls::Label(brls::LabelStyle::MEDIUM, APP_NAME, false);
        title->setHorizontalAlign(NVG_ALIGN_CENTER);
        l->addView(title);
    }
    l->addView(row(T("made_by"), "adxmm"));
    l->addView(row(T("friend_code"), fmtCode(net::getPref("credit_fc", "SW-????-????-????"))));
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

    // See the matching comment on buildFriendsImpl()/LiveList::frame(): the
    // l->clear() at the top of this function can null Application's global
    // focus (e.g. when this rebuild was triggered by clicking a row on this
    // very list), which otherwise silently breaks all controller input from
    // here on.
    if (!brls::Application::getCurrentFocus())
        brls::Application::giveFocus(l->getDefaultFocus());
}

// ================================================================ main
int main(int argc, char* argv[]) {
    // libnx's default __appInit() only auto-initializes sm/applet/hid/time/fs
    // (checked against libnx's own runtime/init.c) — it does NOT init `pl`
    // (shared font service) or leave `set:sys` open. borealis's Switch font
    // loader calls plGetSharedFontByType() and its theme detection calls
    // setsysGetColorSetId() assuming both are already initialized; neither
    // borealis's own example nor this app ever called plInitialize(), so the
    // shared font silently fails to load (Result is checked, but nothing
    // downstream verifies fontStash.regular ended up valid) and the very
    // first text draw call hands nanovg an invalid font handle — a very
    // plausible cause of an instant crash on launch. Must happen before
    // Application::init(), since it loads that font as part of init() itself.
    plInitialize(PlServiceType_User);
    setsysInitialize();

    // Also needed before Application::init(): our own icon.jpg and
    // net::CAINFO's cacert.pem are romfs:/ paths too, and devkitA64 doesn't
    // auto-mount romfs for .nro homebrew.
    romfsInit();

    brls::Logger::setLogLevel(brls::LogLevel::INFO);
    auto* themeVariants = new brls::LibraryViewsThemeVariantsWrapper(new NextendoLightTheme(), new NextendoDarkTheme());
    if (!brls::Application::init(APP_NAME, new NextendoStyle(), themeVariants)) { romfsExit(); setsysExit(); plExit(); return EXIT_FAILURE; }
    brls::Application::setBackground(new NextendoBackground());
    // borealis's OWN i18n system (brls::i18n — distinct from this app's own
    // i18n::T() above) is what supplies the text for its built-in hints: the
    // "B ⟶ Back" / "A ⟶ OK" strings registered internally by
    // AppletFrame/Dialog/Dropdown/List (applet_frame.cpp, dialog.cpp,
    // dropdown.cpp, list.cpp all do `registerAction("brls/hints/back"_i18n,
    // ...)` etc). Its header is explicit: "Must be called before trying to
    // get a translation!" — brls::i18n::loadTranslations() was never called
    // anywhere in this app (same class of bug as the missing plInitialize()
    // earlier), so every one of those lookups silently falls back to
    // returning the raw, untranslated string key itself
    // ("brls/hints/back") instead of "Back" — exactly the raw/garbled hint
    // text reported at the bottom of the screen.
    brls::i18n::loadTranslations();
    net::init();
    // English-only build: ignore any stored language pref and pin "en" so a
    // previously-saved fr/es choice can't leak French/Spanish strings into
    // the UI. (The Language picker is gone from Settings, and setup.sh keeps
    // only romfs:/i18n/en-US so borealis's own hints stay English too.)
    i18n::setLang("en");
    applyTheme(net::getPref("theme", "system"));

    auto* root = new brls::TabFrame();
    root->setTitle(APP_NAME);
    // The Homebrew Menu tile / NACP icon (icon.jpg, baseline JPEG — required,
    // can't have alpha) is untouched. The in-app header icon is a separate
    // asset so it can be a transparent PNG instead of sitting in an opaque
    // black square.
    root->setIcon("romfs:/icon-header.png");

    // Tab order and default (first-added = shown on launch) match the exe's
    // taskbar exactly: Profile, Online, Status, Settings — see
    // renderer/index.html's <nav class="taskbar"> and app.js's S.tab default.
    auto* friends = new brls::List();
    buildFriends(friends);
    friends->registerAction(T("refresh_hint"), brls::Key::X, [friends]() { buildFriends(friends); return true; });
    root->addTab(T("tab_profile"), friends);

    root->addTab(T("tab_online"), new LiveList(net::onlineCounts, renderOnline, 15000));
    root->addTab(T("tab_status"), new LiveList(net::serverStatus, renderStatus, 60000));

    root->addSeparator();

    auto* settings = new brls::List();
    buildSettings(settings);
    settings->registerAction(T("refresh_hint"), brls::Key::X, [settings]() { buildSettings(settings); return true; });
    root->addTab(T("tab_settings"), settings);

    brls::Application::pushView(root);
    while (brls::Application::mainLoop());

    // See LiveList::joinAllBeforeShutdown()'s comment: without this, a
    // still-running background fetch can be using libcurl/sockets at the
    // exact moment net::shutdown() tears them down underneath it — a real
    // crash on close, not just a slow one.
    LiveList::joinAllBeforeShutdown();
    AvatarGrid::joinAll(); // same reason: its image-fetch thread must not outlive net::shutdown()
    net::shutdown();
    romfsExit();
    setsysExit();
    plExit();
    return EXIT_SUCCESS;
}
