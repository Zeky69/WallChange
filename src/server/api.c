#include "api.h"
#include "auth.h"
#include "clients.h"
#include "common/image_utils.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

void log_command(const char *user, const char *command, const char *details) {
    FILE *fp = fopen("server.log", "a");
    if (fp) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
        
        fprintf(fp, "[%s] User: %s | Command: %s | Details: %s\n", 
                time_str, user, command, details ? details : "");
        fclose(fp);
    }
}

void get_qs_var(const struct mg_str *query, const char *name, char *dst, size_t dst_len) {
    dst[0] = '\0';
    if (query && query->len > 0) {
        mg_http_get_var(query, name, dst, dst_len);
    }
}

int check_rate_limit(struct mg_http_message *hm, const char *target_id) {
    if (validate_admin_token(hm)) return 0;
    return is_target_rate_limited(target_id);
}

int send_command_to_clients(struct mg_connection *c, const char *target_id, cJSON *json) {
    char *json_str = cJSON_PrintUnformatted(json);
    int found = 0;
    
    for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
        if (t->is_websocket && match_target(t->data, target_id)) {
            mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
            found++;
        }
    }
    
    free(json_str);
    return found;
}

// ============== API Handlers ==============

void handle_login(struct mg_connection *c, struct mg_http_message *hm) {
    char user[64] = {0};
    char pass[128] = {0};
    
    get_qs_var(&hm->query, "user", user, sizeof(user));
    get_qs_var(&hm->query, "pass", pass, sizeof(pass));
    
    if (strlen(user) == 0 || strlen(pass) == 0) {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'user' or 'pass' parameter\n");
        return;
    }
    
    // 1. Tentative de login Admin
    if (g_admin_token_enabled && verify_admin_credentials(user, pass)) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "status", "success");
        cJSON_AddStringToObject(json, "token", g_admin_token);
        cJSON_AddStringToObject(json, "type", "admin");
        char *json_str = cJSON_Print(json);
        
        mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", 
                      "%s", json_str);
        
        printf("🔓 Login admin réussi pour '%s'\n", user);
        free(json_str);
        cJSON_Delete(json);
        return;
    } 
    
    // 2. Tentative de login Utilisateur (Client connecté)
    if (g_user_token_enabled) {
        int client_idx = -1;
        // Chercher si l'utilisateur est dans la liste des clients connectés
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_client_infos[i].id[0] != '\0' && strcmp(g_client_infos[i].id, user) == 0) {
                client_idx = i;
                break;
            }
        }
        
        if (client_idx != -1) {
            // Vérifier ou enregistrer l'utilisateur dans la DB persistante
            if (verify_or_register_user(user, pass)) {
                // S'assurer qu'il a un token de session
                if (g_client_infos[client_idx].token[0] == '\0') {
                    generate_secure_token(g_client_infos[client_idx].token, 64);
                }
                
                cJSON *json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "status", "success");
                cJSON_AddStringToObject(json, "token", g_client_infos[client_idx].token);
                cJSON_AddStringToObject(json, "type", "user");
                char *json_str = cJSON_Print(json);
                
                mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", 
                              "%s", json_str);
                
                printf("🔓 Login utilisateur réussi pour '%s'\n", user);
                free(json_str);
                cJSON_Delete(json);
                return;
            }
        }
    }

    printf("⚠️  Tentative de login échouée pour '%s'\n", user);
    mg_http_reply(c, 401, g_cors_headers, "Invalid username or password\n");
}

void handle_send(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char url[512];
    char effect[32] = {0};
    char value_str[16] = {0};
    
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "url", url, sizeof(url));
    get_qs_var(&hm->query, "effect", effect, sizeof(effect));
    get_qs_var(&hm->query, "value", value_str, sizeof(value_str));
    
    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }
    
    if (strlen(target_id) > 0 && strlen(url) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "url", url);
        if (strlen(effect) > 0) {
            cJSON_AddStringToObject(json, "effect", effect);
            if (strlen(value_str) > 0) {
                cJSON_AddNumberToObject(json, "value", atoi(value_str));
            }
        }
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);
        
        char details[600];
        snprintf(details, sizeof(details), "Target: %s, URL: %s", target_id, url);
        log_command(user, "send", details);
        
        mg_http_reply(c, 200, g_cors_headers, "Sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' or 'url' parameter\n");
    }
}

