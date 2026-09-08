import Foundation
import Security

/// Multi-account bearer-token store. Mirrors the Electron build's session.bin
/// ({ current, accounts: { id: { token, username } } }) but each account's
/// token is a discrete Keychain item; the small index blob (current id +
/// id->username map) is one more item. All WhenUnlockedThisDeviceOnly — the
/// same accessibility the original NextendoApp used.
enum Keychain {

    private static let service = "network.nextendo.hub"
    private static let indexKey = "__index__"

    struct Index: Codable { var current: String = ""; var names: [String: String] = [:] }

    // MARK: raw item helpers

    private static func set(_ key: String, _ data: Data) {
        let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                kSecAttrService as String: service,
                                kSecAttrAccount as String: key]
        SecItemDelete(q as CFDictionary)
        var add = q
        add[kSecValueData as String] = data
        add[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
        SecItemAdd(add as CFDictionary, nil)
    }

    private static func get(_ key: String) -> Data? {
        let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
                                kSecAttrService as String: service,
                                kSecAttrAccount as String: key,
                                kSecReturnData as String: true,
                                kSecMatchLimit as String: kSecMatchLimitOne]
        var out: CFTypeRef?
        return SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess ? out as? Data : nil
    }

    private static func remove(_ key: String) {
        SecItemDelete([kSecClass as String: kSecClassGenericPassword,
                       kSecAttrService as String: service,
                       kSecAttrAccount as String: key] as CFDictionary)
    }

    // MARK: index

    static func index() -> Index {
        guard let d = get(indexKey), let i = try? JSONDecoder().decode(Index.self, from: d) else { return Index() }
        return i
    }
    private static func saveIndex(_ i: Index) {
        if let d = try? JSONEncoder().encode(i) { set(indexKey, d) }
    }

    // MARK: public API used by the bridge

    static func currentToken() -> String? {
        let i = index()
        guard !i.current.isEmpty, let d = get(i.current) else { return nil }
        return String(data: d, encoding: .utf8)
    }

    static func addAccount(token: String, username: String) {
        var i = index()
        let id = username.isEmpty ? "account-\(i.names.count + 1)" : username
        set(id, Data(token.utf8))
        i.names[id] = username.isEmpty ? id : username
        i.current = id
        saveIndex(i)
    }

    static func setCurrentUsername(_ name: String) {
        var i = index()
        guard !i.current.isEmpty else { return }
        i.names[i.current] = name
        saveIndex(i)
    }

    @discardableResult
    static func removeCurrent() -> Int {
        var i = index()
        if !i.current.isEmpty { remove(i.current); i.names[i.current] = nil }
        i.current = i.names.keys.sorted().first ?? ""
        saveIndex(i)
        return i.names.count
    }

    static func setCurrent(_ id: String) -> Bool {
        var i = index()
        guard i.names[id] != nil else { return false }
        i.current = id; saveIndex(i); return true
    }

    static func removeAccount(_ id: String) {
        var i = index()
        remove(id); i.names[id] = nil
        if i.current == id { i.current = i.names.keys.sorted().first ?? "" }
        saveIndex(i)
    }

    static func list() -> [[String: Any]] {
        let i = index()
        return i.names.keys.sorted().map { ["id": $0, "username": i.names[$0] ?? $0, "current": $0 == i.current] }
    }
}
