#include "mongoose.h"
#include "cJSON.h"
#include <sys/stat.h>
#include <sys/types.h>

static const char *s_listen_on = "ws://0.0.0.0:8000";
static const char *s_upload_dir = "uploads";

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
                
                mg_http_reply(c, 200, "", "Sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, "", "Missing 'id' or 'url' parameter\n");
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
                // On parcourt toutes les connexions pour trouver la bonne
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                        mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                        found++;
                    }
                }

                free(json_str);
                cJSON_Delete(json);

                mg_http_reply(c, 200, "", "Update request sent to %d client(s)\n", found);
            } else {
                mg_http_reply(c, 400, "", "Missing 'id' parameter\n");
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
                    
                    mg_http_reply(c, 200, "", "Uploaded and sent to %d client(s)\n", found);
                } else {
                    mg_http_reply(c, 200, "", "Uploaded but no target id provided\n");
                }
            } else {
                mg_http_reply(c, 400, "", "No file found in request\n");
            }
        }
        // 3. Servir les fichiers uploadés
        else if (mg_match(hm->uri, mg_str("/uploads/*"), NULL)) {
            struct mg_http_serve_opts opts = {.root_dir = "."}; // On sert depuis la racine car l'URI contient déjà /uploads/
            mg_http_serve_dir(c, hm, &opts);
        }
        // 4. Gestion de la connexion WebSocket (Client)
        // On suppose que l'URI est /ID (ex: /zakburak)
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
            mg_http_reply(c, 200, "Content-Type: text/html\r\n", 
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

    printf("Serveur démarré sur %s\n", listen_on);
    mg_http_listen(&mgr, listen_on, fn, NULL);
    
    for (;;) mg_mgr_poll(&mgr, 1000);
    
    mg_mgr_free(&mgr);
    return 0;
}