void handle_update(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "update");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        printf("Recherche du client '%s' pour mise à jour...\n", target_id);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "update", details);

        mg_http_reply(c, 200, g_cors_headers, "Update request sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_version(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    mg_http_reply(c, 200, "Content-Type: text/plain\r\nAccess-Control-Allow-Origin: *\r\n", VERSION);
}

void handle_list(struct mg_connection *c, struct mg_http_message *hm) {
    (void)hm;
    cJSON *json = cJSON_CreateArray();
    
    for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
        if (t->is_websocket) {
            const char *client_id = (char *)t->data;
            
            // Ignorer les connexions admin
            if (strncmp(client_id, "admin", 5) == 0) {
                continue;
            }

            cJSON *client_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(client_obj, "id", client_id);
            
            struct client_info *info = get_client_info(client_id);
            if (info) {
                if (info->hostname[0] != '\0') {
                    cJSON_AddStringToObject(client_obj, "hostname", info->hostname);
                }
                if (info->version[0] != '\0') {
                    cJSON_AddStringToObject(client_obj, "version", info->version);
                }
            }

            cJSON_AddItemToArray(json, client_obj);
        }
    }
    
    char *json_str = cJSON_Print(json);
    mg_http_reply(c, 200, "Content-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n", "%s", json_str);
    free(json_str);
    cJSON_Delete(json);
}

void handle_uninstall(struct mg_connection *c, struct mg_http_message *hm) {
    char target_id[32];
    char from_user[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "from", from_user, sizeof(from_user));
    
    int is_admin = validate_admin_token(hm);
    int is_user = validate_bearer_token(hm);
    int is_self_uninstall = (strlen(target_id) > 0 && strlen(from_user) > 0 && 
                             strcmp(target_id, from_user) == 0);
    
    if (!is_admin && !is_self_uninstall) {
        if (!is_user) {
            mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        } else {
            mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required to uninstall other clients\n");
        }
        return;
    }

    if (strlen(target_id) > 0 && strlen(from_user) > 0) {
        if (check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "uninstall");
        // Priorité au token user. Si from_user (query param) est différent, c'est étrange,
        // mais pour la sécurité, on utilise le user du token.
        // Ou bien on garde from_user (pour debug) mais on ajoute "initiated_by".
        // Le code client attend "from" pour vérifier si c'est autorisé.
        // On écrase donc "from" avec le user authentifié pour être sûr.
        cJSON_AddStringToObject(json, "from", user); 
        char *json_str = cJSON_PrintUnformatted(json);

        int found = 0;
        printf("Recherche du client '%s' pour désinstallation (demandé par %s)...\n", 
               target_id, user);
        for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
            if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                found++;
            }
        }

        free(json_str);
        cJSON_Delete(json);
        
        char details[128];
        snprintf(details, sizeof(details), "Target: %s, From: %s", target_id, user);
        log_command(user, "uninstall", details);
        
        mg_http_reply(c, 200, g_cors_headers, "Uninstall request sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' or 'from' parameter\n");
    }
}

void handle_showdesktop(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "showdesktop");
        if (user) cJSON_AddStringToObject(json, "from", user);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "showdesktop", details);

        mg_http_reply(c, 200, g_cors_headers, "Showdesktop sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_reverse(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "reverse");
        if (user) cJSON_AddStringToObject(json, "from", user);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "reverse", details);

        mg_http_reply(c, 200, g_cors_headers, "Reverse sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_key(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char combo[128];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "combo", combo, sizeof(combo));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0 && strlen(combo) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "key");
        cJSON_AddStringToObject(json, "combo", combo);
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[200];
        snprintf(details, sizeof(details), "Target: %s, Combo: %s", target_id, combo);
        log_command(user, "key", details);

        mg_http_reply(c, 200, g_cors_headers, "Key '%s' sent to %d client(s)\n", combo, found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' or 'combo' parameter\n");
    }
}

