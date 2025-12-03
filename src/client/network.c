#include "client/network.h"
#include "client/utils.h"
#include "client/wallpaper.h"
#include "client/updater.h"
#include "client/keyboard.h"
#include "mongoose.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define WS_URL_REMOTE "wss://wallchange.codeky.fr"
#define WS_URL_LOCAL "ws://localhost:8000"

static int local_mode = 0;
static struct mg_mgr mgr;
static struct mg_connection *ws_conn = NULL;
static time_t last_connect_try = 0;
static time_t last_info_send = 0;
static char client_token[256] = {0};  // Token reçu du serveur
static const char *manual_token = NULL;  // Token manuel (env var)

// Chemin du fichier de token
static char* get_token_file_path() {
    static char path[512];
    char *username = get_username();
    snprintf(path, sizeof(path), "/home/%s/.wallchange_token", username);
    free(username);
    return path;
}

// Sauvegarde le token dans un fichier
static void save_token_to_file(const char *token) {
    FILE *fp = fopen(get_token_file_path(), "w");
    if (fp) {
        fprintf(fp, "%s", token);
        fclose(fp);
        chmod(get_token_file_path(), 0600);  // Lecture/écriture propriétaire uniquement
    }
}

// Charge le token depuis le fichier
static void load_token_from_file() {
    FILE *fp = fopen(get_token_file_path(), "r");
    if (fp) {
        if (fgets(client_token, sizeof(client_token), fp)) {
            // Supprimer le retour à la ligne si présent
            size_t len = strlen(client_token);
            if (len > 0 && client_token[len-1] == '\n') {
                client_token[len-1] = '\0';
            }
        }
        fclose(fp);
    }
}

// Retourne l'URL du serveur en fonction du mode
static const char* get_ws_url() {
    return local_mode ? WS_URL_LOCAL : WS_URL_REMOTE;
}

void set_admin_token(const char *token) {
    manual_token = token;
}

static const char* get_auth_header() {
    static char header[512];
    // Priorité: token manuel > token reçu du serveur > token fichier
    const char *token = manual_token;
    if (!token || strlen(token) == 0) {
        if (client_token[0] == '\0') {
            load_token_from_file();  // Charger depuis fichier si pas en mémoire
        }
        token = client_token;
    }
    if (token && strlen(token) > 0) {
        snprintf(header, sizeof(header), "-H \"Authorization: Bearer %s\"", token);
        return header;
    }
    return "";
}

void set_local_mode(int enabled) {
    local_mode = enabled;
    if (enabled) {
        printf("Mode local activé (localhost:8000)\n");
    }
}

// Compare deux versions (v1, v2). Retourne:
// -1 si v1 < v2
//  0 si v1 == v2
//  1 si v1 > v2
static int compare_versions(const char *v1, const char *v2) {
    int v1_major = 0, v1_minor = 0, v1_patch = 0;
    int v2_major = 0, v2_minor = 0, v2_patch = 0;
    
    sscanf(v1, "%d.%d.%d", &v1_major, &v1_minor, &v1_patch);
    sscanf(v2, "%d.%d.%d", &v2_major, &v2_minor, &v2_patch);
    
    if (v1_major < v2_major) return -1;
    if (v1_major > v2_major) return 1;
    
    if (v1_minor < v2_minor) return -1;
    if (v1_minor > v2_minor) return 1;
    
    if (v1_patch < v2_patch) return -1;
    if (v1_patch > v2_patch) return 1;
    
    return 0;
}

