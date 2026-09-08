/*
 * bridge-shim.js — the iOS replacement for Electron's preload.js + contextBridge.
 * Injected at documentStart. Defines window.nextendo so renderer/index.html runs
 * unchanged. Each method posts {id, method, args} to the native "nx" handler and
 * returns a Promise that native resolves via window.__nxResolve(id, value).
 */
(function () {
  "use strict";
  if (window.nextendo && window.__nxResolve) return;   // already installed (injected twice)
  var pending = Object.create(null);
  var seq = 0;

  window.__nxResolve = function (id, value) {
    var p = pending[id];
    if (!p) return;
    delete pending[id];
    p(value);
  };

  function call(method, args) {
    return new Promise(function (resolve) {
      var id = "nx" + (++seq) + "_" + Date.now();
      pending[id] = resolve;
      try {
        window.webkit.messageHandlers.nx.postMessage({ id: id, method: method, args: args || [] });
      } catch (e) {
        delete pending[id];
        resolve({ ok: false, error: String(e) });
      }
    });
  }

  window.nextendo = {
    // public feeds
    serverStatus: function () { return call("serverStatus"); },
    online:       function () { return call("online"); },

    // auth
    authStatus:   function () { return call("authStatus"); },
    login:        function (login, password) { return call("login", [{ login: login, password: password }]); },
    register:     function (fields) { return call("register", [fields]); },
    logout:       function () { return call("logout"); },
    accountsList: function () { return call("accountsList"); },
    accountsSwitch: function (id) { return call("accountsSwitch", [id]); },
    accountsRemove: function (id) { return call("accountsRemove", [id]); },

    // friends
    friends:      function () { return call("friends"); },
    addFriend:    function (code) { return call("addFriend", [code]); },
    accept:       function (pid) { return call("accept", [pid]); },
    decline:      function (pid) { return call("decline", [pid]); },

    // account settings
    profileGet:   function () { return call("profileGet"); },
    profileSave:  function (fields) { return call("profileSave", [fields]); },
    usernameSet:  function (u) { return call("usernameSet", [u]); },
    usernameCheck:function (u) { return call("usernameCheck", [u]); },
    countryList:  function () { return call("countryList"); },
    countrySet:   function (code) { return call("countrySet", [code]); },
    savesList:    function () { return call("savesList"); },
    savesDelete:  function (titleId) { return call("savesDelete", [titleId]); },
    savesDownload:function (titleId, name) { return call("savesDownload", [{ titleId: titleId, name: name }]); },
    modsFavorites:function () { return call("modsFavorites"); },
    avatarsList:  function () { return call("avatarsList"); },
    avatarImage:  function (name) { return call("avatarImage", [name]); }
  };
})();
