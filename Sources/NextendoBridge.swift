import Foundation
import WebKit
import UIKit

/// The Electron main.js port. Every `ipcMain.handle(...)` becomes a `case` here.
/// The renderer calls `window.nextendo.<method>(...args)` → bridge-shim.js posts
/// `{id, method, args}` to message handler "nx" → we do the work with URLSession
/// and resolve the promise via `window.__nxResolve(id, json)`.
final class NextendoBridge: NSObject, WKScriptMessageHandler {

    weak var webView: WKWebView?
    private let present: (UIViewController) -> Void
    private let session: URLSession = {
        let c = URLSessionConfiguration.ephemeral
        c.requestCachePolicy = .reloadIgnoringLocalCacheData
        c.timeoutIntervalForRequest = 20
        return URLSession(configuration: c)
    }()

    init(present: @escaping (UIViewController) -> Void) { self.present = present }

    // MARK: message entry point

    func userContentController(_ ucc: WKUserContentController, didReceive msg: WKScriptMessage) {
        guard msg.name == "nx",
              let body = msg.body as? [String: Any],
              let id = body["id"] as? String,
              let method = body["method"] as? String else { return }
        let args = body["args"] as? [Any] ?? []

        Task {
            let result: Any
            do { result = try await self.handle(method, args) }
            catch { result = ["ok": false, "error": "\(error)"] }
            self.resolve(id, result)
        }
    }

    private func resolve(_ id: String, _ value: Any) {
        let data = (try? JSONSerialization.data(withJSONObject: value, options: [.fragmentsAllowed])) ?? Data("null".utf8)
        let json = String(data: data, encoding: .utf8) ?? "null"
        DispatchQueue.main.async {
            self.webView?.evaluateJavaScript("window.__nxResolve(\(jsString(id)), \(json))", completionHandler: nil)
        }
    }

    // MARK: HTTP helper (== nxRequest / getJSON in main.js)

    private struct Resp { let status: Int; let json: Any?; let data: Data }