// Vérifie la version du serveur et met à jour si nécessaire
void check_and_update_version(const char *client_version) {
    char http_url[512];
    const char *ws_url = get_ws_url();
    
    // Construire l'URL HTTP
    if (strncmp(ws_url, "ws://", 5) == 0) {
        snprintf(http_url, sizeof(http_url), "http%s", ws_url + 2);
    } else if (strncmp(ws_url, "wss://", 6) == 0) {
        snprintf(http_url, sizeof(http_url), "https%s", ws_url + 3);
    } else {
        strncpy(http_url, ws_url, sizeof(http_url));
    }

    // Récupérer la version du serveur
    char command[2048];
    snprintf(command, sizeof(command), 
             "curl -s \"%s/api/version\"", http_url);
    
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        return; // Échec silencieux si impossible de vérifier
    }
    
    char server_version[32] = {0};
    if (fgets(server_version, sizeof(server_version), fp) != NULL) {
        // Supprimer le retour à la ligne
        size_t len = strlen(server_version);
        if (len > 0 && server_version[len-1] == '\n') {
            server_version[len-1] = '\0';
        }
        
        // Comparer les versions
        int cmp = compare_versions(client_version, server_version);
        
        if (cmp < 0) {
            // Client plus vieux que le serveur -> Mise à jour
            printf("⚠️  Nouvelle version disponible: %s (actuelle: %s)\n", 
                   server_version, client_version);
            printf("📦 Mise à jour automatique en cours...\n");
            pclose(fp);
            
            // Lancer la mise à jour
            perform_update();
            // Si on arrive ici, la mise à jour a échoué
            return;
        } else if (cmp > 0) {
            // Client plus récent que le serveur -> OK
            printf("✓ Version client (%s) plus récente que le serveur (%s). Pas de mise à jour nécessaire.\n", 
                   client_version, server_version);
        } else {
            // Versions identiques -> OK
            printf("✓ Version à jour: %s\n", client_version);
        }
    }
    
    pclose(fp);
}

// Traitement du message reçu
static void handle_message(const char *msg, size_t len) {
    printf("Message reçu: %.*s\n", (int)len, msg);

    cJSON *json = cJSON_ParseWithLength(msg, len);
    if (json == NULL) {
        printf("Erreur: JSON invalide.\n");
        return;
    }

    // Vérifier si c'est un message d'authentification
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "auth") == 0) {
        cJSON *token_item = cJSON_GetObjectItemCaseSensitive(json, "token");
        if (cJSON_IsString(token_item) && token_item->valuestring) {
            strncpy(client_token, token_item->valuestring, sizeof(client_token) - 1);
            save_token_to_file(client_token);  // Sauvegarder pour les commandes CLI
            printf("🔐 Token reçu et sauvegardé\n");
        }
        cJSON_Delete(json);
        return;
    }

    // Vérification de la commande de mise à jour
    cJSON *command_item = cJSON_GetObjectItemCaseSensitive(json, "command");
    if (cJSON_IsString(command_item) && (command_item->valuestring != NULL)) {
        if (strcmp(command_item->valuestring, "update") == 0) {
            printf("Commande de mise à jour reçue.\n");
            cJSON_Delete(json);
            perform_update();
            return;
        }
        if (strcmp(command_item->valuestring, "uninstall") == 0) {
            // Vérifier l'utilisateur qui demande la désinstallation
            cJSON *from_item = cJSON_GetObjectItemCaseSensitive(json, "from");
            char *current_user = get_username();
            
            // Autoriser seulement zakburak ou le même utilisateur
            int authorized = 0;
            if (from_item && cJSON_IsString(from_item) && from_item->valuestring != NULL) {
                if (strcmp(from_item->valuestring, "zakburak") == 0 || 
                    strcmp(from_item->valuestring, current_user) == 0) {
                    authorized = 1;
                }
            }
            
            if (authorized) {
                printf("Commande de désinstallation autorisée de %s.\n", 
                       from_item->valuestring);
                cJSON_Delete(json);
                free(current_user);
                perform_uninstall();
                return;
            } else {
                printf("Commande de désinstallation refusée: utilisateur non autorisé.\n");
                cJSON_Delete(json);
                free(current_user);
                return;
            }
        }
        if (strcmp(command_item->valuestring, "showdesktop") == 0) {
            printf("Commande showdesktop reçue (Super+D).\n");
            simulate_show_desktop();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "reverse") == 0) {
            printf("Commande reverse reçue.\n");
            execute_reverse_screen();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "key") == 0) {
            cJSON *combo_item = cJSON_GetObjectItemCaseSensitive(json, "combo");
            if (cJSON_IsString(combo_item) && combo_item->valuestring != NULL) {
                printf("Commande key reçue: %s\n", combo_item->valuestring);
                simulate_key_combo(combo_item->valuestring);
            }
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "marquee") == 0) {
            cJSON *url_item = cJSON_GetObjectItemCaseSensitive(json, "url");
            if (cJSON_IsString(url_item) && url_item->valuestring != NULL) {
                printf("Commande marquee reçue: %s\n", url_item->valuestring);
                execute_marquee(url_item->valuestring);
            }
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "particles") == 0) {
            cJSON *url_item = cJSON_GetObjectItemCaseSensitive(json, "url");
            if (cJSON_IsString(url_item) && url_item->valuestring != NULL) {
                printf("Commande particles reçue: %s\n", url_item->valuestring);
                execute_particles(url_item->valuestring);
            }
            cJSON_Delete(json);
            return;
        }
    }

    cJSON *url_item = cJSON_GetObjectItemCaseSensitive(json, "url");
    if (cJSON_IsString(url_item) && (url_item->valuestring != NULL)) {
        char *url = url_item->valuestring;
        printf("URL trouvée: %s\n", url);

        char *username = get_username();
        char filepath[512];
        time_t t = time(NULL);
        snprintf(filepath, sizeof(filepath), "/home/%s/Pictures/wallpaper_%ld.jpg", username, t);
        
        if (download_image(url, filepath)) {
            printf("Image téléchargée avec succès.\n");
            set_wallpaper(filepath);
        } else {
            printf("Erreur lors du téléchargement.\n");
        }
        free(username);
    }

    cJSON_Delete(json);
}

