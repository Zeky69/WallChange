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

// Fonction pour envoyer une image locale à un autre utilisateur
int send_local_image(const char *filepath, const char *target_user) {
    char command[2048];
    // On utilise curl pour uploader le fichier
    // L'URL de base est WS_URL mais avec http:// au lieu de ws://
    char http_url[512];
    strncpy(http_url, WS_URL, sizeof(http_url));
    if (strncmp(http_url, "ws://", 5) == 0) {
        // Remplacement simple ws -> http
        http_url[0] = 'h'; http_url[1] = 't'; http_url[2] = 't'; http_url[3] = 'p';
        // On décale le reste si besoin, mais ici ws:// et http:// ont pas la même longueur (5 vs 7)
        // Plus simple : on remplace ws par http
        snprintf(http_url, sizeof(http_url), "http%s", WS_URL + 2);
    }

    printf("Envoi de l'image %s à %s via %s...\n", filepath, target_user, http_url);
    
    snprintf(command, sizeof(command), 
             "curl -F \"file=@%s\" \"%s/api/upload?id=%s\"", 
             filepath, http_url, target_user);
             
    int ret = system(command);
    if (ret == 0) {
        printf("\nImage envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi.\n");
        return 1;
    }
}

int main(int argc, char **argv) {
    // Mode commande : envoi d'image
    if (argc >= 4 && strcmp(argv[1], "send") == 0) {
        const char *filepath = argv[2];
        const char *target_user = argv[3];
        return send_local_image(filepath, target_user);
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
