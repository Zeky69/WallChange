#include "mongoose.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <sys/types.h>

static const char *s_listen_on = "ws://0.0.0.0:8000";
static const char *s_upload_dir = "uploads";
static const char *s_cors_headers = "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n";

// Rate limiting
#define RL_COOLDOWN_SEC 0.5
#define MAX_RL_CLIENTS 50

struct rl_entry {
    unsigned long ip;
    double last_time;
};

static struct rl_entry s_rl_entries[MAX_RL_CLIENTS];

static unsigned long get_client_ip(struct mg_connection *c, struct mg_http_message *hm) {
    struct mg_str *h = mg_http_get_header(hm, "X-Forwarded-For");
    if (h != NULL && h->len > 0) {
        unsigned long hash = 5381;
        for (size_t i = 0; i < h->len; i++) {
            if (h->buf[i] == ',' || h->buf[i] == ' ') break;
            hash = ((hash << 5) + hash) + h->buf[i];
        }
        return hash;
    }
    // Hash the IP address from c->rem.ip (uint8_t[16])
    unsigned long hash = 5381;
    for (int i = 0; i < 16; i++) {
        hash = ((hash << 5) + hash) + c->rem.ip[i];
    }
    return hash;
}

static int is_rate_limited(unsigned long ip) {
    double now = (double)mg_millis() / 1000.0;
    for (int i = 0; i < MAX_RL_CLIENTS; i++) {
        if (s_rl_entries[i].ip == ip) {
            if (now - s_rl_entries[i].last_time < RL_COOLDOWN_SEC) {
                return 1; // Limited
            }
            s_rl_entries[i].last_time = now;
            return 0; // Allowed
        }
    }
    // Find empty slot
    for (int i = 0; i < MAX_RL_CLIENTS; i++) {
        if (s_rl_entries[i].ip == 0) {
            s_rl_entries[i].ip = ip;
            s_rl_entries[i].last_time = now;
            return 0;
        }
    }
    // Overwrite random (using time as random-ish index)
    int idx = (int)(now * 1000) % MAX_RL_CLIENTS;
    s_rl_entries[idx].ip = ip;
    s_rl_entries[idx].last_time = now;
    return 0;
}

// Fonction utilitaire pour récupérer un paramètre de la query string
// Mongoose a mg_http_get_var mais on va le faire simplement
void get_qs_var(const struct mg_str *query, const char *name, char *dst, size_t dst_len) {
    dst[0] = '\0';
    if (query && query->len > 0) {
        mg_http_get_var(query, name, dst, dst_len);
    }
}

