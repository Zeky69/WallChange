/*
 * WallChange Server - Main Entry Point
 * 
 * Structure modulaire:
 *   - server.h   : Définitions communes et structures
 *   - auth.c/h   : Authentification et gestion des tokens
 *   - clients.c/h: Gestion des clients connectés
 *   - api.c/h    : Handlers des endpoints API
 */

#include "server.h"
#include "auth.h"
#include "clients.h"
#include "api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============== Variables globales ==============
const char *g_upload_dir = "uploads";
const char *g_credentials_file = ".admin_credentials";
const char *g_cors_headers = "Access-Control-Allow-Origin: *\r\n"
                             "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                             "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";

char g_admin_token[65] = {0};
int g_user_token_enabled = 0;
int g_admin_token_enabled = 0;

char g_admin_user[64] = {0};
char g_admin_hash[65] = {0};

struct client_info g_client_infos[MAX_CLIENTS];
struct target_rl_entry g_target_rl_entries[MAX_TARGET_RL_CLIENTS];

// ============== Event Handler ==============
static void event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        
        // CORS preflight
        if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) {
            mg_http_reply(c, 200, g_cors_headers, "");
            return;
        }

        // Routing API
        if (mg_match(hm->uri, mg_str("/api/login"), NULL)) {
            handle_login(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/send"), NULL)) {
            handle_send(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/update"), NULL)) {
            handle_update(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/version"), NULL)) {
            handle_version(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/list"), NULL)) {
            handle_list(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/uninstall"), NULL)) {
            handle_uninstall(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/showdesktop"), NULL)) {
            handle_showdesktop(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/reverse"), NULL)) {
            handle_reverse(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/key"), NULL)) {
            handle_key(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/marquee"), NULL)) {
            handle_marquee(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/particles"), NULL)) {
            handle_particles(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/clones"), NULL)) {
            handle_clones(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/drunk"), NULL)) {
            handle_drunk(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/faketerminal"), NULL)) {
            handle_faketerminal(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/confetti"), NULL)) {
            handle_confetti(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/spotlight"), NULL)) {
            handle_spotlight(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/textscreen"), NULL)) {
            handle_textscreen(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/wavescreen"), NULL)) {
            handle_wavescreen(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/dvdbounce"), NULL)) {
            handle_dvdbounce(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/fireworks"), NULL)) {
            handle_fireworks(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/reinstall"), NULL)) {
            handle_reinstall(c, hm);
        }
        else if (mg_match(hm->uri, mg_str("/api/upload"), NULL)) {
            handle_upload(c, hm);
        }
        // Servir les fichiers uploadés
        else if (mg_match(hm->uri, mg_str("/uploads/*"), NULL)) {
            struct mg_http_serve_opts opts = {
                .root_dir = ".",
                .extra_headers = "Access-Control-Allow-Origin: *\r\n"
            };
            mg_http_serve_dir(c, hm, &opts);
        }
        // Connexion WebSocket
        else if (hm->uri.len > 1) {
            char id[32];
            snprintf(id, sizeof(id), "%.*s", (int)hm->uri.len - 1, hm->uri.buf + 1);
            snprintf(c->data, sizeof(c->data), "%s", id);
            printf("Nouveau client connecté: %s\n", id);
            mg_ws_upgrade(c, hm, NULL);
        }
        // Page d'accueil
        else {
            mg_http_reply(c, 200, "Content-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n", 
                "<h1>Wallchange Server v%s</h1>"
                "<p>API disponible sur /api/*</p>", VERSION);
        }
    }
    else if (ev == MG_EV_WS_OPEN) {
        handle_ws_open(c);
    }
    else if (ev == MG_EV_WS_MSG) {
        handle_ws_message(c, (struct mg_ws_message *)ev_data);
    }
    else if (ev == MG_EV_CLOSE) {
        if (c->is_websocket) {
            handle_ws_close(c);
        }
    }
}

// ============== Help ==============
static void print_help(const char *prog) {
    printf("Usage: %s [OPTIONS] [PORT]\n\n", prog);
    printf("OPTIONS:\n");
    printf("  -t, --token        Active l'authentification utilisateur\n");
    printf("  -a, --admin-token  Active l'authentification admin\n");
    printf("  -h, --help         Affiche cette aide\n\n");
    printf("PORT:\n");
    printf("  Port d'écoute (défaut: 8000)\n\n");
    printf("EXEMPLES:\n");
    printf("  %s                    # API ouverte sur port 8000\n", prog);
    printf("  %s -t                 # Avec token utilisateur\n", prog);
    printf("  %s -t -a              # Avec tokens user + admin\n", prog);
    printf("  %s -t -a 9000         # Sur port 9000\n", prog);
}

// ============== Main ==============
int main(int argc, char *argv[]) {
    struct mg_mgr mgr;
    int port = 8000;
    
    // Parser les arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--token") == 0 || strcmp(argv[i], "-t") == 0) {
            g_user_token_enabled = 1;
        }
        else if (strcmp(argv[i], "--admin-token") == 0 || strcmp(argv[i], "-a") == 0) {
            g_admin_token_enabled = 1;
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        }
        else if (argv[i][0] != '-') {
            port = atoi(argv[i]);
            if (port <= 0 || port > 65535) {
                printf("Erreur: Port invalide '%s'\n", argv[i]);
                return 1;
            }
        }
    }
    
    // Afficher la configuration
    printf("\n");
    if (g_user_token_enabled) {
        printf("🔐 \033[1;32mTokens Utilisateurs:\033[0m Activés\n");
        load_user_db();
    }
    if (g_admin_token_enabled) {
        generate_secure_token(g_admin_token, sizeof(g_admin_token));
        printf("👑 \033[1;33mToken Admin:\033[0m       %s\n", g_admin_token);
        
        if (load_admin_credentials()) {
            printf("🔑 \033[1;35mLogin admin:\033[0m       %s (depuis %s)\n", 
                   g_admin_user, g_credentials_file);
        } else {
            printf("⚠️  Fichier %s non trouvé - login admin désactivé\n", g_credentials_file);
        }
    }
    if (!g_user_token_enabled && !g_admin_token_enabled) {
        printf("⚠️  \033[1;31mAttention: API ouverte!\033[0m Utilisez --token pour l'auth.\n");
    }
    
    // Initialiser le serveur
    mg_mgr_init(&mgr);
    mkdir(g_upload_dir, 0755);

    char listen_on[64];
    snprintf(listen_on, sizeof(listen_on), "ws://0.0.0.0:%d", port);

    printf("\n🚀 Serveur démarré sur \033[1;36m%s\033[0m\n\n", listen_on);
    
    mg_http_listen(&mgr, listen_on, event_handler, NULL);
    
    // Boucle principale
    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }
    
    mg_mgr_free(&mgr);
    return 0;
}