    private func req(_ urlString: String,
                     method: String = "GET",
                     body: Any? = nil,
                     token: String? = nil) async throws -> Resp {
        guard let url = URL(string: urlString) else { throw Err.badURL }
        var r = URLRequest(url: url)
        r.httpMethod = method
        r.setValue("application/json", forHTTPHeaderField: "Accept")
        r.setValue("NextendoHub-iOS/1.0", forHTTPHeaderField: "User-Agent")
        if let token { r.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization") }
        if let body {
            r.setValue("application/json", forHTTPHeaderField: "Content-Type")
            r.httpBody = try JSONSerialization.data(withJSONObject: body)
        }
        let (data, resp) = try await session.data(for: r)
        let status = (resp as? HTTPURLResponse)?.statusCode ?? 0
        let json = try? JSONSerialization.jsonObject(with: data, options: [.fragmentsAllowed])
        return Resp(status: status, json: json, data: data)
    }

    enum Err: Error { case badURL }

    // MARK: dispatch

    private func handle(_ method: String, _ args: [Any]) async throws -> Any {
        switch method {

        // ---------- public feeds ----------
        case "serverStatus": return try await serverStatus()
        case "online":       return try await online()

        // ---------- auth ----------
        case "authStatus":   return try await authStatus()
        case "login":        return try await login(args)
        case "register":     return try await register(args)
        case "logout":       return try await logout()
        case "accountsList": return ["ok": true, "accounts": Keychain.list()]
        case "accountsSwitch": return try await accountsSwitch(args)
        case "accountsRemove":
            if let id = args.first as? String { Keychain.removeAccount(id) }
            return ["ok": true, "accounts": Keychain.list()]

        // ---------- friends ----------
        case "friends":   return try await friends()
        case "addFriend": return try await friendsPost("/api/friends", ["friend_code": str(args, 0)])
        case "accept":    return try await friendsPost("/api/friends/accept", ["pid": num(args, 0)])
        case "decline":   return try await friendsPost("/api/friends/decline", ["pid": num(args, 0)])

        // ---------- profile / account ----------
        case "profileGet":   return try await profileGet()
        case "profileSave":  return try await profileSave(args)
        case "usernameSet":  return try await usernameSet(args)
        case "usernameCheck":return try await usernameCheck(args)
        case "countryList":  return try await countryList()
        case "countrySet":   return try await countrySet(args)

        // ---------- cloud saves ----------
        case "savesList":     return try await savesList()
        case "savesDelete":   return try await savesDelete(args)
        case "savesDownload": return try await savesDownload(args)

        // ---------- mods (best effort) ----------
        case "modsFavorites": return try await modsFavorites()

        // ---------- avatar gallery ----------
        case "avatarsList":  return try await avatarsList()
        case "avatarImage":  return try await avatarImage(args)

        // ---------- desktop-only: no-ops on iOS ----------
        case "startupGet":   return ["ok": true, "enabled": false]
        case "startupSet":   return ["ok": false, "error": "not supported on iOS"]

        default: return ["ok": false, "error": "unknown method \(method)"]
        }
    }

    // MARK: arg helpers
    private func str(_ a: [Any], _ i: Int) -> String { i < a.count ? "\(a[i])" : "" }
    private func num(_ a: [Any], _ i: Int) -> Int { i < a.count ? Int("\(a[i])") ?? 0 : 0 }
    private func obj(_ a: [Any], _ i: Int) -> [String: Any] { (i < a.count ? a[i] as? [String: Any] : nil) ?? [:] }
    private func token() -> String? { Keychain.currentToken() }

    // ======================================================================
    // status.nextendo.network (Uptime Kuma) — mirrors getServerStatus()
    // ======================================================================
    private func serverStatus() async throws -> Any {
        async let cfgR = req("https://status.nextendo.network/api/status-page/next")
        async let hbR  = req("https://status.nextendo.network/api/status-page/heartbeat/next")
        let (cfg, hb) = try await (cfgR, hbR)
        guard cfg.status == 200, hb.status == 200,
              let c = cfg.json as? [String: Any], let h = hb.json as? [String: Any] else {
            return ["ok": false, "error": "status HTTP \(cfg.status)/\(hb.status)"]
        }
        let beats = h["heartbeatList"] as? [String: Any] ?? [:]
        let uptime = h["uptimeList"] as? [String: Any] ?? [:]
        var up = 0, down = 0, total = 0
        var groups: [[String: Any]] = []
        for g in c["publicGroupList"] as? [[String: Any]] ?? [] {
            let name = ("\(g["name"] ?? "")").replacingOccurrences(of: #"^Nextendo\s*[-–]\s*"#, with: "", options: .regularExpression)
            var mons: [[String: Any]] = []
            for m in g["monitorList"] as? [[String: Any]] ?? [] {
                let mid = "\(m["id"] ?? "")"
                let list = (beats[mid] as? [[String: Any]]) ?? []
                let last = list.last
                let stt = (last?["status"] as? Int) ?? -1
                total += 1
                if stt == 1 || stt == 3 { up += 1 } else if stt == 0 || stt == 2 { down += 1 }
                mons.append([
                    "name": ("\(m["name"] ?? "")").replacingOccurrences(of: #"^Nextendo\s*[-–]\s*"#, with: "", options: .regularExpression),
                    "status": stt,
                    "ping": (last?["ping"] as? Double).map { Int($0.rounded()) } as Any? ?? NSNull(),
                    "uptime24": uptime["\(mid)_24"] as? Double as Any? ?? NSNull()
                ])
            }
            groups.append(["name": name.isEmpty ? "Nextendo" : name, "monitors": mons])
        }
        return ["ok": true, "data": [
            "fetched_at": iso8601(), "up": up, "down": down, "total": total,
            "groups": groups, "incidents": (c["incidents"] as? [[String: Any]]) ?? []
        ]]
    }

    // ======================================================================
    // nextendo.network/api/online-counts — mirrors getOnlineCounts()
    // ======================================================================
    private func online() async throws -> Any {
        let r = try await req("https://nextendo.network/api/online-counts")
        guard r.status == 200, let d = r.json as? [String: Any] else { return ["ok": false, "error": "HTTP \(r.status)"] }
        var jeux = (d["jeux"] as? [[String: Any]] ?? []).map { j -> [String: Any] in
            ["nom": "\(j["nom"] ?? "?")", "joueurs": Int("\(j["joueurs"] ?? 0)") ?? 0]
        }
        jeux.sort { ($0["joueurs"] as! Int) > ($1["joueurs"] as! Int) }
        let total = jeux.reduce(0) { $0 + ($1["joueurs"] as! Int) }
        return ["ok": true, "data": ["fetched_at": iso8601(), "total": total, "jeux": jeux]]
    }

    // ======================================================================
    // account model helpers
    // ======================================================================
    private func imgURI(_ v: Any?) -> Any {
        let s = "\(v ?? "")"
        if s.isEmpty { return "" }
        if s.hasPrefix("data:image/") { return s }
        if s.count > 64, s.range(of: #"^[A-Za-z0-9+/=\s]+$"#, options: .regularExpression) != nil {
            return "data:image/jpeg;base64," + s.replacingOccurrences(of: #"\s"#, with: "", options: .regularExpression)
        }
        return ""
    }

    private func shapeMe(_ me: [String: Any]?, _ prof: [String: Any]?) -> [String: Any] {
        let acc = (me?["account"] as? [String: Any]) ?? me ?? [:]
        let p = (prof?["profile"] as? [String: Any]) ?? prof ?? [:]
        return [
            "username": acc["username"] ?? acc["pseudo"] ?? acc["name"] ?? "",
            "code": acc["friend_code"] ?? acc["code"] ?? "",
            "pid": acc["pid"] ?? NSNull(),
            "email": acc["email"] ?? "",
            "avatar": imgURI(p["image"] ?? acc["image"] ?? acc["avatar"])
        ]
    }

    private func authStatus() async throws -> Any {
        guard let t = token() else { return ["ok": false, "signedIn": false] }
        async let meR = req("https://nextendo.network/api/me", token: t)
        async let prR = req("https://nextendo.network/api/profile", token: t)
        let (me, pr) = try await (meR, prR)
        if me.status == 200 {
            return ["ok": true, "signedIn": true, "me": shapeMe(me.json as? [String: Any], pr.json as? [String: Any])]
        }
        _ = Keychain.removeCurrent()
        return ["ok": false, "signedIn": false]
    }

    private func login(_ a: [Any]) async throws -> Any {
        let f = obj(a, 0)
        let r = try await req("https://nextendo.network/api/login", method: "POST",
                              body: ["login": f["login"] ?? "", "password": f["password"] ?? ""])
        return try await finishAuth(r, fallbackName: "")
    }

    private func register(_ a: [Any]) async throws -> Any {
        let f = obj(a, 0)
        var body: [String: Any] = ["username": f["username"] ?? "", "email": f["email"] ?? "", "password": f["password"] ?? ""]
        if let c = f["country"] as? String, !c.isEmpty { body["country"] = c }
        let r = try await req("https://nextendo.network/api/register", method: "POST", body: body)
        return try await finishAuth(r, fallbackName: "\(f["username"] ?? "")")
    }

    /// shared tail of login/register: store token, fetch me/profile, return {ok, me, accounts}
    private func finishAuth(_ r: Resp, fallbackName: String) async throws -> Any {
        guard r.status == 200, let d = r.json as? [String: Any], let tok = d["token"] as? String else {
            let e = (r.json as? [String: Any])?["error"] as? String
            return ["ok": false, "error": e ?? "Auth failed (\(r.status))"]
        }
        async let meR = req("https://nextendo.network/api/me", token: tok)
        async let prR = req("https://nextendo.network/api/profile", token: tok)
        let (me, pr) = try await (meR, prR)
        var meObj = me.status == 200 ? shapeMe(me.json as? [String: Any], pr.json as? [String: Any]) : [:]
        if meObj["username"] as? String == "" || meObj.isEmpty, let acc = d["account"] as? [String: Any] {
            meObj = shapeMe(["account": acc], nil)
        }
        let uname = (meObj["username"] as? String).flatMap { $0.isEmpty ? nil : $0 } ?? fallbackName
        Keychain.addAccount(token: tok, username: uname)
        return ["ok": true, "me": meObj, "accounts": Keychain.list()]
    }

    private func logout() async throws -> Any {
        if let t = token() { _ = try? await req("https://nextendo.network/api/logout", method: "POST", token: t) }
        let remaining = Keychain.removeCurrent()
        if remaining > 0, let t2 = token() {
            async let meR = req("https://nextendo.network/api/me", token: t2)
            async let prR = req("https://nextendo.network/api/profile", token: t2)
            let (me, pr) = try await (meR, prR)
            let meObj = me.status == 200 ? shapeMe(me.json as? [String: Any], pr.json as? [String: Any]) : NSNull()
            return ["ok": true, "switchedTo": meObj, "accounts": Keychain.list()]
        }
        return ["ok": true, "accounts": []]
    }

    private func accountsSwitch(_ a: [Any]) async throws -> Any {
        guard let id = a.first as? String, Keychain.setCurrent(id) else { return ["ok": false, "error": "Unknown account"] }
        guard let t = token() else { return ["ok": false, "signedOut": true, "accounts": Keychain.list()] }
        async let meR = req("https://nextendo.network/api/me", token: t)
        async let prR = req("https://nextendo.network/api/profile", token: t)
        let (me, pr) = try await (meR, prR)
        if me.status == 200 {
            return ["ok": true, "me": shapeMe(me.json as? [String: Any], pr.json as? [String: Any]), "accounts": Keychain.list()]
        }
        if me.status == 401 { _ = Keychain.removeCurrent(); return ["ok": false, "signedOut": true, "accounts": Keychain.list()] }
        return ["ok": false, "error": "HTTP \(me.status)"]
    }

    // ======================================================================
    // friends — mirrors friends:list, friends:add/accept/decline + gameinfo
    // ======================================================================
    private var gameNames: [String: String] = [:]

    private func friends() async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        async let meR = req("https://nextendo.network/api/me", token: t)
        async let prR = req("https://nextendo.network/api/profile", token: t)
        async let frR = req("https://nextendo.network/api/friends", token: t)
        let (me, pr, fr) = try await (meR, prR, frR)
        if fr.status == 401 { _ = Keychain.removeCurrent(); return ["ok": false, "error": "Session expired — sign in again", "signedOut": true] }
        guard fr.status == 200 else { return ["ok": false, "error": "HTTP \(fr.status)"] }

        let d = fr.json as? [String: Any] ?? [:]
        let rawFriends = (fr.json as? [[String: Any]]) ?? (d["friends"] as? [[String: Any]]) ?? (d["list"] as? [[String: Any]]) ?? []
        let rawReq = (d["requests"] as? [[String: Any]]) ?? (d["incoming"] as? [[String: Any]]) ?? []

        var friends = rawFriends.map(normFriend)
        // resolve "what game are they playing"
        let ids = Set(friends.filter { $0["inGame"] as? Bool == true }.compactMap { ($0["appId"] as? String)?.lowercased() }.filter { !$0.isEmpty && gameNames[$0] == nil })
        await withTaskGroup(of: Void.self) { grp in
            for id in ids {
                grp.addTask {
                    if let r = try? await self.req("https://nextendo.network/api/gameinfo?title_id=\(id.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? id)", token: t),
                       r.status == 200, let g = r.json as? [String: Any],
                       let n = (g["name"] as? String) ?? (g["title"] as? String) {
                        await MainActor.run { self.gameNames[id] = n }
                    } else { await MainActor.run { self.gameNames[id] = "" } }
                }
            }
        }
        for i in friends.indices where friends[i]["inGame"] as? Bool == true {
            let id = (friends[i]["appId"] as? String ?? "").lowercased()
            let n = gameNames[id]
            friends[i]["gameName"] = (n?.isEmpty == false ? n : (friends[i]["gameDetail"] as? String)) as Any
        }
        func rank(_ f: [String: Any]) -> Int { (f["inGame"] as? Bool == true) ? 0 : (f["online"] as? Bool == true ? 1 : 2) }
        friends.sort { a, b in
            if rank(a) != rank(b) { return rank(a) < rank(b) }
            let fa = (a["favorite"] as? Bool == true) ? 1 : 0, fb = (b["favorite"] as? Bool == true) ? 1 : 0
            if fa != fb { return fa > fb }
            return "\(a["name"] ?? "")".localizedCaseInsensitiveCompare("\(b["name"] ?? "")") == .orderedAscending
        }
        let requests = rawReq.map(normRequest)
        return ["ok": true,
                "me": shapeMe(me.json as? [String: Any], pr.json as? [String: Any]),
                "friends": friends,
                "requests": requests,
                "counts": ["total": friends.count,
                           "online": friends.filter { $0["online"] as? Bool == true }.count,
                           "inGame": friends.filter { $0["inGame"] as? Bool == true }.count]]
    }

    private func normFriend(_ f: [String: Any]) -> [String: Any] {
        let p = f["presence"] as? [String: Any] ?? [:]
        let online = (Double("\(p["status"] ?? 0)") ?? 0) > 0
        let appId = "\(p["app_id"] ?? p["appId"] ?? "")"
        return [
            "pid": f["pid"] ?? f["id"] ?? NSNull(),
            "name": f["name"] ?? f["username"] ?? "—",
            "code": f["friend_code"] ?? f["code"] ?? "",
            "favorite": (f["favorite"] as? Bool ?? f["fav"] as? Bool ?? false),
            "image": imgURI(f["image"]),
            "online": online,
            "inGame": online && !appId.isEmpty,
            "appId": appId,
            "gameDetail": "\(p["app_detail"] ?? "")".trimmingCharacters(in: .whitespaces),
            "gameName": NSNull()
        ]
    }
    private func normRequest(_ r: [String: Any]) -> [String: Any] {
        ["pid": r["pid"] ?? r["id"] ?? NSNull(),
         "name": r["name"] ?? r["username"] ?? "—",
         "code": r["friend_code"] ?? r["code"] ?? "",
         "image": imgURI(r["image"])]
    }

    private func friendsPost(_ path: String, _ body: [String: Any]) async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let r = try await req("https://nextendo.network\(path)", method: "POST", body: body, token: t)
        if r.status == 200 { return path == "/api/friends" ? ["ok": true, "data": r.json ?? NSNull()] : ["ok": true] }
        return ["ok": false, "error": ((r.json as? [String: Any])?["error"] as? String) ?? "HTTP \(r.status)"]
    }

    // ======================================================================
    // profile / username / country — mirrors the "full document PUT" fix
    // ======================================================================
    private func profileGet() async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        async let meR = req("https://nextendo.network/api/me", token: t)
        async let prR = req("https://nextendo.network/api/profile", token: t)
        let (me, pr) = try await (meR, prR)
        if me.status == 401 { _ = Keychain.removeCurrent(); return ["ok": false, "signedOut": true] }
        let acc = (me.json as? [String: Any])?["account"] as? [String: Any] ?? me.json as? [String: Any] ?? [:]
        let p = (pr.json as? [String: Any])?["profile"] as? [String: Any] ?? pr.json as? [String: Any] ?? [:]
        let color = "\(p["color"] ?? "")"
        return ["ok": true,
                "username": acc["username"] ?? "",
                "country": acc["country"] ?? p["country"] ?? "",
                "friend_code": acc["friend_code"] ?? "",
                "email": acc["email"] ?? "",
                "image": imgURI(p["image"] ?? acc["image"]),
                "color": color.range(of: #"^#[0-9a-fA-F]{6}$"#, options: .regularExpression) != nil ? color : ""]
    }

    private func profileSave(_ a: [Any]) async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let f = obj(a, 0)
        async let meR = req("https://nextendo.network/api/me", token: t)
        async let prR = req("https://nextendo.network/api/profile", token: t)
        let (me, pr) = try await (meR, prR)
        let acc = (me.json as? [String: Any])?["account"] as? [String: Any] ?? [:]
        let cur = (pr.json as? [String: Any])?["profile"] as? [String: Any] ?? [:]
        let body: [String: Any] = [
            "name":  f["username"] ?? acc["username"] ?? "",
            "mii":   cur["mii"] ?? "",
            "image": f["image"] ?? cur["image"] ?? "",
            "color": f["color"] ?? cur["color"] ?? "#1ca9e0",
            "avatar": f["avatar"] ?? cur["avatar"] ?? ""
        ]
        let r = try await req("https://nextendo.network/api/profile", method: "PUT", body: body, token: t)
        guard r.status == 200 else { return ["ok": false, "error": ((r.json as? [String: Any])?["error"] as? String) ?? "HTTP \(r.status)"] }
        let check = try? await req("https://nextendo.network/api/profile", token: t)
        let saved = (check?.json as? [String: Any])?["profile"] as? [String: Any] ?? (r.json as? [String: Any])?["profile"] as? [String: Any]
        return ["ok": true, "profile": saved ?? NSNull(), "image": imgURI(saved?["image"] ?? body["image"])]
    }

    private func usernameSet(_ a: [Any]) async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let u = str(a, 0)
        guard u.range(of: #"^[A-Za-z0-9_-]{3,16}$"#, options: .regularExpression) != nil else {
            return ["ok": false, "error": "Username must be 3–16 characters (letters, digits, _ or -)."]
        }
        let r = try await req("https://nextendo.network/api/username", method: "PUT", body: ["username": u], token: t)
        guard r.status == 200 else { return ["ok": false, "error": ((r.json as? [String: Any])?["error"] as? String) ?? "HTTP \(r.status)"] }
        let acc = (r.json as? [String: Any])?["account"] as? [String: Any] ?? [:]
        let newName = (acc["username"] as? String) ?? u
        Keychain.setCurrentUsername(newName)
        return ["ok": true, "account": acc, "accounts": Keychain.list()]
    }

    private func usernameCheck(_ a: [Any]) async throws -> Any {
        let u = str(a, 0)
        guard u.range(of: #"^[A-Za-z0-9_-]{3,16}$"#, options: .regularExpression) != nil else {
            return ["ok": false, "available": false, "invalid": true]
        }
        let r = try await req("https://nextendo.network/api/username-available?username=\(u.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? u)", token: token())
        let d = r.json as? [String: Any] ?? [:]
        let avail = (d["available"] as? Bool == true) || (d["free"] as? Bool == true) || (d["ok"] as? Bool == true)
        return ["ok": r.status == 200, "available": avail]
    }

    private func countryList() async throws -> Any {
        let r = try await req("https://nextendo.network/api/country", token: token())
        let list = (r.json as? [String: Any])?["countries"] as? [String] ?? []
        return ["ok": r.status == 200, "countries": list]
    }

    private func countrySet(_ a: [Any]) async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let c = str(a, 0).uppercased()
        guard c.range(of: #"^[A-Z]{2}$"#, options: .regularExpression) != nil else { return ["ok": false, "error": "Invalid country code."] }
        let r = try await req("https://nextendo.network/api/country", method: "POST", body: ["country": c], token: t)
        return r.status == 200 ? ["ok": true, "account": (r.json as? [String: Any])?["account"] ?? NSNull()]
                               : ["ok": false, "error": ((r.json as? [String: Any])?["error"] as? String) ?? "HTTP \(r.status)"]
    }

    // ======================================================================
    // cloud saves
    // ======================================================================
    private func savesList() async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let r = try await req("https://nextendo.network/api/saves", token: t)
        if r.status == 401 { _ = Keychain.removeCurrent(); return ["ok": false, "signedOut": true] }
        guard r.status == 200, let d = r.json as? [String: Any] else { return ["ok": false, "error": "HTTP \(r.status)"] }
        let limit = (d["limit"] as? Int) ?? 5 * 1024 * 1024
        let saves = (d["saves"] as? [[String: Any]] ?? []).map { s -> [String: Any] in
            ["titleId": s["titleId"] ?? s["title_id"] ?? s["id"] ?? "",
             "name": s["name"] ?? s["title"] ?? s["titleId"] ?? "—",
             "size": s["size"] ?? s["bytes"] ?? NSNull(),
             "updated": s["updated"] ?? s["updated_at"] ?? s["mtime"] ?? s["date"] ?? ""]
        }
        return ["ok": true,
                "eligible": (d["eligible"] as? Bool) != false,
                "reasonCode": d["reasonCode"] ?? "",
                "isBooster": d["isBooster"] as? Bool ?? false,
                "totalSize": d["totalSize"] as? Int ?? 0,
                "limit": limit,
                "saves": saves]
    }

