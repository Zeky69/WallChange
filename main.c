#include "mongoose.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Configuration
#define WS_URL "wss://wallchange.codeky.fr" 

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

// Fonction de mise à jour automatique
void perform_update() {
    printf("Mise à jour demandée...\n");
    
    // 1. Déterminer le dossier source
    char source_dir[PATH_MAX];
    const char *home = getenv("HOME");
    int source_found = 0;

    if (home) {
        snprintf(source_dir, sizeof(source_dir), "%s/.wallchange_source", home);
        if (access(source_dir, F_OK) == 0) {
            source_found = 1;
        }
    }

    // Fallback: dossier courant si Makefile présent
    if (!source_found && access("Makefile", F_OK) == 0) {
        if (getcwd(source_dir, sizeof(source_dir)) != NULL) {
            source_found = 1;
            printf("Utilisation du dossier courant comme source: %s\n", source_dir);
        }
    }

    if (!source_found) {
        fprintf(stderr, "Erreur: Impossible de localiser le dossier source (.wallchange_source ou dossier courant).\n");
        return;
    }

    // 2. Sauvegarder le chemin de l'exécutable actuel
    char current_exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", current_exe, sizeof(current_exe) - 1);
    if (len == -1) {
        perror("readlink failed");
        return;
    }
    current_exe[len] = '\0';

    // 3. Se déplacer dans le dossier source
    printf("Changement de répertoire vers %s\n", source_dir);
    if (chdir(source_dir) != 0) {
        perror("chdir failed");
        return;
    }
    
    // 4. Git pull
    printf("Exécution de git pull...\n");
    if (system("git pull") != 0) {
        printf("Attention: git pull a échoué ou n'est pas nécessaire.\n");
    }
    
    // 5. Recompile
    printf("Recompilation...\n");
    if (system("make re") != 0) {
        printf("Erreur lors de la compilation.\n");
        return;
    }
    
    // 6. Copier le nouveau binaire si nécessaire
    char new_binary[PATH_MAX + 16];
    snprintf(new_binary, sizeof(new_binary), "%s/wallchange", source_dir);
    
    // On compare les chemins
    char *real_new = realpath(new_binary, NULL);
    char *real_current = realpath(current_exe, NULL);

    if (real_new && real_current && strcmp(real_new, real_current) != 0) {
        printf("Mise à jour du binaire installé: %s -> %s\n", new_binary, current_exe);
        
        // Suppression de l'ancien pour éviter "Text file busy"
        unlink(current_exe);
        
        size_t cmd_len = strlen(new_binary) + strlen(current_exe) + 64;
        char *cp_cmd = malloc(cmd_len);
        if (cp_cmd) {
            snprintf(cp_cmd, cmd_len, "cp '%s' '%s'", new_binary, current_exe);
            if (system(cp_cmd) != 0) {
                fprintf(stderr, "Erreur lors de la copie du binaire.\n");
                free(cp_cmd);
                if (real_new) free(real_new);
                if (real_current) free(real_current);
                return;
            }
            free(cp_cmd);
        }
    } else {
        printf("Le binaire s'exécute déjà depuis la source ou chemins identiques.\n");
    }
    
    if (real_new) free(real_new);
    if (real_current) free(real_current);
    
    // 7. Restart
    printf("Redémarrage du client...\n");
    
    char *args[] = {current_exe, NULL};
    execv(current_exe, args);
    
    // Si execv échoue
    perror("execv failed");
}

// Traitement du message reçu
void handle_message(const char *msg, size_t len) {
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
