#include "client/network.h"
#include "client/utils.h"
#include "client/wallpaper.h"
#include "client/updater.h"
#include "mongoose.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WS_URL "wss://wallchange.codeky.fr"

static struct mg_mgr mgr;
static struct mg_connection *ws_conn = NULL;
static time_t last_connect_try = 0;

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
    // Construction de l'URL avec l'ID dans le chemin: ws://localhost:8000/zakburak
    snprintf(url, sizeof(url), "%s/%s", WS_URL, username);
    free(username);

    printf("Tentative de connexion à %s...\n", url);
    mg_ws_connect(&mgr, url, fn, NULL, NULL);
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
    if (strncmp(WS_URL, "ws://", 5) == 0) {
        snprintf(http_url, sizeof(http_url), "http%s", WS_URL + 2);
    } else if (strncmp(WS_URL, "wss://", 6) == 0) {
        snprintf(http_url, sizeof(http_url), "https%s", WS_URL + 3);
    } else {
        strncpy(http_url, WS_URL, sizeof(http_url));
    }

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
    if (strncmp(WS_URL, "ws://", 5) == 0) {
        snprintf(http_url, sizeof(http_url), "http%s", WS_URL + 2);
    } else if (strncmp(WS_URL, "wss://", 6) == 0) {
        snprintf(http_url, sizeof(http_url), "https%s", WS_URL + 3);
    } else {
        strncpy(http_url, WS_URL, sizeof(http_url));
    }

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
