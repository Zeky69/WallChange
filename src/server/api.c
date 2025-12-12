#include "api.h"
#include "auth.h"
#include "clients.h"
#include "common/image_utils.h"
#include <string.h>
#include <stdio.h>

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "url", url);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);
        
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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "update");
        
        printf("Recherche du client '%s' pour mise à jour...\n", target_id);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "uninstall");
        cJSON_AddStringToObject(json, "from", from_user);
        char *json_str = cJSON_PrintUnformatted(json);

        int found = 0;
        printf("Recherche du client '%s' pour désinstallation (demandé par %s)...\n", 
               target_id, from_user);
        for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
            if (t->is_websocket && strcmp(t->data, target_id) == 0) {
                mg_ws_send(t, json_str, strlen(json_str), WEBSOCKET_OP_TEXT);
                found++;
            }
        }

        free(json_str);
        cJSON_Delete(json);
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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "showdesktop");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "reverse");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "key");
        cJSON_AddStringToObject(json, "combo", combo);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "marquee");
        cJSON_AddStringToObject(json, "url", url);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        mg_http_reply(c, 200, g_cors_headers, "Marquee sent to %d client(s)\n", found);
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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "particles");
        cJSON_AddStringToObject(json, "url", url);
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "clones");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "drunk");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "faketerminal");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "confetti");
        if (strlen(url) > 0) {
            cJSON_AddStringToObject(json, "url", url);
        }
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "spotlight");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

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

        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "command", "reinstall");
        int found = send_command_to_clients(c, target_id, json);
        cJSON_Delete(json);

        mg_http_reply(c, 200, g_cors_headers, "Reinstall request sent to %d client(s)\n", found);
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
        get_qs_var(&hm->query, "id", target_id, sizeof(target_id));
        get_qs_var(&hm->query, "type", type, sizeof(type));
        
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
            }
            cJSON_AddStringToObject(json, "url", full_url);
            int found = send_command_to_clients(c, target_id, json);
            cJSON_Delete(json);
            
            mg_http_reply(c, 200, g_cors_headers, "Uploaded and sent to %d client(s)\n", found);
        } else {
            mg_http_reply(c, 200, g_cors_headers, "Uploaded but no target id provided\n");
        }
    } else {
        mg_http_reply(c, 400, g_cors_headers, "No file found in request\n");
    }
}

// ============== WebSocket Handlers ==============

void handle_ws_open(struct mg_connection *c) {
    if (g_user_token_enabled) {
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
                    
                    // Envoyer la commande start_logs au client cible
                    cJSON *cmd = cJSON_CreateObject();
                    cJSON_AddStringToObject(cmd, "command", "start_logs");
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

        mg_http_reply(c, 200, g_cors_headers, "Fireworks sent to %d client(s)\n", found);
    } else {
        mg_http_reply(c, 400, g_cors_headers, "Missing 'id' parameter\n");
    }
}
