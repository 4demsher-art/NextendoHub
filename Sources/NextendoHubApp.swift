import SwiftUI
import UIKit

@main
struct NextendoHubApp: App {
    var body: some Scene {
        WindowGroup {
            WebHostView()
                .ignoresSafeArea(.container, edges: .bottom)
                .statusBar(hidden: false)
                .preferredColorScheme(nil)   // the web UI follows the system theme itself
        }
    }
}

/// SwiftUI wrapper around the UIKit WKWebView host.
struct WebHostView: UIViewControllerRepresentable {
    func makeUIViewController(context: Context) -> WebViewController { WebViewController() }
    func updateUIViewController(_ vc: WebViewController, context: Context) {}
}