static void fn(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *) ev_data;
        
        if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) {
            mg_http_reply(c, 200, s_cors_headers, "");
            return;
        }

        // Rate limiting check for API calls
        if (mg_match(hm->uri, mg_str("/api/*"), NULL)) {
             unsigned long ip = get_client_ip(c, hm);
             if (is_rate_limited(ip)) {
                 mg_http_reply(c, 429, s_cors_headers, "Too Many Requests\n");
                 return;
             }
        }

        // 1. API pour envoyer une image (Admin / Script)
        if (mg_match(hm->uri, mg_str("/api/send"), NULL)) {
            char target_id[32];
            char url[512];
            
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "url", url, sizeof(url));
            
            if (strlen(target_id) > 0 && strlen(url) > 0) {
                // Création du JSON
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "url", url);
                char *json_str = cJSON_PrintUnformatted(json);
                
                int found = 0;
                // On parcourt toutes les connexions pour trouver la bonne
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }
                
                free(json_str);
                cJSON_Delete(json);
                
                mg_http_reply(c, 200, s_cors_headers, "Sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'url' parameter\n");
            }
        }
        // 1.5 API pour demander une mise à jour au client
        else if (mg_match(hm->uri, mg_str("/api/update"), NULL)) {
            char target_id[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

            if (strlen(target_id) > 0) {
                // Création du JSON
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "update");
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                printf("Recherche du client '%s' pour mise à jour...\n", target_id);
                // On parcourt toutes les connexions pour trouver la bonne
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket) {
                        printf(" - Client connecté: '%s'\n", (char *)t->data);
                        if (strcmp(t->data, target_id) == 0) {
                            mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                            found++;
                        }
                    }
                }

                free(json_str);
                cJSON_Delete(json);

                mg_http_reply(c, 200, s_cors_headers, "Update request sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' parameter\n");
            }
        }
        // 1.55 API pour obtenir la version du serveur
        else if (mg_match(hm->uri, mg_str("/api/version"), NULL)) {
            mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", "1.0.22");
        }
        // 1.6 API pour lister les clients connectés
        else if (mg_match(hm->uri, mg_str("/api/list"), NULL)) {
            cJSON *json = cJSON_CreateArray();
            int count = 0;
            for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                if (t->is_websocket) {
                    cJSON_AddItemToArray(json, cJSON_CreateString((char *)t->data));
                    count++;
                }
            }
            char *json_str = cJSON_PrintUnformatted(json);
            mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s", json_str);
            free(json_str);
            cJSON_Delete(json);
        }
        // 1.65 API pour désinstaller un client
        else if (mg_match(hm->uri, mg_str("/api/uninstall"), NULL)) {
            char target_id[32];
            char from_user[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "from", from_user, sizeof(from_user));

            if (strlen(target_id) > 0 && strlen(from_user) > 0) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "uninstall");
                cJSON_AddStringToObject(json, "from", from_user);
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                printf("Recherche du client '%s' pour désinstallation (demandé par %s)...\n", 
                       target_id, from_user);
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket) {
                        if (strcmp(t->data, target_id) == 0) {
                            mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                            found++;
                        }
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Uninstall request sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'from' parameter\n");
            }
        }
        // 1.7 API pour envoyer la commande showdesktop (Super+D)
        else if (mg_match(hm->uri, mg_str("/api/showdesktop"), NULL)) {
            char target_id[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

            if (strlen(target_id) > 0) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "showdesktop");
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Showdesktop sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' parameter\n");
            }
        }
        // 1.75 API pour envoyer la commande reverse
        else if (mg_match(hm->uri, mg_str("/api/reverse"), NULL)) {
            char target_id[32];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

            if (strlen(target_id) > 0) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "reverse");
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Reverse sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' parameter\n");
            }
        }
        // 1.8 API pour envoyer un raccourci clavier personnalisé
        else if (mg_match(hm->uri, mg_str("/api/key"), NULL)) {
            char target_id[32];
            char combo[128];
            get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
            get_qs_var(&hm->query, "combo", combo, sizeof(combo));

            if (strlen(target_id) > 0 && strlen(combo) > 0) {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "command", "key");
                cJSON_AddStringToObject(json, "combo", combo);
                char *json_str = cJSON_PrintUnformatted(json);

                int found = 0;
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);
                mg_http_reply(c, 200, s_cors_headers, "Key '%s' sent to %d client(s)\n", combo, found);
            } else {
                mg_http_reply(c, 400, s_cors_headers, "Missing 'id' or 'combo' parameter\n");
            }
        }
        // 2. API pour uploader une image
        else if (mg_match(hm->uri, mg_str("/api/upload"), NULL)) {
            // On utilise mg_http_upload pour gérer le multipart
            // Il va sauvegarder le fichier dans le dossier uploads
            // On doit récupérer le nom du fichier pour construire l'URL
            
            // Note: mg_http_upload traite tout le body.
            // Pour simplifier, on va itérer sur les parts pour trouver le fichier et le sauver nous-même
            // car mg_http_upload ne retourne pas facilement le nom du fichier sauvegardé.
            
            struct mg_http_part part;
            size_t ofs = 0;
            int uploaded = 0;
            char saved_path[512] = {0};
            
            while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
                if (part.filename.len > 0) {
                    // C'est un fichier
                    snprintf(saved_path, sizeof(saved_path), "%s/%.*s", s_upload_dir, (int)part.filename.len, part.filename.buf);
                    
                    FILE *fp = fopen(saved_path, "wb");
                    if (fp) {
                        fwrite(part.body.buf, 1, part.body.len, fp);
                        fclose(fp);
                        printf("Fichier uploadé: %s\n", saved_path);
                        uploaded = 1;
                    }
                }
            }
            
            if (uploaded) {
                // Si on a un ID cible, on envoie la notif
                char target_id[32];
                get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
                
                if (strlen(target_id) > 0) {
                    // Construction de l'URL
                    char host[128];
                    struct mg_str *h = mg_http_get_header(hm, "Host");
                    if (h) snprintf(host, sizeof(host), "%.*s", (int)h->len, h->buf);
                    else strcpy(host, "localhost:8000");
                    
                    char full_url[1024];
                    snprintf(full_url, sizeof(full_url), "http://%s/%s", host, saved_path);
                    
                    // Envoi WebSocket
                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddStringToObject(json, "url", full_url);
                    char *json_str = cJSON_PrintUnformatted(json);
                    
                    int found = 0;
                    for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                        if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                            mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                            found++;
                        }
                    }
                    free(json_str);
                    cJSON_Delete(json);
                    
                    mg_http_reply(c, 200, s_cors_headers, "Uploaded and sent to %d client(s)\n", found);
                } else {
                    mg_http_reply(c, 200, s_cors_headers, "Uploaded but no target id provided\n");
                }
            } else {
                mg_http_reply(c, 400, s_cors_headers, "No file found in request\n");
            }
        }
        // 3. Servir les fichiers uploadés
        else if (mg_match(hm->uri, mg_str("/uploads/*"), NULL)) {
            struct mg_http_serve_opts opts = {
                .root_dir = ".",
                .extra_headers = "Access-Control-Allow-Origin: *\r\n"
            };
            mg_http_serve_dir(c, hm, &opts);
        }
        // 4. Gestion de la connexion WebSocket (Client)
        else if (hm->uri.len > 1) {
            char id[32];
            // On copie l'URI sans le premier slash
            snprintf(id, sizeof(id), "%.*s", (int)hm->uri.len - 1, hm->uri.buf + 1);
            
            // On stocke l'ID dans c->data
            snprintf(c->data, sizeof(c->data), "%s", id);
            printf("Nouveau client connecté: %s\n", id);
            mg_ws_upgrade(c, hm, NULL);
        }
        // Page d'accueil simple
        else {
            mg_http_reply(c, 200, "Content-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n", 
                "<h1>Wallchange Server</h1>"
                "<p>Utilisez /api/send?id=USER&url=URL pour changer le fond d'ecran.</p>");
        }
    } else if (ev == MG_EV_WS_OPEN) {
        // Connexion WS établie (après upgrade)
    } else if (ev == MG_EV_WS_MSG) {
        // Message reçu du client (on ignore pour l'instant)
    } else if (ev == MG_EV_CLOSE) {
        if (c->is_websocket) {
            printf("Client déconnecté: %s\n", (char *)c->data);
        }
    }
}

int main(int argc, char *argv[]) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    
    // Création du dossier uploads s'il n'existe pas
    mkdir(s_upload_dir, 0755);

    const char *listen_on = s_listen_on;
    char dynamic_listen_on[64];

    if (argc > 1) {
        snprintf(dynamic_listen_on, sizeof(dynamic_listen_on), "ws://0.0.0.0:%s", argv[1]);
        listen_on = dynamic_listen_on;
    }

    printf("Serveur démarré sur %s !\n", listen_on);
    mg_http_listen(&mgr, listen_on, fn, NULL);
    
    for (;;) mg_mgr_poll(&mgr, 1000);
    
    mg_mgr_free(&mgr);
    return 0;

}