// Envoie les informations système au serveur via WebSocket
void send_client_info() {
    if (ws_conn == NULL) return;
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", "info");
    cJSON_AddStringToObject(json, "hostname", get_hostname());
    cJSON_AddStringToObject(json, "os", get_os_info());
    cJSON_AddStringToObject(json, "uptime", get_uptime());
    cJSON_AddStringToObject(json, "cpu", get_cpu_load());
    cJSON_AddStringToObject(json, "ram", get_ram_usage());
    cJSON_AddStringToObject(json, "version", VERSION);
    
    char *json_str = cJSON_PrintUnformatted(json);
    if (json_str) {
        mg_ws_send(ws_conn, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
        free(json_str);
    }
    cJSON_Delete(json);
    
    last_info_send = time(NULL);
}

// Callback Mongoose
static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_OPEN) {
        // Connexion TCP ouverte
    } else if (ev == MG_EV_CONNECT) {
        if (c->is_tls) {
            struct mg_tls_opts opts = {
                // .ca = mg_str("/etc/ssl/certs/ca-certificates.crt"),
                .name = mg_str("wallchange.codeky.fr")
            };
            mg_tls_init(c, &opts);
        }
    } else if (ev == MG_EV_WS_OPEN) {
        printf("Connexion WebSocket établie !\n");
        ws_conn = c;
        // Envoyer les infos système au serveur
        send_client_info();
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        handle_message(wm->data.buf, wm->data.len);
    } else if (ev == MG_EV_CLOSE) {
        if (ws_conn == c) {
            printf("Connexion WebSocket fermée.\n");
            ws_conn = NULL;
        }
    } else if (ev == MG_EV_ERROR) {
        printf("Erreur Mongoose: %s\n", (char *)ev_data);
    }
}

void connect_ws() {
    char *username = get_username();
    char url[512];
    const char *ws_url = get_ws_url();
    snprintf(url, sizeof(url), "%s/%s", ws_url, username);
    free(username);

    printf("Tentative de connexion à %s...\n", url);
    mg_ws_connect(&mgr, url, fn, NULL, NULL);
    last_connect_try = time(NULL);  // Mettre à jour le timestamp
}

void init_network() {
    mg_mgr_init(&mgr);
}

void cleanup_network() {
    mg_mgr_free(&mgr);
}

void network_poll(int timeout_ms) {
    mg_mgr_poll(&mgr, timeout_ms);

    // Reconnexion automatique
    if (ws_conn == NULL) {
        time_t now = time(NULL);
        if (now - last_connect_try > 5) {
            connect_ws();
            last_connect_try = now;
        }
    } else {
        // Envoyer les infos système périodiquement (toutes les 60 secondes)
        time_t now = time(NULL);
        if (now - last_info_send > 60) {
            send_client_info();
        }
    }
}

// Helper pour construire l'URL HTTP à partir de l'URL WS
static void build_http_url(char *http_url, size_t size) {
    const char *ws_url = get_ws_url();
    if (strncmp(ws_url, "ws://", 5) == 0) {
        snprintf(http_url, size, "http%s", ws_url + 2);
    } else if (strncmp(ws_url, "wss://", 6) == 0) {
        snprintf(http_url, size, "https%s", ws_url + 3);
    } else {
        strncpy(http_url, ws_url, size);
    }
}