    private func savesDelete(_ a: [Any]) async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let id = str(a, 0).addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? str(a, 0)
        let r = try await req("https://nextendo.network/api/save/\(id)", method: "DELETE", token: t)
        return (r.status == 200 || r.status == 204) ? ["ok": true]
            : ["ok": false, "error": ((r.json as? [String: Any])?["error"] as? String) ?? "HTTP \(r.status)"]
    }

    /// Downloads the zip, writes it to a temp file, and presents the iOS share
    /// sheet (which includes "Save to Files").
    private func savesDownload(_ a: [Any]) async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let f = obj(a, 0)
        let titleId = "\(f["titleId"] ?? "")"
        let name = "\(f["name"] ?? titleId)"
        let idEsc = titleId.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? titleId
        var r = URLRequest(url: URL(string: "https://nextendo.network/api/save/\(idEsc)")!)
        r.setValue("Bearer \(t)", forHTTPHeaderField: "Authorization")
        let (data, resp) = try await session.data(for: r)
        let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
        if code == 204 { return ["ok": false, "error": "No cloud save for this game."] }
        guard code == 200 else { return ["ok": false, "error": "HTTP \(code)"] }
        let safe = name.replacingOccurrences(of: #"[^\w .()\-]+"#, with: "_", options: .regularExpression)
        let url = FileManager.default.temporaryDirectory.appendingPathComponent((safe.isEmpty ? titleId : safe) + ".zip")
        try data.write(to: url, options: .atomic)
        await MainActor.run {
            let av = UIActivityViewController(activityItems: [url], applicationActivities: nil)
            self.present(av)
        }
        return ["ok": true, "shared": true]
    }

    // ======================================================================
    // favourite mods (best effort — shape unknown without a real account)
    // ======================================================================
    private func modsFavorites() async throws -> Any {
        guard let t = token() else { return ["ok": false, "error": "Not signed in"] }
        let r = try await req("https://nextendo.network/api/mod-favorites", token: t)
        guard r.status == 200 else { return ["ok": false, "error": ((r.json as? [String: Any])?["error"] as? String) ?? "HTTP \(r.status)", "status": r.status] }
        let arr: [[String: Any]]
        if let a = r.json as? [[String: Any]] { arr = a }
        else if let d = r.json as? [String: Any] {
            arr = (d["favorites"] as? [[String: Any]]) ?? (d["mods"] as? [[String: Any]]) ?? (d["list"] as? [[String: Any]]) ?? (d["items"] as? [[String: Any]]) ?? []
        } else { arr = [] }
        let mods = arr.map { m -> [String: Any] in
            ["name": m["name"] ?? m["title"] ?? m["mod_name"] ?? "—",
             "game": m["game"] ?? m["game_name"] ?? m["title_name"] ?? "",
             "author": m["author"] ?? m["by"] ?? m["owner"] ?? "",
             "id": m["mod_id"] ?? m["id"] ?? ""]
        }
        return ["ok": true, "mods": mods]
    }

    // ======================================================================
    // avatar gallery — /assets/avatars/manifest.json + PNG bytes
    // ======================================================================
    private func avatarsList() async throws -> Any {
        let r = try await req("https://nextendo.network/assets/avatars/manifest.json")
        guard r.status == 200, let d = r.json as? [String: Any] else { return ["ok": false, "error": "HTTP \(r.status)"] }
        return ["ok": true,
                "firmware": d["avatars"] as? [String] ?? [],
                "custom": d["custom"] as? [String] ?? [],
                "base": "https://nextendo.network/assets/avatars/"]
    }

    private func avatarImage(_ a: [Any]) async throws -> Any {
        let name = str(a, 0)
        guard name.range(of: #"^[A-Za-z0-9_.\-]+\.png$"#, options: .regularExpression) != nil else { return ["ok": false, "error": "bad name"] }
        var req = URLRequest(url: URL(string: "https://nextendo.network/assets/avatars/\(name)")!)
        req.setValue("NextendoHub-iOS/1.0", forHTTPHeaderField: "User-Agent")
        let (data, resp) = try await session.data(for: req)
        guard (resp as? HTTPURLResponse)?.statusCode == 200 else { return ["ok": false, "error": "HTTP"] }
        return ["ok": true, "dataURI": "data:image/png;base64," + data.base64EncodedString()]
    }

    // MARK: misc
    private func iso8601() -> String {
        let f = ISO8601DateFormatter(); f.formatOptions = [.withInternetDateTime]
        return f.string(from: Date())
    }
}

private func jsString(_ s: String) -> String {
    let d = (try? JSONSerialization.data(withJSONObject: [s], options: [])) ?? Data("[\"\"]".utf8)
    var t = String(data: d, encoding: .utf8) ?? "[\"\"]"
    t.removeFirst(); t.removeLast()
    return t
}
