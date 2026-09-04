// i18n.hpp — en / fr / es, mirrors the exe's I18N table (visible strings only).
#pragma once
#include <string>
#include <map>
#include "net.hpp"

namespace i18n {

inline std::string& langRef() { static std::string L = "en"; return L; }
inline void setLang(const std::string& l) { langRef() = (l == "fr" || l == "es") ? l : "en"; net::setPref("lang", langRef()); }
inline void loadLang() { langRef() = net::getPref("lang", "en"); }
inline const std::string& lang() { return langRef(); }

inline const char* T(const char* k) {
    static const std::map<std::string, std::map<std::string, const char*>> M = {
      // Taskbar labels — short, matching the exe's taskbar exactly (its
      // longer in-tab <h1> titles are separate keys below).
      { "tab_online",  {{"en","Online"},{"fr","En ligne"},{"es","En línea"}} },
      { "tab_status",  {{"en","Status"},{"fr","État"},{"es","Estado"}} },
      { "tab_friends", {{"en","Friends"},{"fr","Amis"},{"es","Amigos"}} },
      { "tab_profile", {{"en","Profile"},{"fr","Profil"},{"es","Perfil"}} },
      { "tab_settings",{{"en","Settings"},{"fr","Réglages"},{"es","Ajustes"}} },
      { "online_heading",{{"en","In game right now"},{"fr","En jeu maintenant"},{"es","En juego ahora"}} },
      { "status_heading",{{"en","Server status"},{"fr","État des serveurs"},{"es","Estado de los servidores"}} },
      { "connecting",{{"en","Connecting…"},{"fr","Connexion…"},{"es","Conectando…"}} },
      { "offline",   {{"en","Offline"},{"fr","Hors ligne"},{"es","Sin conexión"}} },
      { "players_ingame",{{"en","Players in game"},{"fr","Joueurs en jeu"},{"es","Jugadores en juego"}} },
      { "games_active",{{"en","Games active"},{"fr","Jeux actifs"},{"es","Juegos activos"}} },
      { "player",  {{"en","Player"},{"fr","Joueur"},{"es","Jugador"}} },
      { "players", {{"en","Players"},{"fr","Joueurs"},{"es","Jugadores"}} },
      { "all_ok",  {{"en","All systems operational"},{"fr","Tous les services opérationnels"},{"es","Todos los servicios operativos"}} },
      { "down_one",{{"en","Service down"},{"fr","Service en panne"},{"es","Servicio caído"}} },
      { "down_many",{{"en","Services down"},{"fr","Services en panne"},{"es","Servicios caídos"}} },
      { "up_of",   {{"en","Up"},{"fr","Opérationnels"},{"es","Operativos"}} },
      { "st_up",   {{"en","Operational"},{"fr","Opérationnel"},{"es","Operativo"}} },
      { "st_down", {{"en","Down"},{"fr","Hors service"},{"es","Caído"}} },
      { "st_maint",{{"en","Maintenance"},{"fr","Maintenance"},{"es","Mantenimiento"}} },
      { "unreachable",{{"en","Couldn't reach the server."},{"fr","Serveur injoignable."},{"es","No se pudo contactar el servidor."}} },
      { "no_data", {{"en","No data yet."},{"fr","Aucune donnée."},{"es","Sin datos."}} },
      { "online_word", {{"en","Online"},{"fr","En ligne"},{"es","En línea"}} },
      { "offline_word",{{"en","Offline"},{"fr","Hors ligne"},{"es","Sin conexión"}} },
      { "playing", {{"en","Playing"},{"fr","Joue à"},{"es","Jugando a"}} },
      { "in_game", {{"en","In game"},{"fr","En jeu"},{"es","En juego"}} },
      { "not_signed_in",{{"en","Not signed in."},{"fr","Non connecté."},{"es","Sin sesión."}} },
      { "sign_in", {{"en","Sign in"},{"fr","Se connecter"},{"es","Iniciar sesión"}} },
      { "sign_out",{{"en","Sign out"},{"fr","Se déconnecter"},{"es","Cerrar sesión"}} },
      { "create_account",{{"en","Create account"},{"fr","Créer un compte"},{"es","Crear cuenta"}} },
      { "add_account",{{"en","Add account"},{"fr","Ajouter un compte"},{"es","Añadir cuenta"}} },
      { "switch_account",{{"en","Switch account"},{"fr","Changer de compte"},{"es","Cambiar de cuenta"}} },
      { "email",   {{"en","E-mail"},{"fr","E-mail"},{"es","Correo"}} },
      { "password",{{"en","Password"},{"fr","Mot de passe"},{"es","Contraseña"}} },
      { "password_again",{{"en","Confirm password"},{"fr","Confirme le mot de passe"},{"es","Confirma la contraseña"}} },
      { "username",{{"en","Username"},{"fr","Pseudo"},{"es","Usuario"}} },
      { "country", {{"en","Country (Mario Kart flag)"},{"fr","Pays (drapeau Mario Kart)"},{"es","País (bandera de Mario Kart)"}} },
      { "friend_code",{{"en","Friend code"},{"fr","Code ami"},{"es","Código de amigo"}} },
      { "add_friend",{{"en","Add friend by code"},{"fr","Ajouter par code ami"},{"es","Añadir por código"}} },
      { "friend_requests",{{"en","Friend requests"},{"fr","Demandes d'ami"},{"es","Solicitudes"}} },
      { "wants_to_add",{{"en","Wants to add you"},{"fr","Veut t'ajouter"},{"es","Quiere añadirte"}} },
      { "accept",  {{"en","Accept"},{"fr","Accepter"},{"es","Aceptar"}} },
      { "decline", {{"en","Decline"},{"fr","Refuser"},{"es","Rechazar"}} },
      { "no_friends",{{"en","No friends yet."},{"fr","Aucun ami."},{"es","Sin amigos."}} },
      { "session_expired",{{"en","Session expired — sign in again."},{"fr","Session expirée — reconnecte-toi."},{"es","Sesión caducada — inicia sesión."}} },
      { "friends_hdr",{{"en","Friends"},{"fr","Amis"},{"es","Amigos"}} },
      { "profile", {{"en","Profile"},{"fr","Profil"},{"es","Perfil"}} },
      { "profile_picture",{{"en","Profile picture"},{"fr","Photo de profil"},{"es","Foto de perfil"}} },
      { "choose_avatar",{{"en","Choose from gallery"},{"fr","Choisir dans la galerie"},{"es","Elegir de la galería"}} },
      { "color",   {{"en","Colour"},{"fr","Couleur"},{"es","Color"}} },
      { "saved",   {{"en","Saved"},{"fr","Enregistré"},{"es","Guardado"}} },
      { "failed",  {{"en","Failed"},{"fr","Échec"},{"es","Error"}} },
      { "taken",   {{"en","Taken"},{"fr","Déjà pris"},{"es","En uso"}} },
      { "available",{{"en","Available"},{"fr","Disponible"},{"es","Disponible"}} },
      { "cloud_saves",{{"en","Cloud saves"},{"fr","Sauvegardes cloud"},{"es","Guardados en la nube"}} },
      { "no_saves",{{"en","No cloud saves."},{"fr","Aucune sauvegarde."},{"es","Sin guardados."}} },
      { "gate_email",{{"en","Verify your e-mail to enable cloud saves."},{"fr","Vérifie ton e-mail pour activer les sauvegardes."},{"es","Verifica tu correo para activar los guardados."}} },
      { "gate_discord",{{"en","Link Discord to enable cloud saves."},{"fr","Lie Discord pour activer les sauvegardes."},{"es","Vincula Discord para activar los guardados."}} },
      { "download",{{"en","Download to SD"},{"fr","Télécharger sur SD"},{"es","Descargar a SD"}} },
      { "del",     {{"en","Delete"},{"fr","Supprimer"},{"es","Eliminar"}} },
      { "downloaded",{{"en","Saved to"},{"fr","Enregistré dans"},{"es","Guardado en"}} },
      { "fav_mods",{{"en","Favourite mods"},{"fr","Mods favoris"},{"es","Mods favoritos"}} },
      { "no_mods", {{"en","No favourite mods."},{"fr","Aucun mod favori."},{"es","Sin mods favoritos."}} },
      { "theme",   {{"en","Theme"},{"fr","Thème"},{"es","Tema"}} },
      { "language",{{"en","Language"},{"fr","Langue"},{"es","Idioma"}} },
      { "th_system",{{"en","System"},{"fr","Système"},{"es","Sistema"}} },
      { "th_light",{{"en","Light"},{"fr","Clair"},{"es","Claro"}} },
      { "th_dark", {{"en","Dark"},{"fr","Sombre"},{"es","Oscuro"}} },
      { "startup", {{"en","Launch at startup"},{"fr","Lancer au démarrage"},{"es","Abrir al inicio"}} },
      { "startup_na",{{"en","Not applicable on Switch"},{"fr","Non applicable sur Switch"},{"es","No aplica en Switch"}} },
      { "account", {{"en","Account"},{"fr","Compte"},{"es","Cuenta"}} },
      { "about",   {{"en","About"},{"fr","À propos"},{"es","Acerca de"}} },
      { "made_by", {{"en","Made by"},{"fr","Créé par"},{"es","Hecho por"}} },
      { "founders",{{"en","Founders of Nextendo Network"},{"fr","Fondateurs de Nextendo Network"},{"es","Fundadores de Nextendo Network"}} },
      { "version", {{"en","Version"},{"fr","Version"},{"es","Versión"}} },
      { "refresh_hint",{{"en","Refresh"},{"fr","Actualiser"},{"es","Actualizar"}} },
      { "live",    {{"en","Live"},{"fr","En direct"},{"es","En vivo"}} },
      { "theme_na",{{"en","Saved — this build always follows the console theme"},{"fr","Enregistré — cette version suit toujours le thème de la console"},{"es","Guardado — esta versión siempre sigue el tema de la consola"}} },
      { "loading", {{"en","Loading..."},{"fr","Chargement..."},{"es","Cargando..."}} },
      { "pw_mismatch",{{"en","Passwords differ"},{"fr","Les mots de passe diffèrent"},{"es","Las contraseñas no coinciden"}} },
      { "retranslate_hint",{{"en","Reopen tabs to fully re-translate"},{"fr","Rouvre les onglets pour tout traduire"},{"es","Vuelve a abrir las pestañas para traducir todo"}} },
      { "about_line",{{"en","Nextendo Hub — server status & online counts from status.nextendo.network and nextendo.network/api/online-counts; friends from your account. Unofficial client."},
                       {"fr","Nextendo Hub — état des serveurs et joueurs en ligne depuis status.nextendo.network et nextendo.network/api/online-counts ; amis depuis ton compte. Client non officiel."},
                       {"es","Nextendo Hub — estado de servidores y jugadores en línea desde status.nextendo.network y nextendo.network/api/online-counts; amigos desde tu cuenta. Cliente no oficial."}} },
      { "uname_len_rule",{{"en","3-16 characters"},{"fr","3 à 16 caractères"},{"es","3 a 16 caracteres"}} },
      { "uname_char_rule",{{"en","Letters, digits, _ or -"},{"fr","Lettres, chiffres, _ ou -"},{"es","Letras, dígitos, _ o -"}} },
    };
    auto it = M.find(k);
    if (it == M.end()) return k;
    auto j = it->second.find(lang());
    if (j != it->second.end()) return j->second;
    auto e = it->second.find("en");
    return e != it->second.end() ? e->second : k;
}

} // namespace i18n