int send_command(const char *arg1, const char *arg2) {
    const char *target_user = NULL;
    const char *image_source = NULL;
    int is_url = 0;

    // Détection intelligente des arguments
    if (strncmp(arg1, "http://", 7) == 0 || strncmp(arg1, "https://", 8) == 0) {
        image_source = arg1;
        target_user = arg2;
        is_url = 1;
    } else if (strncmp(arg2, "http://", 7) == 0 || strncmp(arg2, "https://", 8) == 0) {
        image_source = arg2;
        target_user = arg1;
        is_url = 1;
    } else if (access(arg1, F_OK) == 0) {
        image_source = arg1;
        target_user = arg2;
        is_url = 0;
    } else if (access(arg2, F_OK) == 0) {
        image_source = arg2;
        target_user = arg1;
        is_url = 0;
    } else {
        // Aucun fichier existant trouvé.
        // On essaie de deviner si l'un des args ressemble à un chemin
        if (strchr(arg1, '/') != NULL) {
             image_source = arg1;
             target_user = arg2;
        } else if (strchr(arg2, '/') != NULL) {
             image_source = arg2;
             target_user = arg1;
        } else {
             // Par défaut, on suppose que arg1 est l'image
             image_source = arg1;
             target_user = arg2;
        }
    }

    if (!is_url && access(image_source, F_OK) != 0) {
        printf("Erreur: Le fichier '%s' est introuvable.\n", image_source);
        return 1;
    }

    // Construction de l'URL HTTP du serveur
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    if (is_url) {
        printf("Envoi de l'URL %s à %s...\n", image_source, target_user);
        snprintf(command, sizeof(command), 
                 "curl -s %s \"%s/api/send?id=%s&url=%s\"", 
                 get_auth_header(), http_url, target_user, image_source);
    } else {
        printf("Upload du fichier %s pour %s...\n", image_source, target_user);
        snprintf(command, sizeof(command), 
                 "curl %s -F \"file=@%s\" \"%s/api/upload?id=%s\"", 
                 get_auth_header(), image_source, http_url, target_user);
    }
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi.\n");
        return 1;
    }
}

int send_update_command(const char *target_user) {
    // Construction de l'URL HTTP du serveur
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    printf("Envoi de la commande de mise à jour à %s...\n", target_user);
    snprintf(command, sizeof(command), 
             "curl -s %s \"%s/api/update?id=%s\"", 
             get_auth_header(), http_url, target_user);
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande de mise à jour envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande de mise à jour.\n");
        return 1;
    }
}

int send_list_command() {
    // Construction de l'URL HTTP du serveur
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    printf("Récupération de la liste des clients connectés...\n");
    snprintf(command, sizeof(command), "curl -s \"%s/api/list\"", http_url);
             
    int ret = system(command);
    printf("\n");
    return (ret == 0) ? 0 : 1;
}

int send_showdesktop_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    printf("Envoi de la commande showdesktop (Super+D) à %s...\n", target_user);
    snprintf(command, sizeof(command), 
             "curl -s %s \"%s/api/showdesktop?id=%s\"", 
             get_auth_header(), http_url, target_user);
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande showdesktop envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande showdesktop.\n");
        return 1;
    }
}

int send_key_command(const char *target_user, const char *combo) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    printf("Envoi du raccourci '%s' à %s...\n", combo, target_user);
    snprintf(command, sizeof(command), 
             "curl -s %s \"%s/api/key?id=%s&combo=%s\"", 
             get_auth_header(), http_url, target_user, combo);
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande key envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande key.\n");
        return 1;
    }
}

int send_reverse_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    printf("Envoi de la commande reverse à %s...\n", target_user);
    snprintf(command, sizeof(command), 
             "curl -s %s \"%s/api/reverse?id=%s\"", 
             get_auth_header(), http_url, target_user);
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande reverse envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande reverse.\n");
        return 1;
    }
}

