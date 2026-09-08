import UIKit
import WebKit

/// Hosts the renderer (www/index.html) in a full-screen WKWebView and installs
/// the JS <-> native bridge that stands in for Electron's preload + ipcRenderer.
final class WebViewController: UIViewController, WKNavigationDelegate {

    private var webView: WKWebView!
    private var bridge: NextendoBridge!

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = UIColor(red: 0.09, green: 0.06, blue: 0.13, alpha: 1) // #171021

        let cfg = WKWebViewConfiguration()
        cfg.allowsInlineMediaPlayback = true
        cfg.defaultWebpagePreferences.allowsContentJavaScript = true

        let ucc = WKUserContentController()
        bridge = NextendoBridge(present: { [weak self] vc in self?.present(vc, animated: true) })
        ucc.add(bridge, name: "nx")

        // preload.js replacement — injected before the page's own scripts run.
        if let shimURL = Bundle.main.url(forResource: "bridge-shim", withExtension: "js", subdirectory: "www"),
           let shim = try? String(contentsOf: shimURL, encoding: .utf8) {
            ucc.addUserScript(WKUserScript(source: shim, injectionTime: .atDocumentStart, forMainFrameOnly: true))
        }
        cfg.userContentController = ucc

        webView = WKWebView(frame: view.bounds, configuration: cfg)
        webView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        webView.navigationDelegate = self
        webView.isOpaque = false
        webView.backgroundColor = .clear
        webView.scrollView.contentInsetAdjustmentBehavior = .never
        webView.scrollView.bounces = false
        if #available(iOS 16.4, *) { webView.isInspectable = true }  // Safari Web Inspector in debug
        view.addSubview(webView)

        bridge.webView = webView

        if let index = Bundle.main.url(forResource: "index", withExtension: "html", subdirectory: "www") {
            webView.loadFileURL(index, allowingReadAccessTo: index.deletingLastPathComponent())
        }
    }

    // Any target=_blank / external link opens in Safari, never a nested web view.
    func webView(_ webView: WKWebView, decidePolicyFor nav: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        if nav.navigationType == .linkActivated, let url = nav.request.url, url.scheme?.hasPrefix("http") == true {
            UIApplication.shared.open(url)
            decisionHandler(.cancel); return
        }
        decisionHandler(.allow)
    }
}
