#include "mongoose.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>

// Configuration
#define WS_URL "ws://localhost:8000" 

// Variables globales
static int interrupted = 0;
static struct mg_mgr mgr;
static struct mg_connection *ws_conn = NULL;
static time_t last_connect_try = 0;

// Fonction pour récupérer le nom de l'utilisateur
char *get_username() {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return strdup(pw->pw_name);
    }
    return strdup(getenv("USER"));
}

// Fonction pour télécharger l'image via la commande curl système
int download_image(const char *url, const char *filepath) {
    char command[2048];
    // -s: silencieux, -L: suivre les redirections, -o: fichier de sortie
    // On met des quotes autour des chemins pour gérer les espaces
    snprintf(command, sizeof(command), "curl -s -L -o '%s' '%s'", filepath, url);
    printf("Exécution: %s\n", command);
    int ret = system(command);
    return (ret == 0);
}

// Fonction pour changer le fond d'écran
void set_wallpaper(const char *filepath) {
    char command[2048];
    // Pour GNOME / Ubuntu
    snprintf(command, sizeof(command), "gsettings set org.gnome.desktop.background picture-uri 'file://%s'", filepath);
    printf("Changement fond d'écran: %s\n", command);
    if (system(command) != 0) fprintf(stderr, "Erreur lors de l'exécution de la commande\n");
    
    // Pour le mode sombre
    snprintf(command, sizeof(command), "gsettings set org.gnome.desktop.background picture-uri-dark 'file://%s'", filepath);
    if (system(command) != 0) fprintf(stderr, "Erreur lors de l'exécution de la commande (dark mode)\n");
}

// Traitement du message reçu
void handle_message(const char *msg, size_t len) {
    printf("Message reçu: %.*s\n", (int)len, msg);

    cJSON *json = cJSON_ParseWithLength(msg, len);
    if (json == NULL) {
        printf("Erreur: JSON invalide.\n");
        return;
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

// Fonction de connexion
void connect_ws() {
    char *username = get_username();
    char url[512];
    // Construction de l'URL avec l'ID dans le chemin: ws://localhost:8000/zakburak
    snprintf(url, sizeof(url), "%s/%s", WS_URL, username);
    free(username);

    printf("Tentative de connexion à %s...\n", url);
    mg_ws_connect(&mgr, url, fn, NULL, NULL);
}

// Fonction pour envoyer une image (locale ou URL) à un autre utilisateur
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
        // Par défaut, on suppose que arg1 est l'image pour afficher l'erreur
        image_source = arg1;
        target_user = arg2;
    }

    // Construction de l'URL HTTP du serveur
    char http_url[512];
    strncpy(http_url, WS_URL, sizeof(http_url));
    if (strncmp(http_url, "ws://", 5) == 0) {
        snprintf(http_url, sizeof(http_url), "http%s", WS_URL + 2);
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

int main(int argc, char **argv) {
    // Mode commande : envoi d'image
    if (argc >= 4 && strcmp(argv[1], "send") == 0) {
        return send_command(argv[2], argv[3]);
    }

    mg_mgr_init(&mgr);
    
    // Premier essai
    connect_ws();
    last_connect_try = time(NULL);

    printf("Client démarré. Appuyez sur Ctrl+C pour quitter.\n");

    while (!interrupted) {
        mg_mgr_poll(&mgr, 100);

        // Reconnexion automatique
        if (ws_conn == NULL) {
            time_t now = time(NULL);
            if (now - last_connect_try > 5) {
                connect_ws();
                last_connect_try = now;
            }
        }
    }

    mg_mgr_free(&mgr);
    return 0;
}