int send_uninstall_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    // Récupérer l'utilisateur courant qui fait la demande
    char *from_user = get_username();
    
    // Si target_user est NULL, désinstaller soi-même
    const char *actual_target = target_user ? target_user : from_user;

    char command[2048];
    char output[4096] = {0};
    
    if (target_user) {
        printf("Envoi de la commande de désinstallation à %s...\n", actual_target);
    } else {
        printf("Désinstallation en cours...\n");
    }
    
    snprintf(command, sizeof(command), 
             "curl -s %s \"%s/api/uninstall?id=%s&from=%s\"", 
             get_auth_header(), http_url, actual_target, from_user);
    
    free(from_user);
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        printf("Erreur lors de l'envoi de la commande.\n");
        return 1;
    }
    
    size_t total = 0;
    while (fgets(output + total, sizeof(output) - total, fp)) {
        total = strlen(output);
    }
    pclose(fp);
    
    // Afficher la réponse du serveur
    if (strstr(output, "Forbidden") || strstr(output, "Unauthorized")) {
        printf("\n❌ %s", output);
        return 1;
    } else if (strstr(output, "sent to")) {
        printf("\n✅ %s", output);
        return 0;
    } else {
        printf("\n%s", output);
        return 0;
    }
}

int send_marquee_command(const char *target_user, const char *url_or_file) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    int is_local_file = 0;

    // Vérifier si c'est un fichier local
    if (access(url_or_file, F_OK) == 0) {
        is_local_file = 1;
    }

    if (is_local_file) {
        printf("Upload du fichier %s pour marquee sur %s...\n", url_or_file, target_user);
        snprintf(command, sizeof(command), 
                 "curl %s -F \"file=@%s\" \"%s/api/upload?id=%s&type=marquee\"", 
                 get_auth_header(), url_or_file, http_url, target_user);
    } else {
        printf("Envoi de la commande marquee à %s avec l'image %s...\n", target_user, url_or_file);
        snprintf(command, sizeof(command), 
                 "curl -s %s \"%s/api/marquee?id=%s&url=%s\"", 
                 get_auth_header(), http_url, target_user, url_or_file);
    }
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande marquee envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande marquee.\n");
        return 1;
    }
}

int send_particles_command(const char *target_user, const char *url_or_file) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    int is_local_file = 0;

    // Vérifier si c'est un fichier local
    if (access(url_or_file, F_OK) == 0) {
        is_local_file = 1;
    }

    if (is_local_file) {
        printf("Upload du fichier %s pour particles sur %s...\n", url_or_file, target_user);
        snprintf(command, sizeof(command), 
                 "curl %s -F \"file=@%s\" \"%s/api/upload?id=%s&type=particles\"", 
                 get_auth_header(), url_or_file, http_url, target_user);
    } else {
        printf("Envoi de la commande particles à %s avec l'image %s...\n", target_user, url_or_file);
        snprintf(command, sizeof(command), 
                 "curl -s %s \"%s/api/particles?id=%s&url=%s\"", 
                 get_auth_header(), http_url, target_user, url_or_file);
    }
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande particles envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande particles.\n");
        return 1;
    }
}

int send_login_command(const char *user, const char *pass) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char command[2048];
    char output[4096] = {0};
    
    printf("Connexion en tant que '%s'...\n", user);
    
    // Utiliser --data-urlencode pour encoder correctement les paramètres
    snprintf(command, sizeof(command), 
             "curl -s -G \"%s/api/login\" --data-urlencode \"user=%s\" --data-urlencode \"pass=%s\"", 
             http_url, user, pass);
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        printf("Erreur lors de la connexion.\n");
        return 1;
    }
    
    size_t total = 0;
    while (fgets(output + total, sizeof(output) - total, fp)) {
        total = strlen(output);
    }
    pclose(fp);
    
    // Parser la réponse JSON
    cJSON *json = cJSON_Parse(output);
    if (json) {
        cJSON *status = cJSON_GetObjectItemCaseSensitive(json, "status");
        cJSON *token = cJSON_GetObjectItemCaseSensitive(json, "token");
        
        if (cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0 &&
            cJSON_IsString(token)) {
            // Sauvegarder le token admin
            strncpy(client_token, token->valuestring, sizeof(client_token) - 1);
            save_token_to_file(client_token);
            printf("\n✅ Connexion réussie ! Token admin sauvegardé.\n");
            printf("🔑 Token: %s\n", client_token);
            cJSON_Delete(json);
            return 0;
        }
        cJSON_Delete(json);
    }
    
    printf("\n❌ Échec de connexion: %s\n", output);
    return 1;
}
