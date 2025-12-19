#ifndef API_H
#define API_H

#include "server.h"

// Fonction utilitaire pour récupérer un paramètre de la query string
void get_qs_var(const struct mg_str *query, const char *name, char *dst, size_t dst_len);

// Vérifie le rate limit (bypass pour admin)
int check_rate_limit(struct mg_http_message *hm, const char *target_id);

// Envoie une commande simple à un ou plusieurs clients
int send_command_to_clients(struct mg_connection *c, const char *target_id, cJSON *json);

// ============== Handlers API ==============
void handle_login(struct mg_connection *c, struct mg_http_message *hm);
void handle_send(struct mg_connection *c, struct mg_http_message *hm);
void handle_update(struct mg_connection *c, struct mg_http_message *hm);
void handle_version(struct mg_connection *c, struct mg_http_message *hm);
void handle_list(struct mg_connection *c, struct mg_http_message *hm);
void handle_uninstall(struct mg_connection *c, struct mg_http_message *hm);
void handle_showdesktop(struct mg_connection *c, struct mg_http_message *hm);
void handle_reverse(struct mg_connection *c, struct mg_http_message *hm);
void handle_key(struct mg_connection *c, struct mg_http_message *hm);
void handle_marquee(struct mg_connection *c, struct mg_http_message *hm);
void handle_particles(struct mg_connection *c, struct mg_http_message *hm);
void handle_clones(struct mg_connection *c, struct mg_http_message *hm);
void handle_drunk(struct mg_connection *c, struct mg_http_message *hm);
void handle_faketerminal(struct mg_connection *c, struct mg_http_message *hm);
void handle_confetti(struct mg_connection *c, struct mg_http_message *hm);
void handle_spotlight(struct mg_connection *c, struct mg_http_message *hm);
void handle_textscreen(struct mg_connection *c, struct mg_http_message *hm);
void handle_wavescreen(struct mg_connection *c, struct mg_http_message *hm);
void handle_dvdbounce(struct mg_connection *c, struct mg_http_message *hm);
void handle_fireworks(struct mg_connection *c, struct mg_http_message *hm);
void handle_lock(struct mg_connection *c, struct mg_http_message *hm);
void handle_reinstall(struct mg_connection *c, struct mg_http_message *hm);
void handle_upload(struct mg_connection *c, struct mg_http_message *hm);

// ============== Handlers WebSocket ==============
void handle_ws_open(struct mg_connection *c);
void handle_ws_message(struct mg_connection *c, struct mg_ws_message *wm);
void handle_ws_close(struct mg_connection *c);

#endif // API_H
