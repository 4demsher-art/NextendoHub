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
      { "tab_online",  {{"en","In game now"},{"fr","En jeu"},{"es","En juego"}} },
      { "tab_status",  {{"en","Server status"},{"fr","État"},{"es","Estado"}} },
      { "tab_friends", {{"en","Friends"},{"fr","Amis"},{"es","Amigos"}} },
      { "tab_settings",{{"en","Settings"},{"fr","Réglages"},{"es","Ajustes"}} },
      { "players_ingame",{{"en","players in game"},{"fr","joueurs en jeu"},{"es","jugadores en juego"}} },
      { "games_active",{{"en","games active"},{"fr","jeux actifs"},{"es","juegos activos"}} },
      { "player",  {{"en","player"},{"fr","joueur"},{"es","jugador"}} },
      { "players", {{"en","players"},{"fr","joueurs"},{"es","jugadores"}} },
      { "all_ok",  {{"en","All systems operational"},{"fr","Tous les services opérationnels"},{"es","Todos los servicios operativos"}} },
      { "services_down",{{"en","service(s) down"},{"fr","service(s) en panne"},{"es","servicio(s) caído(s)"}} },
      { "up_of",   {{"en","up"},{"fr","opérationnels"},{"es","operativos"}} },
      { "st_up",   {{"en","operational"},{"fr","opérationnel"},{"es","operativo"}} },
      { "st_down", {{"en","down"},{"fr","hors service"},{"es","caído"}} },
      { "st_maint",{{"en","maintenance"},{"fr","maintenance"},{"es","mantenimiento"}} },
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
      { "wants_to_add",{{"en","wants to add you"},{"fr","veut t'ajouter"},{"es","quiere añadirte"}} },
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
      { "taken",   {{"en","taken"},{"fr","déjà pris"},{"es","en uso"}} },
      { "available",{{"en","available"},{"fr","disponible"},{"es","disponible"}} },
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
      { "live",    {{"en","live"},{"fr","en direct"},{"es","en vivo"}} },
    };
    auto it = M.find(k);
    if (it == M.end()) return k;
    auto j = it->second.find(lang());
    if (j != it->second.end()) return j->second;
    auto e = it->second.find("en");
    return e != it->second.end() ? e->second : k;
}

} // namespace i18n