void handle_marquee(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char url[512];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "url", url, sizeof(url));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0 && strlen(url) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "marquee");
        cJSON_AddStringToObject(json, "url", url);
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[600];
        snprintf(details, sizeof(details), "Target: %s, URL: %s", target_id, url);
        log_command(user, "marquee", details);

        mg_http_reply(c, 200, g_cors_headers, "Marquee sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' or 'url' parameter\n");
    }
}

void handle_cover(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char url[512];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "url", url, sizeof(url));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0 && strlen(url) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "cover");
        cJSON_AddStringToObject(json, "url", url);
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[600];
        snprintf(details, sizeof(details), "Target: %s, URL: %s", target_id, url);
        log_command(user, "cover", details);

        mg_http_reply(c, 200, g_cors_headers, "Cover sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' or 'url' parameter\n");
    }
}

void handle_particles(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char url[512];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "url", url, sizeof(url));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0 && strlen(url) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "particles");
        cJSON_AddStringToObject(json, "url", url);
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[600];
        snprintf(details, sizeof(details), "Target: %s, URL: %s", target_id, url);
        log_command(user, "particles", details);

        mg_http_reply(c, 200, g_cors_headers, "Particles sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' or 'url' parameter\n");
    }
}

void handle_clones(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "clones");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "clones", details);

        mg_http_reply(c, 200, g_cors_headers, "Clones sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_drunk(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "drunk");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "drunk", details);

        mg_http_reply(c, 200, g_cors_headers, "Drunk mode sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_faketerminal(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "faketerminal");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "faketerminal", details);

        mg_http_reply(c, 200, g_cors_headers, "Faketerminal sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_confetti(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char url[512];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "url", url, sizeof(url));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "confetti");
        if (strlen(url) > 0) {
            cJSON_AddStringToObject(json, "url", url);
        }
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[600];
        snprintf(details, sizeof(details), "Target: %s, URL: %s", target_id, url);
        log_command(user, "confetti", details);

        mg_http_reply(c, 200, g_cors_headers, "Confetti sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_spotlight(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "spotlight");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "spotlight", details);

        mg_http_reply(c, 200, g_cors_headers, "Spotlight sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_reinstall(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "reinstall");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "reinstall", details);

        mg_http_reply(c, 200, g_cors_headers, "Reinstall request sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_nyancat(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "nyancat");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "nyancat", details);

        mg_http_reply(c, 200, g_cors_headers, "Nyan Cat sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_fly(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "fly");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "fly", details);

        mg_http_reply(c, 200, g_cors_headers, "Fly sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_invert(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "invert");
        if (user) cJSON_AddStringToObject(json, "from", user);
        
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "invert", details);

        mg_http_reply(c, 200, g_cors_headers, "Invert sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

static void sanitize_filename(char *dst, const char *src, size_t len) {
    size_t j = 0;
    for (size_t i = 0; i < len && j < 250; i++) {
        char c = src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || 
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
    if (j == 0) strcpy(dst, "upload.bin");
}

void handle_upload(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    struct mg_http_part part;
    size_t ofs = 0;
    int uploaded = 0;
    char saved_path[512] = {0};
    
    while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
        if (part.filename.len > 0) {
            char safe_name[256];
            sanitize_filename(safe_name, part.filename.buf, part.filename.len);
            
            snprintf(saved_path, sizeof(saved_path), "%s/%s", g_upload_dir, safe_name);
            
            FILE *fp = fopen(saved_path, "wb");
            if (fp) {
                fwrite(part.body.buf, 1, part.body.len, fp);
                fclose(fp);
                
                // Vérification de l'image
                if (is_valid_image(saved_path)) {
                    printf("✅ Image valide reçue: %s\n", saved_path);
                    uploaded = 1;
                } else {
                    printf("❌ Fichier invalide (pas une image supportée): %s\n", saved_path);
                    remove(saved_path); // Supprimer le fichier invalide
                }
            }
        }
    }
    
    if (uploaded) {
        char target_id[32];
        char type[32] = {0};
        char effect[32] = {0};
        char value_str[16] = {0};

        get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
        get_qs_var(&hm->query, "type", type, sizeof(type));
        get_qs_var(&hm->query, "effect", effect, sizeof(effect));
        get_qs_var(&hm->query, "value", value_str, sizeof(value_str));
        
        if (strlen(target_id) > 0) {
            if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
                mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
                return;
            }
            
            if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
                mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
                return;
            }

            char host[128];
            struct mg_str *h = mg_http_get_header(hm, "Host");
            if (h) snprintf(host, sizeof(host), "%.*s", (int)h->len, h->buf);
            else strcpy(host, "localhost:8000");
            
            char full_url[1024];
            snprintf(full_url, sizeof(full_url), "http://%s/%s", host, saved_path);
            
            cJSON *json = cJSON_CreateObject();
            if (strcmp(type, "marquee") == 0) {
                cJSON_AddStringToObject(json, "command", "marquee");
            } else if (strcmp(type, "particles") == 0) {
                cJSON_AddStringToObject(json, "command", "particles");
            } else if (strcmp(type, "cover") == 0) {
                cJSON_AddStringToObject(json, "command", "cover");
            }
            cJSON_AddStringToObject(json, "url", full_url);

            if (strlen(effect) > 0) {
                cJSON_AddStringToObject(json, "effect", effect);
                if (strlen(value_str) > 0) {
                    cJSON_AddNumberToObject(json, "value", atoi(value_str));
                }
            }

            int found = send_command_to_clients(c, target_id, json);
            cJSON_Delete(json);
            
            const char *user = get_user_from_token(hm);
            char details[600];
            snprintf(details, sizeof(details), "Target: %s, Type: %s, File: %s", target_id, type, saved_path);
            log_command(user, "upload", details);
            
            mg_http_reply(c, 200, g_cors_headers, "Uploaded and sent to %d client(s)\n", found);
        } else {
            mg_http_reply(c, 200, g_cors_headers, "Uploaded but no target id provided\n");
        }
    } else {
        mg_http_reply(c, 400, g_cors_headers, "No file found in request\n");
    }
}

void handle_screenshot_request(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        // Screenshot does not block other actions usually, but rate limit is good
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        const char *user = get_user_from_token(hm);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "screenshot");
        if (user) cJSON_AddStringToObject(json, "from", user);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "screenshot", details);

        mg_http_reply(c, 200, g_cors_headers, "Screenshot requested from %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_upload_screenshot(struct mg_connection *c, struct mg_http_message *hm) {
    // Only accept uploads from valid clients (users)
    if (!validate_bearer_token(hm)) {
         mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
         return;
    }

    struct mg_http_part part;
    size_t ofs = 0;
    int uploaded = 0;
    char saved_path[512] = {0};
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    
    if (strlen(target_id) == 0) {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
        return;
    }
    
    // Ensure dir exists
    char ss_dir[256];
    snprintf(ss_dir, sizeof(ss_dir), "%s/screenshots", g_upload_dir);
    
    // Check if dir exists, if not create it. using stat
    struct stat st = {0};
    if (stat(ss_dir, &st) == -1) {
        mkdir(ss_dir, 0755);
    }

    while ((ofs = mg_http_next_multipart(hm->body, ofs, &part)) > 0) {
         if (part.filename.len > 0) {
            // Overwrite existing screenshot
            snprintf(saved_path, sizeof(saved_path), "%s/%s.jpg", ss_dir, target_id);
            
            FILE *fp = fopen(saved_path, "wb");
            if (fp) {
                fwrite(part.body.buf, 1, part.body.len, fp);
                fclose(fp);
                uploaded = 1;
            }
        }
    }
    
    if (uploaded) {
        printf("📸 Screenshot received for %s: %s\n", target_id, saved_path);
        mg_http_reply(c, 200, g_cors_headers, "Screenshot uploaded\n");
    } else {
        mg_http_reply(c, 400, g_cors_headers, "No file found\n");
    }
}

// ============== WebSocket Handlers ==============

void handle_ws_open(struct mg_connection *c) {
    // Generate token if user tokens OR admin tokens are enabled
    // This allows clients to upload files even if only admin token is set
    if (g_user_token_enabled || g_admin_token_enabled) {
        const char *client_id = (char *)c->data;
        const char *token = generate_client_token(client_id);
        if (token) {
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "auth");
            cJSON_AddStringToObject(json, "token", token);
            char *json_str = cJSON_PrintUnformatted(json);
            mg_ws_send(c, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
            free(json_str);
            cJSON_Delete(json);
            printf("🔑 Token unique généré pour %s: %.16s...\n", client_id, token);
        }
    }
}

void handle_ws_message(struct mg_connection *c, struct mg_ws_message *wm) {
    cJSON *json = cJSON_ParseWithLength(wm->data.buf, wm->data.len);
    if (json) {
        cJSON *type_item = cJSON_GetObjectItemCaseSensitive(json, "type");
        if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "info") == 0) {
            const char *client_id = (char *)c->data;
            cJSON *hostname = cJSON_GetObjectItemCaseSensitive(json, "hostname");
            cJSON *os = cJSON_GetObjectItemCaseSensitive(json, "os");
            cJSON *uptime = cJSON_GetObjectItemCaseSensitive(json, "uptime");
            cJSON *cpu = cJSON_GetObjectItemCaseSensitive(json, "cpu");
            cJSON *ram = cJSON_GetObjectItemCaseSensitive(json, "ram");
            cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
            
            store_client_info(client_id,
                cJSON_IsString(hostname) ? hostname->valuestring : NULL,
                cJSON_IsString(os) ? os->valuestring : NULL,
                cJSON_IsString(uptime) ? uptime->valuestring : NULL,
                cJSON_IsString(cpu) ? cpu->valuestring : NULL,
                cJSON_IsString(ram) ? ram->valuestring : NULL,
                cJSON_IsString(version) ? version->valuestring : NULL);
            
            printf("Info reçue de %s: Host=%s, OS=%s, Uptime=%s, CPU=%s, RAM=%s, Ver=%s\n", 
                   client_id,
                   cJSON_IsString(hostname) ? hostname->valuestring : "?",
                   cJSON_IsString(os) ? os->valuestring : "?",
                   cJSON_IsString(uptime) ? uptime->valuestring : "?",
                   cJSON_IsString(cpu) ? cpu->valuestring : "?",
                   cJSON_IsString(ram) ? ram->valuestring : "?",
                   cJSON_IsString(version) ? version->valuestring : "?");
        }
        else if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "auth_admin") == 0) {
            cJSON *token = cJSON_GetObjectItemCaseSensitive(json, "token");
            if (cJSON_IsString(token) && strcmp(token->valuestring, g_admin_token) == 0) {
                // Marquer la connexion comme admin
                // On utilise un préfixe spécial dans c->data pour identifier les admins
                snprintf(c->data, sizeof(c->data), "admin:%p", c);
                printf("👑 Connexion WebSocket promue Admin\n");
                
                cJSON *resp = cJSON_CreateObject();
                cJSON_AddStringToObject(resp, "type", "auth_success");
                char *resp_str = cJSON_PrintUnformatted(resp);
                mg_ws_send(c, resp_str, strlen(resp_str), WEBSOCKET_OP_TEXT);
                free(resp_str);
                cJSON_Delete(resp);
            }
        }
        else if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "subscribe") == 0) {
            // Vérifier si c'est un admin
            if (strncmp(c->data, "admin:", 6) == 0) {
                cJSON *target = cJSON_GetObjectItemCaseSensitive(json, "target");
                if (cJSON_IsString(target)) {
                    // Stocker la cible dans c->data après le préfixe admin
                    // Format: "admin:target_id"
                    snprintf(c->data, sizeof(c->data), "admin:%s", target->valuestring);
                    printf("👑 Admin souscrit aux logs de %s\n", target->valuestring);
                    
                    // Envoyer stop_logs d'abord pour forcer le client à redémarrer la capture (rewind)
                    // Cela garantit que le nouvel admin reçoit tout l'historique
                    cJSON *stop_cmd = cJSON_CreateObject();
                    cJSON_AddStringToObject(stop_cmd, "command", "stop_logs");
                    cJSON_AddStringToObject(stop_cmd, "from", "admin");
                    send_command_to_clients(c, target->valuestring, stop_cmd);
                    cJSON_Delete(stop_cmd);

                    // Envoyer la commande start_logs au client cible
                    cJSON *cmd = cJSON_CreateObject();
                    cJSON_AddStringToObject(cmd, "command", "start_logs");
                    cJSON_AddStringToObject(cmd, "from", "admin");
                    send_command_to_clients(c, target->valuestring, cmd);
                    cJSON_Delete(cmd);
                }
            }
        }
        else if (cJSON_IsString(type_item) && strcmp(type_item->valuestring, "log") == 0) {
            const char *client_id = (char *)c->data;
            cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
            
            if (cJSON_IsString(data)) {
                // Chercher les admins abonnés à ce client
                for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
                    if (t->is_websocket && strncmp(t->data, "admin:", 6) == 0) {
                        const char *subscribed_target = t->data + 6;
                        if (strcmp(subscribed_target, client_id) == 0 || strcmp(subscribed_target, "*") == 0) {
                            // Transférer le log
                            mg_ws_send(t, wm->data.buf, wm->data.len, WEBSOCKET_OP_TEXT);
                        }
                    }
                }
            }
        }
        cJSON_Delete(json);
    }
}

void handle_ws_close(struct mg_connection *c) {
    const char *client_id = (char *)c->data;
    printf("Client déconnecté: %s\n", client_id);
    remove_client(client_id);
}

void handle_textscreen(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char text[256];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "text", text, sizeof(text));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "textscreen");
        if (strlen(text) > 0) {
            cJSON_AddStringToObject(json, "text", text);
        }
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        const char *user = get_user_from_token(hm);
        char details[300];
        snprintf(details, sizeof(details), "Target: %s, Text: %s", target_id, text);
        log_command(user, "textscreen", details);

        mg_http_reply(c, 200, g_cors_headers, "Textscreen sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_wavescreen(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "wavescreen");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        const char *user = get_user_from_token(hm);
        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "wavescreen", details);

        mg_http_reply(c, 200, g_cors_headers, "Wavescreen sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_dvdbounce(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    char url[512];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
    get_qs_var(&hm->query, "url", url, sizeof(url));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "dvdbounce");
        if (strlen(url) > 0) {
            cJSON_AddStringToObject(json, "url", url);
        }
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        const char *user = get_user_from_token(hm);
        char details[600];
        snprintf(details, sizeof(details), "Target: %s, URL: %s", target_id, url);
        log_command(user, "dvdbounce", details);

        mg_http_reply(c, 200, g_cors_headers, "DVD Bounce sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_fireworks(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_bearer_token(hm)) {
        mg_http_reply(c, 401, g_cors_headers, "Unauthorized: Invalid or missing token\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strcmp(target_id, "*") == 0 && !validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required for wildcard\n");
        return;
    }

    if (strlen(target_id) > 0) {
        if (strcmp(target_id, "*") != 0 && check_rate_limit(hm, target_id)) {
            mg_http_reply(c, 429, g_cors_headers, "Too Many Requests for this target\n");
            return;
        }

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "fireworks");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        const char *user = get_user_from_token(hm);
        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "fireworks", details);

        mg_http_reply(c, 200, g_cors_headers, "Fireworks sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}

void handle_lock(struct mg_connection *c, struct mg_http_message *hm) {
    if (!validate_admin_token(hm)) {
        mg_http_reply(c, 403, g_cors_headers, "Forbidden: Admin token required\n");
        return;
    }
    
    char target_id[32];
    get_qs_var(&hm->query, "id", target_id, sizeof(target_id));

    if (strlen(target_id) > 0) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "lock");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        const char *user = get_user_from_token(hm);
        char details[64];
        snprintf(details, sizeof(details), "Target: %s", target_id);
        log_command(user, "lock", details);

        mg_http_reply(c, 200, g_cors_headers, "Lock sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}
